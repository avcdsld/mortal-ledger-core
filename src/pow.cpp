// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <util/check.h>

#include <string>
#include <vector>

// Mortal Ledger: LWMA averaging window N (blocks). Per-block retarget responsive
// enough for a tiny, volatile network without the death-spiral Bitcoin's 2016-block
// window would cause here. Fixed consensus constant; calibrated on the Pi.
static const int64_t MORTAL_LEDGER_LWMA_N = 60;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Mortal Ledger: after the fork, retarget every block by LWMA. Before there is a
    // full window of post-fork history, hold the (reset) target.
    if (params.fMortalLedgerLWMA && pindexLast->nHeight >= MORTAL_LEDGER_LWMA_N) {
        return GetNextWorkRequiredLWMA(pindexLast, params);
    }

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then it MUST be a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Mortal Ledger: LWMA-1 retarget (Zawy). Next target from the last N blocks,
// weighting recent solve times most. Larger target = easier. This is the float
// reference (node/difficulty_lwma.cpp) ported to Core's arith_uint256 fixed point.
//
//   next = avgTarget * SWS / (T * sumw)
//   SWS  = Σ j·solvetime_j (j=1..N, oldest..newest), each solvetime clamped to [0,6T]
//          and floored as a whole at sumw·T/3 (limits how fast difficulty can rise)
//   sumw = N(N+1)/2,  k = sumw·T
//
// To stay within 256 bits we divide each window target by (k·N) before summing, so
//   next = SWS · Σ(target_i/(k·N)) = (Σtarget_i/N)·SWS/k = avgTarget·SWS/(sumw·T).
arith_uint256 CalculateNextWorkRequiredLWMA(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    const int64_t T = params.nPowTargetSpacing;
    const int64_t N = MORTAL_LEDGER_LWMA_N;

    // Need a full window plus one prior block for the oldest solve time.
    if (pindexLast == nullptr || pindexLast->nHeight < N) {
        return bnPowLimit;
    }

    const int64_t sumw = N * (N + 1) / 2;
    const int64_t k = sumw * T;
    const arith_uint256 scale{(uint64_t)(k * N)};

    int64_t SWS = 0;
    arith_uint256 sumTarget = 0;
    const int height = pindexLast->nHeight;
    for (int64_t i = 0; i < N; i++) {
        const int64_t bi = height - N + 1 + i;            // oldest .. newest
        const CBlockIndex* cur = pindexLast->GetAncestor(bi);
        const CBlockIndex* prev = pindexLast->GetAncestor(bi - 1);
        int64_t solvetime = cur->GetBlockTime() - prev->GetBlockTime();
        if (solvetime < 0) solvetime = 0;
        if (solvetime > 6 * T) solvetime = 6 * T;
        SWS += (i + 1) * solvetime;                       // newest carries the most weight
        arith_uint256 target;
        target.SetCompact(cur->nBits);
        sumTarget += target / scale;
    }
    if (SWS < k / 3) SWS = k / 3;                          // floor: cap the rise rate

    arith_uint256 bnNew = sumTarget;
    bnNew *= (uint32_t)SWS;                                // SWS <= 6T·sumw fits in 32 bits
    if (bnNew > bnPowLimit || bnNew == 0) bnNew = bnPowLimit;
    return bnNew;
}

unsigned int GetNextWorkRequiredLWMA(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    return CalculateNextWorkRequiredLWMA(pindexLast, params).GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

// Mortal Ledger: Proof of Quotation. The mined hash's head byte must equal the
// next byte of the novel being transcribed. We constrain the lowest internal
// byte (hash.begin()[0]); the proof-of-work magnitude lives in the high bytes,
// so the two predicates do not collide. Heights map to byte offsets, so the
// chain's head bytes, read in order, reproduce the novel exactly. (Demo: a fixed
// opening with k=1 byte/block; the full version reads the offset from canon
// state and carves k bytes.)
// ---- Mortal Ledger canon state + OP_SOURCE succession ----
// Demo-grade: process-global, advanced on real block connection. Not reorg- or
// reindex-safe (a production node would track this per-block-index / in the
// chainstate and recompute on restart). Sufficient for the regtest demo of
// succession on a single linear chain.
static const int CANON_K = 1; // writing granularity (demo; calibrated on the Pi)
static std::string g_canon = "旅への誘いが、次第に私の空想から消えて行つた。"; // genesis canon (萩原朔太郎『猫町』)
static size_t g_canon_off = 0;

// The K bytes the next block must carry. If the active novel is exhausted, the
// block must register a successor (reg = the registered novel bytes); with no
// registration the result is empty, meaning no valid block can be made (the
// chain starves at completion).
std::vector<unsigned char> CanonExpectedSlice(const std::vector<unsigned char>& reg)
{
    std::string novel = g_canon;
    size_t off = g_canon_off;
    if (off >= novel.size()) {
        if (reg.empty()) return {};
        novel.assign(reg.begin(), reg.end());
        off = 0;
    }
    const size_t n = std::min((size_t)CANON_K, novel.size() - off);
    return std::vector<unsigned char>(novel.begin() + off, novel.begin() + off + n);
}

// Advance the canon after a block is accepted (real connection only).
void CanonAdvance(const std::vector<unsigned char>& reg)
{
    if (g_canon_off >= g_canon.size() && !reg.empty()) {
        g_canon.assign(reg.begin(), reg.end());
        g_canon_off = 0;
    }
    g_canon_off += CANON_K;
}

// Does the hash carry the expected slice in its head bytes? (We constrain the
// low internal bytes; the proof-of-work magnitude lives in the high bytes, so
// the two predicates are orthogonal.) Empty slice => no valid block.
bool HashCarriesSlice(const uint256& hash, const std::vector<unsigned char>& slice)
{
    if (slice.empty()) return false;
    for (size_t i = 0; i < slice.size(); i++)
        if ((unsigned char)hash.begin()[i] != slice[i]) return false;
    return true;
}

// OP_SOURCE succession: a miner registers the successor novel in the coinbase
// scriptSig, tagged "MLSR" (Mortal Ledger Source Registration) followed by the
// novel's bytes. (Demo encoding; the full work registers via an OP_SOURCE output.)
// Returns the registered bytes, or empty if none.
std::vector<unsigned char> ExtractCanonRegistration(const CBlock& block)
{
    static const unsigned char TAG[4] = {'M','L','S','R'};
    if (block.vtx.empty() || block.vtx[0]->vin.empty()) return {};
    const CScript& s = block.vtx[0]->vin[0].scriptSig;
    if (s.size() < sizeof(TAG)) return {};
    for (size_t i = 0; i + sizeof(TAG) <= s.size(); i++) {
        bool match = true;
        for (size_t j = 0; j < sizeof(TAG); j++) if (s[i + j] != TAG[j]) { match = false; break; }
        if (match) return std::vector<unsigned char>(s.begin() + i + sizeof(TAG), s.end());
    }
    return {};
}

// 写字本位 (transcription standard): the coinbase mints BAB equal to the bytes
// actually transcribed this block (rate: 1 BAB per byte). That is the length of
// the slice this block carries, given the successor registration reg. At the
// writing granularity k this is k BAB per block, less on a novel's final partial
// block, and zero once the canon is exhausted with no successor (death = no
// issuance). Supply is thus bounded by the text's length and grows only with
// transcription. The literary unit (the book's bytes) and the coin (BAB) are
// different units; we do not force them equal, the amount only scales with the
// count.
CAmount CanonIssuance(const std::vector<unsigned char>& reg)
{
    return (CAmount)CanonExpectedSlice(reg).size() * COIN;
}

// Mortal Ledger: the pace half of Proof of Quotation. The quotation half binds the
// LOW k internal bytes of the hash (the next bytes of the novel); the pace half
// binds the MAGNITUDE. We zero the quotation bytes before the magnitude comparison
// so the two predicates are exactly orthogonal: the expected work to mine a block is
// ≈ 2^(8k) (find the quotation bytes) × (2^256 / target) (meet the pace). The target
// moves every block via LWMA. At k=1 the zeroed byte shifts the 256-bit magnitude by
// < 256, so this equals CheckProofOfWork in practice; zeroing makes the split exact.
bool CheckPaceTarget(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    for (int i = 0; i < CANON_K; i++) hash.begin()[i] = 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}
