"""Concurrent sessions share a device only after its active verification finishes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BIN = Path(__file__).resolve().parents[1] / "bin"


class DeviceLeaseTest(unittest.TestCase):
    def test_one_device_waits_while_another_can_proceed(self):
        sys.path.insert(0, str(BIN))
        from device_lease import device_lease
        with tempfile.TemporaryDirectory() as tmp:
            script = ("from device_lease import device_lease\n"
                      "import sys\nfrom pathlib import Path\n"
                      "print('ready', flush=True)\n"
                      "with device_lease(sys.argv[1], Path(sys.argv[2])): print('acquired', flush=True)")
            with device_lease("A", Path(tmp)):
                with device_lease("B", Path(tmp)):
                    child = subprocess.Popen([sys.executable, "-c", script, "A", tmp], cwd=BIN,
                                             stdout=subprocess.PIPE, text=True)
                    self.assertEqual("ready", child.stdout.readline().strip())
                    with self.assertRaises(subprocess.TimeoutExpired):
                        child.wait(timeout=0.2)
            output, _ = child.communicate(timeout=3)
            self.assertIn("acquired", output)
            self.assertEqual(0, child.returncode)
