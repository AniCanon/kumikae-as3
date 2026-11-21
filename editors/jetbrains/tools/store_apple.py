"""Archive for TestFlight internal testing and follow the exact build through processing."""
import plistlib
import subprocess
import time
from urllib.parse import quote, urlencode

from store_auth import apple_token
from store_http import request

API = "https://api.appstoreconnect.apple.com/v1"


def client(account):
    def call(method, path, body=None):
        return request(method, API + path, body, apple_token(account))
    return call


def app_id(api, identifier):
    result = api("GET", "/apps?" + urlencode({"filter[bundleId]": identifier}))["data"]
    if len(result) != 1:
        raise ValueError("Create an App Store Connect app for this bundle identifier and give the key access.")
    return result[0]["id"]


def resources(api, path):
    while path:
        page = api("GET", path)
        yield from page["data"]
        link = page.get("links", {}).get("next")
        if link and not link.startswith(API + "/"):
            raise ValueError("Invalid TestFlight pagination link.")
        path = link[len(API):] if link else None


def groups(api, app):
    path = "/apps/" + quote(app, safe="") + "/betaGroups?limit=200"
    return [{"id": g["id"], "name": g["attributes"]["name"]}
            for g in resources(api, path) if g["attributes"]["isInternalGroup"]]


def check_archive(info, spec):
    for field, key in (("identifier", "CFBundleIdentifier"), ("version", "CFBundleShortVersionString"),
                       ("build", "CFBundleVersion")):
        if str(info.get(key, "")) != spec[field]:
            raise ValueError(f"The archived {field} differs from the settings you reviewed.")


def _run_xcodebuild(args, step, timeout=1800):
    try:
        subprocess.run(args, check=True, timeout=timeout)
    except subprocess.CalledProcessError as error:
        raise ValueError(f"Xcode {step} failed with exit code {error.returncode}.") from None
    except subprocess.TimeoutExpired:
        raise ValueError(f"Xcode {step} timed out after {timeout} seconds.") from None


def upload(project, folder, account, spec):
    archive = folder / "Game.xcarchive"
    auth = ["-allowProvisioningUpdates", "-authenticationKeyPath", account["file"],
            "-authenticationKeyID", account["keyId"], "-authenticationKeyIssuerID", account["issuer"]]
    print("Archiving for an iOS device…", flush=True)
    _run_xcodebuild(["/usr/bin/xcodebuild", "-project", str(project), "-scheme", project.stem, "-configuration",
                     "Release", "-destination", "generic/platform=iOS", "-archivePath", str(archive),
                     "-derivedDataPath", str(folder / "DerivedData"), *auth, "archive",
                     "DEVELOPMENT_TEAM=" + spec["team"]], "archive")
    with (archive / "Info.plist").open("rb") as source:
        info = plistlib.load(source)["ApplicationProperties"]
    check_archive(info, spec)
    options = folder / "ExportOptions.plist"
    with options.open("wb") as out:
        plistlib.dump(dict(method="app-store-connect", destination="upload", signingStyle="automatic",
                           teamID=spec["team"], manageAppVersionAndBuildNumber=False,
                           testFlightInternalTestingOnly=True), out)
    print("Uploading to TestFlight for internal testing…", flush=True)
    _run_xcodebuild(["/usr/bin/xcodebuild", "-exportArchive", "-archivePath", str(archive), "-exportOptionsPlist",
                     str(options), "-exportPath", str(folder / "upload"), *auth], "upload")


def find_build(api, app, spec):
    filters = {"filter[app]": app, "filter[preReleaseVersion.platform]": "IOS",
               "filter[preReleaseVersion.version]": spec["version"], "filter[version]": spec["build"]}
    builds = api("GET", "/builds?" + urlencode(filters))["data"]
    return builds[0] if builds else None


def finish(api, app, spec, group, build, timeout=1800):
    deadline = time.monotonic() + timeout
    previous = None
    while True:
        state = build["attributes"]["processingState"] if build else "Awaiting processing"
        if state != previous:
            print("TestFlight:", state, flush=True)
            previous = state
        if state == "VALID":
            break
        if state in ("INVALID", "FAILED"):
            raise ValueError("Apple rejected this build during processing. Check App Store Connect for diagnostics.")
        if time.monotonic() >= deadline:
            raise ValueError("Apple is still processing. Use Distribute to testers again to check this same build.")
        time.sleep(15)
        build = find_build(api, app, spec)
    if build["attributes"].get("expired"):
        raise ValueError("This TestFlight build has expired. Increase the build number and try again.")
    group_path = "/betaGroups/" + quote(group, safe="")
    group_info = api("GET", group_path)["data"]["attributes"]
    if not group_info["isInternalGroup"]:
        raise ValueError("Choose an internal testing group.")
    if not group_info.get("hasAccessToAllBuilds"):
        membership = group_path + "/relationships/builds"
        if not any(item["id"] == build["id"] for item in resources(api, membership + "?limit=200")):
            api("POST", membership, {"data": [{"type": "builds", "id": build["id"]}]})
    detail = api("GET", "/builds/" + quote(build["id"], safe="") + "/buildBetaDetail")["data"]["attributes"]
    state = detail.get("internalBuildState", "PROCESSING")
    return f"TestFlight group {group_info['name']} · {spec['version']} ({spec['build']}) · {state.replace('_', ' ').lower()}"
