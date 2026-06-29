// Link against REAL Bitcoin Core and call the actual EvalScript (script/interpreter.cpp)
// to prove the Mortal Ledger opcode set: revived splice/bitwise/arith opcodes work,
// OP_PROMPT stays sealed (fatal even in a dead branch), the added primitives and LLM
// verbs work, and the three locks (death / completion / ownership=quotation) compose
// from primitives. Mirrors node/script_interpreter.cpp's checks on the real evaluator.
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <vector>
#include <string>
#include <cstdio>

using valtype = std::vector<unsigned char>;
static valtype B(const std::string& s) { return valtype(s.begin(), s.end()); }
static valtype N(int64_t v) { return CScriptNum(v).getvch(); }

static bool eval(const CScript& s, std::vector<valtype>& stack, ScriptError& err) {
    BaseSignatureChecker checker;
    stack.clear();
    return EvalScript(stack, s, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, &err);
}
static bool truthy(const valtype& v) {
    for (size_t i = 0; i < v.size(); i++)
        if (v[i] != 0) return !(i == v.size() - 1 && v[i] == 0x80); // negative zero is false
    return false;
}
static bool top_true(const std::vector<valtype>& st) { return !st.empty() && truthy(st.back()); }

static int pass = 0, total = 0;
static void check(const char* name, bool cond) {
    total++; if (cond) pass++;
    printf("  [%s] %s\n", cond ? "ok" : "FAIL", name);
}

int main() {
    std::vector<valtype> st; ScriptError err;

    // 1. revived OP_CAT joins 猫 + 町 = 猫町
    {
        CScript s; s << B("猫") << B("町") << OP_CAT;
        bool ok = eval(s, st, err);
        check("revived OP_CAT joins 猫+町 = 猫町", ok && st.size()==1 && st[0]==B("猫町"));
    }
    // 2. revived OP_SUBSTR excerpts the first 6 bytes (猫町) of 猫町です
    {
        CScript s; s << B("猫町です") << 0 << 6 << OP_SUBSTR;
        bool ok = eval(s, st, err);
        check("revived OP_SUBSTR excerpts 猫町", ok && st.back()==B("猫町"));
    }
    // 3. revived OP_LEFT / OP_RIGHT
    {
        CScript s; s << B("abcdef") << 2 << OP_LEFT;
        bool ok = eval(s, st, err);
        bool left = ok && st.back()==B("ab");
        CScript s2; s2 << B("abcdef") << 2 << OP_RIGHT;
        ok = eval(s2, st, err);
        check("revived OP_LEFT=ab, OP_RIGHT=ef", left && ok && st.back()==B("ef"));
    }
    // 4. revived arithmetic: 6 * 7 = 42, 20 % 7 = 6
    {
        CScript s; s << 6 << 7 << OP_MUL;
        bool ok = eval(s, st, err);
        bool mul = ok && CScriptNum(st.back(), false).getint()==42;
        CScript s2; s2 << 20 << 7 << OP_MOD;
        ok = eval(s2, st, err);
        check("revived OP_MUL=42, OP_MOD=6", mul && ok && CScriptNum(st.back(), false).getint()==6);
    }
    // 5. OP_PROMPT sealed when executed
    {
        CScript s; s << B("anything") << OP_PROMPT;
        bool ok = eval(s, st, err);
        check("OP_PROMPT executed -> DISABLED_OPCODE", !ok && err==SCRIPT_ERR_DISABLED_OPCODE);
    }
    // 6. OP_PROMPT sealed even in a DEAD branch (the key property)
    {
        CScript s; s << OP_0 << OP_IF << OP_PROMPT << OP_ENDIF;
        bool ok = eval(s, st, err);
        check("OP_PROMPT in unexecuted IF branch -> still DISABLED", !ok && err==SCRIPT_ERR_DISABLED_OPCODE);
    }
    // 7. LLM ops deterministic (model C): same input -> same output, and DREAM != TRANSLATE
    {
        CScript s; s << B("snow ash river") << OP_DREAM;  eval(s, st, err); valtype d1 = st.back();
        eval(s, st, err); valtype d2 = st.back();
        CScript s2; s2 << B("snow ash river") << OP_TRANSLATE; eval(s2, st, err); valtype t1 = st.back();
        check("OP_DREAM deterministic, distinct from OP_TRANSLATE", d1==d2 && d1!=t1);
    }
    // 8. death lock: OP_TTL < 10 opens only when life is low
    {
        g_mortal_ttl = 5;
        CScript s; s << OP_TTL << 10 << OP_LESSTHAN; bool lo = eval(s, st, err) && top_true(st);
        g_mortal_ttl = 50;
        bool hi = eval(s, st, err) && top_true(st);
        check("death lock opens when ttl<10, shut at ttl=50", lo && !hi);
    }
    // 9. completion lock: OP_SOURCELEFT < 1 opens only at the instant the novel ends
    {
        g_mortal_source_left = 0;
        CScript s; s << OP_SOURCELEFT << 1 << OP_LESSTHAN; bool done = eval(s, st, err) && top_true(st);
        g_mortal_source_left = 4;
        bool writing = eval(s, st, err) && top_true(st);
        check("completion lock opens at sourceleft=0, shut while writing (=4)", done && !writing);
    }
    // 10. ownership = quotation: OP_SNIPPET == the claimed passage unlocks
    {
        g_mortal_snippet = [](int64_t h) -> valtype { return h==840000 ? B("をはり") : valtype(); };
        CScript s; s << 840000 << OP_SNIPPET << B("をはり") << OP_EQUAL;
        bool right = eval(s, st, err) && top_true(st);
        CScript s2; s2 << 840000 << OP_SNIPPET << B("wrong") << OP_EQUAL;
        bool wrong = eval(s2, st, err) && top_true(st);
        check("ownership=quotation: right passage unlocks, wrong does not", right && !wrong);
    }

    printf("\n%d/%d  %s\n", pass, total, pass==total ? "PASS" : "FAIL");
    return pass==total ? 0 : 1;
}
