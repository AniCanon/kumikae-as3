"""By-name access inside headless Godot. Each build uses its own extension, compiler and runtime headers."""
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from native_godot import Runner, USED


def check(label, ok, detail):
    if not ok:
        sys.exit(f"{label}: {detail}")


def benchmark(arguments, native):
    godot = os.environ.get("KAS3_GODOT") or shutil.which("godot")
    builds = [Path(arguments[0]).resolve()]
    if "--against" in arguments:
        builds.append(Path(arguments[arguments.index("--against") + 1]).resolve())
    runs = int(arguments[arguments.index("--runs") + 1]) if "--runs" in arguments else 7
    cases = ["script"] if "--script-only" in arguments else ["script", "inherited", "node", "value", "refused"]
    with tempfile.TemporaryDirectory() as folder:
        commands = []
        for i, build in enumerate(builds):
            tests = build.parent / "tests"
            where = Path(folder) / str(i)
            runner = Runner(godot, tests / "godot_project", tests, build / "kas3c", check)
            project = runner.project(where, HERE / "languages", ["Untyped.as", "untyped.gd", str(HERE.parent / "godot_native" / "project.godot")])
            env = dict(os.environ, KAS3_NATIVE_PLAY="0")
            env.pop("KAS3_PROGRAM_LIBRARY", None)
            if native:
                env["KAS3_PROGRAM_LIBRARY"] = runner.library("untyped", project, ["Untyped.as"], where)
            cmd = [godot, "--headless", "--path", project, "-s", "untyped.gd"]
            if "--script-only" in arguments:
                cmd += ["--", "script-only"]
            commands.append((cmd, env))
        samples = {}
        for turn in range(runs):
            for build, (cmd, env) in zip(builds, commands):
                run = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=120)
                output = run.stdout + run.stderr
                rows = re.findall(r"^NAMED (\w+) (\d+)$", output, re.M)
                check(str(build), run.returncode == 0 and (USED in output) == native and [r[0] for r in rows] == cases, output)
                for case, micros in rows:
                    samples.setdefault((build, case), []).append(int(micros) / 1000)
        for (build, case), times in samples.items():
            print(f"{build} {case:8} {statistics.median(times):8.3f} ms  spread {max(times)-min(times):.3f} ms ({runs} runs, {min(times):.3f} to {max(times):.3f})")
