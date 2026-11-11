"""Locate the Godot 4.7 binary the spike runs against."""
import os
import shutil
import subprocess
import sys

MESSAGE = "Godot 4.7 not found: set KAS3_GODOT"
PROJECT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "project")


def godot_path() -> str:
    path = os.environ.get("KAS3_GODOT") or shutil.which("godot")
    version = ""
    if path and os.path.exists(path):
        version = subprocess.run([path, "--version"], capture_output=True, text=True).stdout.strip()
    if not version.startswith("4.7."):
        sys.exit(MESSAGE)
    return path


def ensure_imported() -> None:
    """Godot only discovers extensions and scripts during an editor import."""
    subprocess.run([godot_path(), "--headless", "--path", PROJECT, "--import"],
                   capture_output=True, text=True, timeout=600)
