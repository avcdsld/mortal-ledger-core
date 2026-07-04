#!/usr/bin/env bash
# Run the Mortal Ledger DEVNET — a REAL fork chain from its own genesis (own magic/port/address
# prefix, real LWMA difficulty), mining one character per block, continuously. NOT regtest.
#
#   bash test/mortal-ledger/run-devnet.sh                 # start, auto-mine, print the display cmd
#   MORTAL_FRESH=1 bash test/mortal-ledger/run-devnet.sh  # wipe and start a fresh chain from genesis
#   MORTAL_AUTOMINE=0 ...                                  # don't auto-mine (mine by hand with dv)
#
# Env: MORTAL_DATADIR (~/mortal-devnet), MORTAL_GENESIS_FILE (novels/nekomachi.txt), MORTAL_MODEL,
#      MORTAL_DREAM_MAXNEW (200), MORTAL_MAXTRIES (300M — a kanji needs ~16M grinds).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
B="${BINDIR:-$ROOT/build/bin}"
DD="${MORTAL_DATADIR:-$HOME/mortal-devnet}"
MODEL="${MORTAL_MODEL:-/tmp/qwen3-1.7b.mlm}"
MAXNEW="${MORTAL_DREAM_MAXNEW:-200}"
MAXTRIES="${MORTAL_MAXTRIES:-300000000}"
GENESIS="${MORTAL_GENESIS_FILE:-$ROOT/test/mortal-ledger/novels/nekomachi.txt}"
AUTOMINE="${MORTAL_AUTOMINE:-1}"
RPCPORT=28332

cli() { "$B/bitcoin-cli" -chain=mortaldev -datadir="$DD" "$@"; }

[ -f "$GENESIS" ] || { echo "genesis novel not found: $GENESIS (set MORTAL_GENESIS_FILE)"; exit 1; }
pkill -f "mortaldev-automine" 2>/dev/null
pkill -f "bitcoind -chain=mortaldev -datadir=$DD" 2>/dev/null; sleep 1
[ "${MORTAL_FRESH:-0}" = "1" ] && rm -rf "$DD"
mkdir -p "$DD"
GHASH="$(shasum -a256 "$GENESIS" | cut -d' ' -f1)"
MODELARG=(); [ -f "$MODEL" ] && MODELARG=(-mortalmodel="$MODEL")
echo "genesis: $GENESIS ($(wc -c <"$GENESIS" | tr -d ' ')B, sha ${GHASH:0:12}…);  voice: $([ -f "$MODEL" ] && echo "ON (cap $MAXNEW)" || echo off)"

# NB: ${arr[@]+"${arr[@]}"} — safe empty-array expansion under `set -u` on macOS bash 3.2.
"$B/bitcoind" -chain=mortaldev -datadir="$DD" -daemon ${MODELARG[@]+"${MODELARG[@]}"} -mortaldreammaxnew="$MAXNEW" \
  -mortalgenesisfile="$GENESIS" -mortalgenesishash="$GHASH" -fallbackfee=0.0001 >/dev/null
cli -rpcwait getblockchaininfo >/dev/null
# Ensure the 'ml' wallet is loaded: on a resume it exists on disk (load it); on a fresh chain it
# does not (create it); if it is already loaded both fail harmlessly. Then get an address from it
# EXPLICITLY (-rpcwallet), and refuse to start the miner with an empty address (the resume bug).
cli loadwallet ml >/dev/null 2>&1 || cli createwallet ml >/dev/null 2>&1 || true
ADDR="$(cli -rpcwallet=ml getnewaddress 2>/dev/null)"
if [ -z "$ADDR" ]; then echo "ERROR: no mining address — wallet 'ml' not usable on $DD"; exit 1; fi
echo "node up — chain mortaldev, height $(cli getblockcount), mining address $ADDR"

if [ "$AUTOMINE" = "1" ]; then
  # A continuous miner: one character per block, forever. Named so it can be stopped by pkill.
  nohup bash -c "exec -a mortaldev-automine sh -c 'while true; do \"$B/bitcoin-cli\" -chain=mortaldev -datadir=\"$DD\" generatetoaddress 1 \"$ADDR\" $MAXTRIES >/dev/null 2>&1 || sleep 2; done'" >/dev/null 2>&1 &
  echo "auto-mining ON (one character/block; ~40s each with the voice).  stop: pkill -f mortaldev-automine"
fi

COOKIE="$DD/mortaldev/.cookie"
cat <<EOF

── watch it ────────────────────────────────────────────────────────────────
Point the display at the devnet, then open it fullscreen:
  python3 $ROOT/test/mortal-ledger/display/server.py --rpcport $RPCPORT --rpccookie "$COOKIE"
  open -a "Google Chrome" --args --kiosk http://localhost:8888

Shorthand for this node:
  dv() { $B/bitcoin-cli -chain=mortaldev -datadir=$DD "\$@"; }
  dv getnovel           # the transcription (tail + the character under the pen)
  dv getmininghashes    # sampled hashes the miner is trying = the labour of Proof of Quotation
  dv getblockchaininfo  # chain=mortaldev, difficulty (LWMA)
  dv getpeerinfo        # P2P peers (wire a 2nd node with -addnode / -connect)
  pkill -f mortaldev-automine    # stop auto-mining
  dv stop                         # stop the node
────────────────────────────────────────────────────────────────────────────
EOF
