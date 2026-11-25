"""Run the station's system tests and compare every checkpoint of the first watch."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "bench"))
from run_game_bench import check, stage_project

KIT = HERE.parents[1] / "testing"


def scene(godot, project, name, env, *args, frames=6):
    run = subprocess.run([godot, "--headless", "--path", project, "--fixed-fps", "60",
                          "--quit-after", str(frames), name, *args], env=env,
                         capture_output=True, text=True, timeout=600)
    report = run.stdout + run.stderr
    check(name, run.returncode == 0 and "ERROR:" not in report and "WARNING:" not in report, report)
    return run.stdout.splitlines()


def main():
    compiler = Path(sys.argv[1]).resolve()
    godot = os.getenv("KAS3_GODOT_DOTNET")
    if not godot:
        print("Lantern Reach needs KAS3_GODOT_DOTNET and the built Godot extension.")
        return 77
    env = dict(os.environ, KAS3_NATIVE_PLAY="0")
    env.pop("KAS3_PROGRAM_LIBRARY", None)
    with tempfile.TemporaryDirectory() as scratch:
        project = stage_project(compiler.parent, godot, Path(scratch).resolve())
        tests = dict(env, KAS3_TESTS="1", KAS3_TEST_SOURCES=os.pathsep.join(str(KIT / tier) for tier in ("as3", "godot")))
        for lang in ("as3", "cs"):
            scene(godot, project, "res://main.tscn", env, "--", "--lang", lang)
            scene(godot, project, f"res://scenes/{lang}.tscn", env)
            steps = scene(godot, project, "res://tests/menu.tscn", tests, "--", "--lang", lang, frames=40)
            check(f"{lang} menu walk", "PASS menu walk" in steps, steps)
        scene(godot, project, "res://scenes/overlay.tscn", env)
        subprocess.run([sys.executable, KIT / "runner.py", project / "kas3-tests.json",
                        "--compiler", compiler, "--godot", godot, "--require-pass"], env=env, check=True)
        expected = None
        for lang in ("as3", "cs"):
            lines = scene(godot, project, f"res://tests/{lang}.tscn", tests)
            for line in lines:
                if line.startswith("PASS "):
                    print(line)
            result, = [json.loads(line) for line in lines if line.startswith("{")]
            checks = result["checksums"]
            check(lang, result["night"] == 2 and result["tick"] == 18001 and result["lit"] > 0
                  and result["entries"] == 1 and result["unlocked"] and result["restored"] == checks[-1]
                  and len(checks) == 301, result)
            if expected is not None:
                check("night replay", checks == expected, f"{lang} diverged from AS3")
            expected = checks
            print(f"{lang}: night 1 dawn, {len(checks)} checkpoints, restored {checks[-1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
