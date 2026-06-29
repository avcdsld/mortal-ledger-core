#!/usr/bin/env python3
"""Chat through the node's *exact* voice — the integer fixed-point forward, temperature 0
(greedy), one token at a time via test/mortal-ledger/next_token (which calls the node's
OP_DREAM path). The Qwen3 tokenizer (text<->tokens + chat template) lives here; the node
itself only deals in token ids. This is the real consensus voice, not a fast proxy.

It is SLOW on the real model (no KV cache, naive kernels: tens of seconds to minutes per
token). That slowness is the mortal machine's real speed — use short prompts.

Setup:
  pip install transformers          # tokenizer only; no torch needed
  # build the helper (from the Core root, after building bitcoind):
  g++ -std=c++20 -I src -I build/src test/mortal-ledger/next_token.cpp src/mortalllm.cpp \
      build/lib/libbitcoin_common.a build/lib/libbitcoin_consensus.a build/lib/libbitcoin_util.a \
      build/lib/libbitcoin_crypto.a build/src/univalue/libunivalue.a \
      build/src/secp256k1/lib/libsecp256k1.a -o next_token

Use:
  python3 test/mortal-ledger/chat_integer.py --model qwen3-1.7b.mlm --bin ./next_token
  python3 test/mortal-ledger/chat_integer.py --model qwen3-1.7b.mlm --bin ./next_token \
      --dream "猫 町 月 影 路地 風 灯 夢 二重 街路 既視 帰路"
  python3 test/mortal-ledger/chat_integer.py --model toy.mlm --bin ./next_token --raw
"""
import argparse
import subprocess
import sys


def start_helper(bin_path, model):
    return subprocess.Popen([bin_path, model], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True, bufsize=1)


def next_token(p, ids):
    p.stdin.write(" ".join(str(i) for i in ids) + "\n")
    p.stdin.flush()
    return int(p.stdout.readline().strip())


def generate(p, ids, eos_ids, max_new, on_token=None):
    gen = []
    for _ in range(max_new):
        nt = next_token(p, ids)
        if nt < 0 or nt in eos_ids:
            break
        ids.append(nt)
        gen.append(nt)
        if on_token:
            on_token(nt)
    return gen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="path to the .mlm the node loads")
    ap.add_argument("--bin", default="./next_token", help="path to the built next_token helper")
    ap.add_argument("--max-new", type=int, default=64)
    ap.add_argument("--dream", help="space-separated words (e.g. the mined 12) to dream from")
    # The framing instruction for --dream. This is the candidate OP_DREAM template — the
    # text that would be pinned into consensus once chosen. Experiment with it here.
    ap.add_argument("--instruction",
                    default="次の言葉をあなたに渡します。ここから想像して、夢の日記を書いてください",
                    help="framing instruction for --dream (the candidate OP_DREAM template)")
    ap.add_argument("--system", default="", help="optional system prompt")
    ap.add_argument("--think", action="store_true",
                    help="show the model's <think> reasoning (Qwen3 thinking mode; much longer/slower)")
    ap.add_argument("--tokenizer", default="Qwen/Qwen3-1.7B")
    ap.add_argument("--raw", action="store_true", help="enter token ids directly; no tokenizer")
    args = ap.parse_args()

    p = start_helper(args.bin, args.model)

    if args.raw:
        print("raw mode: enter token ids (space-separated), blank line to quit.")
        while True:
            try:
                line = input("ids> ").strip()
            except EOFError:
                break
            if not line:
                break
            print("next:", next_token(p, [int(x) for x in line.split()]))
        return

    from transformers import AutoTokenizer
    tok = AutoTokenizer.from_pretrained(args.tokenizer)
    eos = {t for t in (tok.eos_token_id, tok.convert_tokens_to_ids("<|im_end|>"))
           if isinstance(t, int) and t >= 0}

    def apply_template(messages):
        try:
            return tok.apply_chat_template(messages, add_generation_prompt=True,
                                           enable_thinking=args.think, tokenize=True)
        except TypeError:
            return tok.apply_chat_template(messages, add_generation_prompt=True, tokenize=True)

    def run_chat(messages):
        ids = list(apply_template(messages))
        print("  (generating — integer forward, temp 0, slow)", flush=True)
        gen = []
        shown = ""  # text already emitted, for clean append-streaming of multi-byte chars

        def show(nt):
            nonlocal shown
            gen.append(nt)
            full = tok.decode(gen, skip_special_tokens=True)  # <think>..</think> is plain text, still shown
            sys.stdout.write(full[len(shown):])  # only the new suffix -> token-by-token stream
            sys.stdout.flush()
            shown = full

        generate(p, ids, eos, args.max_new, on_token=show)
        print()
        return tok.decode(gen, skip_special_tokens=True)

    if args.dream:
        words = args.dream.split()
        prompt = args.instruction + "：" + "、".join(words)
        run_chat(([{"role": "system", "content": args.system}] if args.system else [])
                 + [{"role": "user", "content": prompt}])
        return

    print("chat through the node's integer voice (temp 0). blank line to quit.")
    history = [{"role": "system", "content": args.system}] if args.system else []
    while True:
        try:
            user = input("\nあなた> ").strip()
        except EOFError:
            break
        if not user:
            break
        history.append({"role": "user", "content": user})
        reply = run_chat(history)
        history.append({"role": "assistant", "content": reply})


if __name__ == "__main__":
    main()
