"""Assemble a local Claude marketplace from Kumikae's native release artifacts."""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import tempfile

PLUGIN = Path(__file__).resolve().parent
ROOT = PLUGIN.parents[1]


def configure_starter_web(starter, native):
    wasm = Path("web/libkas3_godot.wasm")
    if (native / wasm).is_file():
        return

    descriptor = starter / "kas3.gdextension"
    descriptor.write_text("".join(line for line in descriptor.read_text().splitlines(keepends=True)
                                  if not line.startswith("web.wasm32 =")))
    presets = starter / "export_presets.cfg"
    # Godot numbers presets consecutively. Keep each preset together with its options.
    sections = re.split(r"(?m)^\[preset\.\d+\]\n", presets.read_text())
    kept = [block for block in sections[1:] if 'platform="Web"' not in block.splitlines()]
    presets.write_text(sections[0] + "".join(
        f"[preset.{index}]\n" + re.sub(r"(?m)^\[preset\.\d+\.options\]$", f"[preset.{index}.options]", block)
        for index, block in enumerate(kept)))


def assemble(native, destination):
    destination = Path(os.path.abspath(destination))
    if os.path.lexists(destination):
        raise ValueError(f"{destination} already exists. Choose a new marketplace directory.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        market = Path(temporary) / "market"
        package = market / "plugins/kumikae"
        for name in ("bin", "skills", ".claude-plugin"):
            shutil.copytree(PLUGIN / name, package / name, ignore=shutil.ignore_patterns("__pycache__"))
        for name in (".lsp.json", "README.md"):
            shutil.copy2(PLUGIN / name, package / name)
        kit = package / "kit"
        kit.mkdir()
        for name in ("kas3c", "libkas3_godot.dylib"):
            shutil.copy2(native / name, kit / name)
        for name in ("ios", "android", "web"):
            if (native / name).is_dir():
                shutil.copytree(native / name, kit / name)
        template = native / "ios-with-arm64-simulator.zip"
        if template.is_file():
            shutil.copy2(template, kit)
        shutil.copytree(ROOT / "testing", kit / "testing",
                        ignore=shutil.ignore_patterns("tests", "__pycache__", "*.uid"))
        tools = kit / "tools"
        tools.mkdir()
        for name in ("android_run.py", "game_export.py", "web_game.py", "build-ios-simulator.sh"):
            shutil.copy2(ROOT / "editors/jetbrains/tools" / name, tools / name)
        starter = kit / "starter"
        starter.mkdir()
        example = ROOT / "examples/coin-dash"
        for pattern in ("*.tscn", "*.gdextension", "project.godot", "*.png", "export_presets.cfg", "kas3-tests.json"):
            for file in example.glob(pattern):
                shutil.copy2(file, starter)
        configure_starter_web(starter, native)
        shutil.copytree(example / "src", starter / "src")
        shutil.copytree(example / "assets", starter / "assets")
        shutil.copytree(example / "tests", starter / "tests", ignore=shutil.ignore_patterns("*.uid"))
        shutil.copy2(ROOT / "LICENSE", package)
        shutil.copy2(ROOT / "compiler/godot/NOTICE.md", package / "GODOT-NOTICE.md")
        manifest = {"name": "anicanon", "owner": {"name": "AniCanon"},
                    "metadata": {"description": "AS3 game development tools for Godot"},
                    "plugins": [{"name": "kumikae", "source": "./plugins/kumikae"}]}
        metadata = market / ".claude-plugin"
        metadata.mkdir()
        (metadata / "marketplace.json").write_text(json.dumps(manifest, indent=2) + "\n")
        for file in (kit / "kas3c", package / "bin/kumikae", tools / "build-ios-simulator.sh"):
            file.chmod(file.stat().st_mode | 0o755)
        destination.mkdir()
        market.rename(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path, help="Extracted Kumikae native release directory")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        assemble(args.native.resolve(), args.destination)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + "\n")
