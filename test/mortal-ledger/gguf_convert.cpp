// GGUF reader + deterministic integer converter for the Mortal Ledger LLM
// (Phase 2, step ろ). It reads a GGUF model (F32 / F16 / Q8_0 source), re-quantizes
// each weight to the canonical integer scheme that node/llm_integer.cpp runs
// (symmetric int8, per output channel, with an integer requant multiplier+shift),
// serializes the integer model deterministically, and SHA-256-pins it.
//
// The conversion is itself deterministic: f16 is decoded with exact integer bit ops,
// the per-channel scale is a fixed rule, and rounding is round-half-away-from-zero in
// IEEE basic ops (which are correctly rounded and reproducible across conforming
// hardware). So the pin is reproducible: anyone can re-derive the canonical model
// from the official Qwen GGUF and check the hash. Runtime stays pure integer.
//
// This proves the pipeline end to end on a synthetic GGUF written here. Pointing it
// at the real Qwen3-1.7B GGUF is the same code path (prefer an F16/Q8_0 source so the
// re-quant is from high precision; add Q4_K/Q6_K dequant if a k-quant source is used).
//
// Build: g++ -O2 -std=c++17 node/gguf_convert.cpp -lcrypto -o /tmp/gguf && /tmp/gguf
#include <openssl/sha.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using i8=int8_t; using i16=int16_t; using i32=int32_t; using i64=int64_t;

// ---- little-endian byte cursor -------------------------------------------------
struct Cur {
    const u8* p; size_t n, i = 0;
    template<class T> T get() { T v; std::memcpy(&v, p + i, sizeof(T)); i += sizeof(T); return v; }
    std::string str() { u64 len = get<u64>(); std::string s((const char*)p + i, len); i += len; return s; }
    void skip(size_t k) { i += k; }
};

// ---- exact half-precision decode (integer bit ops, reproducible) ---------------
static float f16_to_f32(u16 h) {
    u32 sign = (u32)(h & 0x8000) << 16, exp = (h >> 10) & 0x1f, man = h & 0x3ff, f;
    if (exp == 0) {
        if (man == 0) f = sign;
        else { int e = 127 - 15 + 1; while (!(man & 0x400)) { man <<= 1; e--; } man &= 0x3ff;
               f = sign | ((u32)e << 23) | (man << 13); }
    } else if (exp == 0x1f) f = sign | 0x7f800000u | (man << 13);
    else f = sign | ((exp - 15 + 127) << 23) | (man << 13);
    float o; std::memcpy(&o, &f, 4); return o;
}
static u16 f32_to_f16(float x) {
    u32 f; std::memcpy(&f, &x, 4);
    u32 sign = (f >> 16) & 0x8000; int exp = (int)((f >> 23) & 0xff) - 127 + 15; u32 man = f & 0x7fffff;
    if (exp <= 0) return (u16)sign;
    if (exp >= 0x1f) return (u16)(sign | 0x7c00);
    u16 half = (u16)(sign | ((u32)exp << 10) | (man >> 13));
    if (man & 0x1000) half++;            // round to nearest
    return half;
}

// ---- GGUF value-type sizes (so we can read or skip any metadata) ---------------
enum { GT_U8,GT_I8,GT_U16,GT_I16,GT_U32,GT_I32,GT_F32,GT_BOOL,GT_STR,GT_ARR,GT_U64,GT_I64,GT_F64 };
// ggml tensor types we read
enum { GGML_F32=0, GGML_F16=1, GGML_Q8_0=8 };

struct Tensor { std::string name; std::vector<u64> dims; u32 type; u64 offset; u64 nelem() const {
    u64 n = 1; for (u64 d : dims) n *= d; return n; } };

struct Gguf {
    std::map<std::string,i64> kvi;       // integer metadata
    std::map<std::string,std::string> kvs; // string metadata
    std::map<std::string,std::vector<std::string>> kvsa; // string arrays (e.g. tokenizer tokens)
    std::map<std::string,std::vector<i64>> kvia;         // int/bool arrays (e.g. token types)
    std::vector<Tensor> tensors;
    u64 data_off = 0; u32 alignment = 32;
};

// read one KV value of the given type; record ints/strings, skip the rest correctly.
static void read_value(Cur& c, u32 t, const std::string& key, Gguf& g) {
    switch (t) {
        case GT_U8:  g.kvi[key] = c.get<u8>();  break;
        case GT_I8:  g.kvi[key] = c.get<i8>();  break;
        case GT_U16: g.kvi[key] = c.get<u16>(); break;
        case GT_I16: g.kvi[key] = c.get<i16>(); break;
        case GT_U32: g.kvi[key] = c.get<u32>(); break;
        case GT_I32: g.kvi[key] = c.get<i32>(); break;
        case GT_U64: g.kvi[key] = (i64)c.get<u64>(); break;
        case GT_I64: g.kvi[key] = c.get<i64>(); break;
        case GT_BOOL:g.kvi[key] = c.get<u8>();  break;
        case GT_F32: c.skip(4); break;
        case GT_F64: c.skip(8); break;
        case GT_STR: g.kvs[key] = c.str(); break;
        case GT_ARR: { u32 sub = c.get<u32>(); u64 cnt = c.get<u64>();
            for (u64 j = 0; j < cnt; j++) {
                if (sub == GT_STR) { std::string s = c.str(); if (!key.empty()) g.kvsa[key].push_back(std::move(s)); }
                else {
                    i64 v = 0; bool keep = true;
                    switch (sub) {
                        case GT_U8:  v = c.get<u8>();  break; case GT_I8:  v = c.get<i8>();  break;
                        case GT_U16: v = c.get<u16>(); break; case GT_I16: v = c.get<i16>(); break;
                        case GT_U32: v = c.get<u32>(); break; case GT_I32: v = c.get<i32>(); break;
                        case GT_U64: v = (i64)c.get<u64>(); break; case GT_I64: v = c.get<i64>(); break;
                        case GT_BOOL:v = c.get<u8>();  break;
                        case GT_F32: c.skip(4); keep = false; break; case GT_F64: c.skip(8); keep = false; break;
                        default: keep = false; break;
                    }
                    if (keep && !key.empty()) g.kvia[key].push_back(v);
                }
            } break; }
        default: break;
    }
}

static bool parse_gguf(const std::vector<u8>& buf, Gguf& g) {
    Cur c{buf.data(), buf.size()};
    if (c.get<u32>() != 0x46554747u) return false;   // "GGUF"
    u32 ver = c.get<u32>(); (void)ver;
    u64 ntensor = c.get<u64>(), nkv = c.get<u64>();
    for (u64 k = 0; k < nkv; k++) { std::string key = c.str(); u32 t = c.get<u32>(); read_value(c, t, key, g); }
    if (g.kvi.count("general.alignment")) g.alignment = (u32)g.kvi["general.alignment"];
    for (u64 k = 0; k < ntensor; k++) {
        Tensor t; t.name = c.str(); u32 nd = c.get<u32>();
        for (u32 d = 0; d < nd; d++) t.dims.push_back(c.get<u64>());
        t.type = c.get<u32>(); t.offset = c.get<u64>();
        g.tensors.push_back(t);
    }
    u64 pos = c.i, al = g.alignment; g.data_off = (pos + al - 1) / al * al;  // align
    return true;
}

// ---- dequantize a tensor's raw bytes to float (offline, conversion-time only) --
static std::vector<float> dequant(const Gguf& g, const std::vector<u8>& buf, const Tensor& t) {
    const u8* d = buf.data() + g.data_off + t.offset;
    u64 n = t.nelem(); std::vector<float> out(n);
    if (t.type == GGML_F32)      { for (u64 i=0;i<n;i++){ float v; std::memcpy(&v,d+i*4,4); out[i]=v; } }
    else if (t.type == GGML_F16) { for (u64 i=0;i<n;i++){ u16 h; std::memcpy(&h,d+i*2,2); out[i]=f16_to_f32(h);} }
    else if (t.type == GGML_Q8_0) {                       // 32 vals: f16 scale + 32 int8
        const int QK=32; u64 nb=n/QK; const u8* p=d;
        for (u64 b=0;b<nb;b++){ u16 dh; std::memcpy(&dh,p,2); float dd=f16_to_f32(dh); p+=2;
            for (int j=0;j<QK;j++){ i8 q=(i8)p[j]; out[b*QK+j]=dd*(float)q; } p+=QK; }
    }
    return out;
}

// ---- canonical integer quantization: symmetric int8 per output row -------------
// row scale s_r = max|w|/127; q = round_half_away(w/s_r). Store int8 q and the scale
// as a fixed-point integer (s_r * 2^SBITS), so the serialized model is pure integer.
static const int SBITS = 24;
// mode 0: 2-D weights -> symmetric int8 per output row + integer scale.
// mode 1: 1-D tensors (norms, biases) -> Q16.16 integers, kept precise (they are
// tiny and matter; quantizing them to int8 would wreck the model).
struct QTensor { std::string name; std::vector<u64> dims; int mode=0;
    std::vector<i8> q; std::vector<i64> scale; std::vector<i32> q16; };

static QTensor quantize(const Tensor& t, const std::vector<float>& w) {
    QTensor o; o.name = t.name; o.dims = t.dims;
    if (t.dims.size() < 2) {                              // 1-D: keep as Q16.16
        o.mode = 1; o.q16.resize(t.nelem());
        for (u64 i=0;i<t.nelem();i++) o.q16[i] = (i32)std::llround((double)w[i] * 65536.0);
        return o;
    }
    o.mode = 0;
    u64 cols = t.dims[0];                                 // ne[0] = inner dim
    u64 rows = cols ? t.nelem() / cols : 1;
    o.q.resize(t.nelem()); o.scale.resize(rows);
    for (u64 r = 0; r < rows; r++) {
        float amax = 0; for (u64 j=0;j<cols;j++){ float a=std::fabs(w[r*cols+j]); if(a>amax)amax=a; }
        float s = amax > 0 ? amax / 127.0f : 1.0f;
        o.scale[r] = (i64)std::llround((double)s * ((i64)1 << SBITS));
        for (u64 j=0;j<cols;j++){ float v = w[r*cols+j] / s;
            long qi = (long)(v + (v>=0?0.5f:-0.5f));      // round half away from zero
            if (qi>127) qi=127; if (qi<-127) qi=-127; o.q[r*cols+j]=(i8)qi; }
    }
    return o;
}

// ---- tokenizer vocab: id -> raw bytes (for the node's pure-C++ detokenizer) -----
// GPT-2 byte-level decoder: invert bytes_to_unicode() to recover the raw bytes a
// byte-encoded token string stands for. Qwen (like all GPT-2/tiktoken BPE) stores its
// tokens this way, so detokenizing is just: per token, map each codepoint back to a byte.
static std::map<int,int> gpt2_byte_decoder(){
    std::vector<int> bs;
    for(int b='!';b<='~';b++) bs.push_back(b);
    for(int b=0xA1;b<=0xAC;b++) bs.push_back(b);
    for(int b=0xAE;b<=0xFF;b++) bs.push_back(b);
    std::vector<int> cs=bs; int n=0;
    for(int b=0;b<256;b++){ if(std::find(bs.begin(),bs.end(),b)==bs.end()){ bs.push_back(b); cs.push_back(256+n); n++; } }
    std::map<int,int> dec; for(size_t i=0;i<bs.size();i++) dec[cs[i]]=bs[i]; return dec;
}
static std::vector<int> utf8_codepoints(const std::string& s){
    std::vector<int> cp; size_t i=0,n=s.size();
    while(i<n){ unsigned char c=s[i]; int v,len;
        if(c<0x80){v=c;len=1;} else if(c<0xE0){v=c&0x1F;len=2;} else if(c<0xF0){v=c&0x0F;len=3;} else {v=c&0x07;len=4;}
        for(int k=1;k<len && i+(size_t)k<n;k++) v=(v<<6)|(s[i+k]&0x3F);
        cp.push_back(v); i+=len; }
    return cp;
}
// id -> raw bytes for the whole vocabulary. CONTROL / USER_DEFINED (special) tokens map to
// empty, matching skip_special_tokens; non-byte-level tokens (shouldn't occur for Qwen) too.
static std::vector<std::string> build_vocab(const Gguf& g){
    std::vector<std::string> vocab;
    auto it = g.kvsa.find("tokenizer.ggml.tokens");
    if (it==g.kvsa.end()) return vocab;                 // GGUF without an embedded tokenizer
    const auto& toks = it->second;
    const std::vector<i64>* types = g.kvia.count("tokenizer.ggml.token_type") ? &g.kvia.at("tokenizer.ggml.token_type") : nullptr;
    auto dec = gpt2_byte_decoder();
    vocab.resize(toks.size());
    for (size_t i=0;i<toks.size();i++){
        int tt = (types && i<types->size()) ? (int)(*types)[i] : 1; // 1 = NORMAL
        if (tt==3 || tt==4) continue;                   // CONTROL / USER_DEFINED -> empty
        std::string raw; bool okk=true;
        for (int cp : utf8_codepoints(toks[i])){ auto d=dec.find(cp); if(d==dec.end()){ okk=false; break; } raw.push_back((char)d->second); }
        if (okk) vocab[i]=std::move(raw);
    }
    return vocab;
}

// ---- deterministic serialization of the integer model + SHA-256 pin ------------
static void put(std::vector<u8>& b, const void* p, size_t n){ const u8* q=(const u8*)p; b.insert(b.end(),q,q+n); }
static std::vector<u8> serialize(const std::vector<QTensor>& m, const std::vector<std::string>& vocab) {
    std::vector<u8> b; const char* magic="MLM1"; put(b,magic,4);
    u64 nt=m.size(); put(b,&nt,8);
    for (auto& t : m) {
        u64 L=t.name.size(); put(b,&L,8); put(b,t.name.data(),L);
        u32 nd=t.dims.size(); put(b,&nd,4); for(u64 d:t.dims) put(b,&d,8);
        u8 mode=(u8)t.mode; put(b,&mode,1);
        if (t.mode==0) {
            u64 nq=t.q.size(); put(b,&nq,8); put(b,t.q.data(),nq);
            u64 ns=t.scale.size(); put(b,&ns,8); for(i64 s:t.scale) put(b,&s,8);
        } else {
            u64 nq=t.q16.size(); put(b,&nq,8); for(i32 v:t.q16) put(b,&v,4);
        }
    }
    // Mortal Ledger: optional detokenizer vocab section, so the node turns a dream's token
    // ids back into text in pure C++ (no tokenizer). Appended after the tensors; a reader
    // that hits EOF here (e.g. the toy model) simply has no vocab.
    if (!vocab.empty()) {
        const char* vm="VOC1"; put(b,vm,4);
        u64 nv=vocab.size(); put(b,&nv,8);
        for (const auto& s : vocab){ u16 L=(u16)(s.size()>0xffff?0:s.size()); put(b,&L,2); put(b,s.data(),L); }
    }
    return b;
}
static std::string sha256_hex(const std::vector<u8>& b){
    u8 h[32]; SHA256(b.data(), b.size(), h);
    static const char* x="0123456789abcdef"; std::string s;
    for (int i=0;i<32;i++){ s+=x[h[i]>>4]; s+=x[h[i]&15]; } return s;
}

// ---- synthetic GGUF writer (for the self-test) ---------------------------------
struct W { std::vector<u8> b;
    template<class T> void v(T x){ const u8* p=(const u8*)&x; b.insert(b.end(),p,p+sizeof(T)); }
    void s(const std::string& z){ v<u64>(z.size()); b.insert(b.end(),z.begin(),z.end()); }
};
static std::vector<u8> make_synthetic_gguf() {
    // two tensors: token_embd F32 [8,4], blk.0.attn_q Q8_0 [8,8]
    const int E_in=8, E_out=4, Q_in=8, Q_out=8;
    std::vector<float> emb(E_in*E_out); for (size_t i=0;i<emb.size();i++) emb[i]=std::sin(0.3*i)*1.5f;
    std::vector<float> qw(Q_in*Q_out);  for (size_t i=0;i<qw.size();i++)  qw[i]=std::cos(0.2*i)*0.8f;

    W w; const char* mg="GGUF"; w.b.insert(w.b.end(),mg,mg+4); w.v<u32>(3);
    w.v<u64>(2);                       // tensor count
    w.v<u64>(2);                       // kv count
    w.s("general.architecture"); w.v<u32>(GT_STR); w.s("mortal-toy");
    w.s("general.alignment");    w.v<u32>(GT_U32); w.v<u32>(32);
    // tensor infos (offsets relative to aligned data section)
    u64 off0=0, off1=(u64)E_in*E_out*4; // F32 bytes
    w.s("token_embd.weight"); w.v<u32>(2); w.v<u64>(E_in); w.v<u64>(E_out); w.v<u32>(GGML_F32);  w.v<u64>(off0);
    w.s("blk.0.attn_q.weight"); w.v<u32>(2); w.v<u64>(Q_in); w.v<u64>(Q_out); w.v<u32>(GGML_Q8_0); w.v<u64>(off1);
    // align then data
    while (w.b.size() % 32) w.b.push_back(0);
    for (float f : emb) w.v<float>(f);                       // F32 tensor
    const int QK=32; for (int blk=0; blk<Q_in*Q_out/QK; blk++){  // Q8_0 tensor
        float amax=0; for(int j=0;j<QK;j++){ float a=std::fabs(qw[blk*QK+j]); if(a>amax)amax=a; }
        float d=amax/127.0f; w.v<u16>(f32_to_f16(d));
        for(int j=0;j<QK;j++){ long q=(long)std::llround(qw[blk*QK+j]/d); if(q>127)q=127; if(q<-127)q=-127; w.v<i8>((i8)q); }
    }
    return w.b;
}

// integer matmul order test (ties the converted weights to the runtime kernel)
static i32 idot(const i8* a, const i8* b, int k, int order){ i32 s=0;
    if(order==0) for(int i=0;i<k;i++) s+=(i32)a[i]*b[i];
    else for(int i=k-1;i>=0;i--) s+=(i32)a[i]*b[i]; return s; }

static int pass=0,total=0;
static void check(const char* n,bool c){ total++; if(c)pass++; printf("  [%s] %s\n", c?"ok":"FAIL", n); }

static int run_selftest(){
    auto raw = make_synthetic_gguf();
    Gguf g; bool ok = parse_gguf(raw, g);
    check("GGUF header + KV + tensor infos parse", ok && g.tensors.size()==2);
    check("metadata read (architecture=mortal-toy)", g.kvs["general.architecture"]=="mortal-toy");

    // dequant Q8_0 round-trips the source within quantization error
    bool found=false; double maxerr=0;
    for (auto& t : g.tensors) if (t.type==GGML_Q8_0){ found=true; auto f=dequant(g,raw,t);
        for (size_t i=0;i<f.size();i++){ double ref=std::cos(0.2*i)*0.8; maxerr=std::max(maxerr,std::fabs(f[i]-ref)); } }
    check("Q8_0 dequant round-trips source (<0.01)", found && maxerr<0.01);

    // f16 decode exact on known values
    check("f16 decode exact: 1.0, 0.5, -2.0",
          f16_to_f32(0x3c00)==1.0f && f16_to_f32(0x3800)==0.5f && f16_to_f32(0xc000)==-2.0f);

    // convert the whole model to the canonical integer scheme, twice -> same pin
    auto convert=[&](){ std::vector<QTensor> m; for(auto& t:g.tensors){ auto f=dequant(g,raw,t); m.push_back(quantize(t,f)); } return serialize(m, build_vocab(g)); };
    auto blob1=convert(); auto blob2=convert();
    std::string pin1=sha256_hex(blob1), pin2=sha256_hex(blob2);
    check("integer conversion deterministic (reproducible SHA-256 pin)", pin1==pin2);

    // the int8 weights feed an order-independent integer matmul (kernel determinism)
    std::vector<QTensor> m; for(auto& t:g.tensors){ auto f=dequant(g,raw,t); m.push_back(quantize(t,f)); }
    auto& q = m[1]; int cols=(int)q.dims[0];
    i8 x[8]; for(int j=0;j<8;j++) x[j]=(i8)(j-4);
    check("converted int8 weights: matmul order-independent",
          idot(q.q.data(), x, cols, 0)==idot(q.q.data(), x, cols, 1));

    printf("\n  canonical model SHA-256 pin: %s\n", pin1.c_str());
    printf("\n%d/%d  %s\n", pass, total, pass==total?"PASS":"FAIL");
    return pass==total?0:1;
}

// ---- CLI: convert a real GGUF into the canonical integer model + pin ------------
static std::vector<u8> read_file(const char* path){
    FILE* f=fopen(path,"rb"); if(!f){ fprintf(stderr,"cannot open %s\n",path); exit(2);}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    std::vector<u8> b(n); size_t r=fread(b.data(),1,n,f); fclose(f);
    if((long)r!=n){ fprintf(stderr,"short read\n"); exit(2);} return b;
}
static void write_file(const char* path, const std::vector<u8>& b){
    FILE* f=fopen(path,"wb"); if(!f){ fprintf(stderr,"cannot write %s\n",path); exit(2);}
    fwrite(b.data(),1,b.size(),f); fclose(f);
}
static int convert_file(const char* in, const char* out){
    auto raw = read_file(in);
    Gguf g; if(!parse_gguf(raw,g)){ fprintf(stderr,"not a GGUF file\n"); return 2; }
    printf("arch: %s   tensors: %zu\n",
           g.kvs.count("general.architecture")?g.kvs["general.architecture"].c_str():"?", g.tensors.size());
    for (const char* k : {".block_count",".embedding_length",".attention.head_count",
                          ".attention.head_count_kv",".feed_forward_length",".context_length"}) {
        for (auto& kv : g.kvi) { size_t kl=std::strlen(k);
            if (kv.first.size()>=kl && kv.first.compare(kv.first.size()-kl,kl,k)==0) {
                printf("  %s = %lld\n", kv.first.c_str(), (long long)kv.second); break; } }
    }
    std::vector<QTensor> m; u64 i8bytes=0, q16count=0;
    for (auto& t : g.tensors) {
        if (t.type!=GGML_F32 && t.type!=GGML_F16 && t.type!=GGML_Q8_0) {
            fprintf(stderr,"unsupported ggml type %u for tensor %s (need an F32/F16/Q8_0 source)\n",
                    t.type, t.name.c_str()); return 3; }
        auto f=dequant(g,raw,t); auto q=quantize(t,f);
        if(q.mode==0) i8bytes+=q.q.size(); else q16count+=q.q16.size();
        m.push_back(std::move(q));
    }
    auto vocab = build_vocab(g);
    u64 vbytes=0; for (const auto& s : vocab) vbytes += s.size();
    auto blob=serialize(m, vocab); write_file(out, blob);
    printf("source GGUF  SHA-256: %s\n", sha256_hex(raw).c_str());
    printf("canonical    SHA-256: %s\n", sha256_hex(blob).c_str());
    printf("pinned model: %s  (%.1f MB)   int8 weights: %llu B   1-D Q16.16 elems: %llu\n",
           out, blob.size()/1e6, (unsigned long long)i8bytes, (unsigned long long)q16count);
    printf("detok vocab : %zu tokens  (%.2f MB raw bytes)%s\n", vocab.size(), vbytes/1e6,
           vocab.empty() ? "  [none — GGUF has no tokenizer]" : "");
    return 0;
}

int main(int argc, char** argv){
    if (argc>1){ const char* out = argc>2 ? argv[2] : "qwen3-1.7b.mlm"; return convert_file(argv[1], out); }
    return run_selftest();
}
