B="${BINDIR:-$PWD/build/bin}"
DD=/tmp/btc/rtfull
NOVEL="旅への誘いが、次第に私の空想から消えて行つた。"
LEN=$(python3 -c "print(len('$NOVEL'.encode('utf-8')))")
echo "novel: $NOVEL"
echo "byte length (= blocks to fully transcribe at k=1): $LEN"
pkill -f "bitcoind -regtest" 2>/dev/null
rm -rf $DD; mkdir -p $DD
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="旅への誘いが、次第に私の空想から消えて行つた。" -fallbackfee=0.0001
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
"$B/bitcoin-cli" -regtest -datadir=$DD -rpcwait createwallet t >/dev/null
ADDR=$("$B/bitcoin-cli" -regtest -datadir=$DD getnewaddress)
echo "mining $LEN blocks (transcribing the whole sentence)..."
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress $LEN "$ADDR" >/dev/null
HEX=""
for h in $(seq 1 $LEN); do
  HASH=$("$B/bitcoin-cli" -regtest -datadir=$DD getblockhash $h)
  HEX="$HEX${HASH: -2}"
done
echo -n "recovered from the chain: "
python3 -c "print(bytes.fromhex('$HEX').decode('utf-8'))"
echo "blockcount: $("$B/bitcoin-cli" -regtest -datadir=$DD getblockcount)"
echo "--- no successor magazine configured (-mortalsuccessor); mining past the end starves ---"
echo "--- (completion = death; load a successor to continue, see succession_demo.sh) ---"
"$B/bitcoin-cli" -regtest -datadir=$DD generatetoaddress 1 "$ADDR" 2>&1 | head -3
echo "blockcount after: $("$B/bitcoin-cli" -regtest -datadir=$DD getblockcount)  (unchanged = chain starved at completion)"
"$B/bitcoin-cli" -regtest -datadir=$DD stop >/dev/null
