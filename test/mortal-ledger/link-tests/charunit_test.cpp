// Mortal Ledger: the transcription unit is ONE CHARACTER — k = the UTF-8 byte length of the
// character at the current offset (1..4). The regtest demos use ASCII novels (all k=1), so this
// link-test covers the multi-byte path (k=2,3) directly against the real consensus function.
#include <pow.h>

#include <cstdint>
#include <cstdio>
#include <vector>

static int fails = 0;
static void chk(bool ok, const char* what) { if (!ok) { std::printf("  FAIL: %s\n", what); fails++; } }

int main()
{
    // novel: 猫(E7 8C AB, 3) 町(E7 94 BA, 3) \n(0A, 1) A(41, 1) é(C3 A9, 2)
    const std::vector<unsigned char> novel = {0xe7,0x8c,0xab, 0xe7,0x94,0xba, 0x0a, 0x41, 0xc3,0xa9};
    auto slice = [&](uint64_t off) { return NovelExpectedSlice(NovelState{1, off, 0}, novel, {}); };

    chk(slice(0).size() == 3, "猫 spans 3 bytes (one block)");
    chk(slice(3).size() == 3, "町 spans 3 bytes");
    chk(slice(6).size() == 1, "newline spans 1 byte");
    chk(slice(7).size() == 1, "ASCII 'A' spans 1 byte");
    chk(slice(8).size() == 2, "é spans 2 bytes");

    // NovelNext advances the offset by exactly one whole character.
    chk(NovelNext(NovelState{1, 0, 0}, novel, 2, {}).offset == 3, "advance past 猫 -> offset 3");
    chk(NovelNext(NovelState{1, 6, 0}, novel, 2, {}).offset == 7, "advance past newline -> offset 7");
    chk(NovelNext(NovelState{1, 8, 0}, novel, 2, {}).offset == 10, "advance past é -> offset 10");

    // Issuance stays per-byte: a 3-byte character mints 3 BAB.
    chk(NovelIssuance(NovelState{1, 0, 0}, novel, {}) == 3 * COIN, "猫 block mints 3 BAB (per byte)");
    chk(NovelIssuance(NovelState{1, 6, 0}, novel, {}) == 1 * COIN, "newline block mints 1 BAB");

    if (fails) { std::printf("charunit: %d failure(s)\n", fails); return 1; }
    std::printf("charunit: ok (per-character k = UTF-8 length; issuance per byte)\n");
    return 0;
}
