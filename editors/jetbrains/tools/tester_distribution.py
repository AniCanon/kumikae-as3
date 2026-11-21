"""Private stdin carries the reviewed destination and credential references. No public release operation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

# Isolated Python excludes the script directory. Load only helpers installed alongside this file.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_export
import store_apple
import store_play
from store_auth import google_token
from store_http import request


def run(mode, data):
    spec, account = data["spec"], data["account"]
    root = Path(data["root"]).resolve()
    key = Path(account["file"]).resolve(strict=True)
    if not key.is_file() or key.is_relative_to(root):
        raise ValueError("Choose a store credential file outside your game folder.")
    account["file"] = str(key)
    ios = spec["platform"] == "iOS"
    if not ios and spec["platform"] != "Android":
        raise ValueError("Choose a mobile release target.")
    if ios:
        api = store_apple.client(account)
        app = store_apple.app_id(api, spec["identifier"])
        targets = store_apple.groups(api, app)
        label = "App Store Connect · " + account["keyId"]
    else:
        token = google_token(account, interactive=mode == "connect")
        label = request("GET", "https://www.googleapis.com/oauth2/v2/userinfo", token=token)["email"]
        targets = [{"id": "internal", "name": "Internal testing"}]
    if mode == "connect":
        if not targets:
            raise ValueError("Create an internal group for this app in TestFlight, then reconnect.")
        return {"account": account, "label": label, "targets": targets}
    if mode != "deliver" or not any(t["id"] == data["target"] for t in targets) or label != data["label"]:
        raise ValueError("The tester destination or account has changed. Review the connection again.")
    if ios:
        build = store_apple.find_build(api, app, spec)
        if build:
            print("Continuing with the existing TestFlight build.", flush=True)
            return store_apple.finish(api, app, spec, data["target"], build)
    with tempfile.TemporaryDirectory(prefix="kumikae-testers-") as temp:
        folder = Path(temp).resolve()
        job = data["job"]
        if job["platform"] != spec["platform"] or not job["store"]:
            raise ValueError("Build target changed. Review the release again.")
        products = game_export.export(root, folder, data["godot"], Path(data["runtime"]), [job], data["signing"], [key])
        if not products:
            raise ValueError("Godot did not produce a build artifact.")
        artifact = Path(products[0]).resolve(strict=True)
        if not artifact.is_relative_to(folder):
            raise ValueError("Unexpected build output path.")
        if ios:
            store_apple.upload(artifact, folder, account, spec)
        else:
            return store_play.deliver(google_token(account), spec, artifact)
    return store_apple.finish(api, app, spec, data["target"], None)


if __name__ == "__main__":
    game_export.install_sigterm_handler()
    try:
        mode = sys.argv[1]
        result = run(mode, json.load(sys.stdin))
        print(json.dumps(result) if mode == "connect" else result, flush=True)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
