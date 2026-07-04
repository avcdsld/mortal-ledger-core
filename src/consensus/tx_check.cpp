// Copyright (c) 2017-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <pow.h>
#include <script/script.h>

#include <algorithm>

bool CheckTransaction(const CTransaction& tx, TxValidationState& state)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-empty");
    // Mortal Ledger: a coinbase may carry a registered novel in an unspendable OP_SOURCE
    // output (scriptPubKey = OP_SOURCE <novel>, up to MAX_NOVEL_BYTES) and an inscribed dream
    // in an unspendable OP_RETURN output (up to MAX_DREAM_BYTES). Those bytes are exempt from
    // the per-transaction size limit, matching the block's size/weight exemption — a seam block
    // can carry both a whole novel and a dream.
    uint64_t novel_exempt = 0;
    if (tx.IsCoinBase()) {
        uint64_t dream_exempt = 0;
        for (const auto& txout : tx.vout) {
            const CScript& s = txout.scriptPubKey;
            std::vector<unsigned char> tb;
            if (IsDreamInscription(s, &tb)) { dream_exempt += tb.size() + sizeof(MORTAL_DREAM_MAGIC); continue; }
            if (s.empty() || s[0] != OP_SOURCE) continue;
            CScript::const_iterator pc = s.begin();
            opcodetype op;
            std::vector<unsigned char> data;
            if (!s.GetOp(pc, op, data)) continue;            // OP_SOURCE
            data.clear();
            if (s.GetOp(pc, op, data)) novel_exempt += data.size(); // the pushed novel
        }
        novel_exempt = std::min<uint64_t>(novel_exempt, MAX_NOVEL_BYTES) +
                       std::min<uint64_t>(dream_exempt, MAX_DREAM_BYTES);
    }
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if ((uint64_t)::GetSerializeSize(TX_NO_WITNESS(tx)) * WITNESS_SCALE_FACTOR > (uint64_t)MAX_BLOCK_WEIGHT + novel_exempt * WITNESS_SCALE_FACTOR) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-oversize");
    }

    // Check for negative or overflow output values (see CVE-2010-5139)
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs (see CVE-2018-17144)
    // While Consensus::CheckTxInputs does check if all inputs of a tx are available, and UpdateCoins marks all inputs
    // of a tx as spent, it does not check if the tx has duplicate inputs.
    // Failure to run this check will result in either a crash or an inflation bug, depending on the implementation of
    // the underlying coins database.
    std::set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin) {
        if (!vInOutPoints.insert(txin.prevout).second)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-inputs-duplicate");
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-cb-length");
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-prevout-null");
    }

    return true;
}
