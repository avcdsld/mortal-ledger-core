// Link against REAL Bitcoin Core and call the actual EvalScript (script/interpreter.cpp)
// to prove OP_MNEMONIC: an ARBITRARY byte string + a language id become mnemonic words via
// a BIP39 wordlist (a deterministic table lookup, NOT a checksummed seed phrase). Any
// non-empty input yields >=1 word. Intended to turn bytes into word tokens (e.g. to feed
// the LLM verbs). Deterministic (byte-pinned wordlists), so it is safe in a spend condition.
//
// Build (Core root, after a normal build):
//   g++ -std=c++20 -I src -I build/src node/patches/mnemonic_test.cpp \
//     build/lib/libbitcoin_consensus.a build/lib/libbitcoin_crypto.a \
//     build/lib/libbitcoin_util.a build/src/secp256k1/lib/libsecp256k1.a -o mnemonictest && ./mnemonictest
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <vector>
#include <string>
#include <cstdio>

using valtype = std::vector<unsigned char>;
static valtype B(const std::string& s) { return valtype(s.begin(), s.end()); }
static std::string S(const valtype& v) { return std::string(v.begin(), v.end()); }

static bool eval(const CScript& s, std::vector<valtype>& stack, ScriptError& err) {
    BaseSignatureChecker checker;
    stack.clear();
    return EvalScript(stack, s, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, &err);
}

static int pass = 0, total = 0;
static void check(const char* name, bool cond) {
    total++; if (cond) pass++;
    printf("  [%s] %s\n", cond ? "ok" : "FAIL", name);
}

int main() {
    std::vector<valtype> st; ScriptError err;

    // 1. one byte -> exactly one word (minimum-1-word, any size in)
    {
        CScript s; s << valtype{0x00} << 0 << OP_MNEMONIC;
        bool ok = eval(s, st, err);
        check("1 byte 0x00 (EN) -> 1 word 'abandon'", ok && st.size()==1 && S(st[0])=="abandon");
    }
    // 2. one byte, high bits -> single word from the 11-bit (zero-padded) index
    {
        CScript s; s << valtype{0xff} << 0 << OP_MNEMONIC;
        bool ok = eval(s, st, err);
        check("1 byte 0xff (EN) -> 1 word 'yellow'", ok && S(st.back())=="yellow");
    }
    // 3. arbitrary 2-byte string -> 2 words
    {
        CScript s; s << B("Hi") << 0 << OP_MNEMONIC;
        bool ok = eval(s, st, err);
        check("'Hi' (2 bytes, EN) -> 'embody elite'", ok && S(st.back())=="embody elite");
    }
    // 4. arbitrary 3 bytes -> 3 words (e.g. a hash prefix)
    {
        CScript s; s << valtype{0xde,0xad,0xbe} << 0 << OP_MNEMONIC;
        bool ok = eval(s, st, err);
        check("0xdeadbe (3 bytes, EN) -> 'team hospital length'", ok && S(st.back())=="team hospital length");
    }
    // 5. language selectable: same byte, Japanese wordlist
    {
        CScript s; s << valtype{0x00} << 1 << OP_MNEMONIC;
        bool ok = eval(s, st, err);
        check("1 byte 0x00 (JA) -> 'あいこくしん'", ok && S(st.back())=="あいこくしん");
    }
    // 6. composes in a spend condition: result == expected -> TRUE
    {
        CScript s; s << B("Hi") << 0 << OP_MNEMONIC << B("embody elite") << OP_EQUAL;
        bool ok = eval(s, st, err);
        check("OP_MNEMONIC + OP_EQUAL composes (lock on the words)", ok && st.size()==1 && st.back()==valtype{1});
    }
    // 7. feeds another opcode: bytes -> words -> (here) OP_SIZE is non-trivial
    {
        CScript s; s << valtype{0xde,0xad,0xbe} << 0 << OP_MNEMONIC << OP_SIZE;
        bool ok = eval(s, st, err);
        // "team hospital length" is 20 bytes; OP_SIZE pushes that count on top
        check("output feeds the next opcode (OP_SIZE sees the words)", ok && CScriptNum(st.back(), false).getint()==20);
    }
    // 8. unknown language id fails the script
    {
        CScript s; s << valtype{0x00} << 2 << OP_MNEMONIC;
        bool ok = eval(s, st, err);
        check("unknown language id fails", !ok);
    }
    // 9. deterministic: same bytes+lang -> same words
    {
        CScript s; s << B("the same input") << 0 << OP_MNEMONIC;
        std::vector<valtype> a, b; ScriptError e; BaseSignatureChecker ck;
        EvalScript(a, s, SCRIPT_VERIFY_NONE, ck, SigVersion::BASE, &e);
        EvalScript(b, s, SCRIPT_VERIFY_NONE, ck, SigVersion::BASE, &e);
        check("deterministic: identical input -> identical words", !a.empty() && a==b);
    }
    // 10. a 32-byte (mined-hash sized) input still works -> 24 words
    {
        CScript s; s << valtype(32, 0xab) << 0 << OP_MNEMONIC;
        bool ok = eval(s, st, err);
        int words = 1; for (char c : S(st.back())) if (c==' ') words++;
        check("32-byte input (EN) -> 24 words (ceil(256/11))", ok && words==24);
    }

    printf("\n%d/%d  %s\n", pass, total, pass==total ? "PASS" : "FAIL");
    return pass==total ? 0 : 1;
}
