#!/usr/bin/env python3
"""Mortal Ledger — exhibition display ("the window").

A tiny, dependency-free server that watches a Mortal Ledger node over RPC and shows, fullscreen
in a browser, three things in real time:
  1. the novel transcribed so far (only the tail that fits on screen),
  2. the character currently being transcribed,
  3. the latest dream.
It needs NO model and NO tokenizer — the node returns the dream as text (getblockdream) and the
transcription state as a bounded window (getnovel). Meant to run on a SEPARATE machine from the
(mortal) node: a window that looks in over the network.

  python3 server.py                                   # local regtest node (datadir cookie)
  python3 server.py --rpcconnect 192.168.1.20 --rpcport 8332 --rpcuser u --rpcpassword p
  python3 server.py --demo                            # no node — preview the page with a fixture

Open http://localhost:8888 and go fullscreen. Updates arrive the instant a block is mined
(waitfornewblock), so the room stays still between blocks.
"""
import argparse
import base64
import json
import os
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
TAIL_CHARS = 320  # how much of the novel tail to fetch (the page shows the last NOVEL_TAIL of it)

# Shared state the page reads from /state. Updated by the watcher thread (or fixed in --demo).
STATE = {
    "height": 0,
    "novel": {"active": False, "index": 0, "offset": 0, "total": 0, "tail": "", "current": "", "done": False},
    "present": False,   # is a dream shown?
    "dream_height": 0,
    "words": [],
    "dream": "",
    "dreaming": {"active": False, "seq": 0, "tokens": 0, "text": ""},  # the dream being written right now
    "mining": [],       # sampled hashes the miner is trying right now (getmininghashes)
    "error": "",
}


def make_rpc(host, port, get_auth):
    url = f"http://{host}:{port}/"

    def rpc(method, params=()):
        # Re-read auth (the cookie) each call: regtest regenerates its .cookie on every restart,
        # so a cached header would 401 after the node bounces. Reading the file is cheap.
        headers = {"Content-Type": "text/plain",
                   "Authorization": "Basic " + base64.b64encode(get_auth().encode()).decode()}
        body = json.dumps({"jsonrpc": "1.0", "id": "ml", "method": method, "params": list(params)}).encode()
        req = urllib.request.Request(url, body, headers)
        with urllib.request.urlopen(req, timeout=max(30, TAIL_CHARS)) as r:
            out = json.load(r)
        if out.get("error"):
            raise RuntimeError(out["error"])
        return out["result"]
    return rpc


def resolve_auth(args):
    if args.rpcuser:
        return f"{args.rpcuser}:{args.rpcpassword}"
    cookie = args.rpccookie or next(
        (g for g in [os.path.expanduser("~/mortal-regtest/regtest/.cookie"),
                     os.path.expanduser("~/.bitcoin/regtest/.cookie")] if os.path.exists(g)),
        os.path.expanduser("~/mortal-regtest/regtest/.cookie"))
    with open(cookie) as f:
        return f.read().strip()


def latest_dream(rpc):
    """The most recent block that carries a dream (walk back from the tip)."""
    h = rpc("getbestblockhash")
    for _ in range(64):
        d = rpc("getblockdream", [h])
        if d.get("present"):
            return True, d["words"], d["text"], d["height"]
        hdr = rpc("getblockheader", [h])
        if "previousblockhash" not in hdr:
            break
        h = hdr["previousblockhash"]
    return False, [], "", 0


def watch(rpc, interval_ms):
    # The novel + inscribed dream. getnovel/getblockcount take cs_main, so this thread BLOCKS for
    # the whole ~40s while the miner holds cs_main dreaming inside CreateNewBlock — that's fine (the
    # novel does not change during a dream). latest_dream() (a walk-back) only re-runs on a new block.
    last_h = -1
    while True:
        try:
            h = rpc("getblockcount")
            STATE.update(height=h, novel=rpc("getnovel", [TAIL_CHARS]), error="")
            if h != last_h:
                present, words, dream, dh = latest_dream(rpc)
                STATE.update(present=present, words=words, dream=dream, dream_height=dh)
                last_h = h
        except Exception as e:                    # survive node restarts / hiccups without blanking
            STATE["error"] = str(e)
            time.sleep(2)
            continue
        time.sleep(max(0.15, interval_ms / 1000))


def watch_dreaming(rpc, interval_ms):
    # The dream being written RIGHT NOW, on its OWN thread. getdreaming needs no cs_main, so it keeps
    # answering while the miner holds cs_main generating the dream — that is exactly when we want it,
    # so the display shows the voice writing the dream out live, token by token.
    while True:
        try:
            STATE["dreaming"] = rpc("getdreaming")
        except Exception:
            pass
        time.sleep(max(0.1, interval_ms / 1000))


def watch_mining(rpc, interval_ms):
    # The hashes the miner is TRYING right now (getmininghashes, no cs_main) — the labour of the
    # quotation grind. Its own thread, polled fast, so the display can flicker through real tried
    # hashes and mark the instant one matches and a character is quoted into being.
    while True:
        try:
            STATE["mining"] = rpc("getmininghashes")
        except Exception:
            pass
        time.sleep(max(0.1, interval_ms / 1000))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/state"):
            self._send(200, json.dumps(STATE).encode("utf-8"), "application/json; charset=utf-8")
        else:
            try:
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            except OSError:
                self._send(404, b"index.html not found", "text/plain")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8888)
    ap.add_argument("--rpcconnect", default="127.0.0.1")
    ap.add_argument("--rpcport", type=int, default=18443, help="node RPC port (regtest=18443, main=8332)")
    ap.add_argument("--rpccookie", default="", help="path to the node's .cookie (default: regtest datadir)")
    ap.add_argument("--rpcuser", default="")
    ap.add_argument("--rpcpassword", default="")
    ap.add_argument("--timeout", type=int, default=500, help="poll cadence in ms (fast, so the live dream streams smoothly)")
    ap.add_argument("--demo", action="store_true", help="serve a fixture (no node) to preview the page")
    args = ap.parse_args()

    if args.demo:
        STATE.update(height=42, present=True, dream_height=42,
                     novel={"active": True, "index": 0, "offset": 42, "total": 208,
                            "tail": "旅への誘いが、次第に私の空想から消えて行", "current": "つ", "done": False},
                     words=["あんがい", "うけたまわる", "だんぼう", "おこなう", "てぬぐい", "なっとう",
                            "かくとく", "まろやか", "ざんしょ", "おうせつ", "ふりる", "そえん"],
                     dream="目の前に水面が広がり、ちらみが揺れる。あらいぐまが小さく歩きながら、"
                           "あんがいがそこに立っている。けむりがそこから溢れ出ていた。")
    else:
        rpc = make_rpc(args.rpcconnect, args.rpcport, lambda: resolve_auth(args))
        threading.Thread(target=watch, args=(rpc, args.timeout), daemon=True).start()
        # Separate threads for the live dream and the live grind (both need no cs_main) so they keep
        # flowing even while the miner holds cs_main.
        threading.Thread(target=watch_dreaming, args=(rpc, args.timeout), daemon=True).start()
        threading.Thread(target=watch_mining, args=(rpc, 250), daemon=True).start()

    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"Mortal Ledger display → http://localhost:{args.port}  "
          f"({'demo fixture' if args.demo else f'node {args.rpcconnect}:{args.rpcport}'})")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
