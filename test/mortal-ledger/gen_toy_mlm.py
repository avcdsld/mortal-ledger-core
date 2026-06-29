#!/usr/bin/env python3
# Generate a tiny .mlm voice model for wiring/determinism tests. It has the same
# architecture as the real pinned model (RMSNorm, QK-norm, RoPE, GQA, SwiGLU, tied
# lm_head) but minuscule dimensions, so src/mortalllm.cpp can load it and run a real
# forward in CI without the 1.7 GB model. Hyperparameters are derived from tensor
# shapes by mortalllm.cpp, so these toy dims are picked up automatically. Weights are
# deterministic (fixed PRNG seed); their values are meaningless — the test checks the
# pipeline and the integer determinism, not " Paris" (that needs the real model).
#
# Usage: python3 gen_toy_mlm.py <out.mlm>
import struct, sys, random

D, VOCAB, N_HEAD, N_KV, HD, FF, N_LAYER = 8, 32, 2, 1, 4, 16, 2
random.seed(0)

def q8(n):   return bytes((random.randint(-100, 100) & 0xff) for _ in range(n))
def scal(n): return b"".join(struct.pack("<q", 1 << 20) for _ in range(n))
def q16(n):  return b"".join(struct.pack("<i", 65536) for _ in range(n))

out = bytearray()
out += b"MLM1"

defs = []
defs.append(("token_embd.weight", [D], 0, VOCAB * D, VOCAB))   # in=D, out=VOCAB
defs.append(("output_norm.weight", [D], 1, D, 0))
for l in range(N_LAYER):
    p = f"blk.{l}."
    defs.append((p + "attn_norm.weight",   [D],  1, D, 0))
    defs.append((p + "attn_q_norm.weight", [HD], 1, HD, 0))
    defs.append((p + "attn_k_norm.weight", [HD], 1, HD, 0))
    defs.append((p + "ffn_norm.weight",    [D],  1, D, 0))
    defs.append((p + "attn_q.weight",      [D], 0, (N_HEAD * HD) * D, N_HEAD * HD))
    defs.append((p + "attn_k.weight",      [D], 0, (N_KV * HD) * D,   N_KV * HD))
    defs.append((p + "attn_v.weight",      [D], 0, (N_KV * HD) * D,   N_KV * HD))
    defs.append((p + "attn_output.weight", [N_HEAD * HD], 0, D * (N_HEAD * HD), D))
    defs.append((p + "ffn_gate.weight",    [D], 0, FF * D, FF))
    defs.append((p + "ffn_up.weight",      [D], 0, FF * D, FF))
    defs.append((p + "ffn_down.weight",    [FF], 0, D * FF, D))

out += struct.pack("<Q", len(defs))
for name, dims, mode, nq, ns in defs:
    nb = name.encode()
    out += struct.pack("<Q", len(nb)) + nb
    out += struct.pack("<I", len(dims))
    for d in dims:
        out += struct.pack("<Q", d)
    out += struct.pack("<B", mode)
    if mode == 0:
        out += struct.pack("<Q", nq) + q8(nq)
        out += struct.pack("<Q", ns) + scal(ns)
    else:
        out += struct.pack("<Q", nq) + q16(nq)

with open(sys.argv[1], "wb") as f:
    f.write(out)
print(f"wrote {sys.argv[1]} ({len(out)} bytes, {len(defs)} tensors)")
