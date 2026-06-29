// Link against REAL Bitcoin Core and call the actual EvalScript to prove the LLM
// runtime boundary: OP_DREAM/JUDGE/TRANSLATE route through the installed deterministic
// runtime (g_mortal_llm), the execution-block seed (g_mortal_block_seed) reaches it,
// the same (input, seed) gives the same bytes (so OP_JUDGE-in-lock is consensus-safe),
// the dream varies block to block, and with no runtime installed the stub still works.
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <uint256.h>
#include <vector>
#include <string>
#include <cstdio>

using valtype = std::vector<unsigned char>;
static valtype B(const std::string& s) { return valtype(s.begin(), s.end()); }

static bool eval(const CScript& s, std::vector<valtype>& stack, ScriptError& err) {
    BaseSignatureChecker checker;
    stack.clear();
    return EvalScript(stack, s, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, &err);
}
static bool truthy(const valtype& v) {
    for (size_t i = 0; i < v.size(); i++) if (v[i] != 0) return !(i==v.size()-1 && v[i]==0x80);
    return false;
}

// A stand-in deterministic INTEGER runtime: a pure function of (verb, input, seed).
// (The real one is the Qwen3-1.7B integer kernel; here a fold proves the boundary.)
static int g_calls = 0;
static valtype test_runtime(uint8_t verb, const valtype& in, const uint256& seed) {
    g_calls++;
    uint64_t h = 0xABCDEF0123456789ULL ^ verb; // distinct from the built-in stub's basis
    for (int i = 0; i < 32; i++) { h ^= seed.begin()[i]; h *= 1099511628211ULL; }
    for (unsigned char c : in) { h ^= c; h *= 1099511628211ULL; }
    valtype out(8); for (int i = 0; i < 8; i++) out[i] = (unsigned char)(h >> (8*i));
    return out;
}

static int pass = 0, total = 0;
static void check(const char* name, bool cond) { total++; if (cond) pass++; printf("  [%s] %s\n", cond?"ok":"FAIL", name); }

int main() {
    std::vector<valtype> st; ScriptError err;
    uint256 blockA = uint256::ONE;
    uint256 blockB; blockB = uint256{"00000000000000000000000000000000000000000000000000000000000000ff"};

    // 1. OP_DREAM routes through the installed runtime (not the stub).
    {
        g_mortal_llm = nullptr; g_mortal_block_seed = blockA;
        CScript s; s << B("snow ash river") << OP_DREAM;
        eval(s, st, err); valtype stub = st.back();
        g_mortal_llm = test_runtime;
        eval(s, st, err); valtype viaRuntime = st.back();
        check("OP_DREAM routes through the installed runtime (differs from stub)", stub != viaRuntime);
    }
    // 2. deterministic: same input + same block seed -> same bytes.
    {
        g_mortal_llm = test_runtime; g_mortal_block_seed = blockA;
        CScript s; s << B("snow ash river") << OP_DREAM;
        eval(s, st, err); valtype d1 = st.back();
        eval(s, st, err); valtype d2 = st.back();
        check("OP_DREAM deterministic for same (input, block seed)", d1 == d2);
    }
    // 3. block-seeded: same input, different execution block -> different dream.
    {
        g_mortal_llm = test_runtime;
        CScript s; s << B("snow ash river") << OP_DREAM;
        g_mortal_block_seed = blockA; eval(s, st, err); valtype dA = st.back();
        g_mortal_block_seed = blockB; eval(s, st, err); valtype dB = st.back();
        check("OP_DREAM varies block to block (seed reaches the runtime)", dA != dB);
    }
    // 4. OP_JUDGE: a 1-bit verdict, deterministic given (input, seed). Consensus-safe.
    {
        g_mortal_llm = test_runtime; g_mortal_block_seed = blockA;
        CScript s; s << B("is this a city of cats?") << OP_JUDGE;
        eval(s, st, err); bool v1 = truthy(st.back());
        eval(s, st, err); bool v2 = truthy(st.back());
        bool one_bit = (st.back().empty() || st.back() == valtype{1});
        check("OP_JUDGE deterministic 1-bit verdict (lock is consensus-safe)", v1==v2 && one_bit);
    }
    // 5. DREAM and TRANSLATE differ on the same input (distinct verbs).
    {
        g_mortal_llm = test_runtime; g_mortal_block_seed = blockA;
        CScript sd; sd << B("the town") << OP_DREAM;      eval(sd, st, err); valtype d = st.back();
        CScript str; str << B("the town") << OP_TRANSLATE; eval(str, st, err); valtype t = st.back();
        check("OP_DREAM and OP_TRANSLATE differ (verb reaches the runtime)", d != t);
    }
    // 6. fallback: with no runtime installed, the stub still runs deterministically.
    {
        g_mortal_llm = nullptr; g_mortal_block_seed = blockA;
        CScript s; s << B("x") << OP_DREAM;
        eval(s, st, err); valtype a = st.back();
        eval(s, st, err); valtype b = st.back();
        check("fallback stub runs deterministically when no runtime is installed", err==SCRIPT_ERR_OK && a==b);
    }

    printf("\n%d/%d  %s\n", pass, total, pass==total ? "PASS" : "FAIL");
    return pass==total ? 0 : 1;
}
