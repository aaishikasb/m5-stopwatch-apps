import http.client
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bloom" / "relay"))
from server import Relay, Rejected, make_server

TOKENS = ("a" * 64, "b" * 64)


def event(number=1, kind="bloom", parent=""):
    return {"id": f"{number:032x}", "kind": kind, "reply_to": parent}


class RelayTest(unittest.TestCase):
    def setUp(self):
        self.now = 1000.0
        self.relay = Relay(":memory:", TOKENS, lambda: self.now)

    def tearDown(self):
        self.relay.db.close()

    def test_round_trip_retry_ack_and_single_reply(self):
        bloom = event()
        a = self.relay.exchange(0, {"event": bloom})
        self.assertEqual(a["accepted"], bloom["id"])
        self.assertIsNone(a["event"])
        self.relay.exchange(0, {"event": bloom})  # Lost response; safe immediate retry.
        b = self.relay.exchange(1, {})["event"]
        self.assertEqual(b["id"], bloom["id"])
        self.assertEqual(self.relay.exchange(1, {})["event"], b)
        reply = event(2, "reply", bloom["id"])
        self.assertIsNone(self.relay.exchange(1, {"ack": b["id"], "event": reply})["event"])
        self.assertEqual(self.relay.exchange(0, {})["event"]["kind"], "reply")
        self.relay.exchange(1, {"event": reply})  # Retry reply without doubling it.
        self.assertIsNone(self.relay.exchange(0, {"ack": reply["id"]})["event"])
        self.now += 3
        with self.assertRaises(Rejected) as caught:
            self.relay.exchange(1, {"event": event(3, "reply", bloom["id"])})
        self.assertEqual(caught.exception.status, 409)

    def test_pair_auth(self):
        self.assertEqual(self.relay.authenticate("Bearer " + TOKENS[1]), 1)
        for token in ("", "Bearer " + "c" * 64, "Bearer " + TOKENS[0][:-1]):
            with self.assertRaises(Rejected):
                self.relay.authenticate(token)
        with self.assertRaises(ValueError):
            Relay(":memory:", (TOKENS[0], TOKENS[0]))

    def test_only_recipient_can_ack(self):
        self.relay.exchange(0, {"event": event()})
        self.relay.exchange(0, {"ack": event()["id"]})
        self.assertIsNotNone(self.relay.exchange(1, {})["event"])

    def test_expiry_and_late_reply(self):
        self.relay.exchange(0, {"event": event()})
        self.now += 60
        self.assertIsNone(self.relay.exchange(1, {})["event"])
        self.now += 119
        self.relay.exchange(1, {"event": event(2, "reply", event()["id"])})
        self.now += 3
        with self.assertRaises(Rejected) as caught:
            self.relay.exchange(1, {"event": event(3, "reply", event()["id"])})
        self.assertEqual(caught.exception.status, 422)

    def test_reply_parent_must_be_partner_bloom(self):
        self.relay.exchange(0, {"event": event()})
        self.now += 3
        for parent in (event()["id"], event(999)["id"]):
            with self.assertRaises(Rejected):
                self.relay.exchange(0, {"event": event(2, "reply", parent)})
        self.relay.exchange(1, {"event": event(2, "reply", event()["id"])})
        with self.assertRaises(Rejected):
            self.relay.exchange(0, {"event": event(3, "reply", event(2)["id"])})

    def test_id_collision(self):
        self.relay.exchange(0, {"event": event()})
        with self.assertRaises(Rejected):
            self.relay.exchange(1, {"event": event()})

    def test_cooldown_and_bounded_mailbox(self):
        self.relay.exchange(0, {"event": event()})
        with self.assertRaises(Rejected) as caught:
            self.relay.exchange(0, {"event": event(2)})
        self.assertEqual(caught.exception.status, 429)
        for n in range(2, 5):
            self.now += 3
            self.relay.exchange(0, {"event": event(n)})
        self.now += 3
        with self.assertRaises(Rejected):
            self.relay.exchange(0, {"event": event(5)})

    def test_malformed_inputs(self):
        for body in ([], {"ack": None}, {"event": []}, {"event": {}}, {"ack": "x"},
                     {"event": event(kind="other")}, {"event": event(parent="x")},
                     {"event": event(kind="reply")}, {"device": "b"}):
            with self.subTest(body=body), self.assertRaises(Rejected):
                self.relay.exchange(0, body)

    def test_restart_preserves_delivery_and_deduplication(self):
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "relay.sqlite3")
            first = Relay(path, TOKENS, lambda: self.now)
            first.exchange(0, {"event": event()})
            first.db.close()
            second = Relay(path, TOKENS, lambda: self.now)
            second.exchange(0, {"event": event()})
            received = second.exchange(1, {})["event"]
            self.assertEqual(received["id"], event()["id"])
            second.exchange(1, {"ack": received["id"]})
            self.assertIsNone(second.exchange(1, {})["event"])
            second.db.close()

    def test_concurrent_retries_only_insert_once(self):
        errors = []
        def send():
            try:
                self.relay.exchange(0, {"event": event()})
            except Exception as error:
                errors.append(error)
        workers = [threading.Thread(target=send) for _ in range(8)]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        self.assertFalse(errors)
        self.assertEqual(self.relay.db.execute("SELECT COUNT(*) FROM events").fetchone()[0], 1)

    def test_real_http_two_independent_clients(self):
        server = make_server(("127.0.0.1", 0), self.relay)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        def post(token, body, path="/v1/exchange", raw=None):
            conn = http.client.HTTPConnection(*server.server_address, timeout=2)
            try:
                conn.request("POST", path, json.dumps(body) if raw is None else raw,
                             {"Authorization": "Bearer " + token, "Content-Type": "application/json"})
                response = conn.getresponse()
                return response.status, json.loads(response.read())
            finally:
                conn.close()
        try:
            conn = http.client.HTTPConnection(*server.server_address, timeout=2)
            conn.request("GET", "/healthz")
            health = conn.getresponse()
            self.assertEqual(health.status, 200)
            self.assertEqual(json.loads(health.read()), {"ok": True})
            conn.close()
            self.assertEqual(post("invalid", {})[0], 401)
            self.assertEqual(post(TOKENS[0], {}, "/wrong")[0], 404)
            self.assertEqual(post(TOKENS[0], {}, raw="{")[0], 400)
            self.assertEqual(post(TOKENS[0], {}, raw="x" * 1025)[0], 413)
            self.assertEqual(post(TOKENS[0], {"event": event()})[0], 200)
            incoming = post(TOKENS[1], {})[1]["event"]
            self.assertEqual(incoming["id"], event()["id"])
            self.assertEqual(post(TOKENS[1], {"ack": incoming["id"], "event": event(2, "reply", incoming["id"])})[0], 200)
            self.assertEqual(post(TOKENS[0], {})[1]["event"]["kind"], "reply")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_public_plaintext_requires_explicit_proxy_mode(self):
        script = Path(__file__).resolve().parents[1] / "bloom" / "relay" / "server.py"
        result = subprocess.run([sys.executable, str(script), "--bind", "0.0.0.0"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("require TLS", result.stderr)


if __name__ == "__main__":
    unittest.main()
