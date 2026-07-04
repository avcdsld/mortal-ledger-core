// Copyright (c) 2026 The Mortal Ledger developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MORTALLLM_H
#define BITCOIN_MORTALLLM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class uint256;

/** Mortal Ledger: SHA-256 of the canonical voice model (qwen3-1.7b.mlm), produced by
 *  test/mortal-ledger/gguf_convert.cpp from the pinned Qwen3-1.7B-Q8_0 GGUF. The
 *  conversion is deterministic, so anyone can re-derive this and verify it. See
 *  test/mortal-ledger/MODEL.md. */
inline constexpr const char* MORTAL_CANONICAL_MLM_SHA256 =
    "f15557797f9d1323a66ed6bca269ecb72a38e6ef1003d0e2050dbcb84f9eb46c";

/** Mortal Ledger: load the pinned canonical voice model (.mlm) and install its
 *  integer-deterministic forward behind g_mortal_llm, so OP_JUDGE / OP_DREAM /
 *  OP_TRANSLATE run the real Qwen during script evaluation instead of the built-in
 *  FNV stub. The forward is the fully fixed-point path (no float, no libm), so the
 *  one-bit OP_JUDGE verdict is bit-identical on any conforming integer hardware —
 *  required because OP_JUDGE can gate a spend (consensus).
 *
 *  If expected_sha256_hex is non-empty, the file's SHA-256 must equal it (lower-case
 *  hex), else this throws: a node must run the exact pinned weights or its OP_JUDGE
 *  bits could diverge and split consensus. Pass "" to skip the check (regtest/tests
 *  with a toy model). Also throws std::runtime_error if the model cannot be read.
 *  Hyperparameters are derived from the model's tensor shapes. */
void MortalInstallLLM(const std::string& mlm_path, const std::string& expected_sha256_hex = "");

/** Mortal Ledger: true once a voice model has been loaded (MortalInstallLLM succeeded). The
 *  dream generator (MortalDreamBegin/Next and MortalBlockDream) needs the weights, so the
 *  miner/RPC guard on this before attempting to dream. */
bool MortalModelLoaded();

/** Mortal Ledger: turn a dream's token ids back into UTF-8 text using the .mlm's pinned
 *  detokenizer vocab (id -> raw bytes; the optional VOC1 section). Pure C++ table lookup +
 *  byte concatenation — no tokenizer, no Python. Drops an incomplete trailing UTF-8 sequence
 *  (byte-level BPE can split a character across tokens) and bounds the output to max_bytes on
 *  a character boundary. Returns empty if the model has no vocab section (toy model). */
std::string MortalDetokenize(const std::vector<int>& tokens, std::size_t max_bytes = SIZE_MAX);

/** Mortal Ledger: a streaming dream generator with a KV cache. Bit-identical to iterating
 *  the stateless forward, but each token costs ~one position instead of reprocessing the
 *  whole prompt, so multi-token generation is roughly constant per token regardless of
 *  prompt length. Tokens are seeded-sampled (same prompt + same seed -> same dream). A
 *  model must be installed (MortalInstallLLM) first. Begin processes the prompt; each Next
 *  returns the next token; End frees the session. */
struct MortalDreamSession;
MortalDreamSession* MortalDreamBegin(const std::vector<int>& prompt, const uint256& seed);
int MortalDreamNext(MortalDreamSession* session);
void MortalDreamEnd(MortalDreamSession* session);

#endif // BITCOIN_MORTALLLM_H
