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
  # the words are the mined block's BIP39 mnemonic (hiragana for JA, or the English list):
  python3 test/mortal-ledger/chat_integer.py --model qwen3-1.7b.mlm --bin ./next_token \
      --dream "あいこくしん あいさつ あかちゃん あきる あける あさい あさひ あしあと"
  python3 test/mortal-ledger/chat_integer.py --model toy.mlm --bin ./next_token --raw
"""
import argparse
import subprocess
import sys
import time


def start_helper(bin_path, model, seed=""):
    argv = [bin_path, model] + ([seed] if seed else [])
    return subprocess.Popen(argv, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True, bufsize=1)


def req(p, ids):
    # Send a line (prompt ids, or empty to continue) and read one token. The helper keeps a
    # KV cache, so the prompt is processed once and each continuation is ~one position.
    p.stdin.write(" ".join(str(i) for i in ids) + "\n")
    p.stdin.flush()
    return int(p.stdout.readline().strip())


def generate(p, prompt_ids, eos_ids, max_new, on_token=None):
    gen = []
    nt = req(p, prompt_ids)            # prompt -> first token (cache built once)
    for _ in range(max_new):
        if nt < 0 or nt in eos_ids:
            break
        gen.append(nt)
        if on_token:
            on_token(nt)
        nt = req(p, [])                # empty -> continue from the cache
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
                    default="次の言葉をあなたに渡します。ここから想像して、日本語で短い夢の日記を書いてください",
                    help="framing instruction for --dream (the candidate OP_DREAM template)")
    ap.add_argument("--system", default="", help="optional system prompt")
    ap.add_argument("--think", action="store_true",
                    help="show the model's <think> reasoning (Qwen3 thinking mode; much longer/slower)")
    ap.add_argument("--tokenizer", default="Qwen/Qwen3-1.7B")
    ap.add_argument("--seed", default="", help="block seed (hex): same words + different seed -> different dream (DREAM only; JUDGE ignores it)")
    ap.add_argument("--raw", action="store_true", help="enter token ids directly; no tokenizer")
    args = ap.parse_args()

    p = start_helper(args.bin, args.model, args.seed)

    if args.raw:
        print("raw mode: enter token ids (space-separated), blank line to quit.")
        while True:
            try:
                line = input("ids> ").strip()
            except EOFError:
                break
            if not line:
                break
            print("next:", req(p, [int(x) for x in line.split()]))
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
            # hold a trailing incomplete multi-byte char (decodes to U+FFFD) until the next
            # token completes it, so the live stream never flashes a broken glyph.
            stable = full[:-1] if full.endswith("�") else full
            if len(stable) > len(shown):
                sys.stdout.write(stable[len(shown):])  # new complete suffix -> token-by-token stream
                sys.stdout.flush()
                shown = stable

        t0 = time.time()
        generate(p, ids, eos, args.max_new, on_token=show)
        dt = time.time() - t0
        full = tok.decode(gen, skip_special_tokens=True)
        if len(full) > len(shown):           # flush whatever was held back at the end
            sys.stdout.write(full[len(shown):])
            sys.stdout.flush()
        print()
        if gen:
            print(f"  [ {len(gen)} tokens in {dt:.1f}s | {len(gen) / dt:.2f} tok/s ]")
        return full

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
