#!/usr/bin/env bash
# Sweep dream INSTRUCTION candidates over word sets x seeds, running the node's exact integer
# voice for each, and SAVE every dream so you can compare and pick one to pin.
#
#   bash test/mortal-ledger/sweep_prompts.sh                 # ALL: 8 candidates x 3 wordsets x seeds 0-4
#   MORTAL_ONLY=C_bodyonly bash .../sweep_prompts.sh          # only candidates whose label matches
#   MORTAL_WSONLY=w1 MORTAL_SEEDS="0 1" bash .../sweep_prompts.sh   # one wordset, two seeds (quick)
#
# Each run is the real integer voice (~40-90s). The FULL grid is 8*3*5 = 120 runs ≈ 2 h — run it
# in the background ( append ' &' ) or overnight. Results are written INCREMENTALLY to
# $MORTAL_OUTDIR (default /tmp/dream-sweep), so a partial run is still usable:
#   summary.txt        one line per run: label / wordset / seed / tokens / time / preamble / preview
#   <label>.txt        the full dreams for that candidate
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODEL="${MORTAL_MODEL:-/tmp/qwen3-1.7b.mlm}"
BIN=/tmp/next_token
MAXNEW="${MORTAL_MAXNEW:-200}"
SEEDS="${MORTAL_SEEDS:-0 1 2 3 4}"
OUTDIR="${MORTAL_OUTDIR:-/tmp/dream-sweep}"
ONLY="${MORTAL_ONLY:-}"       # substring filter on candidate label (empty = all)
WSONLY="${MORTAL_WSONLY:-}"   # substring filter on wordset label   (empty = all)

# label|instruction. Instructions never contain '|'. End each with a colon so the words follow
# naturally (the node assembles: instruction + "、".join(words)).
CANDIDATES=(
"A_current|夢日記を日本語で書いて. 参考にしてほしい言葉:"
"B_fewlines|夢日記を数行で書いて。タイトル不要。本文のみ。次の言葉 から1つの日記を連想して:"
"C_bodyonly|前置き無しで夢日記の本文だけ書いて。説明・前置き・見出し・記号は書かない。本文のみ。言葉:"
"D_terse|夢の本文だけ。前置き禁止。言葉:"
"E_firstperson|ゆうべ見た夢を一人称で短く綴って。前置きや見出しは無し。夢に出た言葉:"
"F_prosepoem|前置きをせず、次の言葉から短い散文詩のような夢の記録を一段落で書いて。言葉:"
"G_scene|夢で見た情景を数行で描写して。タイトルや前置きは無く本文のみ。手がかりの言葉:"
"H_weaveall|次の言葉をすべて織り込んだ短い夢日記を、前置き無しで本文だけ書いて。言葉:"
)

# label|space-separated 12 BIP39-Japanese words.
WORDSETS=(
"w1|くうぐん よねつ いふく ずぶぬれ みかた しはい おたがい てんし もどる こまつな びじゅつかん たべる"
"w2|てちがい はちみつ てつがく うつくしい ていこく めいきょく たんさん あてな ひそか ふそく なのか へいがい"
"w3|せんむ ちこく だいすき こまつな てほん こうこう どんぶり みほん じゆう わすれもの まぬけ はあく"
)

# Build the slow generator once (rebuild if the voice source changed).
LIBS=("$ROOT"/build/lib/libbitcoin_common.a "$ROOT"/build/lib/libbitcoin_consensus.a \
      "$ROOT"/build/lib/libbitcoin_util.a "$ROOT"/build/lib/libbitcoin_crypto.a \
      "$ROOT"/build/src/univalue/libunivalue.a "$ROOT"/build/src/secp256k1/lib/libsecp256k1.a)
if [ ! -x "$BIN" ] || [ "$ROOT/src/mortalllm.cpp" -nt "$BIN" ]; then
  echo "(building next_token…)" >&2
  g++ -std=c++20 -I "$ROOT/src" -I "$ROOT/build/src" \
      "$ROOT/test/mortal-ledger/next_token.cpp" "$ROOT/src/mortalllm.cpp" "${LIBS[@]}" -o "$BIN" || exit 1
fi

mkdir -p "$OUTDIR"
SUMMARY="$OUTDIR/summary.txt"
{ echo "# dream prompt sweep"; echo "# seeds: $SEEDS   max-new: $MAXNEW"; for w in "${WORDSETS[@]}"; do echo "# ${w%%|*}: ${w#*|}"; done; echo; } > "$SUMMARY"

for entry in "${CANDIDATES[@]}"; do
  label="${entry%%|*}"; instr="${entry#*|}"
  [ -n "$ONLY" ] && [[ "$label" != *"$ONLY"* ]] && continue
  cfile="$OUTDIR/$label.txt"
  { echo "=== $label ==="; echo "instruction: $instr"; echo; } > "$cfile"
  { echo "## $label"; echo "   $instr"; } >> "$SUMMARY"
  for wentry in "${WORDSETS[@]}"; do
    wlabel="${wentry%%|*}"; words="${wentry#*|}"
    [ -n "$WSONLY" ] && [[ "$wlabel" != *"$WSONLY"* ]] && continue
    for s in $SEEDS; do
      echo "[$label/$wlabel] seed=$s …" >&2
      out="$(python3 "$ROOT/test/mortal-ledger/chat_integer.py" --model "$MODEL" --bin "$BIN" \
              --seed "$s" --max-new "$MAXNEW" --dream "$words" --instruction "$instr" 2>/dev/null)"
      metric="$(grep -F '[ prompt' <<<"$out" | sed 's/^ *//')"
      dream="$(grep -vE '^[[:space:]]*\(|^[[:space:]]*\[ prompt' <<<"$out" | sed '/^[[:space:]]*$/d' | tr '\n' ' ')"
      pre="no "; case "$dream" in もちろん*|はい*|以下*|分かりました*|了解*|"これは"*) pre="YES" ;; esac
      { echo "--- $wlabel seed $s ---"; echo "$metric"; echo "$dream"; echo; } >> "$cfile"
      printf '   %s seed %s | %s | preamble:%s\n      %s\n' "$wlabel" "$s" "$metric" "$pre" "${dream:0:140}" >> "$SUMMARY"
    done
  done
  echo >> "$SUMMARY"
done
echo "done → $OUTDIR" >&2
echo "compare:  less $SUMMARY      (full dreams: $OUTDIR/<label>.txt)" >&2
