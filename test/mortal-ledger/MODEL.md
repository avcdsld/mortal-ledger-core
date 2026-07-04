# The voice model — provenance and pin

Mortal Ledger's voice (`OP_DREAM` / `OP_JUDGE` / `OP_TRANSLATE`) is a local Qwen run by
the node. Because `OP_JUDGE` can gate a spend, the voice is consensus and the weights are
strictly pinned. The weights themselves (large) are **not** in this repo — only the
provenance and hashes are. The conversion is deterministic, so anyone can re-derive the
canonical model from the official Qwen GGUF and verify the hash.

## What the node loads

The node loads a `.mlm` file (the canonical integer model) via `-mortalmodel=<path>`. It
does **not** load GGUF directly: the GGUF→`.mlm` conversion *is* the pinning step (it
fixes the int8 quantization and scales so the forward is bit-identical across
architectures). Off regtest, the node verifies the file's SHA-256 against the pin below
and refuses to start on mismatch (`src/mortalllm.cpp`, `MORTAL_CANONICAL_MLM_SHA256`).

## Source (official)

- Model: **Qwen3-1.7B** (Apache 2.0 — clean to pin, redistribute, and exhibit).
- File: `Qwen3-1.7B-Q8_0.gguf` (Hugging Face `Qwen/Qwen3-1.7B-GGUF`).
- Source GGUF SHA-256: `061b54daade076b5d3362dac252678d17da8c68f07560be70818cace6590cb1a`
- Arch (from GGUF metadata): `qwen3`, block_count 28, embedding_length 2048,
  head_count 16 / head_count_kv 8 (GQA), feed_forward_length 6144.

## Canonical model (.mlm)

Produced by `test/mortal-ledger/gguf_convert.cpp` (symmetric int8 per output channel with
an integer scale; 1-D norm tensors as Q16.16). Serialized as `MLM1` and SHA-256-pinned.

```
g++ -O2 -std=c++17 -I "$(brew --prefix openssl)/include" test/mortal-ledger/gguf_convert.cpp \
    -L "$(brew --prefix openssl)/lib" -lcrypto -o gguf
./gguf Qwen3-1.7B-Q8_0.gguf qwen3-1.7b.mlm
```

- Canonical `.mlm` SHA-256 (pin): `f15557797f9d1323a66ed6bca269ecb72a38e6ef1003d0e2050dbcb84f9eb46c`
- Size ~1728.1 MB. Reproducible: same input → byte-identical `.mlm` across `-O0`/`-O2`.

### Detokenizer vocab (`VOC1` section)

After the tensors, the `.mlm` carries an optional **`VOC1`** section: the tokenizer's
`id → raw bytes` map (151,936 tokens, ~1 MB), read by `gguf_convert.cpp` straight from the
GGUF's embedded tokenizer (`tokenizer.ggml.tokens` + `token_type`, byte-level/GPT-2 decoded;
CONTROL/USER_DEFINED tokens → empty). This is the "tokenizer travels with the model" approach
(like llama.cpp). The node loads it and turns a dream's token ids back into UTF-8 **in pure
C++** (`MortalDetokenize`) — detokenization is a flat lookup, unlike the BPE *tokenization*
that the prompt tables (`src/mortal_dream_prompt.h`) pin for the input side. Layout: `"VOC1"`,
`u64 count`, then per token `u16 len` + `len` bytes. A model with no section (the toy test
`.mlm`) simply has no vocab. The weights before `VOC1` are byte-identical to the pre-vocab
`.mlm` (`804b39e0…`), so inference is unchanged.

## Determinism (why integer)

`src/mortalllm.cpp` runs the fully fixed-point forward — int8×int8→int32 matmuls,
fixed-point RMSNorm/softmax/SiLU, and an integer-CORDIC RoPE table, with no float and no
libm on the inference path. The reference (`mortal-ledger/node/llm_forward.cpp`) verified
that the logits fingerprint is identical across `-O0`/`-O2`/`-Ofast` (incl. `-ffast-math`),
x86 gcc/clang, and **aarch64 under QEMU** — i.e. the one-bit `OP_JUDGE` verdict does not
split consensus across architectures. The float oracle ("The capital of France is" →
token 12095 = " Paris") confirms the architecture; that check needs the real weights.

## Distribution

The `.mlm` ships as a hash-pinned GitHub Release asset (the node loads it; it is not in
git or the binary). The source GGUF may also be attached for provenance. Each running node
holds the weights on its own wearing flash — the pinned original is distributed, but the
voice is only reproducible while some node is alive to compute it.
