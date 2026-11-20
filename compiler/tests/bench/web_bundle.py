"""Measure every file in a clean Godot Web export, optionally comparing the same game in two builds."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import zlib


def chunks(stream):
    return iter(lambda: stream.read(1024 * 1024), b"")


def file_sizes(path, brotli, cache):
    with path.open("rb") as source:
        digest = hashlib.sha256()
        for block in chunks(source):
            digest.update(block)
        key = digest.digest()
        if key in cache:
            return cache[key]
        raw_size = source.tell()
        source.seek(0)
        gzip = zlib.compressobj(level=9, wbits=31)
        gzip_size = sum(len(gzip.compress(block)) for block in chunks(source)) + len(gzip.flush())
        sizes = [raw_size, gzip_size]
        if brotli:
            source.seek(0)
            with subprocess.Popen([brotli, "-q", "11", "-c"], stdin=source, stdout=subprocess.PIPE) as encoder:
                sizes.append(sum(map(len, chunks(encoder.stdout))))
                if encoder.wait():
                    raise ValueError(f"Brotli could not encode {path}")
    cache[key] = sizes
    return sizes


def measure(folder, brotli, cache):
    files = {}
    if not folder.is_dir() or not any(folder.glob("*.html")) or not any(folder.glob("*.wasm")):
        raise ValueError(f"Expected a Web export containing HTML and WebAssembly in {folder}")
    for path in sorted(folder.rglob("*")):
        if not path.is_file():
            continue
        files[str(path.relative_to(folder))] = file_sizes(path, brotli, cache)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export", type=Path)
    parser.add_argument("--against", type=Path, help="baseline export directory")
    parser.add_argument("--brotli", action="store_true", help="also measure Brotli quality 11 (requires brotli on PATH)")
    args = parser.parse_args()
    brotli = shutil.which("brotli") if args.brotli else None
    if args.brotli and not brotli:
        parser.error("Brotli measurement requires brotli on PATH")
    cache = {}
    totals = []
    for folder in [args.against, args.export] if args.against else [args.export]:
        try:
            files = measure(folder, brotli, cache)
        except (ValueError, OSError, subprocess.CalledProcessError) as error:
            parser.exit(1, str(error) + "\n")
        print(folder)
        print("file\traw bytes\tgzip -9 bytes" + ("\tBrotli -11 bytes" if brotli else ""))
        for name, sizes in files.items():
            print(name, *sizes, sep="\t")
        total = [sum(column) for column in zip(*files.values())]
        print("TOTAL", *total, sep="\t")
        totals.append(total)
    if args.against:
        print("Change", *(f"{new - old:+d} ({100 * (new - old) / old:+.2f}%)" if old else f"{new:+d}"
                          for old, new in zip(*totals)), sep="\t")


if __name__ == "__main__":
    main()
