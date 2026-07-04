# Mortal Ledger — fork repo handoff (read this first)

A single page to resume work. This repo is the **fork node**; the design/reference lives in
the sibling repo `../mortal-ledger/` (`node/HANDOFF.md`, `node/MODEL.md`, `IDEA.md`).

> **See also `HANDOFF-voice-display-devnet.md`** — a later block of work: the dream's rhythm (a
> dream once per 12-words-worth of transcription, words+seed from the parent hash, 6 words, live
> streaming), the `getdreaming`/`getmininghashes` RPCs, the **`mortaldev` devnet** (real fork chain
> from genesis), and the live exhibition display.

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
  = 7 real-Core link tests + a voice wire test (9/9) + a model-free dream-wiring test + 7 regtest
  demos (the 7th = `dream`, which self-skips without the model so CI stays green). The inherited Bitcoin
  matrix CI was removed on this branch (it asserts vanilla-Bitcoin behavior the fork changes).
- `-privatebroadcast` is force-disabled (Bitcoin Core 31.0 IP-leak, fixed upstream in 31.1).
- **Voice (Qwen) is wired and fast.** `src/mortalllm.{h,cpp}` runs the integer fixed-point
  forward (no float/libm on the inference path → bit-identical across architectures, verified
  on x86 CI and arm64 Mac, fingerprint `f178c3dcf6ae61d8`, predicts " Paris"). Loaded from a
  pinned `.mlm` via `-mortalmodel`; the pin (`MORTAL_CANONICAL_MLM_SHA256 = f1555779…`) is
  enforced off regtest. The `.mlm` also carries a `VOC1` detokenizer vocab (id→bytes) so the
  node turns dream tokens back into text in pure C++ (`MortalDetokenize`); weights are byte-
  identical to the pre-vocab `.mlm` (804b39e0…), only the vocab section is appended. Optimizations, all **determinism-preserving** (integer sums are
  order-independent): multithreaded matmuls, NEON SIMD (SDOT/vmull), and a **KV-cached
  streaming generator** (`MortalDreamBegin/Next/End`). On an 8-core Mac: ~**0.25 s/token**
  steady-state; a ~40-token dream after an ~80-token prompt is ~36 s.
- **DREAM = deterministic seeded sample** (fixed-point softmax + pinned SplitMix64 RNG seeded
  by the block). Same words + same block-seed → same dream; different block → different dream;
  all exactly reproducible. **JUDGE = greedy, temperature 0, seed-independent** (stable
  consensus lock). Temperature is a pinned constant `MORTAL_DREAM_TEMP_Q16` (default 1.0).

## Run / test it

Model is on this Mac at `/tmp/qwen3-1.7b.mlm` (sha `f1555779…`); GGUF at
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

- `src/mortalllm.{h,cpp}` — integer Qwen forward, sampler, KV-cached session, model pin,
  `MortalModelLoaded()`, `VOC1` vocab load + `MortalDetokenize` (id→text, pure C++).
- `src/mortaldream.{h,cpp}` — block dream: parent-hash→12 word indices, prompt assembly from
  the pinned tables, `MortalBlockDream` (EOS/cap).
- `src/mortal_dream_prompt.h` — AUTO-GENERATED pinned token tables (gen_dream_prompt.py).
- `src/pow.{h,cpp}` — dream inscription (`ExtractDreamInscriptions` / `IsDreamInscription` /
  `MakeDreamInscription`, OP_RETURN+`MLD1` magic) next to the OP_SOURCE **novel** state machine
  (`NovelState` / `NovelEnter/Next/ExpectedSlice/Issuance` / `ExtractNovelRegistration(s)` — the
  old `Canon*` names were renamed to `Novel*` throughout; reject strings are `bad-novel-*`).
- `src/node/miner.cpp` — detokenizes the dream and inscribes it as text; `-mortaldreammaxnew`.
- `src/rpc/blockchain.cpp` — `getblockdream` (dream `text` + words) and `getnovel [chars]`
  (`{active,index,offset,total,tail,current,done}` — the transcription state as a bounded tail
  window + the character being transcribed; never the whole novel).
- `src/init.cpp` — `-mortalmodel` load (AppInitMain) + `-mortaldreammaxnew` + `-privatebroadcast` force-off.
- `src/script/interpreter.cpp` — `OP_DREAM/JUDGE/TRANSLATE` call `g_mortal_llm` (set by MortalInstallLLM).
- `test/mortal-ledger/` — `run-checks.sh`, `link-tests/` (incl. `dream_test.cpp`), `demos/`
  (incl. `dream_demo.sh`), `display/` (the #1 exhibition window), `run-local.sh` (hands-on regtest
  node), `gguf_convert.cpp` (now also carries the tokenizer→`VOC1`), `gen_toy_mlm.py`,
  `gen_dream_prompt.py`, `sweep_prompts.sh`/`try_prompt.sh` (dream-instruction tuning),
  `next_token.cpp`, `chat_integer.py`, `MODEL.md`, this file.
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

## DONE: #2 dream inscription (implemented + verified end-to-end)

**The dream is computed ONCE by the miner and inscribed in the block, so every node holds it;
reading is free; no per-call/per-node recompute.** Proven on regtest: a mined block carries its
dream as **readable UTF-8 text**; `getblockdream` returns it directly (`present: true`,
`text: "目の前に水面が広がり、ちらみが揺れる。あらいぐまが小さく歩き…"`) — no model, no
tokenizer, no Python to read it.

- **What it dreams**: 12 BIP39-Japanese words drawn from the **parent** hash (first 16 bytes,
  the OP_MNEMONIC bit-reading), framed by a pinned instruction `夢で見た情景を数行で描写して。
  タイトルや前置きは無く本文のみ。手がかりの言葉:` (chosen via `sweep_prompts.sh` — a scene
  framing that drops the "もちろん…" preamble; still variable per seed, see #3) and seeded by
  that same parent hash. Both words and seed come from the
  already-fixed parent → the dream is a pure function of the parent, so inscribing it in the
  child does NOT change the child's hash (no circular dependency).
- **No tokenizer on the node**: the prompt is assembled from **pinned token-id tables**
  (`src/mortal_dream_prompt.h`, generated by `gen_dream_prompt.py`): template PREFIX+instruction,
  a 2048-word→tokens table, the `、` separator, SUFFIX, EOS. Verified bit-identical to the real
  Qwen `apply_chat_template` on 200/200 random 12-word draws — so `chat_integer.py` previews
  exactly what the node inscribes.
- **Detokenize in the node, store TEXT** (decided with the artist, symmetric with the OP_SOURCE
  novel which is also on-chain text): the miner generates tokens (`MortalBlockDream`, KV-cached,
  stops at EOS or `-mortaldreammaxnew`, default 256), then **`MortalDetokenize` turns them into
  UTF-8 in pure C++** and inscribes the text. Detok is a flat `id→bytes` lookup (≠ tokenization's
  BPE), carried in the `.mlm`'s optional **`VOC1` section** (`gguf_convert.cpp` reads the
  tokenizer the source GGUF already embeds — the standard "tokenizer travels with the model" way,
  like llama.cpp). So a model-less node needs nothing to READ the dream; only the miner detokens.
- **Inscribe**: an **unspendable `OP_RETURN` coinbase output** `OP_RETURN <MLD1-magic ++ UTF-8
  text>`, trimmed to `MAX_DREAM_BYTES` on a char boundary. NB: the marker is OP_RETURN, **not**
  OP_DREAM (a functional opcode → would be spendable and would run the seed-dependent voice
  during validation = consensus hazard).
- **Validation** (`pow.cpp` `ExtractDreamInscriptions` / `IsDreamInscription`; `validation.cpp`;
  `tx_check.cpp`): at most one dream output, capped at `MAX_DREAM_BYTES` (4096), granted a
  size/weight exemption like the OP_SOURCE novel. The dream's CONTENT is **never** validated
  (NOT consensus) — model-less nodes just store the bytes. (Only OP_JUDGE is consensus.)
- **Read**: `getblockdream <hash>` RPC returns `{present, height, seed(parent hash), words[12],
  text}` — the readable dream, no recompute. The exhibition display (#1) reads this.
- **Tuning knob**: `-mortaldreammaxnew=<n>` caps dream length (0 disables inscription); non-
  consensus mining policy.

## Other pending

- **#1 exhibition display** — DONE (first version): `test/mortal-ledger/display/` (`server.py` +
  `index.html`), a fullscreen browser "window" for a **separate machine** (no model, no
  tokenizer). Shows three things in real time — the novel tail with the character under the pen,
  the latest dream, the 12 words — via `waitfornewblock` + `getnovel` + `getblockdream`. Run
  `python3 display/server.py` (or `--demo` to preview without a node); `--kiosk` in Chrome.
  Refine: typography/animation, dream history, the death (`done`) state.
- **#3 short prompt for the Pi** — the pinned dream prompt is now PREFIX(~20) + 12 words + SUFFIX(9)
  ≈ 70–110 tokens (varies by words); prompt fill is O(prompt length) and is the Pi's bottleneck.
  To shorten, pick a terser `--instruction` (tune in `chat_integer.py`), then **regenerate**
  `src/mortal_dream_prompt.h` via `gen_dream_prompt.py` to re-pin it. Confirm on Pi.
- **#4 temperature** — just pick/pin a value; it's deterministic (see above).
- **Pi perf headroom** — a persistent thread pool (the per-call thread spawn limits scaling)
  and an AVX2 idot for x86 nodes are further determinism-safe levers.
- **Launch stage (HANDOFF §9, the big separate track)** — main `nMortalLedgerHeight` (real H),
  `mortalGenesisNovelHash` (full 猫町 SHA-256 with frozen formatting), difficulty target reset
  to Pi scale, ARM build, and igniting the network. main is currently dormant (H = -1).

## devnet — a REAL fork chain from genesis (not regtest)

- New chain type **`mortaldev`** (`-chain=mortaldev`): own genesis, own network magic (`MLDV`),
  P2P/RPC ports **28333/28332**, address prefix **`mldev1…`**; fork rules from block 1
  (`nMortalLedgerHeight=1`); **real LWMA difficulty** (`fPowNoRetargeting=false`, no min-difficulty
  escape — verified: difficulty holds trivial until the LWMA window fills at block 62, then moves).
  Defined in `kernel/chainparams.cpp` (`CMortalDevParams`) + `util/chaintype.*` +
  `chainparamsbase.cpp`. Genesis novel pin defaults to the ASCII test novel; `-mortalgenesishash`
  overrides it (e.g. 猫町). Seeds empty — wire a small network with `-addnode`/`-connect`.
- **Verified real operation**: a node mines from genesis; a 2nd node syncs the fork over P2P
  **without any novel config** (the novel is delivered on-chain, so peers only validate).
- **Run it**: `bash test/mortal-ledger/run-devnet.sh` — starts the node, auto-mines one character
  per block (voice on), prints the display command (`server.py --rpcport 28332 --rpccookie …`).
- **`getmininghashes` RPC** (`rpc/mining.cpp`): a sampled ring (1 in ~20000 tries, window 512) of
  the hashes the miner TRIED while grinding for the next character — the visible labour of Proof of
  Quotation (a kanji ≈ 16M tries). Each entry: `{nonce, hash, matched}` (leading quotation bytes
  that lined up). Non-consensus, for the exhibition display. (Grind loop also now hashes once/try.)

## Transcription unit = ONE CHARACTER (not one byte)

- **k = the UTF-8 byte length of the character at the current offset** (1..4), computed in
  `pow.cpp` `NovelSliceLen`. So **1 block writes 1 whole character**, and the Proof-of-Quotation
  work is proportional to the character's byte length: a 3-byte kanji ≈ 2^24 grinds (~1.7 s on a
  Pi / ~2.8 s measured on the Mac), a 1-byte newline ≈ 2^8 (instant). Heavier characters cost more
  work — the labour of writing a character IS the character. **LWMA** tunes the average block time
  on top. Issuance stays **per byte** (a kanji block mints 3 BAB). `CheckPaceTarget` zeros a fixed
  `MAX_QUOTATION_K = 4` low bytes (a header check can't know k; negligible magnitude effect).
- **Mining needs a high `maxtries`**: `generatetoaddress N addr <maxtries>` — the 1M default fails
  on a multi-byte character (needs ≥ ~16M). `run-local.sh` passes `MORTAL_MAXTRIES` (default 300M).
- **Demos use ASCII test novels** (all 1-byte chars → k=1 → char==byte, so the byte-based demo
  logic/counts are unchanged and CI stays fast). The multi-byte path is covered by
  `link-tests/charunit_test.cpp` + the real 猫町 run. The regtest genesis pin is now
  `SHA-256("Call me Ishmael. Some years ago, having little money, I went to sea.")`.

## Genesis / next-novel supply (how the book gets on-chain)

- The genesis novel is supplied to the block-H miner as a **FILE** (`-mortalgenesisfile=<path>`;
  the literal `-mortalgenesis=<text>` is only for tiny test strings). Its SHA-256 is pinned by
  consensus (`mortalGenesisNovelHash`); consensus checks `SHA-256(the OP_SOURCE bytes) == pin`.
- Continuation books ride the local magazine: **`-mortalnextnovelfile`** (repeatable; formerly
  `-mortalsuccessor*` — renamed everywhere, incl. the `listnextnovels` RPC). Non-consensus.
- **Frozen 猫町**: `test/mortal-ledger/novels/nekomachi.txt` — 萩原朔太郎『猫町』, LF-normalized,
  31,070 bytes / **10,432 characters**, raw SHA-256 `4df470c2d7b3634dabb08c11ad0a1c81859122792da2b5cd84ea64a0562f8c8f`
  (= **10,432 blocks** to transcribe at one character/block; difficulty changes time-per-block, not the count).
- **Run 猫町 on regtest now**: the compiled regtest genesis stays the short test novel (so the
  demos' block-count assertions hold), but `-mortalgenesishash=<raw sha>` (regtest only) overrides
  it, so `run-local.sh` transcribes the real 猫町 (it computes the file's hash and passes both).
  Point `MORTAL_GENESIS_FILE` at any book to transcribe it.
