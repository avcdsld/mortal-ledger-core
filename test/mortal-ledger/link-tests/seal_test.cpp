// Link against REAL Bitcoin Core (libbitcoin_consensus) and call the actual
// EvalScript to prove OP_PROMPT is sealed: fatal when executed AND in a dead branch.
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <vector>
#include <cstdio>
int main() {
    BaseSignatureChecker checker;
    {
        CScript s; s << OP_PROMPT;
        std::vector<std::vector<unsigned char>> stack; ScriptError err = SCRIPT_ERR_OK;
        bool ok = EvalScript(stack, s, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, &err);
        printf("executed:    ok=%d err=%s\n", ok, ScriptErrorString(err).c_str());
    }
    {
        CScript s; s << OP_0 << OP_IF << OP_PROMPT << OP_ENDIF; // never-executed branch
        std::vector<std::vector<unsigned char>> stack; ScriptError err = SCRIPT_ERR_OK;
        bool ok = EvalScript(stack, s, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, &err);
        printf("dead branch: ok=%d err=%s\n", ok, ScriptErrorString(err).c_str());
    }
    {
        CScript s; s << OP_CAT; // revived elsewhere, but vanilla Core would disable; here check it is NOT prompt
        std::vector<std::vector<unsigned char>> stack; ScriptError err = SCRIPT_ERR_OK;
        bool ok = EvalScript(stack, s, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, &err);
        printf("op_cat:      ok=%d err=%s\n", ok, ScriptErrorString(err).c_str());
    }
    return 0;
}
