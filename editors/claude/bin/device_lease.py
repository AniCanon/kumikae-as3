"""Serialize foreground test and launch operations on the same simulator or emulator."""
from contextlib import contextmanager
import fcntl
from hashlib import sha256
from pathlib import Path
import sys


@contextmanager
def device_lease(device, directory=None):
    if not device:
        yield
        return
    root = directory or Path.home() / "Library/Caches/org.kumikae.as3/device-locks"
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    path = root / sha256(device.encode()).hexdigest()
    # A stable file keeps waiting processes on the same inode. Exit releases the OS lock.
    with path.open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("Waiting for the other Kumikae operation on this device…", file=sys.stderr, flush=True)
            fcntl.flock(lock, fcntl.LOCK_EX)
        yield
