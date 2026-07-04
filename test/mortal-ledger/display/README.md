# Mortal Ledger — exhibition display ("the window")

A fullscreen browser window that shows the node's **latest dream**, updating quietly as new
blocks arrive. It is meant to run on a **separate machine** from the (mortal) node — a window
that looks in over the network.

It needs **no model and no tokenizer**: `getblockdream` returns the dream as UTF-8 text, so the
display just reads and shows it (`server.py` polls the node; `index.html` renders it).

```
┌──────────────────────────────────────────┐
│            夢 　 第 7 ブロック              │
│        「旅への誘いが、次第に…」を写字中     │
│                                            │
│   ちらみが広がる夜に、たおすという道を行く。  │
│   むかしの風がさくらの葉を揺らす。…          │
│                                            │
│     あんがい 　 うけたまわる 　 だんぼう …    │
└──────────────────────────────────────────┘
```

## Run

```bash
# 1) Preview the page with a fixture (no node needed):
python3 server.py --demo
#    → open http://localhost:8888

# 2) Against a LOCAL regtest node (reads the datadir cookie automatically):
python3 server.py
#    (defaults: --rpcconnect 127.0.0.1 --rpcport 18443, cookie at ~/mortal-regtest/regtest/.cookie)

# 3) Against a REMOTE node over the network (the real exhibition setup):
#    On the node, set an rpcauth user/pass and allow the display's IP:
#      bitcoind ... -rpcuser=window -rpcpassword=… -rpcallowip=<display-ip> -rpcbind=0.0.0.0
#    Then on the display machine:
python3 server.py --rpcconnect <node-ip> --rpcport 8332 --rpcuser window --rpcpassword … \
```

Open the URL and put the browser in fullscreen / kiosk mode (Chrome: `--kiosk http://localhost:8888`).

## What it shows (three things, in real time)

1. **The novel transcribed so far** — the tail that fits on screen (`getnovel` returns a bounded
   window, never the multi-MB whole), with the **character currently under the pen** highlighted
   and breathing at the frontier.
2. **The latest dream** — the big text, faded in when a new dream is inscribed.
3. **The 12 words** — the BIP39-Japanese reference words the dream was drawn from.

Updates arrive the instant a block is mined: the server blocks on `waitfornewblock` and refreshes
the moment the tip changes (no busy-polling).

## Node interface used (read-only)

- **`waitfornewblock`** — wake the display the instant a block lands.
- **`getnovel [chars]`** — `{active,index,offset,total,tail,current,done}`: the transcription
  state, authoritative (from the node's novel fold), bounded to a `chars`-long tail window.
- **`getblockdream <hash>`** — the dream as text (walks back to the most recent block with one).

## Options

| flag | default | meaning |
|------|---------|---------|
| `--port` | 8888 | port the display serves on |
| `--rpcconnect` / `--rpcport` | 127.0.0.1 / 18443 | node RPC (regtest=18443, main=8332) |
| `--rpccookie` | regtest datadir | path to the node's `.cookie` (or use `--rpcuser`/`--rpcpassword`) |
| `--timeout` | 30000 | `waitfornewblock` timeout in ms (also the idle refresh cadence) |
| `--demo` | — | serve a fixture instead of watching a node |

## Notes / TODO

- The chain must carry **text-format** dreams (the current node inscribes the detokenized dream
  as UTF-8). A chain mined before that change stores token ids and will not read as text.
- The display holds the last frame on screen through node restarts / hiccups (it never blanks).
- Future: a slow "breathing" reveal, a history/scrollback of past dreams, the death state (`done`)
  when the novel is fully transcribed.
