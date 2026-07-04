# Handoff — the voice's rhythm, the living display, and the devnet

This document covers a block of work layered on top of the base Mortal Ledger fork (see
`HANDOFF.md` for the base: Proof-of-Quotation, 写字本位 issuance, the integer voice, char-unit
transcription, etc.). It is self-contained: what changed, WHY, and WHERE.

Everything here is verified green by `bash test/mortal-ledger/run-checks.sh`.

---

## 1. The dream, rethought — rhythm, words, and live streaming

### 1a. A dream once per 12-words-worth of transcription (not every block)
Previously every block inscribed a dream, so block time was dream-bound (~40s each). Now a dream is
inscribed **only by the block that crosses a `MORTAL_DREAM_SEGMENT_BYTES` (=16) boundary** of the
transcription. So the ledger carves several characters fast (quotation grind only, ~3s each) and
then **pauses to dream** on the boundary block — a rhythm ("すいすい彫る → 夢で止まる → また彫る").

- Where: `src/node/miner.cpp`, in `CreateNewBlock` — the dream block is gated by
  `(leaving_off / 16) > (eff_off / 16)` where `eff_off` is the entering byte offset and `leaving`
  adds the character's byte length. Successor-seam offset resets to 0.
- Constant: `MORTAL_DREAM_SEGMENT_BYTES` in `src/mortaldream.h`.
- Still **non-consensus** (the miner inscribes; validation does not yet recompute). See §6.

### 1b. Words + seed from the PARENT hash (per-chain variation)
The dream's reference words and its sampling seed both come from the **parent block hash** — a pure
function of the already-fixed parent (no circular hash dependency), and it **varies per chain**, so
the *same novel dreams differently on different chains* (the "副音声 / 一回性" property). The words
come from the hash's **low bytes** (`begin()[0..]` = the display hash's tail, the entropy-rich end —
the leading zeros of the pace magnitude are at the HIGH/front end, not here). We deliberately did
NOT tie the words to the transcribed text, because that would make 猫町 always produce the identical
dream diary.

### 1c. Mnemonic word count: 6 (was 12)
- Constant: `MORTAL_DREAM_WORDS` in `src/mortaldream.h` (currently **6**). `MortalDreamWordIndices`
  reads the first `WORDS*11` bits of the parent hash as 11-bit BIP39 groups (OP_MNEMONIC convention).
- Prompt assembly (`MortalComposeDreamPrompt`) is unchanged — it loops over however many words.
- To change the count, edit the one constant. `dream_test.cpp` asserts against
  `MORTAL_DREAM_WORDS`, so the test stays correct.

### 1d. Live streaming of the dream as it is generated
`MortalBlockDream` now pushes each generated token into a small mutex-guarded buffer as it goes, so
a display can watch the voice *write the dream out live* during the ~40s pause.
- Where: `src/mortaldream.cpp` — `g_dreaming_*` buffer + `MortalDreamingSnapshot(active, seq, tokens)`.
- The buffer uses its **own mutex, NOT cs_main**, so the RPC answers even while the miner holds
  cs_main inside `CreateNewBlock`.

---

## 2. RPCs added (all non-consensus, for the display)

| RPC | Returns | Notes |
|-----|---------|-------|
| `getdreaming` | `{active, seq, tokens, text}` | The dream being generated right now, token by token. No cs_main. `seq` bumps per new dream. `src/rpc/blockchain.cpp`. |
| `getmininghashes` | `[{nonce, hash, matched}]` | A sampled ring (1 in ~20000 tries, window 512) of the hashes the miner TRIED while grinding — the labour of the quotation. `matched` = leading quotation bytes that lined up. `src/rpc/mining.cpp` (the grind loop also now hashes once per try). |
| `getnovel` | added `chars` | Characters transcribed so far = post-fork block count (one char/block). Lets a display lay text into fixed columns by absolute position. |

---

## 3. The devnet — a REAL fork chain from genesis (`-chain=mortaldev`)

A genuine fork chain (not regtest) so a real node can mine from its own genesis, with real P2P and
real difficulty. See the `## devnet` section of `HANDOFF.md` for the base details. Key points:

- New chain type **`mortaldev`**: own genesis, magic `MLDV`, ports 28333/28332, address prefix
  `mldev1…`; fork from block 1; **LWMA real difficulty** (fires at block 62). Files:
  `src/util/chaintype.*`, `src/chainparamsbase.cpp`, `src/kernel/chainparams.cpp`
  (`CMortalDevParams`), `src/chainparams.cpp` (dispatch + `-mortalgenesishash` override).
- **`nPowTargetSpacing = 10` seconds** (was 30). This is tuned to ≈ the natural dream-dominated
  average block time (fast quotation-only blocks + one ~40s dream every 16 bytes ≈ 9s/block), so
  **LWMA does NOT raise the pace on the fast blocks** — this preserves the rhythm from §1a. A higher
  target makes LWMA grind the fast blocks up to fill the time, flattening the rhythm. (Measured:
  past block 62 the difficulty stays ~1.4× the trivial floor; fast blocks stay ~3s, dream blocks are
  the pauses.)
- **`test/mortal-ledger/run-devnet.sh`** — starts the node, loads/creates the `ml` wallet robustly
  (refuses to start the miner with an empty address — a resume bug we hit), auto-mines one character
  per block, prints the display command. `MORTAL_FRESH=1` wipes; `MORTAL_AUTOMINE=0` mines by hand.

Run it:
```
bash test/mortal-ledger/run-devnet.sh                 # resume + auto-mine + display command
MORTAL_FRESH=1 bash test/mortal-ledger/run-devnet.sh  # fresh chain from genesis
```

---

## 4. The display — a live window (`test/mortal-ledger/display/`)

A dependency-free Python server + a single HTML page. Meant to run on a machine watching a node over
RPC (locally, it watches the devnet node). No model or tokenizer needed on the display side.

### 4a. server.py — three watcher threads
- `watch` — the novel (`getnovel`) + the latest inscribed dream (walk-back). Takes cs_main, so it
  blocks during a dream (fine — the novel does not change then). `latest_dream` re-runs only on a
  new block.
- `watch_dreaming` — `getdreaming` on its OWN thread (no cs_main) so the live dream keeps flowing
  while the miner holds cs_main generating it.
- `watch_mining` — `getmininghashes` on its own thread, fast (~250ms), for the pen flicker.
- All shared in `STATE`, served at `/state`. Run:
  `python3 server.py --rpcport 28332 --rpccookie ~/mortal-devnet/mortaldev/.cookie`

### 4b. index.html — what it shows
- **Two FIXED halves** (absolute 50vw each): the **dream** on the LEFT (warm, bright — the living
  voice), the **novel** on the RIGHT (cool, dim grey — the carved record). Fixed halves mean the
  dream growing/shrinking as it is written **never moves the novel**.
- **Novel** = fixed-height vertical columns by absolute character position. A full column is stable;
  when the page is full a new column starts and the **oldest (rightmost) column drops WHOLE** (not a
  per-character scroll). Older columns **fade** toward transparency (impermanence). The pen (current
  character) is leftmost, on the centre line, and stays put. Column length (`CPC`) is computed
  **responsively** to fill the same height as the dream (`computeCPC` measures a CJK char's advance);
  `line-height` and `gap` are matched so the 行間 equals the dream's.
- **Dream** = written **one character at a time** (typewriter, ~25 char/s toward the streamed
  target from `getdreaming`), a blinking cursor `▍` trailing it. A new dream (seq change) restarts
  the reveal.
- **The pen (mining)** = while the miner grinds, the current-character slot **flickers through REAL
  tried hashes decoded as UTF-8** — `attemptChar` reads the low k bytes (the quotation region = the
  hash's tail, reversed) and decodes them, usually to a tofu glyph (□/�). The instant a character is
  quoted into being (the novel offset advances) it **flashes** (`.born`) as it joins the record.
- **Meta** (bottom-right): `<N> ブロック　<M> バイト` (block number + bytes; not a fraction).

### 4c. Tuning knobs (in index.html)
- `CPC` — auto-computed to fill the height; `MAX_COLS` (=7) columns shown before the oldest drops.
- Colors in `:root` (`--novel` cool, `--dream` warm, `--live` pen).
- Pen flicker rate (`drawPen` setTimeout, ~55ms) and typewriter rate (`drawDream`, ~40ms).
- Age-fade curve (`0.22 + 0.78*age`), top/bottom mask (8%/92%).

---

## 5. Verified

`run-checks.sh` is green, including: `dream` (now 6 words, 63-token prompt), `demo dream` (mines to
the first 16-byte boundary — block 16 on the ASCII novel — and reads the dream there), and the
devnet chaintype (`chainparams` demo). The devnet was exercised end to end: mine from genesis, 2-node
P2P sync (the sync node validates the on-chain novel without any novel config), LWMA firing at block
62, the rhythm holding past 62 at the 10s target, live dream streaming through `/state`, and the pen
flicker fed by `getmininghashes`.

---

## 6. NOT done / next

- **Dream consensus (assumevalid)** — the big pending item, already agreed in principle. Make the
  dream deterministic-and-verified: validation recomputes the dream (segment/words + parent-hash
  seed → the same tokens) and rejects a mismatch; use an assumevalid-style checkpoint so initial
  sync does not pay the ~40s/block dream cost for buried blocks (only recent blocks re-verify).
  Consequence: the model becomes a full-node requirement (consistent with OP_JUDGE), and the dream
  parameters (`MORTAL_DREAM_WORDS`, `max_new`, temperature, prompt, `MORTAL_DREAM_SEGMENT_BYTES`)
  become pinned consensus constants.
- **Pi / arm64** — a real board is now on hand; verify the integer voice is bit-identical to x86
  (cross-arch determinism), build for ARM, and measure real block times on the Pi.
- **Death / completion handling** — what happens when the novel is fully transcribed (the chain
  starves) and how BAB is spent. Deferred.
- **Grind-flicker refinement idea** — bias the pen's attempted characters toward the real one as
  `matched` grows (near-misses look "closer" to the answer), instead of uniform tofu.
- **Full BTC state inheritance** — deferred to just before release; the devnet starts from a fresh
  genesis for now.
