// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/amount.h>
#include <consensus/params.h>

#include <cstdint>
#include <string>
#include <vector>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, uint256 pow_limit);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Mortal Ledger: per-block LWMA retarget (active after the fork; see Consensus::Params). */
arith_uint256 CalculateNextWorkRequiredLWMA(const CBlockIndex* pindexLast, const Consensus::Params& params);
unsigned int GetNextWorkRequiredLWMA(const CBlockIndex* pindexLast, const Consensus::Params& params);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);

/** Mortal Ledger: the pace half of Proof of Quotation. Zero the k quotation bytes,
 *  then require the magnitude <= target (the LWMA-retargeted pace). */
bool CheckPaceTarget(uint256 hash, unsigned int nBits, const Consensus::Params& params);

/** Mortal Ledger: Proof of Quotation with OP_SOURCE succession, as a per-block canon
 *  state carried on the block index (reorg-/reindex-safe; see chain.h).
 *
 *  The canon transition is a PURE fold over ancestors: the state leaving a block is a
 *  function of the state entering it (= the parent's leaving state) and the successor
 *  the block registers in its coinbase. No process globals, so competing branches never
 *  contaminate each other and a restart recomputes identically. The block hash must
 *  carry the next slice of the active novel in its head bytes; concatenate the head
 *  bytes of the chain and the novel reappears. When the active novel is exhausted the
 *  block must register a successor and transcription continues into it; with none the
 *  chain starves (completion = death).
 *
 *  CanonState identifies the active novel by source_height — the height of the block
 *  that installed it (== Consensus::Params::nMortalLedgerHeight for the genesis novel;
 *  < 0 = inactive / pre-fork) — and the transcription offset reached. The novel's bytes
 *  are resolved on demand (genesis constant, or the source block's coinbase) and passed
 *  in, so this header stays free of block I/O. */
struct CanonState {
    int source_height{-1};
    uint64_t offset{0};
    uint32_t index{0};   // novel ordinal (0 = genesis); selects the local successor magazine entry at a seam
    bool active() const { return source_height >= 0; }
};

/** The canon state ENTERING the block at `height`, given its parent's leaving state.
 *  Below H: inactive. At H: the genesis novel begins. Above H: inherit the parent. */
CanonState CanonEnter(int height, const CanonState& parent_after, const Consensus::Params& params);

/** The K bytes this block must transcribe, given the entering state, the active novel's
 *  bytes (resolved from in.source_height) and any successor `reg` registered in THIS
 *  block (empty if none). When the active novel is exhausted the slice is read from reg;
 *  with neither, the result is empty => no valid block. */
std::vector<unsigned char> CanonExpectedSlice(const CanonState& in, const std::vector<unsigned char>& novel, const std::vector<unsigned char>& reg);

/** The canon state LEAVING the block at `height` (stored on its index), given the
 *  entering state, the active novel's bytes and the block's registration. Installs a
 *  registered successor at the seam where the active novel ends. Pure; no globals. */
CanonState CanonNext(const CanonState& in, const std::vector<unsigned char>& novel, int height, const std::vector<unsigned char>& reg);

/** Does the hash carry the slice in its low internal bytes? (PoW magnitude lives in the
 *  high bytes, so quotation and pace are orthogonal.) Empty slice => no valid block. */
bool HashCarriesSlice(const uint256& hash, const std::vector<unsigned char>& slice);

/** Mortal Ledger: the successor novel a block registers in its coinbase (empty if none),
 *  and the coinbase issuance = bytes transcribed this block × 1 BAB. */
std::vector<unsigned char> ExtractCanonRegistration(const CBlock& block);
/** All canon registrations (OP_SOURCE outputs) in the block's coinbase (normally 0 or 1). */
std::vector<std::vector<unsigned char>> ExtractCanonRegistrations(const CBlock& block);
CAmount CanonIssuance(const CanonState& in, const std::vector<unsigned char>& novel, const std::vector<unsigned char>& reg);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * This function only checks that the new value is within a factor of 4 of the
 * old value for blocks at the difficulty adjustment interval, and otherwise
 * requires the values to be the same.
 *
 * Always returns true on networks where min difficulty blocks are allowed,
 * such as regtest/testnet.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

#endif // BITCOIN_POW_H
