// Copyright (c) 2026 The Mortal Ledger developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mortaldream.h>

#include <mortal_dream_prompt.h>
#include <mortalllm.h>
#include <sync.h>
#include <uint256.h>

#include <cstdint>
#include <iterator>
#include <set>

// Mortal Ledger: the dream being generated RIGHT NOW, exposed token by token so a display can
// watch the voice write it out live during the block's ~40s "pause" (getdreaming reads this). A
// new dream clears it and bumps the sequence. Guarded by its own mutex — NOT cs_main — so the
// RPC stays responsive while the miner holds cs_main assembling the block.
namespace {
Mutex g_dreaming_mutex;
std::vector<int> g_dreaming_tokens GUARDED_BY(g_dreaming_mutex);
bool g_dreaming_active GUARDED_BY(g_dreaming_mutex){false};
uint64_t g_dreaming_seq GUARDED_BY(g_dreaming_mutex){0};
} // namespace

void MortalDreamingSnapshot(bool& active, uint64_t& seq, std::vector<int>& tokens)
{
    LOCK(g_dreaming_mutex);
    active = g_dreaming_active;
    seq = g_dreaming_seq;
    tokens = g_dreaming_tokens;
}

// MORTAL_DREAM_WORDS reference words: the first WORDS*11 bits of the parent hash read as 11-bit
// groups — the same bit reading as OP_MNEMONIC (src/script/interpreter.cpp), so the words here
// match what OP_MNEMONIC would produce from those bytes. (6 words = 66 bits = the first 9 bytes.)
std::vector<int> MortalDreamWordIndices(const uint256& parent_hash)
{
    const unsigned char* b = parent_hash.begin();
    const size_t nbits = (size_t)MORTAL_DREAM_WORDS * 11;
    auto bit = [&](size_t i) -> int { return i < nbits ? ((b[i / 8] >> (7 - i % 8)) & 1) : 0; };
    std::vector<int> idx;
    idx.reserve(MORTAL_DREAM_WORDS);
    for (int w = 0; w < MORTAL_DREAM_WORDS; w++) {
        int v = 0;
        for (int k = 0; k < 11; k++) v = (v << 1) | bit((size_t)w * 11 + k);
        idx.push_back(v); // 0..2047
    }
    return idx;
}

std::vector<int> MortalComposeDreamPrompt(const std::vector<int>& word_indices)
{
    using namespace mortal_dream;
    std::vector<int> p(std::begin(PROMPT_PREFIX), std::end(PROMPT_PREFIX));
    for (size_t i = 0; i < word_indices.size(); i++) {
        if (i) p.insert(p.end(), std::begin(SEP), std::end(SEP));
        const int w = word_indices[i];
        if (w < 0 || w >= N_WORDS) continue; // skip an out-of-range index defensively
        for (int j = WORD_TOK_OFF[w]; j < WORD_TOK_OFF[w + 1]; j++) p.push_back(WORD_TOK_FLAT[j]);
    }
    p.insert(p.end(), std::begin(PROMPT_SUFFIX), std::end(PROMPT_SUFFIX));
    return p;
}

std::vector<int> MortalBlockDream(const uint256& parent_hash, const uint256& seed, int max_new)
{
    if (!MortalModelLoaded()) return {};
    const std::vector<int> prompt = MortalComposeDreamPrompt(MortalDreamWordIndices(parent_hash));
    const std::set<int> eos(std::begin(mortal_dream::EOS), std::end(mortal_dream::EOS));

    MortalDreamSession* s = MortalDreamBegin(prompt, seed);
    { LOCK(g_dreaming_mutex); g_dreaming_tokens.clear(); g_dreaming_active = true; ++g_dreaming_seq; }
    std::vector<int> out;
    for (int i = 0; i < max_new; i++) {
        const int t = MortalDreamNext(s); // first call returns the token after the prompt
        if (eos.count(t)) break;          // EOS ends the dream; the marker itself is not stored
        out.push_back(t);
        { LOCK(g_dreaming_mutex); g_dreaming_tokens.push_back(t); } // stream it out live
    }
    { LOCK(g_dreaming_mutex); g_dreaming_active = false; }
    MortalDreamEnd(s);
    return out;
}
