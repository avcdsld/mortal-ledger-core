#!/usr/bin/env bash
# Try a candidate dream INSTRUCTION (the text that gets pinned into the node's dream prompt)
# and see the dream it produces — fast iteration, no mining. Uses the node's exact integer
# voice (chat_integer.py + next_token). Once you like one, tell me the instruction and I'll
# re-pin it: set MORTAL_DREAM_INSTRUCTION in gen_dream_prompt.py and regenerate the header.
#
#   bash test/mortal-ledger/try_prompt.sh "夢日記を日本語で書いて. 参考にしてほしい言葉:"
#   bash test/mortal-ledger/try_prompt.sh "前置き無しで夢日記の本文だけ書いて. 言葉:" \
#        --system "説明・前置き・見出し・記号は書かない。本文のみ。"
#
# Read the output: if the token count is < --max-new it hit EOS (= the dream COMPLETED, not cut
# off). Override the fixtures with env vars: MORTAL_WORDS, MORTAL_SEED, MORTAL_MAXNEW.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODEL="${MORTAL_MODEL:-/tmp/qwen3-1.7b.mlm}"
BIN=/tmp/next_token
WORDS="${MORTAL_WORDS:-ちらみ たおす むかし しはん さくら ねんきん べんり たべる うやまう りろん あまやかす かんけい}"
SEED="${MORTAL_SEED:-3}"
MAXNEW="${MORTAL_MAXNEW:-160}"

[ $# -ge 1 ] || { echo "usage: try_prompt.sh \"<instruction ending with :>\" [--system \"...\"]"; exit 1; }
INSTR="$1"; shift

# Build the slow generator once (rebuild if the voice source changed).
LIBS=("$ROOT"/build/lib/libbitcoin_common.a "$ROOT"/build/lib/libbitcoin_consensus.a \
      "$ROOT"/build/lib/libbitcoin_util.a "$ROOT"/build/lib/libbitcoin_crypto.a \
      "$ROOT"/build/src/univalue/libunivalue.a "$ROOT"/build/src/secp256k1/lib/libsecp256k1.a)
if [ ! -x "$BIN" ] || [ "$ROOT/src/mortalllm.cpp" -nt "$BIN" ]; then
  echo "(building next_token…)"
  g++ -std=c++20 -I "$ROOT/src" -I "$ROOT/build/src" \
      "$ROOT/test/mortal-ledger/next_token.cpp" "$ROOT/src/mortalllm.cpp" "${LIBS[@]}" -o "$BIN" || exit 1
fi

echo "instruction: $INSTR"
echo "words      : $WORDS"
echo "seed=$SEED  max-new=$MAXNEW   (slow: the real integer voice)"
python3 "$ROOT/test/mortal-ledger/chat_integer.py" --model "$MODEL" --bin "$BIN" \
  --seed "$SEED" --max-new "$MAXNEW" --dream "$WORDS" --instruction "$INSTR" "$@"
