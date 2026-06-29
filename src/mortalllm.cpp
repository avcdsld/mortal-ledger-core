// Copyright (c) 2026 The Mortal Ledger developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The Mortal Ledger voice: the fully fixed-point Qwen forward (RMSNorm, QK-norm,
// NEOX RoPE, GQA, SwiGLU, tied lm_head) ported from the reference node/llm_forward.cpp.
// Everything is integer: the hidden state is i64 Q16.16, matmuls accumulate int8->int32
// and dequantize via __int128, and RMSNorm/softmax/SiLU/RoPE are fixed-point with an
// integer-CORDIC RoPE table — no float, no libm. So the one-bit OP_JUDGE verdict is
// bit-identical on any conforming integer hardware, which matters because OP_JUDGE can
// gate a spend (consensus). Hyperparameters are derived from the .mlm's tensor shapes;
// the model is hash-pinned, so its header is pinned too.

#include <mortalllm.h>

#include <crypto/sha256.h>
#include <script/interpreter.h> // g_mortal_llm, g_mortal_block_seed
#include <uint256.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace {

using i8 = int8_t; using i32 = int32_t; using i64 = int64_t;
using u8 = uint8_t; using u32 = uint32_t; using u64 = uint64_t;
using i128 = __int128;
using valtype = std::vector<unsigned char>;

// ---- .mlm tensor store ---------------------------------------------------------
struct MTensor { std::vector<u64> dims; int mode = 0; std::vector<i8> q; std::vector<i64> scale; std::vector<i32> q16; };
std::map<std::string, MTensor> g_w;

// ---- hyperparameters, derived from the model at load ---------------------------
int N_LAYER = 0, D = 0, N_HEAD = 0, N_KV = 0, HD = 0, FF = 0;
u64 VOCAB = 0;

void rd(void* p, size_t sz, size_t n, FILE* f) {
    if (std::fread(p, sz, n, f) != n) throw std::runtime_error("mortalllm: truncated .mlm");
}
u64 rd_u64(FILE* f) { u64 v; rd(&v, 8, 1, f); return v; }
u32 rd_u32(FILE* f) { u32 v; rd(&v, 4, 1, f); return v; }

std::string sha256_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) throw std::runtime_error(std::string("mortalllm: cannot open ") + path);
    CSHA256 h; std::vector<unsigned char> buf(1 << 20);
    for (;;) { size_t n = std::fread(buf.data(), 1, buf.size(), f); if (n) h.Write(buf.data(), n);
        if (n < buf.size()) { if (std::ferror(f)) { std::fclose(f); throw std::runtime_error("mortalllm: read error hashing model"); } break; } }
    std::fclose(f);
    unsigned char out[CSHA256::OUTPUT_SIZE]; h.Finalize(out);
    static const char* x = "0123456789abcdef"; std::string s;
    for (unsigned char c : out) { s += x[c >> 4]; s += x[c & 15]; }
    return s;
}

void load_mlm(const char* path) {
    g_w.clear();
    FILE* f = std::fopen(path, "rb");
    if (!f) throw std::runtime_error(std::string("mortalllm: cannot open ") + path);
    try {
        char mg[4]; rd(mg, 1, 4, f);
        if (std::memcmp(mg, "MLM1", 4) != 0) throw std::runtime_error("mortalllm: bad magic (expected MLM1)");
        u64 nt = rd_u64(f);
        for (u64 k = 0; k < nt; k++) {
            u64 L = rd_u64(f); std::string name(L, 0); rd(&name[0], 1, L, f);
            u32 nd = rd_u32(f); MTensor t; for (u32 d = 0; d < nd; d++) t.dims.push_back(rd_u64(f));
            u8 mode; rd(&mode, 1, 1, f); t.mode = mode;
            if (mode == 0) {
                u64 nq = rd_u64(f); t.q.resize(nq); if (nq) rd(t.q.data(), 1, nq, f);
                u64 ns = rd_u64(f); t.scale.resize(ns); if (ns) rd(t.scale.data(), 8, ns, f);
            } else {
                u64 nq = rd_u64(f); t.q16.resize(nq); if (nq) rd(t.q16.data(), 4, nq, f);
            }
            g_w[name] = std::move(t);
        }
    } catch (...) { std::fclose(f); throw; }
    std::fclose(f);
}

MTensor& W(const std::string& n) {
    auto it = g_w.find(n);
    if (it == g_w.end()) throw std::runtime_error("mortalllm: missing tensor " + n);
    return it->second;
}

// Derive the hyperparameters from the tensor shapes (one pinned architecture).
void derive_hparams() {
    const MTensor& emb = W("token_embd.weight");
    if (emb.dims.empty() || emb.dims[0] == 0) throw std::runtime_error("mortalllm: bad token_embd");
    D = (int)emb.dims[0];
    VOCAB = emb.q.size() / (u64)D;
    HD = (int)W("blk.0.attn_q_norm.weight").q16.size();   // per-head RMSNorm width
    if (HD == 0) throw std::runtime_error("mortalllm: bad head dim");
    N_HEAD = (int)(W("blk.0.attn_q.weight").q.size() / (u64)D / (u64)HD);
    N_KV = (int)(W("blk.0.attn_k.weight").q.size() / (u64)D / (u64)HD);
    FF = (int)(W("blk.0.ffn_gate.weight").q.size() / (u64)D);
    N_LAYER = 0;
    while (g_w.count("blk." + std::to_string(N_LAYER) + ".attn_q.weight")) N_LAYER++;
    if (N_HEAD == 0 || N_KV == 0 || FF == 0 || N_LAYER == 0)
        throw std::runtime_error("mortalllm: could not derive hyperparameters");
}

// ---- fixed-point kernels (integer only) ----------------------------------------
const i64 FX = 65536; const int FXB = 16;

i64 fxexp_neg(i64 r) { if (r > 0) r = 0; i64 LN2 = 45426; i64 y = (-r * FX) / LN2; i64 k = y >> FXB; i64 f = y - (k << FXB);
    i64 f2 = (f * f) >> FXB, f3 = (f2 * f) >> FXB;
    i64 val = FX - ((45426 * f) >> FXB) + ((15743 * f2) >> FXB) - ((3635 * f3) >> FXB);
    if (k >= 31) return 0; return val >> k; }
i64 fxsigmoid(i64 x) { if (x >= 0) { i64 e = fxexp_neg(-x); return (FX * FX) / (FX + e); } i64 e = fxexp_neg(x); return (e * FX) / (FX + e); }
i64 fxsilu(i64 x) { return ((i128)x * fxsigmoid(x)) >> FXB; }
u64 isqrt_u64(u64 n) { u64 x = 0, b = (u64)1 << 62; while (b > n) b >>= 2; while (b) { if (n >= x + b) { n -= x + b; x = (x >> 1) + b; } else x >>= 1; b >>= 2; } return x; }

// int8 dot, int32 exact accumulate. The order parameter only matters for the reference
// determinism test (orders 1/2); the node always uses order 0. SIMD changes the grouping
// of an integer sum, which is exact and order-independent, so every path below returns the
// identical i32 (and thus bit-identical logits across architectures).
i32 idot(const i8* a, const i8* b, u64 k, int order) {
    if (order == 0) {
#if defined(__ARM_NEON)
        u64 i = 0; i32 s;
#if defined(__ARM_FEATURE_DOTPROD)
        int32x4_t acc = vdupq_n_s32(0);
        for (; i + 16 <= k; i += 16) acc = vdotq_s32(acc, vld1q_s8(a + i), vld1q_s8(b + i));
        s = vaddvq_s32(acc);
#else
        int32x4_t acc = vdupq_n_s32(0);
        for (; i + 8 <= k; i += 8) acc = vpadalq_s16(acc, vmull_s8(vld1_s8(a + i), vld1_s8(b + i)));
        s = vaddvq_s32(acc);
#endif
        for (; i < k; i++) s += (i32)a[i] * b[i];
        return s;
#else
        i32 s = 0; for (u64 i = 0; i < k; i++) s += (i32)a[i] * b[i]; return s;
#endif
    }
    i32 s = 0;
    if (order == 1) { for (i64 i = (i64)k - 1; i >= 0; i--) s += (i32)a[i] * b[i]; }
    else { for (u64 i = 0; i < k; i += 2) s += (i32)a[i] * b[i]; for (u64 i = 1; i < k; i += 2) s += (i32)a[i] * b[i]; }
    return s; }

// rope table (Q16) built by integer CORDIC: no float, bit-identical across arch.
std::vector<i32> g_rcos, g_rsin; int g_rope_S = 0;
const i64 TWO_PI = 6746518852LL, HALF_PI = 1686629713LL, PI_ = 3373259426LL, ROPE_R = 865266461LL;
const i64 CORDIC_K = 652032874LL;
const i64 ATAN30[30] = {843314857, 497837829, 263043837, 133525159, 67021687, 33543516, 16775851, 8388437, 4194283, 2097149, 1048576, 524288, 262144, 131072, 65536, 32768, 16384, 8192, 4096, 2048, 1024, 512, 256, 128, 64, 32, 16, 8, 4, 2};
void cordic(i64 z, i64& c, i64& s) {
    z %= TWO_PI; if (z > PI_) z -= TWO_PI; else if (z < -PI_) z += TWO_PI;
    int sign = 1; if (z > HALF_PI) { z -= PI_; sign = -1; } else if (z < -HALF_PI) { z += PI_; sign = -1; }
    i64 x = CORDIC_K, y = 0;
    for (int i = 0; i < 30; i++) { i64 xs = x >> i, ys = y >> i;
        if (z >= 0) { x -= ys; y += xs; z -= ATAN30[i]; } else { x += ys; y -= xs; z += ATAN30[i]; } }
    c = sign * x; s = sign * y;
}
void build_rope(int S) { if (S <= g_rope_S) return; g_rcos.assign((size_t)S * (HD / 2), 0); g_rsin.assign((size_t)S * (HD / 2), 0);
    std::vector<i64> inv(HD / 2); inv[0] = (i64)1 << 30; for (int j = 1; j < HD / 2; j++) inv[j] = ((i128)inv[j - 1] * ROPE_R) >> 30;
    for (int p = 0; p < S; p++) for (int j = 0; j < HD / 2; j++) { i64 th = ((i128)p * inv[j]); i64 c, s; cordic(th, c, s);
        g_rcos[(size_t)p * (HD / 2) + j] = (i32)(c >> 14); g_rsin[(size_t)p * (HD / 2) + j] = (i32)(s >> 14); } g_rope_S = S; }
void rope_fx(std::vector<i64>& v, int off, int pos) { for (int j = 0; j < HD / 2; j++) { i64 c = g_rcos[(size_t)pos * (HD / 2) + j], s = g_rsin[(size_t)pos * (HD / 2) + j];
    i64 a = v[off + j], b = v[off + j + HD / 2]; v[off + j] = ((i128)a * c - (i128)b * s) >> FXB; v[off + j + HD / 2] = ((i128)a * s + (i128)b * c) >> FXB; } }

void rmsnorm_fx(std::vector<i64>& x, const std::vector<i32>& wq16) {
    i128 ss = 0; for (i64 v : x) ss += (i128)v * v;
    i64 mean = (i64)(ss / (i128)x.size()); u64 rms = isqrt_u64((u64)mean); if (!rms) rms = 1;
    i64 inv = ((i64)1 << 32) / (i64)rms;
    for (size_t i = 0; i < x.size(); i++) { i64 n = ((i128)x[i] * inv) >> FXB; x[i] = ((i128)n * wq16[i]) >> FXB; } }

i64 quant8(const std::vector<i64>& h, std::vector<i8>& xq) { i64 amax = 1; for (i64 v : h) { i64 a = v < 0 ? -v : v; if (a > amax) amax = a; }
    xq.resize(h.size()); for (size_t i = 0; i < h.size(); i++) { i128 num = (i128)h[i] * 127; i64 q;
        if (num >= 0) q = (i64)((num * 2 + amax) / (2 * amax)); else q = -(i64)(((-num) * 2 + amax) / (2 * amax));
        if (q > 127) q = 127; if (q < -127) q = -127; xq[i] = (i8)q; } return amax; }

// Run fn(o) for o in [0,out) across hardware threads. Each output row is independent and
// its reduction order is unchanged, so the result is bit-identical to the serial loop (and
// across thread counts / architectures) — integer determinism is preserved. This is the
// hot path (the matmuls, incl. the lm_head), so threading it is the main speedup.
template <class F>
void parallel_rows(u64 out, F&& fn) {
    static const unsigned NT = [] { unsigned n = std::thread::hardware_concurrency(); return n ? n : 1u; }();
    if (NT == 1 || out < 512) { for (u64 o = 0; o < out; o++) fn(o); return; }
    unsigned nt = (unsigned)std::min<u64>(NT, out);
    std::vector<std::thread> ts; ts.reserve(nt);
    u64 chunk = (out + nt - 1) / nt;
    for (unsigned k = 0; k < nt; k++) {
        u64 b = (u64)k * chunk, e = std::min<u64>(out, b + chunk);
        if (b >= e) break;
        ts.emplace_back([&fn, b, e] { for (u64 o = b; o < e; o++) fn(o); });
    }
    for (auto& th : ts) th.join();
}

std::vector<i64> matvec_fx(const MTensor& t, const std::vector<i64>& h, int order) {
    u64 in = t.dims[0], out = t.q.size() / in; std::vector<i8> xq; i64 amax = quant8(h, xq);
    std::vector<i64> y(out); i128 den = (i128)127 << 24;
    parallel_rows(out, [&](u64 o) { i32 acc = idot(&t.q[o * in], xq.data(), in, order);
        i128 num = (i128)acc * (i128)t.scale[o] * (i128)amax; y[o] = (i64)(num / den); });
    return y; }

std::vector<i64> forward_fixed(const std::vector<int>& toks, int order) {
    int S = (int)toks.size(); build_rope(S);
    const MTensor& emb = W("token_embd.weight"); u64 ein = emb.dims[0];
    std::vector<std::vector<i64>> h(S, std::vector<i64>(D));
    for (int s = 0; s < S; s++) { const i8* row = &emb.q[(u64)toks[s] * ein]; i64 sc = emb.scale[toks[s]];
        for (int i = 0; i < D; i++) h[s][i] = ((i64)row[i] * sc) >> 8; }
    // attn_scale = 1/sqrt(HD) in Q16.16, computed with integer isqrt (no float).
    i64 sqrt_hd_q16 = (i64)isqrt_u64((u64)HD << 32); if (!sqrt_hd_q16) sqrt_hd_q16 = 1;
    i64 attn_scale = ((i64)1 << 32) / sqrt_hd_q16;
    for (int l = 0; l < N_LAYER; l++) { std::string p = "blk." + std::to_string(l) + ".";
        const std::vector<i32>& an = W(p + "attn_norm.weight").q16; const std::vector<i32>& qn = W(p + "attn_q_norm.weight").q16;
        const std::vector<i32>& kn = W(p + "attn_k_norm.weight").q16; const std::vector<i32>& fn = W(p + "ffn_norm.weight").q16;
        std::vector<std::vector<i64>> Q(S), K(S), V(S);
        for (int s = 0; s < S; s++) { std::vector<i64> x = h[s]; rmsnorm_fx(x, an);
            Q[s] = matvec_fx(W(p + "attn_q.weight"), x, order); K[s] = matvec_fx(W(p + "attn_k.weight"), x, order); V[s] = matvec_fx(W(p + "attn_v.weight"), x, order);
            for (int hh = 0; hh < N_HEAD; hh++) { std::vector<i64> hv(Q[s].begin() + hh * HD, Q[s].begin() + hh * HD + HD); rmsnorm_fx(hv, qn); std::copy(hv.begin(), hv.end(), Q[s].begin() + hh * HD); rope_fx(Q[s], hh * HD, s); }
            for (int hh = 0; hh < N_KV; hh++) { std::vector<i64> hv(K[s].begin() + hh * HD, K[s].begin() + hh * HD + HD); rmsnorm_fx(hv, kn); std::copy(hv.begin(), hv.end(), K[s].begin() + hh * HD); rope_fx(K[s], hh * HD, s); } }
        std::vector<std::vector<i64>> ctx(S, std::vector<i64>(N_HEAD * HD));
        for (int s = 0; s < S; s++) for (int hh = 0; hh < N_HEAD; hh++) { int kv = hh / (N_HEAD / N_KV);
            std::vector<i64> sc(s + 1); i64 mx = INT64_MIN;
            for (int t = 0; t <= s; t++) { i128 d = 0; for (int i = 0; i < HD; i++) d += (i128)Q[s][hh * HD + i] * K[t][kv * HD + i]; i64 dq = (i64)(d >> FXB);
                i64 sq = ((i128)dq * attn_scale) >> FXB; sc[t] = sq; if (sq > mx) mx = sq; }
            i64 den = 0; for (int t = 0; t <= s; t++) { sc[t] = fxexp_neg(sc[t] - mx); den += sc[t]; } if (!den) den = 1;
            for (int i = 0; i < HD; i++) { i128 acc = 0; for (int t = 0; t <= s; t++) acc += (i128)sc[t] * V[t][kv * HD + i];
                ctx[s][hh * HD + i] = (i64)((acc / den)); } }
        for (int s = 0; s < S; s++) { auto o = matvec_fx(W(p + "attn_output.weight"), ctx[s], order); for (int i = 0; i < D; i++) h[s][i] += o[i]; }
        for (int s = 0; s < S; s++) { std::vector<i64> x = h[s]; rmsnorm_fx(x, fn);
            auto g = matvec_fx(W(p + "ffn_gate.weight"), x, order), u = matvec_fx(W(p + "ffn_up.weight"), x, order);
            std::vector<i64> a(FF); for (int i = 0; i < FF; i++) a[i] = ((i128)fxsilu(g[i]) * u[i]) >> FXB;
            auto d = matvec_fx(W(p + "ffn_down.weight"), a, order); for (int i = 0; i < D; i++) h[s][i] += d[i]; } }
    std::vector<i64> x = h[S - 1]; rmsnorm_fx(x, W("output_norm.weight").q16);
    return matvec_fx(W("token_embd.weight"), x, order);
}
int argmax64(const std::vector<i64>& v) { int b = 0; for (size_t i = 1; i < v.size(); i++) if (v[i] > v[b]) b = (int)i; return b; }

// Decode the opcode input (u32 LE token ids) into a token list, clamped to VOCAB.
// u32, not u16: Qwen3's vocabulary is 151936, which does not fit in 16 bits.
std::vector<int> decode_tokens(const valtype& in) {
    std::vector<int> ids;
    for (size_t i = 0; i + 3 < in.size(); i += 4) {
        uint32_t id = (uint32_t)in[i] | ((uint32_t)in[i + 1] << 8) | ((uint32_t)in[i + 2] << 16) | ((uint32_t)in[i + 3] << 24);
        if ((u64)id < VOCAB) ids.push_back((int)id);
    }
    return ids;
}

} // namespace

void MortalInstallLLM(const std::string& path, const std::string& expected_sha256_hex)
{
    if (!expected_sha256_hex.empty()) {
        const std::string got = sha256_file(path.c_str());
        if (got != expected_sha256_hex)
            throw std::runtime_error("mortalllm: model hash mismatch (got " + got + ", expected " + expected_sha256_hex +
                                     "); refusing to run non-canonical weights (OP_JUDGE is consensus)");
    }
    load_mlm(path.c_str());
    derive_hparams();
    g_mortal_llm = [](uint8_t verb, const valtype& in, const uint256& /*seed*/) -> valtype {
        std::vector<int> ids = decode_tokens(in);
        if (verb == 'J') {
            // JUDGE: a one-bit verdict — does the model greedily predict the last token
            // as the continuation of the preceding context? Greedy (temp 0) and seed-
            // independent, so the lock is stable and consensus-safe.
            if (ids.size() < 2) return valtype{0};
            int target = ids.back(); std::vector<int> ctx(ids.begin(), ids.end() - 1);
            int pred = argmax64(forward_fixed(ctx, 0));
            return valtype{(unsigned char)(pred == target ? 1 : 0)};
        }
        // DREAM / TRANSLATE: the next greedy token id, as 4 bytes LE (u32: Qwen3's
        // vocabulary exceeds 16 bits). The node produces ONE token per call; a multi-
        // token "dream" is this iterated by the caller.
        if (ids.empty()) return valtype{};
        uint32_t a = (uint32_t)argmax64(forward_fixed(ids, 0));
        return valtype{(unsigned char)(a & 0xff), (unsigned char)((a >> 8) & 0xff),
                       (unsigned char)((a >> 16) & 0xff), (unsigned char)((a >> 24) & 0xff)};
    };
}
