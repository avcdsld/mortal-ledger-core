B="${BINDIR:-$PWD/build/bin}"
DD=/tmp/btc/rtlwma
# Mortal Ledger 発火統合(patch 0013)の実機デモ：難易度の LWMA リターゲットは、フォーク高
# H（regtest は nMortalLedgerHeight=1）＋フォーク後 N=60 ブロックの窓が満ちて初めて発火する。
# 継ぎ目を跨いで相続 Bitcoin の間隔を LWMA 窓へ流し込まないための窓ガード。よって H=1 では
# tip 高さ 61（= H+N）で発火＝block 62 から LWMA。Pi 不要：T=60 の 60 倍速（1 秒間隔）で
# 採掘し、発火後に難易度が締まる（target が下がる＝nBits が最小難度から動く）のを観測する。
N=60
CLI() { "$B/bitcoin-cli" -regtest -datadir=$DD "$@"; }
bits() { CLI getblockheader "$(CLI getblockhash $1)" | python3 -c "import sys,json; d=json.load(sys.stdin); print('bits=%s  difficulty=%.3e' % (d['bits'], d['difficulty']))"; }

pkill -f "bitcoind -regtest" 2>/dev/null
rm -rf $DD; mkdir -p $DD
# load a successor so mining can continue past the 69-byte genesis novel (seam at block 70)
SUCC="Call me Ishmael. Some years ago, never mind how long precisely, having little or no money."
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="Call me Ishmael. Some years ago, having little money, I went to sea." -fallbackfee=0.0001 -mortalnextnovel="$SUCC" >/dev/null
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
CLI -rpcwait createwallet t >/dev/null
ADDR=$(CLI getnewaddress)

echo "regtest fork height H=1, LWMA window N=$N  ->  LWMA fires at block $((1+N+1))"
echo "mining 130 blocks at 1s spacing (60x faster than T=60) via mocktime..."
T0=1700000000
for h in $(seq 1 130); do CLI setmocktime $((T0+h)) >/dev/null; CLI generatetoaddress 1 "$ADDR" >/dev/null; done
echo "blockcount: $(CLI getblockcount)"
echo
echo "height   nBits / difficulty"
for h in 10 40 60 61 62 70 100 130; do printf "h=%-4s " $h; bits $h; done
echo
echo "=> blocks 1..61 hold 207fffff (regtest min difficulty); block 62 fires LWMA"
echo "   (difficulty jumps, then keeps rising as LWMA tightens the 60x-too-fast blocks)."
echo "   The algorithm itself is proven in lwma_test.cpp; the firing height in lwmafire_test.cpp."
CLI stop >/dev/null
