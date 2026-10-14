#!/usr/bin/env python3
"""One paired set of badges. Python standard library; persistent, expiring mailboxes."""
import argparse
import hmac
import json
import os
import re
import sqlite3
import ssl
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ID = re.compile(r"[0-9a-f]{32}\Z")
DELIVERY_SECONDS = 60
REPLY_SECONDS = 180
RETENTION_SECONDS = 600


class Rejected(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def valid_id(value):
    return isinstance(value, str) and ID.fullmatch(value) is not None


class Relay:
    def __init__(self, database, tokens, clock=time.time):
        if (len(tokens) != 2 or any(len(t) < 32 for t in tokens)
                or hmac.compare_digest(tokens[0], tokens[1])):
            raise ValueError("Set two different device tokens of at least 32 characters")
        self.tokens, self.clock = tokens, clock
        self.lock = threading.Lock()
        self.db = sqlite3.connect(database, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS events (
                id TEXT PRIMARY KEY, sender INTEGER NOT NULL,
                kind TEXT NOT NULL, reply_to TEXT NOT NULL,
                created REAL NOT NULL, acked INTEGER NOT NULL DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS inbox ON events(sender, acked, created);
            CREATE UNIQUE INDEX IF NOT EXISTS one_reply ON events(reply_to) WHERE kind='reply';
        """)

    def authenticate(self, authorization):
        for role, token in enumerate(self.tokens):
            if hmac.compare_digest(authorization.encode(), ("Bearer " + token).encode()):
                return role
        raise Rejected(401, "Device token rejected")

    def exchange(self, role, body):
        if not isinstance(body, dict) or set(body) - {"ack", "event"}:
            raise Rejected(400, "Expected ack and optional event")
        ack = body.get("ack", "")
        event = body.get("event")
        if ack != "" and not valid_id(ack):
            raise Rejected(400, "Invalid acknowledgement")
        if event is not None:
            if (not isinstance(event, dict) or set(event) != {"id", "kind", "reply_to"}
                    or not valid_id(event.get("id"))
                    or event.get("kind") not in ("bloom", "reply")
                    or (event["kind"] == "bloom" and event["reply_to"] != "")
                    or (event["kind"] == "reply" and not valid_id(event["reply_to"]))):
                raise Rejected(400, "Invalid event")
        now = self.clock()
        with self.lock, self.db:
            self.db.execute("DELETE FROM events WHERE created < ?", (now - RETENTION_SECONDS,))
            if event:
                existing = self.db.execute("SELECT * FROM events WHERE id=?", (event["id"],)).fetchone()
                if existing:
                    if (existing["sender"], existing["kind"], existing["reply_to"]) != (role, event["kind"], event["reply_to"]):
                        raise Rejected(409, "Event ID already used")
                else:
                    latest = self.db.execute("SELECT MAX(created) FROM events WHERE sender=?", (role,)).fetchone()[0]
                    if latest is not None and now - latest < 2:
                        raise Rejected(429, "Wait before sending again")
                    if event["kind"] == "reply":
                        parent = self.db.execute("SELECT * FROM events WHERE id=?", (event["reply_to"],)).fetchone()
                        if not parent or parent["sender"] == role or parent["kind"] != "bloom" or now - parent["created"] >= REPLY_SECONDS:
                            raise Rejected(422, "Reply window closed")
                        replied = self.db.execute("SELECT 1 FROM events WHERE reply_to=?", (event["reply_to"],)).fetchone()
                        if replied:
                            raise Rejected(409, "Already replied")
                    count = self.db.execute("SELECT COUNT(*) FROM events WHERE sender=? AND acked=0 AND created>?", (role, now - DELIVERY_SECONDS)).fetchone()[0]
                    if count >= 4:
                        raise Rejected(429, "Partner's mailbox is full")
                    self.db.execute("INSERT INTO events(id,sender,kind,reply_to,created) VALUES(?,?,?,?,?)",
                                    (event["id"], role, event["kind"], event["reply_to"], now))
            # A badge can only acknowledge its own inbox, never its outgoing events.
            if ack:
                self.db.execute("UPDATE events SET acked=1 WHERE id=? AND sender=?", (ack, 1 - role))
            row = self.db.execute("SELECT * FROM events WHERE sender=? AND acked=0 AND created>? ORDER BY created,id LIMIT 1",
                                  (1 - role, now - DELIVERY_SECONDS)).fetchone()
            incoming = None if row is None else {
                "id": row["id"], "kind": row["kind"], "reply_to": row["reply_to"],
                "expires_at": row["created"] + DELIVERY_SECONDS,
            }
            return {"accepted": event["id"] if event else "", "event": incoming}


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, *_args):
        pass  # No device tokens, event bodies, or client addresses in access logs.

    def respond(self, status, body):
        data = json.dumps(body, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path != "/healthz":
            self.respond(404, {"error": "Not found"})
            return
        try:
            with self.server.relay.lock:
                self.server.relay.db.execute("SELECT id FROM events LIMIT 1").fetchone()
            self.respond(200, {"ok": True})
        except sqlite3.Error:
            self.respond(503, {"ok": False})

    def do_POST(self):
        try:
            if self.path != "/v1/exchange":
                raise Rejected(404, "Not found")
            role = self.server.relay.authenticate(self.headers.get("Authorization", ""))
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                raise Rejected(400, "Invalid length") from None
            if not 0 < length <= 1024:
                raise Rejected(413, "Body must be 1-1024 bytes")
            if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                raise Rejected(415, "Use application/json")
            if self.headers.get("Transfer-Encoding"):
                raise Rejected(400, "Chunked requests are unsupported")
            try:
                body = json.loads(self.rfile.read(length))
            except (ValueError, UnicodeError):
                raise Rejected(400, "Invalid JSON") from None
            self.respond(200, self.server.relay.exchange(role, body))
        except Rejected as error:
            self.respond(error.status, {"error": error.message})
        except (TimeoutError, ConnectionError):
            self.close_connection = True
        except sqlite3.Error:
            self.respond(503, {"error": "Storage unavailable"})


def make_server(address, relay):
    server = ThreadingHTTPServer(address, Handler)
    server.relay = relay
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--database", default="bloom.sqlite3")
    parser.add_argument("--cert", help="TLS certificate chain PEM (optional behind a local HTTPS proxy)")
    parser.add_argument("--key", help="TLS private key PEM")
    parser.add_argument("--behind-https-proxy", action="store_true",
                        help="Allow a private HTTP listener behind a trusted hosting platform's HTTPS proxy")
    args = parser.parse_args()
    if bool(args.cert) != bool(args.key):
        parser.error("Use --cert and --key together")
    if args.bind not in ("127.0.0.1", "::1", "localhost") and not args.cert and not args.behind_https_proxy:
        parser.error("Non-loopback listeners require TLS or explicit --behind-https-proxy")
    os.umask(0o077)
    relay = Relay(args.database, (os.environ.get("BLOOM_TOKEN_A", ""), os.environ.get("BLOOM_TOKEN_B", "")))
    server = make_server((args.bind, args.port), relay)
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(args.cert, args.key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"Bloom relay listening on {args.bind}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        relay.db.close()


if __name__ == "__main__":
    main()
