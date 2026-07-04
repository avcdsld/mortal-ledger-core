// Mortal Ledger dream inscription — model-free unit checks (CI-runnable, no .mlm needed).
// Exercises the deterministic pieces of the dream pipeline: the 12 reference-word indices read
// from a parent hash, the prompt assembled from the pinned token tables, the u32-LE token
// (de)serialization, and the unspendable OP_RETURN coinbase output round-trip (build -> detect
// -> recover). The actual voice (MortalBlockDream) needs the real model and is exercised by
// demos/dream_demo.sh; here we prove the wiring around it.
//
// Build (from the Core root, after building bitcoind):
//   g++ -std=c++20 -I src -I build/src dream_test.cpp src/mortaldream.cpp src/mortalllm.cpp \
//       <libbitcoin_consensus/crypto/util/common + univalue + secp256k1> -o dream_test
#include <mortal_dream_prompt.h>
#include <mortaldream.h>
#include <pow.h>
#include <script/script.h>
#include <uint256.h>

#include <cstdio>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); failures++; }
}

int main()
{
    // A fixed parent hash -> MORTAL_DREAM_WORDS word indices, each a valid BIP39 index, deterministic.
    const uint256 parent = *uint256::FromHex("00112233445566778899aabbccddeeff0102030405060708090a0b0c0d0e0f10");
    const std::vector<int> idx = MortalDreamWordIndices(parent);
    check(idx.size() == (size_t)MORTAL_DREAM_WORDS, "MORTAL_DREAM_WORDS reference words");
    for (int w : idx) check(w >= 0 && w < mortal_dream::N_WORDS, "word index in [0,2048)");
    check(MortalDreamWordIndices(parent) == idx, "word indices are deterministic");

    // The prompt is PREFIX ++ words ++ SUFFIX: it opens with the chat-template/instruction
    // prefix and closes with the template tail, and is longer than the fixed frame.
    const std::vector<int> prompt = MortalComposeDreamPrompt(idx);
    const size_t nprefix = std::end(mortal_dream::PROMPT_PREFIX) - std::begin(mortal_dream::PROMPT_PREFIX);
    const size_t nsuffix = std::end(mortal_dream::PROMPT_SUFFIX) - std::begin(mortal_dream::PROMPT_SUFFIX);
    check(prompt.size() > nprefix + nsuffix, "prompt longer than the fixed frame");
    check(prompt.front() == mortal_dream::PROMPT_PREFIX[0], "prompt starts with the template prefix");
    check(prompt.back() == mortal_dream::PROMPT_SUFFIX[nsuffix - 1], "prompt ends with the template suffix");

    // The inscription carries the dream as UTF-8 text and round-trips: build -> detect ->
    // recover the same bytes (multibyte Japanese included).
    const std::string dream_text = "もちろん、以下は「夢日記」";
    const std::vector<unsigned char> bytes(dream_text.begin(), dream_text.end());
    const CScript out = MakeDreamInscription(bytes);
    std::vector<unsigned char> recovered;
    check(IsDreamInscription(out, &recovered), "MakeDreamInscription is detected as a dream");
    check(recovered == bytes, "inscribed dream text recovered");

    // Negatives: a plain OP_RETURN with the wrong magic, and a normal script, are not dreams.
    const CScript not_dream = CScript() << OP_RETURN << std::vector<unsigned char>{'X', 'X', 'X', 'X', 1, 2};
    check(!IsDreamInscription(not_dream, nullptr), "wrong-magic OP_RETURN is not a dream");
    check(!IsDreamInscription(CScript() << OP_DUP << OP_HASH160, nullptr), "non-OP_RETURN is not a dream");

    if (failures) { std::printf("dream_test: %d failure(s)\n", failures); return 1; }
    std::printf("dream_test: ok (%d words, prompt %zu tokens, inscription round-trip)\n", MORTAL_DREAM_WORDS, prompt.size());
    return 0;
}
