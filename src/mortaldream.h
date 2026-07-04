// Copyright (c) 2026 The Mortal Ledger developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MORTALDREAM_H
#define BITCOIN_MORTALDREAM_H

#include <vector>

class uint256;

/** Mortal Ledger: the dream a block inscribes.
 *
 *  A dream is inscribed once per 12-words-worth of transcription (a MORTAL_DREAM_SEGMENT_BYTES
 *  boundary), NOT every block — so the ledger carves several characters fast, then pauses to
 *  dream: a rhythm. A dream is the integer voice's reading of 12 BIP39-Japanese words drawn from
 *  the *parent* hash, seeded by that same parent hash. Because both the words and the seed come
 *  from the already-fixed parent, the dream is a pure function of the parent and inscribing it in
 *  the child does not change the child's hash (no circular dependency); and because the parent
 *  hash varies per chain, the same novel dreams differently on different chains. The
 *  dream is NOT consensus: a model-less node just stores the inscribed token ids; the display
 *  detokenizes them. The miner computes it once (KV-cached) and writes it to an unspendable
 *  OP_DREAM coinbase output, so every node holds it without a model or a tokenizer. */

/** The hard cap on a dream's length in tokens (it stops earlier at an EOS id). */
inline constexpr int MORTAL_DREAM_MAX_NEW = 256;

/** A dream is inscribed once per this many transcribed bytes — 12 BIP39 words' worth (the word
 *  reading takes 16 bytes). So the ledger carves several characters quickly, then pauses to dream:
 *  a rhythm. The dream's 12 words + seed still come from the parent hash (varies per chain), so the
 *  same novel dreams differently on different chains. The block that crosses a 16-byte boundary
 *  carries the dream; other blocks carry none (and mine fast). */
inline constexpr int MORTAL_DREAM_SEGMENT_BYTES = 16;

/** How many BIP39-Japanese reference words the dream's prompt draws from the parent hash. */
inline constexpr int MORTAL_DREAM_WORDS = 6;

/** The MORTAL_DREAM_WORDS word indices the parent hash names: its first MORTAL_DREAM_WORDS*11 bits
 *  read as 11-bit groups (the OP_MNEMONIC convention), each in [0, 2047]. */
std::vector<int> MortalDreamWordIndices(const uint256& parent_hash);

/** The dream prompt (token ids) for those word indices, assembled from the pinned tables in
 *  src/mortal_dream_prompt.h: PREFIX ++ word[w0] ++ SEP ++ ... ++ word[w11] ++ SUFFIX. No
 *  tokenizer is involved; this concatenation IS the canonical prompt. */
std::vector<int> MortalComposeDreamPrompt(const std::vector<int>& word_indices);

/** Generate a block's dream: compose the prompt from parent_hash, run the integer voice seeded
 *  by `seed` (the parent hash), and stop at an EOS id or after max_new tokens. Returns the
 *  GENERATED token ids only (the prompt is not included). Returns empty if no model is loaded. */
std::vector<int> MortalBlockDream(const uint256& parent_hash, const uint256& seed,
                                  int max_new = MORTAL_DREAM_MAX_NEW);

/** A snapshot of the dream being generated right now (for a live display): `active` is true while
 *  the voice is dreaming, `seq` bumps on each new dream (detect a fresh stream), `tokens` are the
 *  ids produced so far. Thread-safe; does not touch cs_main. Detokenize the ids to read the text. */
void MortalDreamingSnapshot(bool& active, uint64_t& seq, std::vector<int>& tokens);

#endif // BITCOIN_MORTALDREAM_H
