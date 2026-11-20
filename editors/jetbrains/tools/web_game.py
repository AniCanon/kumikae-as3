"""Export a single-threaded Godot Web game and preview its files on loopback."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import sys
import tempfile
import webbrowser

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_export


def export_stage(stage, native, godot, preset, output):
    game_export.validate_web_preset(stage, preset)
    library = game_export.stage_web_runtime(stage, native)
    base = [godot, "--headless", "--path", str(stage)]
    game_export.run_export(base + ["--import"])
    product = output / "index.html"
    game_export.run_export(base + ["--export-debug", preset, str(product)])
    if not product.is_file() or not (output / library).is_file():
        raise ValueError("Missing Web AS3 runtime in the export. Enable GDExtension support and disable threads in the Web preset.")
    return product


def build(project, native, godot, preset):
    base = project / ".godot/kumikae-builds"
    base.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(dir=base, prefix="web-"))
    try:
        with tempfile.TemporaryDirectory(prefix="kumikae-web-") as tmp:
            stage = Path(tmp) / "game"
            game_export.stage_game(project, stage, None)
            return export_stage(stage, native, godot, preset, output)
    except BaseException:
        shutil.rmtree(output)
        raise


class PreviewHandler(SimpleHTTPRequestHandler):
    def log_request(self, code="-", size="-"):
        if int(code) >= 400:
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


if __name__ == "__main__":
    try:
        project, native, godot, preset = sys.argv[1:]
        preview(build(Path(project), Path(native), godot, preset))
    except (ValueError, OSError) as error:
        sys.exit(str(error))
