B="${BINDIR:-$PWD/build/bin}"
DD=/tmp/btc/rtopsrc
# Mortal Ledger 0016 の実機デモ：後継小説を coinbase の OP_SOURCE 出力で登録する（旧 MLSR
# scriptSig=100バイト上限を撤廃）。OP_SOURCE が在るブロックだけ、その本文ぶん（<= MAX_NOVEL_BYTES
# =4MB）ブロックサイズ/重み上限を免除＝「新しい本が1ブロックでまるごと到着」し、その後 1バイト/
# ブロックで引用されていく。継ぎ目限定・1ブロック1個・PoW で、巨大ブロックはスパムできない。
CLI() { "$B/bitcoin-cli" -regtest -datadir=$DD "$@"; }
recover() { HEX=""; for h in $(seq $1 $2); do H=$(CLI getblockhash $h); HEX="$HEX${H: -2}"; done; python3 -c "import sys;sys.stdout.write(bytes.fromhex('$HEX').decode('utf-8','replace'))"; }
wait_count() { for i in $(seq 1 120); do [ "$(CLI getblockcount 2>/dev/null)" = "$1" ] && return 0; sleep 1; done; return 1; }

# a ~2MB successor novel (deterministic), and a 5MB one (over MAX_NOVEL_BYTES)
python3 -c "open('/tmp/btc/novel2mb.txt','wb').write((b'MortalLedger-'*200000)[:2000000])"
python3 -c "open('/tmp/btc/novel5mb.txt','wb').write(b'X'*5000000)"
GEN=$(python3 -c "print(len('旅への誘いが、次第に私の空想から消えて行つた。'.encode('utf-8')))")  # genesis 69B

pkill -f "bitcoind -regtest" 2>/dev/null; rm -rf $DD; mkdir -p $DD
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="旅への誘いが、次第に私の空想から消えて行つた。" -fallbackfee=0.0001 -mortalsuccessorfile=/tmp/btc/novel2mb.txt >/dev/null
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
CLI -rpcwait createwallet t >/dev/null; ADDR=$(CLI getnewaddress)

echo "=== 1. 創世小説($GEN B)を写し切り、継ぎ目で 2MB の後継を OP_SOURCE 登録 ==="
CLI generatetoaddress $GEN "$ADDR" >/dev/null
SEAM=$((GEN+1))
CLI generatetoaddress 1 "$ADDR" >/dev/null
W=$(CLI getblock "$(CLI getblockhash $SEAM)" | python3 -c "import sys,json;d=json.load(sys.stdin);print('size=%d weight=%d'%(d['size'],d['weight']))")
echo "seam block $SEAM: $W  (weight > 4,000,000 = 通常上限を超過 = OP_SOURCE 免除が効いている)"

echo "=== 2. その後 1バイト/ブロックで 2MB 本を引用していく（先頭6バイトを確認）==="
CLI generatetoaddress 5 "$ADDR" >/dev/null
REC=$(recover $SEAM $((SEAM+5)))
EXP=$(python3 -c "print(open('/tmp/btc/novel2mb.txt','rb').read()[:6].decode())")
echo "recovered: '$REC'  expect: '$EXP'  -> $([ "$REC" = "$EXP" ] && echo OK || echo MISMATCH)"

echo "=== 3. -reindex でも 2MB 継ぎ目ブロックは保持される（serialized 上限も連動引き上げ済み）==="
END=$(CLI getblockcount); CLI stop >/dev/null; sleep 2
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="旅への誘いが、次第に私の空想から消えて行つた。" -reindex >/dev/null
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
wait_count $END && echo "after -reindex count=$(CLI getblockcount) (= $END 保持)" || echo "reindex did not reach $END"
CLI stop >/dev/null; sleep 2

echo "=== 4. MAX_NOVEL_BYTES(4MB)超の後継は無効（分割して次の継ぎ目で続けるしかない）==="
DD=/tmp/btc/rtopsrc2; rm -rf $DD; mkdir -p $DD
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="旅への誘いが、次第に私の空想から消えて行つた。" -fallbackfee=0.0001 -mortalsuccessorfile=/tmp/btc/novel5mb.txt >/dev/null
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
CLI -rpcwait createwallet t >/dev/null; ADDR=$(CLI getnewaddress)
CLI generatetoaddress $GEN "$ADDR" >/dev/null
echo -n "5MB 後継で継ぎ目を掘る -> "; CLI generatetoaddress 1 "$ADDR" 2>&1 | grep -o "bad-canon-size[^\"]*" | head -1
echo "blockcount=$(CLI getblockcount) (= $GEN のまま = 4MB 超は拒否)"
CLI stop >/dev/null
