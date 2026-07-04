B="${BINDIR:-$PWD/build/bin}"
DD=/tmp/btc/rtissue
A="Call me Ishmael. Some years ago, having little money, I went to sea."   # genesis canon: 萩原朔太郎『猫町』
BNOVEL="Call me Ishmael."                              # successor: Melville, Moby-Dick
LA=$(python3 -c "print(len('$A'.encode('utf-8')))")
LB=$(python3 -c "print(len('$BNOVEL'.encode('utf-8')))")
echo "canon A: $A  ($LA bytes)"
echo "succ  B: $BNOVEL  ($LB bytes)"
echo "rule: coinbase mints 1 BAB per byte transcribed (写字本位). supply should equal the letters written."
pkill -f "bitcoind -regtest" 2>/dev/null
rm -rf $DD; mkdir -p $DD
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="Call me Ishmael. Some years ago, having little money, I went to sea." -mortalnextnovel="Call me Ishmael." -fallbackfee=0.0001
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
"$B/bitcoin-cli" -regtest -datadir=$DD -rpcwait createwallet t >/dev/null
ADDR=$("$B/bitcoin-cli" -regtest -datadir=$DD getnewaddress)

supply () { "$B/bitcoin-cli" -regtest -datadir=$DD gettxoutsetinfo | python3 -c "import sys,json;print(json.load(sys.stdin)['total_amount'])"; }
cbval () { H=$("$B/bitcoin-cli" -regtest -datadir=$DD getblockhash $1); "$B/bitcoin-cli" -regtest -datadir=$DD getblock "$H" 2 | python3 -c "import sys,json;print(json.load(sys.stdin)['tx'][0]['vout'][0]['value'])"; }

echo "--- transcribe A ($LA blocks) ---"
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress $LA "$ADDR" >/dev/null
echo "per-block coinbase (block 1):   $(cbval 1) BAB   (= 1 byte written)"
echo "per-block coinbase (block $LA):  $(cbval $LA) BAB   (= 1 byte written)"
echo "total supply after A:           $(supply) BAB   (should be $LA = bytes of A; coinbases are 1 BAB each)"

echo "--- succeed into B ($LB blocks); the seam block ($((LA+1))) mints for the successor's first byte ---"
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress $LB "$ADDR" >/dev/null
echo "seam coinbase (block $((LA+1))):    $(cbval $((LA+1))) BAB   (= 1 byte of B written, not 0)"
echo "total supply after A+B:         $(supply) BAB   (should be $((LA+LB)) = bytes of A + bytes of B)"
echo "blockcount: $("$B/bitcoin-cli" -regtest -datadir=$DD getblockcount)"
echo "==> 発行＝写したバイト数. supply tracks the letters; no halving, no fixed reward; nothing minted past what is written."
"$B/bitcoin-cli" -regtest -datadir=$DD stop >/dev/null
