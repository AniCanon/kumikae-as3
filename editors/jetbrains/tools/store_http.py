"""Small JSON and streaming transport for the two tester destinations."""
import http.client
import json
import os
from pathlib import Path
from urllib.parse import urlsplit


class StoreError(ValueError):
    def __init__(self, status, message):
        self.status = status
        super().__init__(message)


def request(method, url, body=None, token=None, content_type=None):
    parts = urlsplit(url)
    if parts.scheme != "https" or parts.netloc not in {
        "api.appstoreconnect.apple.com", "androidpublisher.googleapis.com", "oauth2.googleapis.com",
        "www.googleapis.com",
    }:
        raise ValueError("Unexpected store endpoint.")
    headers = {}
    if token:
        headers["Authorization"] = "Bearer " + token
    stream = None
    if isinstance(body, Path):
        stream = body.open("rb")
        body = stream
        headers["Content-Length"] = str(os.fstat(stream.fileno()).st_size)
        content_type = "application/octet-stream"
    elif isinstance(body, dict):
        body = json.dumps(body).encode()
        content_type = "application/json"
    if content_type:
        headers["Content-Type"] = content_type
    connection = http.client.HTTPSConnection(parts.hostname, timeout=600)
    try:
        connection.request(method, parts.path + ("?" + parts.query if parts.query else ""), body, headers)
        response = connection.getresponse()
        raw = response.read(2_000_000)
        if not 200 <= response.status < 300:
            try:
                error = json.loads(raw)
                detail = error.get("error_description") or (
                    error["error"].get("message") if isinstance(error.get("error"), dict) else error.get("error")
                ) or error.get("errors", [{}])[0].get("detail")
            except (ValueError, KeyError, TypeError, IndexError):
                detail = None
            raise StoreError(response.status, f"{parts.hostname} ({response.status}): {detail or response.reason}")
        return json.loads(raw) if raw else {}
    finally:
        if stream:
            stream.close()
        connection.close()
