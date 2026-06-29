# Mortal Ledger — fork repo handoff (read this first)

A single page to resume work. This repo is the **fork node**; the design/reference lives in
the sibling repo `../mortal-ledger/` (`node/HANDOFF.md`, `node/MODEL.md`, `IDEA.md`).

## What this is

- `avcdsld/mortal-ledger-core`, branch **`mortal-ledger`** (the default branch; `master` is
  untouched upstream Bitcoin). Based on the **Bitcoin Core v31.0** release tag + the Mortal
  Ledger patch stack, ported as logical commits. Release **`ml-v0.1.0`** (with the
  `qwen3-1.7b.mlm` model attached as an asset). All commits are solely under the user's
  name (no Co-Authored-By — do not re-add).
- Mortal Ledger = a Bitcoin hard fork that mines a **novel** (Proof of Quotation) not money,
  mints **BAB** per byte transcribed, and runs a local **Qwen3-1.7B** for the voice
  (`OP_DREAM`/`OP_JUDGE`/`OP_TRANSLATE`). Designed to physically die.

## State: DONE and green

- Fork builds (`bitcoind`), boots regtest, currency unit `BAB`. **CI green** — the fork CI
  (`.github/workflows/mortal-ledger.yml`, ubuntu) builds + runs `test/mortal-ledger/run-checks.sh`
  = 7 real-Core link tests + a voice wire test (9/9) + 6 regtest demos. The inherited Bitcoin
  matrix CI was removed on this branch (it asserts vanilla-Bitcoin behavior the fork changes).
- `-privatebroadcast` is force-disabled (Bitcoin Core 31.0 IP-leak, fixed upstream in 31.1).
- **Voice (Qwen) is wired and fast.** `src/mortalllm.{h,cpp}` runs the integer fixed-point
  forward (no float/libm on the inference path → bit-identical across architectures, verified
  on x86 CI and arm64 Mac, fingerprint `f178c3dcf6ae61d8`, predicts " Paris"). Loaded from a
  pinned `.mlm` via `-mortalmodel`; the pin (`MORTAL_CANONICAL_MLM_SHA256 = 804b39e0…`) is
  enforced off regtest. Optimizations, all **determinism-preserving** (integer sums are
  order-independent): multithreaded matmuls, NEON SIMD (SDOT/vmull), and a **KV-cached
  streaming generator** (`MortalDreamBegin/Next/End`). On an 8-core Mac: ~**0.25 s/token**
  steady-state; a ~40-token dream after an ~80-token prompt is ~36 s.
- **DREAM = deterministic seeded sample** (fixed-point softmax + pinned SplitMix64 RNG seeded
  by the block). Same words + same block-seed → same dream; different block → different dream;
  all exactly reproducible. **JUDGE = greedy, temperature 0, seed-independent** (stable
  consensus lock). Temperature is a pinned constant `MORTAL_DREAM_TEMP_Q16` (default 1.0).

## Run / test it

Model is on this Mac at `/tmp/qwen3-1.7b.mlm` (sha `804b39e0…`); GGUF at
`/tmp/Qwen3-1.7B-Q8_0.gguf`. To regenerate: `test/mortal-ledger/gguf_convert.cpp` (see MODEL.md).

```bash
# build the node
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix boost);$(brew --prefix libevent);$(brew --prefix sqlite);$(brew --prefix pkgconf)" \
  -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF -DBUILD_GUI=OFF -DWITH_ZMQ=OFF -DBUILD_WALLET_TOOL=OFF -DENABLE_IPC=OFF
cmake --build build --target bitcoind bitcoin-cli -j8

# all fork checks (link tests + voice wire test + regtest demos)
bash test/mortal-ledger/run-checks.sh

# chat through the node's exact voice (integer, KV-cached). Rebuild next_token after any
# src/mortalllm.cpp change. shell is zsh -> use an array for the libs.
LIBS=(build/lib/libbitcoin_common.a build/lib/libbitcoin_consensus.a build/lib/libbitcoin_util.a \
      build/lib/libbitcoin_crypto.a build/src/univalue/libunivalue.a build/src/secp256k1/lib/libsecp256k1.a)
g++ -std=c++20 -I src -I build/src test/mortal-ledger/next_token.cpp src/mortalllm.cpp "${LIBS[@]}" -o /tmp/next_token
W="あいこくしん あいさつ あかちゃん あきる あける あさい あさひ あしあと"
pip install transformers   # tokenizer only
python3 test/mortal-ledger/chat_integer.py --model /tmp/qwen3-1.7b.mlm --bin /tmp/next_token --seed 1 --dream "$W"
#   --seed N changes the "block" (different reproducible dream); --max-new caps length (EOS stops earlier);
#   --instruction "..." is the candidate OP_DREAM template; --think shows Qwen reasoning (very slow).
```

## File map (the ML-specific parts)

- `src/mortalllm.{h,cpp}` — integer Qwen forward, sampler, KV-cached session, model pin.
- `src/init.cpp` — `-mortalmodel` load (AppInitMain) + `-privatebroadcast` force-off (InitParameterInteraction).
- `src/script/interpreter.cpp` — `OP_DREAM/JUDGE/TRANSLATE` call `g_mortal_llm` (set by MortalInstallLLM).
- `test/mortal-ledger/` — `run-checks.sh`, `link-tests/`, `demos/`, `gguf_convert.cpp`,
  `gen_toy_mlm.py`, `next_token.cpp`, `chat_integer.py`, `MODEL.md`, this file.
- `.github/workflows/mortal-ledger.yml` — the fork CI.

## Design decisions reached (don't relitigate)

- **Integer is the method for determinism** (not optional): float is non-associative + libm
  diverges across arch; integer is exact/order-independent → bit-identical AND optimizable.
- **Two axes: determinism vs consensus.** JUDGE = deterministic AND consensus (gates a spend).
  DREAM = deterministic but NOT consensus (a model-less node accepts the block without
  reproducing it).
- **Temperature does NOT break determinism**: the RNG is pinned + block-seeded, so temp only
  shapes the distribution the deterministic draw samples from. Tuning it is fine (pin it).
- **The `.mlm` ships as a hash-pinned Release asset**, not in git/binary; the node loads it.

## NEXT — in progress: #2 dream inscription (agreed design, NOT yet implemented)

Decision just made (correcting an earlier wrong idea of recomputing per RPC call): **the dream
is computed ONCE by the miner and inscribed in the block, so every node holds it; reading is
free; no per-call/per-node recompute.**

- **Inscribe**: `node/miner.cpp` builds the block's dream with the KV-cached `MortalDream`
  (fast) and writes it into an **unspendable coinbase output** (same mechanism style as
  `OP_SOURCE` carrying the novel). It propagates with the block; all nodes store it.
- **No circular hash dependency**: seed the dream from the **parent** hash (the existing
  "seed = parent hash" design). Block N's dream is a function of the already-fixed parent, so
  inscribing it in N does not change N's hash.
- **Validation**: the dream is NOT consensus. Model-having nodes MAY recompute-and-compare
  (for display/integrity); model-less nodes just store the inscribed bytes. So "all nodes hold
  the dream" even without the model. (Only OP_JUDGE is consensus.)
- **Read**: an RPC / index that READS the inscribed dream from a block (no recompute). The
  exhibition display reads this.
- Open sub-questions for next session: exact output format/size limits for the dream bytes;
  whether the dream stores token-ids or text (node has no tokenizer → likely token-ids, the
  display detokenizes); the pinned prompt template + token-count cap.

## Other pending

- **#1 exhibition display** — build it on a **separate machine** (a "window" that reads the
  node's dream over the network, no model), NOT on the mortal Pi. Depends on #2's read path.
- **#3 short prompt for the Pi** — the chat-template prompt is ~73 tokens; prompt fill is
  O(prompt length) and is the Pi's bottleneck. A terser `--instruction` gets ~49; a bare
  (non-chat) prompt could get ~20 (add `--no-template` to experiment). Tune on Mac, confirm on Pi.
- **#4 temperature** — just pick/pin a value; it's deterministic (see above).
- **Pi perf headroom** — a persistent thread pool (the per-call thread spawn limits scaling)
  and an AVX2 idot for x86 nodes are further determinism-safe levers.
- **Launch stage (HANDOFF §9, the big separate track)** — main `nMortalLedgerHeight` (real H),
  `mortalGenesisNovelHash` (full 猫町 SHA-256 with frozen formatting), difficulty target reset
  to Pi scale, ARM build, and igniting the network. main is currently dormant (H = -1).
