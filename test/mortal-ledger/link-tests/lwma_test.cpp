// Link against REAL Bitcoin Core and call the actual CalculateNextWorkRequiredLWMA
// (src/pow.cpp) to prove the arith_uint256 port behaves like the float reference
// (node/difficulty_lwma.cpp): holds the target spacing T, recovers from a 10x
// hashpower jump within ~two windows, and does not freeze when the hashpower leaves.
#include <arith_uint256.h>
#include <uint256.h>
#include <chain.h>
#include <pow.h>
#include <consensus/params.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

// Satisfy the one symbol check.cpp pulls in (only used in assertion-failure paths,
// which this test does not hit) without dragging in the whole client-version unit.
std::string FormatFullVersion() { return "mortal-ledger-lwma-test"; }

int main() {
    const int64_t T = 60;            // one block per minute
    const double  Hpi = 1e6;         // a Raspberry Pi, ~1 MH/s
    const int     N = 60;            // must match MORTAL_LEDGER_LWMA_N in pow.cpp

    Consensus::Params params{};
    params.nPowTargetSpacing = T;
    params.nMortalLedgerHeight = 0;  // fork active; CalculateNextWorkRequiredLWMA is called directly below
    params.powLimit = ArithToUint256(~arith_uint256(0)); // effectively no clamp

    const arith_uint256 maxv = ~arith_uint256(0);

    // Reset target: expected solve == T on the Pi. expected_hashes = 2^256/target,
    // set = T*Hpi  =>  target0 = (2^256-1)/(T*Hpi).
    arith_uint256 target0 = maxv / arith_uint256((uint64_t)(T * (int64_t)Hpi));
    // Round-trip through compact, exactly as nBits stores it on a real block.
    target0.SetCompact(target0.GetCompact());

    auto add_block = [&](CBlockIndex* prev, int height, uint32_t nbits, int64_t ntime) -> CBlockIndex* {
        CBlockIndex* idx = new CBlockIndex(); // leaked deliberately; this is a one-shot test
        idx->pprev = prev;
        idx->nHeight = height;
        idx->nBits = nbits;
        idx->nTime = (uint32_t)ntime;
        return idx;
    };

    // Warm-start a full window (+1 prior block) at equilibrium: spacing T, target0.
    CBlockIndex* tip = nullptr;
    int64_t t = 0;
    for (int i = 0; i <= N; i++) { tip = add_block(tip, i, target0.GetCompact(), t); t += T; }

    auto run_phase = [&](double H, int blocks, std::vector<double>& out) {
        for (int b = 0; b < blocks; b++) {
            arith_uint256 nt = CalculateNextWorkRequiredLWMA(tip, params);
            double hashes = (maxv / nt).getdouble();   // 2^256/target = expected hashes
            double solve = hashes / H;                  // deterministic expected solve time
            int64_t st = (int64_t)llround(solve);
            t = (int64_t)tip->nTime + st;
            tip = add_block(tip, tip->nHeight + 1, nt.GetCompact(), t);
            out.push_back(solve);
        }
    };

    std::vector<double> a, b, c;
    run_phase(Hpi,      120, a);   // steady, one Pi
    run_phase(Hpi * 10, 200, b);   // an ASIC arrives, 10x hashpower
    run_phase(Hpi,      200, c);   // the ASIC leaves

    auto tail_avg = [](const std::vector<double>& v, int n) {
        double s = 0; for (int i = (int)v.size() - n; i < (int)v.size(); i++) s += v[i]; return s / n;
    };
    double avgA = tail_avg(a, 60), avgB = tail_avg(b, 60), avgC = tail_avg(c, 60);

    int trans = (int)b.size();
    for (size_t i = 0; i < b.size(); i++) if (std::fabs(b[i] - T) <= 0.1 * T) { trans = (int)i; break; }

    printf("LWMA in real Core (T=%lds, N=%d):\n", (long)T, N);
    printf("  steady, one Pi:    avg solve (last 60) = %6.2fs\n", avgA);
    printf("  ASIC 10x arrives:  first block fell to %6.2fs, recovered in %d blocks\n", b[0], trans);
    printf("                     avg solve (last 60) = %6.2fs\n", avgB);
    printf("  ASIC leaves:       avg solve (last 60) = %6.2fs (no freeze)\n", avgC);

    bool ok = std::fabs(avgA - T) < 0.02 * T
           && std::fabs(avgB - T) < 0.05 * T
           && std::fabs(avgC - T) < 0.05 * T
           && trans < 2 * N;
    printf("\n%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
