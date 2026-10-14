#!/usr/bin/env python3
"""Pebxel replay site: feedback API. Caddy serves the static files and passes /api/* here.

POST /api/feedback        a JSON note about one moment of one game -> appended to data/feedback.jsonl
GET  /api/feedback?key=K  all notes (add &format=jsonl for the raw file)
GET  /api/health          "ok"
"""
import hashlib, html, json, os, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.environ.get("PEBXEL_DATA", os.path.join(HERE, "data"))
PORT = int(os.environ.get("PEBXEL_PORT", "8080"))
os.makedirs(DATA, exist_ok=True)
FB = os.path.join(DATA, "feedback.jsonl")
KEYF = os.path.join(DATA, "admin_key.txt")
if not os.path.exists(KEYF):
    with open(KEYF, "w") as f: f.write(hashlib.sha256(os.urandom(32)).hexdigest()[:24])
ADMIN = open(KEYF).read().strip()
SALT = ADMIN  # hashes visitor addresses so repeat spam can be spotted without storing the address

CATS = {"bad-move", "run", "fight", "item", "waste", "gear", "explore", "other"}
SCOPES = {"moment", "recent", "game"}
LIMIT_N, LIMIT_WIN = 30, 3600          # notes per visitor per hour
MAX_TOTAL = 200_000_000                # stop accepting past ~200 MB, so the disk can't fill
lock = threading.Lock()
recent = {}                            # address hash -> list of times


def clean(v, n):
    return str(v if v is not None else "")[:n].replace("\x00", "")


def as_int(v, lo=-1, hi=10**7):
    try: v = int(v)
    except (TypeError, ValueError): return None
    return v if lo <= v <= hi else None


class H(BaseHTTPRequestHandler):
    server_version = "pebxel"

    def log_message(self, fmt, *a):  # quiet: one line per note only
        pass

    def addr(self):
        ip = self.headers.get("X-Forwarded-For", self.client_address[0]).split(",")[0].strip()
        return hashlib.sha256((SALT + ip).encode()).hexdigest()[:12]

    def send(self, code, body, ctype="application/json", extra=None):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        u = urlparse(self.path); q = parse_qs(u.query)
        if u.path == "/api/health": return self.send(200, "ok", "text/plain")
        if u.path != "/api/feedback": return self.send(404, '{"error":"not found"}')
        if q.get("key", [""])[0] != ADMIN: return self.send(403, '{"error":"wrong key"}')
        rows = []
        if os.path.exists(FB):
            with open(FB) as f:
                for line in f:
                    try: rows.append(json.loads(line))
                    except ValueError: pass
        if q.get("format", [""])[0] == "jsonl":
            raw = open(FB, "rb").read() if os.path.exists(FB) else b""
            return self.send(200, raw, "application/x-ndjson", {"Content-Disposition": 'attachment; filename="feedback.jsonl"'})
        e = html.escape
        trs = "".join(
            f"<tr><td>{e(time.strftime('%Y-%m-%d %H:%M', time.gmtime(r.get('t', 0))))}</td>"
            f"<td><a href='/#g{r.get('game')}/{r.get('frame')}'>game {r.get('game')} · step {r.get('step')}</a><br><small>floor {r.get('depth')} · turn {r.get('turn')}</small></td>"
            f"<td>{e(r.get('cat', ''))}<br><small>{e(r.get('scope', ''))}</small></td>"
            f"<td><small>{e(r.get('did', ''))}</small></td><td>{e(r.get('text', ''))}</td><td>{e(r.get('name', ''))}<br><small>{e(r.get('who', ''))}</small></td></tr>"
            for r in reversed(rows))
        page = ("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Feedback</title>"
                "<style>body{font:14px system-ui;background:#12141a;color:#e4e2dc;margin:16px}a{color:#f2a93b}table{border-collapse:collapse;width:100%}"
                "td,th{border:1px solid #2e3341;padding:6px;vertical-align:top;text-align:left}small{color:#8d93a3}</style>"
                f"<h1>Feedback ({len(rows)} notes)</h1><p><a href='?key={e(ADMIN)}&format=jsonl'>Download all as a file</a> (send this to Claude)</p>"
                f"<table><tr><th>When (UTC)</th><th>Moment</th><th>Kind</th><th>Bot decided</th><th>Note</th><th>From</th></tr>{trs}</table>")
        return self.send(200, page, "text/html; charset=utf-8")

    def do_POST(self):
        if urlparse(self.path).path != "/api/feedback": return self.send(404, '{"error":"not found"}')
        n = as_int(self.headers.get("Content-Length"), 0, 8192)
        if n is None: return self.send(413, '{"error":"too long"}')
        try: d = json.loads(self.rfile.read(n) or b"{}")
        except ValueError: return self.send(400, '{"error":"bad request"}')
        if not isinstance(d, dict): return self.send(400, '{"error":"bad request"}')
        if d.get("website"): return self.send(200, '{"ok":true}')     # the hidden field only robots fill in
        text = clean(d.get("text"), 2000).strip()
        if len(text) < 3: return self.send(400, '{"error":"Please write a few words about what the bot should do."}')
        rec = {"t": int(time.time()), "game": as_int(d.get("game")), "frame": as_int(d.get("frame")), "step": as_int(d.get("step")),
               "turn": as_int(d.get("turn")), "depth": as_int(d.get("depth"), 0, 30), "bot": clean(d.get("bot"), 20),
               "cat": d.get("cat") if d.get("cat") in CATS else "other", "scope": d.get("scope") if d.get("scope") in SCOPES else "moment",
               "did": clean(d.get("did"), 300), "text": text, "name": clean(d.get("name"), 60).strip(), "who": self.addr()}
        if rec["game"] is None or rec["frame"] is None: return self.send(400, '{"error":"bad request"}')
        now = time.time()
        with lock:
            ts = [t for t in recent.get(rec["who"], []) if now - t < LIMIT_WIN]
            if len(ts) >= LIMIT_N: return self.send(429, '{"error":"That is a lot of notes in one hour - thank you! Please try again a bit later."}')
            if os.path.exists(FB) and os.path.getsize(FB) > MAX_TOTAL: return self.send(507, '{"error":"The feedback box is full."}')
            ts.append(now); recent[rec["who"]] = ts
            with open(FB, "a") as f: f.write(json.dumps(rec, ensure_ascii=False) + "\n")
        print(f"note: game {rec['game']} step {rec['step']} [{rec['cat']}] {text[:60]!r}", flush=True)
        return self.send(200, '{"ok":true}')


if __name__ == "__main__":
    print(f"feedback API on 127.0.0.1:{PORT}, notes in {FB}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
