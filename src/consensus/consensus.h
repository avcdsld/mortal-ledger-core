// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <cstdint>
#include <cstdlib>

/** Mortal Ledger: the largest single novel an OP_SOURCE registration may carry. A
 *  seam block is granted a weight/size exemption of up to this many bytes so it can carry a
 *  whole new novel in one block; larger texts must be split across successive seams. */
static const unsigned int MAX_NOVEL_BYTES = 4000000;
/** Mortal Ledger: the largest a block's inscribed dream may be (the pushed bytes: a 4-byte
 *  magic + u32-LE token ids). The dream is data, not consensus, but the block is granted a
 *  matching size/weight exemption up to this cap, so a dream cannot bloat a block without
 *  bound. ~256 tokens (the default cap) is ~1 KB; this leaves generous headroom. */
static const unsigned int MAX_DREAM_BYTES = 4096;
/** The maximum allowed size for a serialized block, in bytes (network/disk rule). Raised by
 *  MAX_NOVEL_BYTES so a seam block carrying a novel fits on the wire and on disk. */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 4000000 + MAX_NOVEL_BYTES;
/** The maximum allowed weight for a block, see BIP 141 (network rule) */
static const unsigned int MAX_BLOCK_WEIGHT = 4000000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

static const int WITNESS_SCALE_FACTOR = 4;

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid serialized CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for the size of a serialized CTransaction

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);

/**
 * Maximum number of seconds that the timestamp of the first
 * block of a difficulty adjustment period is allowed to
 * be earlier than the last block of the previous period (BIP94).
 */
static constexpr int64_t MAX_TIMEWARP = 600;

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
