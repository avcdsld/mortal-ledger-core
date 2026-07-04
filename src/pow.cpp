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

    // Mortal Ledger: at and after the fork height H, retarget every block by LWMA (the
    // firing point is Consensus nMortalLedgerHeight). Hold the (reset) target until a
    // full window of POST-FORK history exists, so the LWMA window is never fed pre-fork
    // (inherited Bitcoin) block spacing across the seam.
    if (params.IsMortalLedgerActive(pindexLast->nHeight + 1) &&
        pindexLast->nHeight - params.nMortalLedgerHeight >= MORTAL_LEDGER_LWMA_N) {
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
// opening with k=1 byte/block; the full version reads the offset from novel
// state and carves k bytes.)
// ---- Mortal Ledger novel state + OP_SOURCE succession ----
// Reorg-/reindex-safe: the novel state is a PURE fold over ancestors, carried on the
// block index (CBlockIndex::m_novel_*; see chain.h), not in process globals. A reorg
// restores the state from the parent (nothing to undo); a restart reloads it from disk.
// The functions here are pure — the node resolves the active novel's bytes (the genesis
// constant, or the source block's coinbase) and passes them in.
// Mortal Ledger: the transcription unit is ONE CHARACTER, not one byte — the chain writes a
// whole character each block, and each character's Proof-of-Quotation work is proportional to
// its byte length (k). k = the UTF-8 byte length of the character at `off` (1..4): a 3-byte
// kanji costs ~2^24 grinds, a 1-byte newline ~2^8. So heavier characters take more work — the
// labour of writing a character is the character itself — while the LWMA pace tunes the average
// block time on top. Defensive fallback of 1 for a non-lead byte (a well-formed novel is valid
// UTF-8). Empty text past the end => 0 (nothing to transcribe; the chain starves at completion).
static const int MAX_QUOTATION_K = 4; // the longest UTF-8 character (bytes the pace check zeros)
static size_t NovelSliceLen(const std::vector<unsigned char>& text, size_t off)
{
    if (off >= text.size()) return 0;
    const unsigned char c = text[off];
    const size_t k = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
    return std::min(k, text.size() - off);
}

// The novel state ENTERING the block at `height`, given its parent's leaving state.
// Below H: inactive (pre-fork is inherited Bitcoin). At H: the genesis novel begins.
// Above H: inherit the parent's leaving state. Gated only by the fork height.
NovelState NovelEnter(int height, const NovelState& parent_after, const Consensus::Params& params)
{
    if (!params.IsMortalLedgerActive(height)) return NovelState{};          // pre-fork: no novel
    if (height == params.nMortalLedgerHeight) return NovelState{height, 0}; // the genesis novel begins
    return parent_after;                                                    // inherit from the parent
}

// The K bytes the next block must carry, given the entering state, the active novel's
// bytes `novel` (resolved from in.source_height) and any next novel `reg` registered in
// THIS block. When the active novel is exhausted the slice is read from reg; with
// neither, the result is empty (the chain starves at completion).
std::vector<unsigned char> NovelExpectedSlice(const NovelState& in, const std::vector<unsigned char>& novel, const std::vector<unsigned char>& reg)
{
    if (!in.active()) return {};
    const std::vector<unsigned char>* text = &novel;
    size_t off = in.offset;
    if (off >= text->size()) {
        if (reg.empty()) return {};
        text = &reg;
        off = 0;
    }
    const size_t n = NovelSliceLen(*text, off);   // one whole character
    return std::vector<unsigned char>(text->begin() + off, text->begin() + off + n);
}

// The novel state LEAVING the block at `height`, given the entering state, the active
// novel's bytes and the block's registration. At the seam where the active novel ends,
// a registered next novel becomes the active novel — installed by THIS block
// (source_height = height) with the offset restarted. Gating the switch on actual
// exhaustion (offset >= novel.size()) means an early registration cannot hijack the
// novel. Pure; no globals.
NovelState NovelNext(const NovelState& in, const std::vector<unsigned char>& novel, int height, const std::vector<unsigned char>& reg)
{
    if (!in.active()) return NovelState{};
    NovelState out = in;
    const std::vector<unsigned char>* text = &novel;
    size_t off = in.offset;
    if (in.offset >= novel.size() && !reg.empty()) {
        out.source_height = height;
        out.offset = 0;
        out.index = in.index + 1;   // a new novel begins: advance the ordinal
        text = &reg;
        off = 0;
    }
    out.offset += NovelSliceLen(*text, off);   // advance by one whole character
    return out;
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

// OP_SOURCE succession (production): the next novel is carried in an unspendable
// coinbase output whose scriptPubKey is `OP_SOURCE <novel>`. This lifts the coinbase
// scriptSig 100-byte limit (the earlier demo encoding); the novel may be up to
// MAX_NOVEL_BYTES and the seam block is granted a matching weight/size exemption. The
// novel is a single push (so it is skipped by sig-op counting and never executed, the
// output being unspendable). Returns each registration's bytes (normally 0 or 1; >1 is
// rejected by block validation).
std::vector<std::vector<unsigned char>> ExtractNovelRegistrations(const CBlock& block)
{
    std::vector<std::vector<unsigned char>> regs;
    if (block.vtx.empty()) return regs;
    for (const CTxOut& o : block.vtx[0]->vout) {
        const CScript& s = o.scriptPubKey;
        if (s.empty() || s[0] != OP_SOURCE) continue;
        CScript::const_iterator pc = s.begin();
        opcodetype op;
        std::vector<unsigned char> data;
        if (!s.GetOp(pc, op, data)) continue;            // consume OP_SOURCE
        data.clear();
        if (s.GetOp(pc, op, data)) regs.push_back(data); // the pushed novel
        else regs.push_back({});
    }
    return regs;
}

// The single novel registration in this block (empty if none).
std::vector<unsigned char> ExtractNovelRegistration(const CBlock& block)
{
    const auto regs = ExtractNovelRegistrations(block);
    return regs.empty() ? std::vector<unsigned char>{} : regs.front();
}

// Mortal Ledger: dream inscription (production). A block's dream rides an unspendable
// OP_RETURN coinbase output: OP_RETURN <MORTAL_DREAM_MAGIC ++ token-ids>. OP_RETURN (not
// OP_DREAM, a functional opcode) keeps the output provably unspendable and never executes the
// seed-dependent voice during validation. The dream is data, NOT consensus.
bool IsDreamInscription(const CScript& s, std::vector<unsigned char>* token_bytes)
{
    if (s.empty() || s[0] != OP_RETURN) return false;
    CScript::const_iterator pc = s.begin();
    opcodetype op;
    std::vector<unsigned char> data;
    if (!s.GetOp(pc, op, data)) return false;       // consume OP_RETURN
    data.clear();
    if (!s.GetOp(pc, op, data)) return false;        // the pushed payload
    if (data.size() < sizeof(MORTAL_DREAM_MAGIC)) return false;
    for (size_t i = 0; i < sizeof(MORTAL_DREAM_MAGIC); i++)
        if (data[i] != MORTAL_DREAM_MAGIC[i]) return false;
    if (token_bytes) token_bytes->assign(data.begin() + sizeof(MORTAL_DREAM_MAGIC), data.end());
    return true;
}

// Each dream's token bytes (magic stripped). Normally 0 or 1; >1 is rejected by validation.
std::vector<std::vector<unsigned char>> ExtractDreamInscriptions(const CBlock& block)
{
    std::vector<std::vector<unsigned char>> dreams;
    if (block.vtx.empty()) return dreams;
    for (const CTxOut& o : block.vtx[0]->vout) {
        std::vector<unsigned char> tb;
        if (IsDreamInscription(o.scriptPubKey, &tb)) dreams.push_back(std::move(tb));
    }
    return dreams;
}

CScript MakeDreamInscription(const std::vector<unsigned char>& token_bytes)
{
    std::vector<unsigned char> payload(MORTAL_DREAM_MAGIC, MORTAL_DREAM_MAGIC + sizeof(MORTAL_DREAM_MAGIC));
    payload.insert(payload.end(), token_bytes.begin(), token_bytes.end());
    return CScript() << OP_RETURN << payload;
}

// 写字本位 (transcription standard): the coinbase mints BAB equal to the bytes
// actually transcribed this block (rate: 1 BAB per byte). That is the length of
// the slice this block carries, given the next novel registration reg. At the
// writing granularity k this is k BAB per block, less on a novel's final partial
// block, and zero once the novel is exhausted with no next novel (death = no
// issuance). Supply is thus bounded by the text's length and grows only with
// transcription. The literary unit (the book's bytes) and the coin (BAB) are
// different units; we do not force them equal, the amount only scales with the
// count.
CAmount NovelIssuance(const NovelState& in, const std::vector<unsigned char>& novel, const std::vector<unsigned char>& reg)
{
    return (CAmount)NovelExpectedSlice(in, novel, reg).size() * COIN;
}

// Mortal Ledger: the pace half of Proof of Quotation. The quotation half binds the LOW k
// internal bytes of the hash (the next character's bytes); the pace half binds the MAGNITUDE.
// We zero the low quotation bytes before the magnitude comparison so the two predicates are
// orthogonal: the work to mine a block is ≈ 2^(8k) (find the character's bytes) × (2^256/target)
// (meet the pace). This is a context-free header check, so it can't know k (which depends on the
// novel offset) — it zeros the fixed maximum (MAX_QUOTATION_K = 4). Zeroing up to 4 low bytes
// shifts the 256-bit magnitude by < 2^32, negligible against any real target, so the pace and
// quotation stay independent regardless of the actual character length.
bool CheckPaceTarget(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    for (int i = 0; i < MAX_QUOTATION_K; i++) hash.begin()[i] = 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}
