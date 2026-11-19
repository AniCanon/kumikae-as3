"""Commit a signed AAB to the application's internal testing track."""
import hashlib
from urllib.parse import quote
from store_http import request

API = "https://androidpublisher.googleapis.com/androidpublisher/v3"
UPLOAD = "https://androidpublisher.googleapis.com/upload/androidpublisher/v3"


def internal_track(tracks):
    candidates = {t["track"] for t in tracks if t.get("track") in ("internal", "qa")}
    if len(candidates) == 1:
        return candidates.pop()
    if candidates:
        raise ValueError("Play returned two possible internal tracks. Confirm the destination in Play Console.")
    raise ValueError("Set up the app and its internal testing track in Play Console first.")


def deliver(token, spec, artifact, api=request):
    app = "/applications/" + quote(spec["identifier"], safe="") + "/edits"
    edit = api("POST", API + app, {}, token)["id"]
    path = app + "/" + quote(edit, safe="")
    editing = API + path
    committed = False
    try:
        tracks = api("GET", editing + "/tracks", token=token).get("tracks", [])
        name = internal_track(tracks)
        track = next(item for item in tracks if item["track"] == name)
        code = str(spec["build"])
        releases = track.get("releases", [])
        if any(r["status"] != "completed" for r in releases):
            raise ValueError("An unfinished release exists on the internal track. Resolve it in Play Console and retry.")
        if any(int(v) > int(code) for r in releases for v in r.get("versionCodes", [])):
            raise ValueError("The internal track already has a newer version code.")
        bundles = api("GET", editing + "/bundles", token=token).get("bundles", [])
        existing = next((b for b in bundles if str(b["versionCode"]) == code), None)
        if existing:
            digest = hashlib.sha256()
            with artifact.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest().lower() != existing.get("sha256", "").lower():
                raise ValueError("This version code belongs to a different bundle. Increase it and build again.")
        else:
            print("Uploading signed bundle to Google Play…", flush=True)
            uploaded = api("POST", UPLOAD + path + "/bundles?uploadType=media", artifact, token)
            if str(uploaded["versionCode"]) != code:
                raise ValueError("The bundle version code differs from the reviewed build.")
        if any(r["status"] == "completed" and code in r.get("versionCodes", []) for r in releases):
            return "This build is already on the internal testing track."
        api("PUT", editing + "/tracks/" + name,
            {"track": name, "releases": [{"status": "completed", "versionCodes": [code]}]}, token)
        api("POST", editing + ":commit", {}, token)
        committed = True
        return f"Google Play accepted version code {code} on the internal testing track."
    finally:
        if not committed:
            try:
                api("DELETE", editing, token=token)
            except (OSError, ValueError):
                pass
