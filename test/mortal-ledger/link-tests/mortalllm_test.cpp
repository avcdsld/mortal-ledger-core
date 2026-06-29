// Wire test for the embedded voice: load a tiny .mlm (toy architecture) and prove that
// OP_DREAM / OP_JUDGE run the real integer forward (src/mortalllm.cpp) instead of the
// built-in FNV stub, and that the result is deterministic. Correctness against " Paris"
// is NOT checked here (that needs the 1.7 GB pinned model on real hardware); this proves
// the model -> forward -> opcode pipeline and the integer determinism that OP_JUDGE
// needs to be a consensus-safe lock.
//
// Build (from the Core root, after building bitcoind):
//   g++ -std=c++20 -I src -I build/src test/mortal-ledger/link-tests/mortalllm_test.cpp \
//       src/mortalllm.cpp <libbitcoin_consensus/crypto/util/common + univalue + secp256k1> \
//       -o mortalllmtest && ./mortalllmtest <toy.mlm>
#include <mortalllm.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>

#include <cstdio>
#include <vector>

using valtype = std::vector<unsigned char>;

static bool eval_top(const CScript& s, valtype& top)
{
    BaseSignatureChecker checker;
    std::vector<valtype> stack;
    ScriptError err = SCRIPT_ERR_OK;
    bool ok = EvalScript(stack, s, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, &err);
    if (ok && !stack.empty()) top = stack.back();
    return ok;
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <toy.mlm>\n", argv[0]); return 2; }
    int pass = 0, total = 0;
    auto ck = [&](const char* n, bool c) { total++; if (c) pass++; printf("  [%s] %s\n", c ? "ok" : "FAIL", n); };

    // Input = token ids as u16 LE: {5, 10, 3}.
    valtype toks = {5, 0, 10, 0, 3, 0};
    CScript dream; dream << toks << OP_DREAM;

    // Before installing a model: the built-in FNV stub returns an 8-byte digest.
    valtype stub_out; eval_top(dream, stub_out);
    ck("stub OP_DREAM returns the 8-byte fallback digest", stub_out.size() == 8);

    MortalInstallLLM(argv[1]);

    valtype real_out, real_out2;
    eval_top(dream, real_out);
    eval_top(dream, real_out2);
    ck("after install OP_DREAM returns a 2-byte token id (real forward ran)", real_out.size() == 2);
    ck("real OP_DREAM is deterministic (same input -> same token)", real_out == real_out2);
    ck("installing the model changed the result (stub != real)", real_out != stub_out);

    // OP_JUDGE: (context.. target) -- 0|1, a stable one-bit verdict.
    CScript judge; judge << toks << OP_JUDGE;
    valtype j1, j2;
    eval_top(judge, j1);
    eval_top(judge, j2);
    const bool is_bool = j1.empty() || (j1.size() == 1 && j1[0] == 1);
    ck("OP_JUDGE returns a one-bit verdict (true/false)", is_bool);
    ck("OP_JUDGE is deterministic (consensus-safe lock)", j1 == j2);

    printf("\n%d/%d  %s\n", pass, total, pass == total ? "PASS" : "FAIL");
    return pass == total ? 0 : 1;
}
