#!/usr/bin/env python3
"""Generate src/mortal_dream_prompt.h — the pinned token-id pieces the node concatenates to
build a block's OP_DREAM prompt WITHOUT a tokenizer.

The node never tokenizes. A block's dream prompt is, by definition, this concatenation:

    PROMPT = PREFIX  ++  WORD[w0] ++ SEP ++ WORD[w1] ++ SEP ++ ... ++ WORD[w11]  ++ SUFFIX

where w0..w11 are the 12 BIP39-Japanese word indices read from the parent block hash
(see src/node/mortaldream.cpp), and every fixed piece below is pinned here as token ids:

  PREFIX  the Qwen3 chat template head + the framing instruction (MORTAL_DREAM_INSTRUCTION,
          ending in the ASCII ":"), tokenized together in context.
  WORD[i] the i-th BIP39-Japanese word, tokenized in isolation (index-aligned with
          src/script/bip39_wordlists.h — the SAME 2048 words the node holds).
  SEP     the ideographic comma "、" that joins the reference words.
  SUFFIX  the chat template tail (enable_thinking=False, add_generation_prompt=True).
  EOS     the ids that end a dream (<|im_end|>, <|endoftext|>, eos_token_id).

This is NOT required to equal tokenizer.apply_chat_template of the assembled string: the
dream is non-consensus and the display detokenizes the *inscribed* ids directly, so the
node's concatenation is itself the canonical definition. Pinning the pieces (rather than a
runtime tokenizer) is what lets a model-less, tokenizer-less node still hold every dream.

Deterministic: same tokenizer + same wordlist -> byte-identical header. Anyone can re-derive
and diff it, exactly like MORTAL_CANONICAL_MLM_SHA256.

Usage:
  pip install transformers
  python3 test/mortal-ledger/gen_dream_prompt.py            # writes src/mortal_dream_prompt.h
  python3 test/mortal-ledger/gen_dream_prompt.py --check     # re-derive and diff (CI)
"""
import argparse
import os
import re
import sys

# The framing instruction, pinned. The 12 reference words follow the trailing ":". Chosen from
# a sweep (test/mortal-ledger/sweep_prompts.sh): a scene-description framing that reliably drops
# the model's chatty preamble and yields dreamlike imagery rather than a word list.
MORTAL_DREAM_INSTRUCTION = "夢で見た情景を数行で描写して。タイトルや前置きは無く本文のみ。手がかりの言葉:"
SEP_TEXT = "、"  # U+3001 ideographic comma, joins the reference words
TOKENIZER = "Qwen/Qwen3-1.7B"

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORDLIST_H = os.path.join(ROOT, "src", "script", "bip39_wordlists.h")
OUT_H = os.path.join(ROOT, "src", "mortal_dream_prompt.h")


def load_ja_words():
    """The 2048 BIP39 Japanese words, in index order, parsed from the node's pinned header
    so the table is guaranteed index-aligned with OP_MNEMONIC / the dream word selection."""
    src = open(WORDLIST_H, encoding="utf-8").read()
    m = re.search(r"BIP39_WORDS_JA\[2048\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("could not find BIP39_WORDS_JA in " + WORDLIST_H)
    words = re.findall(r'"([^"]+)"', m.group(1))
    if len(words) != 2048:
        sys.exit(f"expected 2048 JA words, parsed {len(words)}")
    return words


def fmt_ints(name, ids, per_line=12):
    out = [f"inline constexpr int {name}[] = {{"]
    for i in range(0, len(ids), per_line):
        out.append("    " + ",".join(str(x) for x in ids[i:i + per_line]) + ",")
    out.append("};")
    return "\n".join(out)


def build_header():
    from transformers import AutoTokenizer
    tok = AutoTokenizer.from_pretrained(TOKENIZER)

    # PREFIX / SUFFIX: render the chat template as a STRING around a sentinel content, split on
    # the sentinel, then tokenize the pieces. Splitting on the string (not on token streams)
    # avoids byte-fallback leakage at the boundary. PREFIX folds the framing instruction in,
    # tokenized in context with the template head; SUFFIX is the template tail.
    SENTINEL = "MORTALCONTENT"
    rendered = tok.apply_chat_template(
        [{"role": "user", "content": SENTINEL}],
        add_generation_prompt=True, enable_thinking=False, tokenize=False)
    if rendered.count(SENTINEL) != 1:
        sys.exit("template split failed: sentinel not found exactly once")
    head_str, tail_str = rendered.split(SENTINEL)

    PREFIX = tok.encode(head_str + MORTAL_DREAM_INSTRUCTION, add_special_tokens=False)
    SUFFIX = tok.encode(tail_str, add_special_tokens=False)
    if not PREFIX or not SUFFIX:
        sys.exit("template split failed: empty prefix or suffix")
    # The template head opens with <|im_start|>; confirm it round-tripped to the special id.
    if PREFIX[0] != tok.convert_tokens_to_ids("<|im_start|>"):
        sys.exit("template head did not retokenize to <|im_start|>")

    SEP = tok.encode(SEP_TEXT, add_special_tokens=False)

    words = load_ja_words()
    flat, off = [], [0]
    for w in words:
        ids = tok.encode(w, add_special_tokens=False)
        if not ids:
            sys.exit(f"word {w!r} tokenized to nothing")
        flat.extend(ids)
        off.append(len(flat))

    eos = []
    for t in (tok.eos_token_id,
              tok.convert_tokens_to_ids("<|im_end|>"),
              tok.convert_tokens_to_ids("<|endoftext|>")):
        if isinstance(t, int) and t >= 0 and t not in eos:
            eos.append(t)

    H = []
    H.append("// AUTO-GENERATED by test/mortal-ledger/gen_dream_prompt.py. Do not edit by hand.")
    H.append("// Mortal Ledger dream inscription: the pinned token-id pieces the node")
    H.append("// concatenates to build a block's OP_DREAM prompt, so a tokenizer-less node can")
    H.append("// still produce (and verify) the dream. See gen_dream_prompt.py for the scheme.")
    H.append(f"// tokenizer = {TOKENIZER}")
    H.append(f"// instruction = {MORTAL_DREAM_INSTRUCTION!r}")
    H.append(f"// separator = {SEP_TEXT!r} (U+3001)")
    H.append("#ifndef BITCOIN_MORTAL_DREAM_PROMPT_H")
    H.append("#define BITCOIN_MORTAL_DREAM_PROMPT_H")
    H.append("")
    H.append("namespace mortal_dream {")
    H.append("")
    H.append("// Chat-template head + framing instruction (ends at the ASCII ':'), in context.")
    H.append(fmt_ints("PROMPT_PREFIX", PREFIX))
    H.append("")
    H.append("// Chat-template tail (enable_thinking=False, add_generation_prompt=True).")
    H.append(fmt_ints("PROMPT_SUFFIX", SUFFIX))
    H.append("")
    H.append("// The ideographic comma that joins the reference words.")
    H.append(fmt_ints("SEP", SEP))
    H.append("")
    H.append("// Ids that end a dream: <|im_end|>, <|endoftext|>, eos_token_id.")
    H.append(fmt_ints("EOS", eos))
    H.append("")
    H.append("// BIP39-Japanese words tokenized in isolation, index-aligned with")
    H.append("// src/script/bip39_wordlists.h. Word i = WORD_TOK_FLAT[WORD_TOK_OFF[i] .. OFF[i+1]).")
    H.append("inline constexpr int N_WORDS = 2048;")
    H.append(fmt_ints("WORD_TOK_FLAT", flat))
    H.append("")
    H.append(fmt_ints("WORD_TOK_OFF", off))
    H.append("")
    H.append("} // namespace mortal_dream")
    H.append("")
    H.append("#endif // BITCOIN_MORTAL_DREAM_PROMPT_H")
    return "\n".join(H) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="re-derive and diff against the committed header")
    ap.add_argument("--out", default=OUT_H)
    args = ap.parse_args()

    header = build_header()
    if args.check:
        have = open(args.out, encoding="utf-8").read() if os.path.exists(args.out) else ""
        if have != header:
            sys.exit(f"{args.out} is stale; re-run gen_dream_prompt.py")
        print(f"{args.out}: up to date")
        return
    open(args.out, "w", encoding="utf-8").write(header)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
