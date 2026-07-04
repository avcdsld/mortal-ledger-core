B="${BINDIR:-$PWD/build/bin}"
DD=/tmp/btc/rtsucc
A="Call me Ishmael. Some years ago, having little money, I went to sea."   # genesis canon: 萩原朔太郎『猫町』
BNOVEL="Call me Ishmael."                              # successor the miner registers: Melville, Moby-Dick
LA=$(python3 -c "print(len('$A'.encode('utf-8')))")
LB=$(python3 -c "print(len('$BNOVEL'.encode('utf-8')))")
echo "genesis canon A: $A  ($LA bytes)"
echo "successor    B: $BNOVEL  ($LB bytes)  [loaded into the node successor magazine via -mortalnextnovel]"
pkill -f "bitcoind -regtest" 2>/dev/null
rm -rf $DD; mkdir -p $DD
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="Call me Ishmael. Some years ago, having little money, I went to sea." -fallbackfee=0.0001 -mortalnextnovel="$BNOVEL" -mortalnextnovel="And so the writing goes on."
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
"$B/bitcoin-cli" -regtest -datadir=$DD -rpcwait createwallet t >/dev/null
ADDR=$("$B/bitcoin-cli" -regtest -datadir=$DD getnewaddress)

echo "--- transcribe A ($LA blocks) ---"
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress $LA "$ADDR" >/dev/null
echo "--- A exhausted; mine on: the seam block registers a successor, transcription continues into B ($LB blocks) ---"
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress $LB "$ADDR" >/dev/null

HEXA=""
for h in $(seq 1 $LA); do
  HASH=$("$B/bitcoin-cli" -regtest -datadir=$DD getblockhash $h)
  HEXA="$HEXA${HASH: -2}"
done
HEXB=""
for h in $(seq $((LA+1)) $((LA+LB))); do
  HASH=$("$B/bitcoin-cli" -regtest -datadir=$DD getblockhash $h)
  HEXB="$HEXB${HASH: -2}"
done
echo -n "recovered A (blocks 1..$LA):        "
python3 -c "print(bytes.fromhex('$HEXA').decode('utf-8'))"
echo -n "recovered B (blocks $((LA+1))..$((LA+LB))):      "
python3 -c "print(bytes.fromhex('$HEXB').decode('utf-8'))"

echo "--- the seam block ($((LA+1))) carries the OP_SOURCE registration in its coinbase ---"
SEAM=$("$B/bitcoin-cli" -regtest -datadir=$DD getblockhash $((LA+1)))
CB=$("$B/bitcoin-cli" -regtest -datadir=$DD getblock "$SEAM" 2 | python3 -c "import sys,json; print(json.load(sys.stdin)['tx'][0]['vin'][0]['coinbase'])")
echo "coinbase scriptSig (hex): $CB"
python3 -c "
s=bytes.fromhex('$CB'); i=s.find(b'MLSR')
print('registered successor:', s[i+4:].decode('utf-8') if i>=0 else '(none)')"

echo "blockcount: $("$B/bitcoin-cli" -regtest -datadir=$DD getblockcount)"
echo "--- mine past B's end: with succession the chain no longer dies at completion, ---"
echo "--- the seam miner names the next novel and transcription continues ---"
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress 1 "$ADDR" >/dev/null
echo "blockcount after: $("$B/bitcoin-cli" -regtest -datadir=$DD getblockcount)  (advanced = succession kept the chain alive; without a registered successor it would starve, as in poq_demo.sh)"
"$B/bitcoin-cli" -regtest -datadir=$DD stop >/dev/null
