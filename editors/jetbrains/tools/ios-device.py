#!/usr/bin/env python3
"""Build, install and follow an iOS app using installed Godot and Xcode tools."""
import argparse
import base64
import hashlib
from contextlib import contextmanager
import fcntl
import os
from pathlib import Path
import re
import plistlib
import subprocess
import sys
import tempfile

# Isolated Python excludes the script directory. Import the staging helper shipped beside this file.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_export import run_export, stage_game, stage_ios_runtime


@contextmanager
def build_lock(output):
    output.mkdir(parents=True, exist_ok=True)
    with (output / "run.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("An iPhone build or install is already running for this project.")
        yield


def certificate_teams(identities, certificates, subject_reader):
    usable = set()
    for identity in identities:
        if "Apple Development:" in identity:
            match = re.search(r"\b[0-9A-Fa-f]{40}\b", identity)
            if match:
                usable.add(match.group().upper())
    teams = set()
    for certificate in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", certificates, re.S):
        encoded = certificate.replace("-----BEGIN CERTIFICATE-----", "").replace("-----END CERTIFICATE-----", "")
        digest = hashlib.sha1(base64.b64decode(encoded)).hexdigest().upper()
        if digest in usable:
            match = re.search(r"(?:^|,)OU=([A-Z0-9]{10})(?:,|$)", subject_reader(certificate).strip())
            if match:
                teams.add(match.group(1))
    return sorted(teams)


def local_signing_teams():
    identities = subprocess.run(["/usr/bin/security", "find-identity", "-v", "-p", "codesigning"],
                                capture_output=True, text=True, check=True, timeout=15).stdout.splitlines()
    certificates = subprocess.run(["/usr/bin/security", "find-certificate", "-a", "-p"],
                                  capture_output=True, text=True, check=True, timeout=15).stdout
    def subject(certificate):
        return subprocess.run(["/usr/bin/openssl", "x509", "-noout", "-subject", "-nameopt", "RFC2253"],
                              input=certificate, capture_output=True, text=True, check=True, timeout=15).stdout
    return certificate_teams(identities, certificates, subject)


def signing_team(preset, available_teams):
    chosen = preset.strip()
    if chosen and chosen != "ABCDE12345":
        if not re.fullmatch(r"[A-Z0-9]{10}", chosen):
            raise ValueError("The signing team must be a 10-character Apple team identifier.")
        return chosen
    teams = set(available_teams())
    if len(teams) == 1:
        return teams.pop()
    raise ValueError("Choose a device signing team in the Godot iOS preset settings.")


def prepare_preset(text, section, template, team, bundle):
    if not re.fullmatch(r"preset\.\d+", section):
        raise ValueError("Choose an iOS export preset.")
    sections = re.split(r"(?m)^[ \t]*(\[[^\]\n]+\])[^\S\n]*$", text)
    header = f"[{section}.options]"
    if f"[{section}]" not in sections:
        raise ValueError("The selected iOS export preset is missing.")
    if header not in sections:
        sections[-1] = sections[-1].rstrip("\n") + "\n"
        sections += [header, "\n"]
    options = sections.index(header) + 1
    values = {
        "custom_template/debug": '"' + template.replace('\\', '\\\\').replace('"', '\\"') + '"',
        "application/app_store_team_id": '"' + team + '"',
        "application/bundle_identifier": '"' + bundle + '"',
        "application/export_project_only": "true",
        "application/code_sign_identity_debug": '"Apple Development"',
        "application/provisioning_profile_uuid_debug": '""',
    }
    body = sections[options]
    for key, value in values.items():
        body = re.sub(r"(?m)^" + re.escape(key) + r"=.*\n?", "", body)
        body += "\n" + key + "=" + value + "\n"
    sections[options] = body
    return "".join(sections)


def run(command, timeout=1800):
    subprocess.run([str(value) for value in command], check=True, timeout=timeout)


def build(args, output):
    project = Path(args.project).resolve()
    team = signing_team(args.team, local_signing_teams)
    if not args.godot:
        raise ValueError("Install Godot to export the game.")
    if args.template and not Path(args.template).is_file():
        raise ValueError("The selected Godot preset's debug template is missing.")
    with tempfile.TemporaryDirectory(prefix="kumikae-ios-device-") as temporary:
        stage = Path(temporary) / "game"
        stage_game(project, stage, None)
        preset = stage / "export_presets.cfg"
        preset.write_text(prepare_preset(preset.read_text(), args.section, args.template, team, args.bundle))
        if args.library:
            stage_ios_runtime(stage, Path(args.library).parent)
        elif not (stage / "bin/ios/kas3_godot.xcframework").is_dir():
            raise ValueError("The iOS AS3 runtime is missing from the game.")
        print("Importing game resources…", flush=True)
        run([args.godot, "--headless", "--path", stage, "--editor", "--quit-after", "120", "--max-fps", "60"])
        print("Exporting for iPhone…", flush=True)
        run_export([args.godot, "--headless", "--path", stage, "--language", "en",
                    "--export-debug", args.preset, output / "Game.ipa"])
    print("Building and signing for the selected iPhone…", flush=True)
    run(["/usr/bin/xcrun", "xcodebuild", "-project", output / "Game.xcodeproj", "-scheme", "Game",
         "-configuration", "Debug", "-sdk", "iphoneos", "-destination", "id=" + (args.destination or args.device),
         "-derivedDataPath", output / "build", "-allowProvisioningUpdates", "-allowProvisioningDeviceRegistration",
         "DEVELOPMENT_TEAM=" + team, "CODE_SIGN_STYLE=Automatic", "CODE_SIGN_IDENTITY=Apple Development",
         "PROVISIONING_PROFILE_SPECIFIER=", "-quiet", "build"])
    return output / "build/Build/Products/Debug-iphoneos/Game.app"


def install_and_launch(args):
    output = Path(args.project).resolve() / ".godot/ios-device"
    with build_lock(output), tempfile.TemporaryDirectory(dir=output) as temporary:
        app = Path(args.app).expanduser().resolve() if args.app else build(args, Path(temporary))
        if not app.is_dir() or app.suffix != ".app":
            raise ValueError("The signed iOS .app folder does not exist: " + str(app))
        with (app / "Info.plist").open("rb") as stream:
            if plistlib.load(stream).get("CFBundleIdentifier") != args.bundle:
                raise ValueError("The app's bundle identifier differs from the run configuration.")
        print("Installing on the selected iOS device…", flush=True)
        run(["/usr/bin/xcrun", "devicectl", "device", "install", "app", "--device", args.device, app], 180)
    print("Launching with console output. Stop ends this run.", flush=True)
    os.execv("/usr/bin/xcrun", ["xcrun", "devicectl", "device", "process", "launch", "--device", args.device,
                              "--console", "--terminate-existing", args.bundle])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("project", "device", "bundle"):
        parser.add_argument("--" + name, required=True)
    for name in ("godot", "template", "library", "team", "app", "destination", "preset", "section"):
        parser.add_argument("--" + name, default="")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9.-]+", args.bundle):
        parser.error("The bundle identifier contains invalid characters.")
    try:
        install_and_launch(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("iPhone run failed: " + str(error), file=sys.stderr)
        print("Check Xcode signing, trust this Mac, unlock the iPhone and enable Developer Mode.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
