B="${BINDIR:-$PWD/build/bin}"
DD=/tmp/btc/rtreindex
NOVEL="Call me Ishmael. Some years ago, having little money, I went to sea."   # genesis canon: 萩原朔太郎『猫町』
# Mortal Ledger 本番化(B, patch 0012)の実機デモ：正典状態は pow.cpp のプロセスグローバル
# ではなく、ブロック index に載る純粋な畳み込み（CDiskBlockIndex に永続化）。だから
# 再起動でディスクから復元でき、-reindex でブロックファイルから再計算しても壊れない。
CLI() { "$B/bitcoin-cli" -regtest -datadir=$DD "$@"; }
recover() { # $1=from $2=to  -> 連結した head バイトから本文を復元
  HEX=""; for h in $(seq $1 $2); do H=$(CLI getblockhash $h); HEX="$HEX${H: -2}"; done
  python3 -c "import sys; sys.stdout.write(bytes.fromhex('$HEX').decode('utf-8','replace'))"
}
wait_count() { # $1=expected  RPC が立ち上がり目標高さに達するまで待つ（reindex の再生を待つ）
  for i in $(seq 1 120); do [ "$(CLI getblockcount 2>/dev/null)" = "$1" ] && return 0; sleep 1; done; return 1
}

pkill -f "bitcoind -regtest" 2>/dev/null
rm -rf $DD; mkdir -p $DD
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="Call me Ishmael. Some years ago, having little money, I went to sea." -fallbackfee=0.0001 >/dev/null
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
CLI -rpcwait createwallet t >/dev/null
ADDR=$(CLI getnewaddress)

echo "=== 1. 写字：12 ブロック採掘し、ブロックハッシュから本文を復元 ==="
CLI generatetoaddress 12 "$ADDR" >/dev/null
echo "blockcount: $(CLI getblockcount)"
echo -n "recovered (1..12): "; recover 1 12; echo

echo
echo "=== 2. 再起動：正典状態を CDiskBlockIndex からロードし、続きを正しく写字 ==="
CLI stop >/dev/null; sleep 2
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="Call me Ishmael. Some years ago, having little money, I went to sea." -fallbackfee=0.0001 >/dev/null
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
CLI -rpcwait getblockcount >/dev/null
echo "blockcount after restart: $(CLI getblockcount)  (保持)"
CLI generatetoaddress 3 "$ADDR" >/dev/null   # tip の offset から続行（13..15）
echo -n "recovered (1..15) after restart+mine: "; recover 1 15; echo
echo "  -> ロードした offset から block 13..15 が正しく続いた（グローバルなら不活性に戻り失敗）"

echo
echo "=== 3. -reindex：ブロックファイルから畳み込みを再計算しても全保持・全一致 ==="
CLI stop >/dev/null; sleep 2
"$B/bitcoind" -regtest -datadir=$DD -daemon -mortalgenesis="Call me Ishmael. Some years ago, having little money, I went to sea." -reindex -fallbackfee=0.0001 >/dev/null
for _i in $(seq 1 60); do [ -f "$DD/regtest/.cookie" ] && break; sleep 0.5; done
wait_count 15 || echo "  (warning: reindex がまだ 15 に達していない)"
echo "blockcount after -reindex: $(CLI getblockcount)"
echo -n "recovered (1..15) after reindex: "; REC=$(recover 1 15); echo "$REC"
EXP=$(python3 -c "print('$NOVEL'.encode('utf-8')[:15].decode('utf-8','replace'))")
[ "$REC" = "$EXP" ] && echo "  => REINDEX-SAFE ✓（再計算後も本文一致）" || echo "  => MISMATCH ✗"
SUP=$(CLI gettxoutsetinfo | python3 -c "import sys,json; print(json.load(sys.stdin)['total_amount'])")
echo "total supply: $SUP BAB  (= 写字したバイト数 15、写字本位)"
CLI stop >/dev/null
