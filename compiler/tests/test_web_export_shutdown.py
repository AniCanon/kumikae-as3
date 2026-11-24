"""Only Godot 4.7's known documentation shutdown diagnostic is tolerated on a successful export."""
import subprocess
import unittest

from test_web_export import check_process

SINGLETON = 'ERROR: Parameter "singleton" is null.\n   at: is_cmdline_mode (editor/editor_node.cpp:6618)\n'
THREAD = ("WARNING: A Thread object is being destroyed without its completion having been realized.\n"
          "Please call wait_to_finish() on it to ensure correct cleanup.\n"
          "   at: ~Thread (drivers/apple/thread_apple.cpp:150)\n")


class ExportShutdown(unittest.TestCase):
    def test_known_shutdown(self):
        for stderr in ("", SINGLETON, SINGLETON + THREAD):
            check_process(subprocess.CompletedProcess([], 0, "", stderr))

    def test_other_errors_and_aborts_fail(self):
        for code, stderr in [(1, SINGLETON), (-6, SINGLETON), (0, THREAD), (0, SINGLETON + "ERROR: export failed\n"),
                             (0, SINGLETON.replace("is_cmdline_mode", "another_function"))]:
            with self.subTest(code=code, stderr=stderr), self.assertRaises(AssertionError):
                check_process(subprocess.CompletedProcess([], code, "", stderr))
