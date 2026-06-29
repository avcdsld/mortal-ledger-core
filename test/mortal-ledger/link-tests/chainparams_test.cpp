// Link against REAL Bitcoin Core and check the four separations of the fork on the
// actual objects: 1) message-start magic, 2) address bytes (base58 version + bech32
// hrp), 3) replay protection via the real SignatureHash + real secp256k1 signing,
// 4) activation (the fork id is gated, off pre-fork). Mirrors node/chainparams.cpp.
#include <chainparams.h>
#include <kernel/chainparams.h>
#include <common/args.h>
#include <util/chaintype.h>
#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

// Satisfy the one symbol check.cpp pulls in (assertion-failure path only).
std::string FormatFullVersion() { return "mortal-ledger-chainparams-test"; }

static int pass = 0, total = 0;
static void check(const char* name, bool cond) {
    total++; if (cond) pass++;
    printf("  [%s] %s\n", cond ? "ok" : "FAIL", name);
}

int main() {
    ECC_Context ecc_context{};
    ArgsManager args;

    auto main = CreateChainParams(args, ChainType::MAIN);
    auto regtest = CreateChainParams(args, ChainType::REGTEST);

    // 1. magic: Mortal Ledger's "MORT", never Bitcoin's f9 be b4 d9.
    const unsigned char BTC_MAIN_MAGIC[4] = {0xf9, 0xbe, 0xb4, 0xd9};
    const unsigned char MORT[4] = {'M','O','R','T'};
    const auto& m = main->MessageStart();
    bool magic_ml  = std::memcmp(m.data(), MORT, 4) == 0;
    bool magic_nbtc = std::memcmp(m.data(), BTC_MAIN_MAGIC, 4) != 0;
    check("mainnet magic is MORT, not Bitcoin's", magic_ml && magic_nbtc);

    // 2. addresses: base58 P2PKH version and bech32 hrp are the fork's, not Bitcoin's.
    bool p2pkh = main->Base58Prefix(CChainParams::PUBKEY_ADDRESS) == std::vector<unsigned char>{77};
    bool hrp_main = main->Bech32HRP() == "ml";
    bool hrp_regt = regtest->Bech32HRP() == "mlrt";
    check("mainnet P2PKH version=0x4d, bech32 hrp=ml (Bitcoin was 0x00 / bc)", p2pkh && hrp_main);
    check("regtest bech32 hrp=mlrt (Bitcoin regtest was bcrt)", hrp_regt);

    // 3 + 4. replay: build a real tx, hash it with the REAL SignatureHash off (pre-fork)
    // and on (post-fork), then sign with real secp256k1 and show no transfer.
    CKey key;
    unsigned char seckey[32]; for (int i = 0; i < 32; i++) seckey[i] = (unsigned char)(i + 1);
    key.Set(seckey, seckey + 32, true);
    CPubKey pub = key.GetPubKey();

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ONE), 0);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 1000;
    CScript scriptCode; scriptCode << OP_DUP; // any code
    CTransaction tx{mtx};

    g_mortal_forkid = false;
    uint256 hBtc  = SignatureHash(scriptCode, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    g_mortal_forkid = true;
    uint256 hMort = SignatureHash(scriptCode, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    check("activation: fork id changes the sighash (off pre-fork, on post-fork)", hBtc != hMort);

    std::vector<unsigned char> sig;
    key.Sign(hMort, sig);                         // signed for Mortal Ledger
    bool okMort = pub.Verify(hMort, sig);          // valid here
    bool okBtc  = pub.Verify(hBtc, sig);           // replay attempt on Bitcoin's digest
    check("signature valid on Mortal Ledger", okMort);
    check("same signature INVALID on Bitcoin (no replay)", !okBtc);

    printf("\n%d/%d  %s\n", pass, total, pass == total ? "PASS" : "FAIL");
    return pass == total ? 0 : 1;
}
