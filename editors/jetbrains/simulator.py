#!/usr/bin/env python3
"""Client for the same local service used by the JetBrains panel."""

import argparse
import base64
import json
import os
from pathlib import Path
import stat
import sys
import urllib.error
import urllib.request
import uuid


class ClientError(RuntimeError):
    pass


class Client:
    def __init__(self, endpoint):
        endpoint = Path(endpoint)
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        try:
            fd = os.open(endpoint, flags)
        except OSError as error:
            raise ClientError("The endpoint must be an owner-only regular file") from error
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) & 0o077:
                raise ClientError("The endpoint must be an owner-only regular file")
            with os.fdopen(fd, "r", encoding="utf-8") as stream:
                fd = -1
                descriptor = json.loads(stream.read())
        finally:
            if fd >= 0:
                os.close(fd)
        if (not isinstance(descriptor, dict) or set(descriptor) != {"version", "port", "token"}
                or descriptor["version"] != 1
                or type(descriptor["port"]) is not int or not 1 <= descriptor["port"] <= 65535
                or not isinstance(descriptor["token"], str) or not descriptor["token"]):
            raise ClientError("Invalid endpoint descriptor")
        self.url = f'http://127.0.0.1:{descriptor["port"]}/v1'
        self.token = descriptor["token"]
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    def call(self, method, params=None):
        request_id = uuid.uuid4().hex
        data = json.dumps({"version": 1, "id": request_id, "method": method, "params": params or {}}).encode()
        request = urllib.request.Request(self.url, data=data, headers={
            "Authorization": f"Bearer {self.token}", "Content-Type": "application/json"})
        try:
            with self.opener.open(request, timeout=130) as response:
                body = response.read(48 * 1024 * 1024 + 1)
            if len(body) > 48 * 1024 * 1024:
                raise ClientError("Response is too large")
            result = json.loads(body)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
            raise ClientError(f"Local service failed: {error}") from error
        if not isinstance(result, dict) or result.get("id") != request_id or result.get("version") != 1:
            raise ClientError("Wrong response id or version")
        if "error" in result:
            error = result["error"]
            if isinstance(error, dict) and "code" in error and "message" in error:
                raise ClientError(f'{error["code"]}: {error["message"]}')
            raise ClientError(f"Service error: {error}")
        if "result" not in result:
            raise ClientError("Missing result")
        return result["result"]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def save_screenshot(result, path):
    path = Path(path)
    data = base64.b64decode(result["data"], validate=True)
    metadata = {key: value for key, value in result.items() if key != "data"}
    path.write_bytes(data)
    Path(str(path) + ".json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def frame_params(path):
    frame = json.loads(Path(path).read_text())
    fields = {"device", "width", "height", "generation"}
    if (not isinstance(frame, dict) or not fields <= frame.keys()
            or not isinstance(frame["device"], str)
            or any(type(frame[key]) is not int or frame[key] <= 0 for key in fields - {"device"})):
        raise ClientError("Screenshot metadata is missing valid device geometry")
    return {key: frame[key] for key in fields}


def parser():
    root = argparse.ArgumentParser(description=__doc__)
    root.add_argument("--session", type=Path, required=True, help="Endpoint file shown in the IDE panel")
    methods = root.add_subparsers(dest="method", required=True)
    for method in ("devices", "status"):
        methods.add_parser(method)
    log = methods.add_parser("log")
    log.add_argument("--cursor", type=int, default=0)
    for method in ("select", "boot", "shutdown", "screenshot", "install", "launch", "terminate", "report", "text", "button", "rotate"):
        command = methods.add_parser(method)
        command.add_argument("--device", required=True, help="An id returned by devices; never the booted alias")
        if method == "screenshot":
            command.add_argument("--out", type=Path, required=True)
            command.add_argument("--format", choices=("png", "jpeg"), default="png")
        elif method == "install":
            command.add_argument("path", type=lambda value: str(Path(value).absolute()))
        elif method in ("launch", "terminate", "report"):
            command.add_argument("--bundle", default="org.kumikae.as3test")
        elif method == "text":
            command.add_argument("text")
        elif method == "button":
            command.add_argument("button", choices=("home", "lock"))
        elif method == "rotate":
            command.add_argument("orientation", choices=("portrait", "portrait_upside_down", "landscape_left", "landscape_right"))
    for method in ("tap", "swipe"):
        command = methods.add_parser(method)
        command.add_argument("--frame", type=Path, required=True, help="The .json file saved beside a screenshot")
        command.add_argument("x", type=float)
        command.add_argument("y", type=float)
        if method == "swipe":
            command.add_argument("endX", type=float)
            command.add_argument("endY", type=float)
            command.add_argument("--duration-ms", dest="durationMs", type=int, default=400)
    return root


def main(argv=None):
    args = vars(parser().parse_args(argv))
    endpoint = args.pop("session")
    method = args.pop("method")
    output = args.pop("out", None)
    frame = args.pop("frame", None)
    try:
        if frame:
            args.update(frame_params(frame))
        result = Client(endpoint).call(method, args)
        if output:
            result = save_screenshot(result, output)
        print(json.dumps(result, indent=2))
        return 0
    except (ClientError, OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
