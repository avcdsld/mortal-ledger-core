// Resident streaming generator for chatting through the node's exact voice. It loads the
// .mlm once and drives a KV-cached dream session (the integer fixed-point forward, seeded
// sampling for DREAM): bit-identical to the node's OP_DREAM, but each token costs ~one
// position instead of reprocessing the whole prompt, so a long prompt no longer makes every
// token slow. SLOW is still relative — this is the real integer voice, just no longer
// quadratic in prompt length.
//
// Protocol (line-based, one token per reply):
//   "<id id id ...>"  -> start a new dream from this prompt; reply with the first token
//   "" (empty line)   -> continue the current dream; reply with the next token
//
// Build (from the Core root, after building bitcoind):
//   g++ -std=c++20 -I src -I build/src test/mortal-ledger/next_token.cpp src/mortalllm.cpp \
//       <libbitcoin_consensus/crypto/util/common + univalue + secp256k1> -o next_token
//   ./next_token qwen3-1.7b.mlm [seed-hex]
#include <mortalllm.h>
#include <uint256.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <model.mlm> [seed-hex]\n", argv[0]); return 2; }
    try {
        MortalInstallLLM(argv[1]); // tool: no hash enforcement (use any .mlm, incl. toy)
    } catch (const std::exception& e) {
        std::fprintf(stderr, "next_token: %s\n", e.what());
        return 2;
    }
    // The "block" seed: the same words with a different seed dream a different (but exactly
    // reproducible) dream. JUDGE would ignore it; DREAM samples from it.
    uint256 seed;
    if (argc > 2) {
        std::string h = argv[2];
        if (h.size() > 64) h = h.substr(0, 64);
        h = std::string(64 - h.size(), '0') + h; // left-pad to 32 bytes
        if (auto o = uint256::FromHex(h)) seed = *o;
    }
    std::fprintf(stderr, "next_token: model loaded; ready (seed=%s)\n", seed.GetHex().c_str());

    MortalDreamSession* sess = nullptr;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<int> ids;
        std::istringstream ss(line);
        long id;
        while (ss >> id) if (id >= 0) ids.push_back((int)id);
        if (!ids.empty()) { // new prompt -> reset the session
            if (sess) MortalDreamEnd(sess);
            sess = MortalDreamBegin(ids, seed);
        }
        if (!sess) { std::cout << -1 << "\n" << std::flush; continue; }
        std::cout << MortalDreamNext(sess) << "\n" << std::flush;
    }
    if (sess) MortalDreamEnd(sess);
    return 0;
}
