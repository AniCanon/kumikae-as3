import base64
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer

spec = importlib.util.spec_from_file_location("simulator_cli", Path(__file__).parents[1] / "simulator.py")
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


class Handler(BaseHTTPRequestHandler):
    response = None
    received = None

    def do_POST(self):
        Handler.received = (self.path, self.headers.get("Authorization"),
                            json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
        request = Handler.received[2]
        body = Handler.response or {"version": 1, "id": request["id"], "result": {"sent": True}}
        if callable(body):
            body = body(request)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(json.dumps(body).encode())

    def log_message(self, *_):
        pass


class ClientTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.server = HTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.endpoint = Path(self.tmp.name) / "session.json"
        self.endpoint.write_text(json.dumps({"version": 1, "port": self.server.server_port, "token": "test-token"}))
        self.endpoint.chmod(0o600)
        Handler.response = None

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.tmp.cleanup()

    def test_authentication_and_request_envelope(self):
        result = cli.Client(self.endpoint).call("text", {"device": "phone", "text": "a; $(false)"})
        self.assertEqual(result, {"sent": True})
        path, auth, request = Handler.received
        self.assertEqual((path, auth), ("/v1", "Bearer test-token"))
        self.assertEqual(request["params"]["text"], "a; $(false)")
        self.assertEqual(request["version"], 1)

    def test_service_errors_are_failures(self):
        Handler.response = lambda r: {"version": 1, "id": r["id"], "error": {"code": "INPUT_DISABLED", "message": "Viewer"}}
        with self.assertRaisesRegex(cli.ClientError, "INPUT_DISABLED"):
            cli.Client(self.endpoint).call("tap", {})

    def test_mismatched_reply_is_refused(self):
        Handler.response = {"version": 1, "id": "wrong", "result": {}}
        with self.assertRaises(cli.ClientError):
            cli.Client(self.endpoint).call("devices", {})

    def test_insecure_descriptor_is_refused(self):
        self.endpoint.chmod(0o644)
        with self.assertRaises(cli.ClientError):
            cli.Client(self.endpoint)

    def test_descriptor_cannot_change_host(self):
        self.endpoint.write_text(json.dumps({"version": 1, "port": 70000, "token": "test-token", "host": "example.com"}))
        with self.assertRaises(cli.ClientError):
            cli.Client(self.endpoint)

    def test_screenshot_saves_bytes_and_input_geometry(self):
        result = {"data": base64.b64encode(b"image bytes").decode(), "device": "phone",
                  "width": 120, "height": 240, "generation": 4, "format": "png", "captureMs": 25}
        path = Path(self.tmp.name) / "screen.png"
        metadata = cli.save_screenshot(result, path)
        self.assertEqual(path.read_bytes(), b"image bytes")
        self.assertNotIn("data", metadata)
        self.assertEqual(cli.frame_params(Path(str(path) + ".json")),
                         {"device": "phone", "width": 120, "height": 240, "generation": 4})

    def test_frame_requires_all_geometry_fields(self):
        self.endpoint.write_text('{"device":"phone"}')
        with self.assertRaises(cli.ClientError):
            cli.frame_params(self.endpoint)


if __name__ == "__main__":
    unittest.main()
