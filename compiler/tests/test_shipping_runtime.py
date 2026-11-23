"""The production runtime and extension have no entry point for the test counters."""
from pathlib import Path
import sys

for file in map(Path, sys.argv[1:]):
    image = file.read_bytes()
    for marker in (b"KAS3_NATIVE_COUNTS", b"KAS3_REPORT_PREPARED", b"kas3: counts thunks="):
        if marker in image:
            raise SystemExit(f"{file.name} contains {marker.decode()}")
print("shipping runtime: OK")
