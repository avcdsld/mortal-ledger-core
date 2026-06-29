// Copyright (c) 2026 The Mortal Ledger developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MORTALLLM_H
#define BITCOIN_MORTALLLM_H

#include <string>

/** Mortal Ledger: SHA-256 of the canonical voice model (qwen3-1.7b.mlm), produced by
 *  test/mortal-ledger/gguf_convert.cpp from the pinned Qwen3-1.7B-Q8_0 GGUF. The
 *  conversion is deterministic, so anyone can re-derive this and verify it. See
 *  test/mortal-ledger/MODEL.md. */
inline constexpr const char* MORTAL_CANONICAL_MLM_SHA256 =
    "804b39e0f63ed788692052244ea4563a76389c6257ea3380d0ca92e4861ea8b7";

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

#endif // BITCOIN_MORTALLLM_H
