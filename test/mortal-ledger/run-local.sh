#!/usr/bin/env bash
# Run a local Mortal Ledger node (regtest) for hands-on play: the BAB currency, the novel
# transcription (Proof of Quotation), and — with the voice model — per-block dreams.
#
#   bash test/mortal-ledger/run-local.sh           # start (resumes existing chain)
#   MORTAL_FRESH=1 bash test/mortal-ledger/run-local.sh   # wipe and start a new chain
#
# Env knobs: MORTAL_DATADIR (default ~/mortal-regtest), MORTAL_MODEL (default /tmp/qwen3-1.7b.mlm),
#            MORTAL_DREAM_MAXNEW (default 16). The node is left RUNNING as a daemon.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
B="${BINDIR:-$ROOT/build/bin}"
DD="${MORTAL_DATADIR:-$HOME/mortal-regtest}"
MODEL="${MORTAL_MODEL:-/tmp/qwen3-1.7b.mlm}"
MAXNEW="${MORTAL_DREAM_MAXNEW:-200}"   # dream length cap; 200 ≈ full dream (~30-40s/block). Lower for faster mining.
# The genesis novel is supplied as a FILE (the realistic path; the whole book goes here, not a
# CLI string). Default 萩原朔太郎『猫町』, LF-normalized. Override with MORTAL_GENESIS_FILE — any
# book works: regtest's pinned genesis hash is overridden with the file's SHA-256 (-mortalgenesishash).
GENESIS="${MORTAL_GENESIS_FILE:-$ROOT/test/mortal-ledger/novels/nekomachi.txt}"
# Optional next novel(s) to transcribe once 猫町 is written through (empty = the chain dies at completion).
NEXT="${MORTAL_NEXTNOVEL_FILE:-}"

cli() { "$B/bitcoin-cli" -regtest -datadir="$DD" "$@"; }

[ -f "$GENESIS" ] || { echo "genesis novel file not found: $GENESIS (set MORTAL_GENESIS_FILE)"; exit 1; }
pkill -f "bitcoind -regtest -datadir=$DD" 2>/dev/null; sleep 1
[ "${MORTAL_FRESH:-0}" = "1" ] && rm -rf "$DD"
mkdir -p "$DD"

MODELARG=()
if [ -f "$MODEL" ]; then MODELARG=(-mortalmodel="$MODEL"); echo "voice: $MODEL (dreams ON, cap $MAXNEW tokens)"
else echo "voice: none at $MODEL (node runs; dreams OFF — set MORTAL_MODEL)"; fi
NEXTARG=(); [ -n "$NEXT" ] && NEXTARG=(-mortalnextnovelfile="$NEXT")
# Override regtest's pinned genesis hash with this file's SHA-256, so any book can be the genesis.
GHASH="$(shasum -a256 "$GENESIS" | cut -d' ' -f1)"
echo "genesis novel: $GENESIS ($(wc -c <"$GENESIS" | tr -d ' ') bytes, sha ${GHASH:0:12}…)"

# NB: ${arr[@]+"${arr[@]}"} — safe empty-array expansion under `set -u` on macOS bash 3.2.
"$B/bitcoind" -regtest -datadir="$DD" -daemon ${MODELARG[@]+"${MODELARG[@]}"} -mortaldreammaxnew="$MAXNEW" \
  -mortalgenesisfile="$GENESIS" -mortalgenesishash="$GHASH" ${NEXTARG[@]+"${NEXTARG[@]}"} -fallbackfee=0.0001 >/dev/null
cli -rpcwait getblockchaininfo >/dev/null

# A wallet to mine to (create on first run, load on resume).
cli createwallet ml >/dev/null 2>&1 || cli loadwallet ml >/dev/null 2>&1 || true
ADDR="$(cli getnewaddress)"
HEIGHT="$(cli getblockcount)"
echo "node up — regtest, unit BAB, height $HEIGHT, datadir $DD"
echo "mining address: $ADDR"

# Transcription is per-CHARACTER now: a 3-byte kanji needs ~16M grinds (Proof of Quotation), so
# generatetoaddress needs a high maxtries (its 1M default fails on multi-byte characters).
MAXTRIES="${MORTAL_MAXTRIES:-300000000}"
# Mine one block to show the chain is alive (and, if the model is loaded, a fresh dream).
echo "mining 1 block (one character; ~seconds of PoQ grind + the voice)…"
cli generatetoaddress 1 "$ADDR" "$MAXTRIES" >/dev/null
H="$(cli getblockcount)"
HASH="$(cli getblockhash "$H")"
echo "height now $H; balance $(cli getbalance) BAB"
if [ -f "$MODEL" ]; then
  # getblockdream returns the dream as readable UTF-8 text (the node detokenized it in C++).
  cli getblockdream "$HASH" | python3 -c "
import json,sys
d=json.load(sys.stdin)
print('reference words:', '、'.join(d['words']))
print('dream:', repr(d['text']))"
fi

cat <<EOF

── play with it ────────────────────────────────────────────────────────────
A shorthand for this node (paste into your shell):
  ml() { $B/bitcoin-cli -regtest -datadir=$DD "\$@"; }

  ml generatetoaddress 1 $ADDR $MAXTRIES   # mine one block = one character (needs high maxtries!)
  ml getblockcount                 # chain height
  ml getbalance                    # your BAB (1 BAB minted per byte transcribed)
  ml getblockhash <h>              # hash at height h
  ml getblockdream <hash>          # the dream inscribed in a block {words, text}  ← readable, no Python
  ml getblock <hash> 2             # full block (see the OP_SOURCE + OP_RETURN dream outputs)
  ml stop                          # shut the node down

Recover the transcribed novel from the chain head bytes (shows only COMPLETE characters; a
multi-byte char mid-write is reported as pending bytes, not mojibake):
  for h in \$(seq 1 \$(ml getblockcount)); do printf %s "\$(ml getblockhash \$h | tail -c 3)"; done \\
    | python3 -c "import sys;b=bytes.fromhex(sys.stdin.read());d=b.decode('utf-8','ignore');p=len(b)-len(d.encode());print(d + (f'  …(+{p}B 書字中)' if p else ''))"
────────────────────────────────────────────────────────────────────────────
EOF
