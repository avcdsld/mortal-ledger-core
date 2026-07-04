B="${BINDIR:-$PWD/build/bin}"
DD=/tmp/btc/rtdream
MODEL="${MORTAL_MODEL:-/tmp/qwen3-1.7b.mlm}"
MAXNEW="${MORTAL_DREAM_MAXNEW:-8}"
# The dream needs the real voice. Without the model (e.g. CI), skip cleanly — a model-less
# node still STORES inscribed dreams, but it cannot MINE one, so there is nothing to show.
if [ ! -f "$MODEL" ]; then
  echo "SKIP: voice model $MODEL not present (dream inscription needs the real Qwen)"; exit 0
fi
# The regtest genesis novel is pinned by consensus (its hash is in the chain params); the
# fork-height block must carry exactly this, same as the other demos.
NOVEL="Call me Ishmael. Some years ago, having little money, I went to sea."
pkill -f "bitcoind -regtest" 2>/dev/null
rm -rf $DD; mkdir -p $DD
echo "starting a regtest node with the voice (-mortalmodel), dream cap = $MAXNEW tokens..."
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="$NOVEL" \
  -mortalmodel="$MODEL" -mortaldreammaxnew=$MAXNEW -fallbackfee=0.0001 >/dev/null
for _i in $(seq 1 120); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
"$B/bitcoin-cli" -regtest -datadir=$DD -rpcwait createwallet t >/dev/null
ADDR=$("$B/bitcoin-cli" -regtest -datadir=$DD getnewaddress)
# A dream is inscribed once per 12-words-worth of transcription (a 16-byte boundary), not every
# block — so the ledger carves several characters fast, then pauses to dream. The ASCII novel is
# 1 byte/char, so the first 16-byte boundary is crossed by block 16: that block carries the dream
# (its 12 words + seed come from its parent hash). Blocks 1..15 mine fast with no dream.
echo "mining to the first 16-byte boundary (block 16 crosses it and dreams; the voice runs once"
echo "during that block's assembly, so it is slow)..."
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress 16 "$ADDR" >/dev/null
H=$("$B/bitcoin-cli" -regtest -datadir=$DD getblockhash 16)
echo "block 16 (the dream block) = $H"
# getblockdream returns the dream as readable UTF-8 text (the node detokenized it at mining
# time) — no tokenizer, no Python detok. Any node, model or not, reads the SAME stored text.
DREAM=$("$B/bitcoin-cli" -regtest -datadir=$DD getblockdream "$H")
echo "$DREAM"
echo "$DREAM" | python3 -c "
import json,sys
d=json.load(sys.stdin)
print('dream present:', d['present'])
print('reference words:', '、'.join(d['words']))
print('inscribed dream:', repr(d['text']))
"
"$B/bitcoin-cli" -regtest -datadir=$DD stop >/dev/null
