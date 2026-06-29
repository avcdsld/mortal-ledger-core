// Copyright (c) 2026 The Mortal Ledger developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MORTALLLM_H
#define BITCOIN_MORTALLLM_H

#include <string>

/** Mortal Ledger: load the pinned canonical voice model (.mlm) and install its
 *  integer-deterministic forward behind g_mortal_llm, so OP_JUDGE / OP_DREAM /
 *  OP_TRANSLATE run the real Qwen during script evaluation instead of the built-in
 *  FNV stub. The forward is the fully fixed-point path (no float, no libm), so the
 *  one-bit OP_JUDGE verdict is bit-identical on any conforming integer hardware —
 *  required because OP_JUDGE can gate a spend (consensus). Throws std::runtime_error
 *  if the model cannot be read. Hyperparameters are derived from the model's tensor
 *  shapes; the .mlm is hash-pinned, so its header is pinned too. */
void MortalInstallLLM(const std::string& mlm_path);

#endif // BITCOIN_MORTALLLM_H
