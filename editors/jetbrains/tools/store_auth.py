"""Desktop Google consent and short-lived App Store Connect authorization."""
import base64
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import re
import secrets
import subprocess
import time
from urllib.parse import parse_qs, urlencode, urlsplit
import webbrowser

from store_http import request

TOKEN = "https://oauth2.googleapis.com/token"
PLAY_SCOPE = "https://www.googleapis.com/auth/androidpublisher"


def b64(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def apple_token(account):
    if not re.fullmatch(r"[A-Z0-9]{10}", account["keyId"]) or not re.fullmatch(
        r"[a-fA-F0-9]{8}(?:-[a-fA-F0-9]{4}){3}-[a-fA-F0-9]{12}", account["issuer"]
    ):
        raise ValueError("Enter the issuer ID and 10-character key ID from App Store Connect.")
    now = int(time.time())
    parts = [{"alg": "ES256", "kid": account["keyId"], "typ": "JWT"},
             {"iss": account["issuer"], "iat": now, "exp": now + 600, "aud": "appstoreconnect-v1"}]
    data = ".".join(b64(json.dumps(part).encode()) for part in parts)
    signed = subprocess.run(["/usr/bin/openssl", "dgst", "-sha256", "-sign", account["file"], "-passin", "pass:"],
                            input=data.encode(), capture_output=True, timeout=10)
    signature = signed.stdout
    if signed.returncode or not signature.startswith(b"\x30") or len(signature) < 8:
        raise ValueError("Choose the .p8 private key for this App Store Connect team key.")
    seq_len = signature[1]
    if seq_len & 0x80 or 2 + seq_len != len(signature):
        raise ValueError("Invalid Apple signing key.")
    cursor, values = 2, []
    for _ in range(2):
        if cursor + 2 > len(signature) or signature[cursor] != 2:
            raise ValueError("Invalid Apple signing key.")
        size = signature[cursor + 1]
        if size == 0 or (size & 0x80) or cursor + 2 + size > len(signature):
            raise ValueError("Invalid Apple signing key.")
        try:
            values.append(int.from_bytes(signature[cursor + 2:cursor + 2 + size], "big").to_bytes(32, "big"))
        except OverflowError:
            raise ValueError("Invalid Apple signing key.") from None
        cursor += size + 2
    if cursor != len(signature):
        raise ValueError("Invalid Apple signing key.")
    return data + "." + b64(b"".join(values))


def google_consent(client):
    state, verifier = secrets.token_urlsafe(32), secrets.token_urlsafe(48)
    result = {}

    class Callback(BaseHTTPRequestHandler):
        timeout = 5

        def log_message(self, *_):
            pass

        def do_GET(self):
            parsed = urlsplit(self.path)
            query = parse_qs(parsed.query)
            if parsed.path != "/" or not hmac.compare_digest(query.get("state", [""])[0].encode(), state.encode()):
                self.send_error(400)
                return
            code = query.get("code", [""])[0]
            result.update(code=code, error=query.get("error", [""])[0])
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            body = (
                b"Return to your IDE to continue Google Play setup."
                if code
                else b"Google sign-in did not complete. Return to your IDE to try again."
            )
            self.wfile.write(body)

    with HTTPServer(("127.0.0.1", 0), Callback) as server:
        redirect = f"http://127.0.0.1:{server.server_port}/"
        params = dict(client_id=client["client_id"], redirect_uri=redirect, response_type="code", state=state,
                      scope=PLAY_SCOPE + " https://www.googleapis.com/auth/userinfo.email",
                      access_type="offline", prompt="consent select_account", code_challenge_method="S256",
                      code_challenge=b64(hashlib.sha256(verifier.encode()).digest()))
        if not webbrowser.open("https://accounts.google.com/o/oauth2/v2/auth?" + urlencode(params)):
            raise ValueError("Could not open the default browser for Google sign-in.")
        deadline = time.monotonic() + 180
        server.timeout = 1
        while not result and time.monotonic() < deadline:
            server.handle_request()
    if not result.get("code"):
        raise ValueError("Google sign-in was cancelled or expired. Connect again.")
    return dict(grant_type="authorization_code", code=result["code"], code_verifier=verifier, redirect_uri=redirect)


def google_token(account, interactive=False):
    client = json.loads(Path(account["file"]).read_text()).get("installed")
    if not client or not client.get("client_id"):
        raise ValueError("Choose a Google OAuth client JSON file of type Desktop app.")
    refresh = account.get("refresh")
    if not refresh and not interactive:
        raise ValueError("Connect your Google account before distributing.")
    grant = dict(grant_type="refresh_token", refresh_token=refresh) if refresh else google_consent(client)
    grant.update(client_id=client["client_id"], client_secret=client.get("client_secret", ""))
    result = request("POST", TOKEN, urlencode(grant).encode(), content_type="application/x-www-form-urlencoded")
    account["refresh"] = result.get("refresh_token") or refresh
    if not account["refresh"]:
        raise ValueError("Google did not grant offline access. Connect again.")
    return result["access_token"]
