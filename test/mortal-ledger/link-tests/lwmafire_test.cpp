// Link against REAL Bitcoin Core and call the actual GetNextWorkRequired (src/pow.cpp)
// to prove the LWMA retarget FIRES at the fork height H — not before, and only once a
// full window of POST-fork history exists. This is the firing-mechanism counterpart to
// lwma_test.cpp (which proves the LWMA algorithm itself). No Raspberry Pi required: the
// switch is a pure function of the chain and Consensus::nMortalLedgerHeight, replayed
// here on a synthetic chain against the real consensus code.
//
// Build (Core root, after a normal build):
//   g++ -std=c++20 -I src -I build/src node/patches/lwmafire_test.cpp \
//     build/lib/libbitcoin_common.a build/lib/libbitcoin_consensus.a \
//     build/lib/libbitcoin_util.a build/lib/libbitcoin_crypto.a \
//     build/src/secp256k1/lib/libsecp256k1.a -o lwmafiretest && ./lwmafiretest
#include <arith_uint256.h>
#include <uint256.h>
#include <chain.h>
#include <pow.h>
#include <primitives/block.h>
#include <consensus/params.h>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <string>

std::string FormatFullVersion() { return "mortal-ledger-lwma-fire-test"; }

static int pass = 0, total = 0;
static void check(const char* n, bool c) { total++; if (c) pass++; printf("  [%s] %s\n", c ? "ok" : "FAIL", n); }

int main()
{
    const int64_t T = 60;                 // target spacing
    const int     N = 60;                  // must match MORTAL_LEDGER_LWMA_N in pow.cpp
    const int     H = 100;                 // fork-activation height under test

    Consensus::Params params{};
    params.nPowTargetSpacing  = T;
    params.nPowTargetTimespan = T * 100000;     // legacy interval huge => mid-interval = hold nBits
    params.fPowAllowMinDifficultyBlocks = false; // legacy mid-interval returns pindexLast->nBits
    params.powLimit = ArithToUint256(~arith_uint256(0));
    params.nMortalLedgerHeight = H;               // <-- the single firing point

    const arith_uint256 maxv = ~arith_uint256(0);
    arith_uint256 target0 = maxv / arith_uint256((uint64_t)1000000);
    target0.SetCompact(target0.GetCompact());     // round-trip through compact like a real nBits

    // Synthetic chain: every block holds nBits=target0; blocks come FAST (spacing T/2),
    // so the LWMA result is strictly tighter than the held target0 — i.e. firing is
    // observable as GetNextWorkRequired != tip->nBits.
    std::vector<CBlockIndex*> chain;
    CBlockIndex* prev = nullptr;
    for (int h = 0; h <= H + N + 8; h++) {
        CBlockIndex* idx = new CBlockIndex(); // one-shot test; leaked deliberately
        idx->pprev   = prev;
        idx->nHeight = h;
        idx->nBits   = target0.GetCompact();
        idx->nTime   = (uint32_t)(h * (T / 2));
        chain.push_back(idx);
        prev = idx;
    }

    CBlockHeader dummy; // only read by the (disabled) min-difficulty branch
    auto gnwr  = [&](CBlockIndex* tip) { return GetNextWorkRequired(tip, &dummy, params); };
    auto lwma  = [&](CBlockIndex* tip) { return GetNextWorkRequiredLWMA(tip, params); };
    const uint32_t held = target0.GetCompact();

    // Sanity: the LWMA value really differs from the held target, so "== held" below
    // genuinely means "LWMA did NOT fire" (not a coincidence).
    check("LWMA result differs from the held target (firing is observable)",
          lwma(chain[H + N + 2]) != held);

    // 1. pre-fork: even though the ABSOLUTE window is satisfied (height >> N), the fork
    //    is not active yet (tip+1 < H), so the retarget stays legacy (holds nBits).
    check("pre-fork tip (height < H, absolute window full): LWMA does NOT fire",
          gnwr(chain[H - 31]) == held);   // tip height 69 -> next 70 < H

    // 2. post-fork but within the warm-up window (fewer than N post-fork blocks): the
    //    fork is active but the LWMA window guard holds the (reset) target.
    check("just after H, post-fork window not yet full: LWMA does NOT fire",
          gnwr(chain[H + 29]) == held);   // tip 129 -> active, 129-100=29 < N

    // 3. once N post-fork blocks exist, the retarget fires to LWMA.
    CBlockIndex* fired = chain[H + N + 4]; // tip 164 -> active, 164-100=64 >= N
    check("H + N reached: retarget fires to LWMA (== GetNextWorkRequiredLWMA, != held)",
          gnwr(fired) == lwma(fired) && gnwr(fired) != held);

    // 4. exact boundary: the first tip L with L+1 > H and L - H >= N fires, the one
    //    before it does not. Boundary tip L = H + N.
    check("fires at exactly tip height H+N (boundary), not at H+N-1",
          gnwr(chain[H + N]) == lwma(chain[H + N]) &&
          gnwr(chain[H + N - 1]) == held);

    printf("\n%d/%d  %s\n", pass, total, pass == total ? "PASS" : "FAIL");
    return pass == total ? 0 : 1;
}
