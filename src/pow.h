// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/amount.h>
#include <consensus/params.h>

#include <cstdint>
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

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);

/** Mortal Ledger: Proof of Quotation with OP_SOURCE succession.
 *
 *  Canon state (current novel + transcription offset) lives in pow.cpp as
 *  process-global state, advanced when a block is connected. The block hash must
 *  carry the next slice of the novel in its head bytes; concatenate the head
 *  bytes of the chain and the novel reappears. When the active novel is fully
 *  transcribed, the block must register a successor (reg, carried in the coinbase)
 *  and transcription continues into it. With no successor the chain starves
 *  (completion = death).
 *
 *  - CanonExpectedSlice(reg): the K bytes the next block must carry, given the
 *    successor registration reg (empty if none). Empty result => no valid block.
 *  - CanonAdvance(reg): advance the canon after a block is accepted (real
 *    connection only); installs the successor at the seam.
 *  - HashCarriesSlice(hash, slice): does the hash carry the slice in its low
 *    internal bytes? (PoW magnitude lives in the high bytes, so the two
 *    predicates are orthogonal.) */
std::vector<unsigned char> CanonExpectedSlice(const std::vector<unsigned char>& reg);
void CanonAdvance(const std::vector<unsigned char>& reg);
bool HashCarriesSlice(const uint256& hash, const std::vector<unsigned char>& slice);

/** Mortal Ledger: the successor novel a block registers in its coinbase (empty if
 *  none), and the coinbase issuance = bytes transcribed this block × 1 BAB. */
std::vector<unsigned char> ExtractCanonRegistration(const CBlock& block);
CAmount CanonIssuance(const std::vector<unsigned char>& reg);

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
