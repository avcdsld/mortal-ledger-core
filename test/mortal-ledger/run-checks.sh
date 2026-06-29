#!/usr/bin/env bash
# Mortal Ledger fork verification: build the real-Core link tests and run the
# regtest demos against this build. This is the fork's own CI gate (the inherited
# Bitcoin test suite asserts vanilla-Bitcoin behavior the fork deliberately changes,
# so it cannot pass unmodified; these checks prove the fork's consensus instead).
#
# Run from the repo root AFTER building bitcoind + bitcoin-cli into ./build.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
BUILD="${BUILD:-$ROOT/build}"
export BINDIR="$BUILD/bin"
HERE="$ROOT/test/mortal-ledger"
LINK_LIBS=(
  "$BUILD/lib/libbitcoin_common.a"
  "$BUILD/lib/libbitcoin_consensus.a"
  "$BUILD/lib/libbitcoin_util.a"
  "$BUILD/lib/libbitcoin_crypto.a"
  "$BUILD/src/univalue/libunivalue.a"
  "$BUILD/src/secp256k1/lib/libsecp256k1.a"
)
CXX="${CXX:-g++}"
fails=0
pass() { echo "  PASS: $1"; }
fail() { echo "  FAIL: $1"; fails=$((fails+1)); }

echo "== link tests (compiled against real Core libraries) =="
for t in seal script mnemonic llmhook lwma lwmafire chainparams; do
  bin="/tmp/ml_${t}test"
  if ! "$CXX" -std=c++20 -I "$ROOT/src" -I "$BUILD/src" \
        "$HERE/link-tests/${t}_test.cpp" "${LINK_LIBS[@]}" -o "$bin" 2>"/tmp/ml_${t}_cc.log"; then
    fail "$t (compile)"; sed 's/^/    /' "/tmp/ml_${t}_cc.log" | head -8; continue
  fi
  out="$("$bin" 2>&1)"; rc=$?
  if [ "$t" = seal ]; then
    # seal_test always returns 0; assert OP_PROMPT is disabled both executed and in a dead branch
    [ "$(grep -c 'disabled opcode' <<<"$out")" -ge 2 ] && pass "$t" || { fail "$t"; echo "$out" | sed 's/^/    /'; }
  else
    [ $rc -eq 0 ] && pass "$t (${out##*$'\n'})" || { fail "$t"; echo "$out" | sed 's/^/    /'; }
  fi
done

run_demo() { # $1=name  $2=grep-marker that must appear in output
  pkill -f "bitcoind -regtest" 2>/dev/null; sleep 1
  local out; out="$(bash "$HERE/demos/$1_demo.sh" 2>&1)"
  if grep -qE "$2" <<<"$out"; then pass "demo $1"; else
    fail "demo $1 (expected /$2/)"; echo "$out" | tail -12 | sed 's/^/    /'
  fi
}

echo "== regtest demos (driving bitcoind/bitcoin-cli) =="
run_demo poq         'blockcount after: 69'
run_demo succession  'blockcount after: 86'
run_demo issuance    'total supply after A\+B: *85\.0 BAB'
run_demo reindex     'REINDEX-SAFE'
run_demo lwma_fire   'h=62[[:space:]]+bits=20[0-6]'
run_demo op_source   'bad-canon-size'
pkill -f "bitcoind -regtest" 2>/dev/null

echo
if [ "$fails" -eq 0 ]; then echo "ALL MORTAL LEDGER CHECKS PASSED"; else echo "$fails CHECK(S) FAILED"; fi
exit "$fails"
