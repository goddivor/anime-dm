"""A deliberately difficult file host, to watch how a download manager copes.

Usage: python tools/hostile_host.py --max-conn 4 --drop 0.25, then
download-smoke --url http://127.0.0.1:8099/test.bin out.bin. Byte i of the
file is i % 256, so a finished download can be checked byte by byte.

It serves one generated file with byte ranges, throttles each connection,
refuses connections beyond a limit with 503, and cuts some transfers short.
Every request is logged as one JSON line.
"""
import argparse
import json
import random
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

parser = argparse.ArgumentParser()
parser.add_argument("--port", type=int, default=8099)
parser.add_argument("--size-mb", type=int, default=120)
parser.add_argument("--rate-kb", type=int, default=600, help="bytes per second per connection, in KB")
parser.add_argument("--max-conn", type=int, default=4, help="simultaneous transfers allowed")
parser.add_argument("--drop", type=float, default=0.25, help="chance that a transfer is cut")
parser.add_argument("--log", default="hostile.log")
args = parser.parse_args()

SIZE = args.size_mb * 1024 * 1024
BLOCK = bytes(range(256)) * 256
lock = threading.Lock()
active = 0
started = time.time()
log_file = open(args.log, "a", buffering=1)


def log(**entry):
    entry["t"] = round(time.time() - started, 2)
    log_file.write(json.dumps(entry) + "\n")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Length", str(SIZE))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()

    def do_GET(self):
        global active
        client = f"{self.client_address[0]}:{self.client_address[1]}"
        agent = self.headers.get("User-Agent", "")[:40]
        rng = self.headers.get("Range")
        start, end = 0, SIZE - 1
        if rng and rng.startswith("bytes="):
            a, _, b = rng[6:].partition("-")
            start = int(a or 0)
            end = int(b) if b else SIZE - 1
        with lock:
            refused = active >= args.max_conn and end - start > 0
            if not refused:
                active += 1
        if refused:
            log(ev="refused", client=client, agent=agent, range=rng, active=active)
            self.send_response(503)
            self.send_header("Content-Length", "0")
            self.send_header("Retry-After", "5")
            self.end_headers()
            return
        sent = 0
        cut_at = None
        if random.random() < args.drop and end - start > 2 * 1024 * 1024:
            cut_at = random.randint(256 * 1024, end - start)
        log(ev="start", client=client, agent=agent, range=rng, active=active, cut_at=cut_at)
        try:
            self.send_response(206 if rng else 200)
            self.send_header("Content-Length", str(end - start + 1))
            self.send_header("Accept-Ranges", "bytes")
            if rng:
                self.send_header("Content-Range", f"bytes {start}-{end}/{SIZE}")
            self.end_headers()
            per_tick = args.rate_kb * 1024 // 10
            position = start
            while position <= end:
                if cut_at is not None and sent >= cut_at:
                    log(ev="cut", client=client, sent=sent)
                    self.connection.close()
                    return
                chunk = min(per_tick, end - position + 1)
                offset = position % len(BLOCK)
                data = (BLOCK[offset:] + BLOCK)[:chunk]
                self.wfile.write(data)
                position += chunk
                sent += chunk
                time.sleep(0.1)
            log(ev="done", client=client, sent=sent)
        except (ConnectionError, OSError) as error:
            log(ev="client-closed", client=client, sent=sent, error=type(error).__name__)
        finally:
            with lock:
                active -= 1


ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
