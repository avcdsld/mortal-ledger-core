// Persistent "next token" helper for chatting through the node's exact voice. It loads
// the .mlm once and then, for each line of space-separated token ids on stdin, runs the
// node's OP_DREAM path (g_mortal_llm('D', ...) = the integer fixed-point forward, argmax
// = temperature 0) and prints the next token id. The model stays resident between calls,
// so a driver (chat_integer.py) can generate greedily without reloading 1.7 GB per token.
//
// This is the real consensus voice, not a fast proxy: every token is decided by the same
// integer forward the node runs. It has no KV cache and naive kernels, so it is SLOW
// (tens of seconds to minutes per token on the full model) — that slowness is the point.
//
// Build (from the Core root, after building bitcoind):
//   g++ -std=c++20 -I src -I build/src test/mortal-ledger/next_token.cpp src/mortalllm.cpp \
//       <libbitcoin_consensus/crypto/util/common + univalue + secp256k1> -o next_token
//   ./next_token qwen3-1.7b.mlm     # then feed "id id id\n" -> prints next id
#include <mortalllm.h>
#include <script/interpreter.h> // g_mortal_llm
#include <uint256.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using valtype = std::vector<unsigned char>;

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <model.mlm>\n", argv[0]); return 2; }
    try {
        MortalInstallLLM(argv[1]); // tool: no hash enforcement (use any .mlm, incl. toy)
    } catch (const std::exception& e) {
        std::fprintf(stderr, "next_token: %s\n", e.what());
        return 2;
    }
    std::fprintf(stderr, "next_token: model loaded; ready\n");
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<unsigned int> ids;
        std::istringstream ss(line);
        long id;
        while (ss >> id) if (id >= 0) ids.push_back((unsigned int)id);
        if (ids.empty()) { std::cout << -1 << "\n" << std::flush; continue; }
        valtype in;
        for (unsigned int t : ids) { in.push_back(t & 0xff); in.push_back((t >> 8) & 0xff); in.push_back((t >> 16) & 0xff); in.push_back((t >> 24) & 0xff); }
        valtype out = g_mortal_llm('D', in, uint256{});
        if (out.size() < 4) { std::cout << -1 << "\n" << std::flush; continue; }
        unsigned int nt = (unsigned)out[0] | ((unsigned)out[1] << 8) | ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24);
        std::cout << nt << "\n" << std::flush;
    }
    return 0;
}
