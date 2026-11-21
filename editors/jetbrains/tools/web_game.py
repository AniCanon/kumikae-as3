"""Export a single-threaded Godot Web game and preview its files on loopback."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import signal
import sys
import tempfile
import webbrowser

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_export


def export_stage(stage, native, godot, preset, output):
    game_export.validate_web_preset(stage, preset)
    libraries = game_export.stage_web_runtime(stage, native)
    product = output / "index.html"
    game_export.run_export([godot, "--headless", "--path", str(stage), "--export-debug", preset, str(product)])
    if not product.is_file() or not any((output / name).is_file() for name in libraries):
        raise ValueError("Missing Web AS3 runtime in the export. Enable GDExtension support and disable threads in the Web preset.")
    return product


def export_project(project, native, godot, preset, output):
    with tempfile.TemporaryDirectory(prefix="kumikae-web-") as tmp:
        stage = Path(tmp) / "game"
        game_export.stage_game(project, stage, None)
        return export_stage(stage, native, godot, preset, output)


def build(project, native, godot, preset):
    base = project / ".godot/kumikae-builds"
    base.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(dir=base, prefix="web-"))
    try:
        return export_project(project, native, godot, preset, output)
    except BaseException:
        shutil.rmtree(output)
        raise


class PreviewHandler(SimpleHTTPRequestHandler):
    def log_request(self, code="-", size="-"):
        if isinstance(code, int) and code >= 400:
            super().log_request(code, size)


def server_for(product):
    handler = partial(PreviewHandler, directory=str(product.parent))
    return ThreadingHTTPServer(("127.0.0.1", 0), handler)


def preview(product):
    with server_for(product) as server:
        url = f"http://127.0.0.1:{server.server_port}/{product.name}"
        print(url, flush=True)
        webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            return


def run(project, native, godot, preset):
    def stop(signum, frame):
        raise SystemExit(128 + signum)

    previous = signal.signal(signal.SIGTERM, stop)
    try:
        with tempfile.TemporaryDirectory(prefix="kumikae-preview-") as output:
            preview(export_project(project, native, godot, preset, Path(output)))
    finally:
        signal.signal(signal.SIGTERM, previous)


if __name__ == "__main__":
    try:
        project, native, godot, preset = sys.argv[1:]
        run(Path(project), Path(native), godot, preset)
    except (ValueError, OSError) as error:
        sys.exit(str(error))
