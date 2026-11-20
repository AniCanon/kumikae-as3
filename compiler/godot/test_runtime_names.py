"""The Web runtime accepts the same dynamic signal spellings as the compiler."""
from pathlib import Path
import unittest

import runtime_names


class RuntimeNamesTest(unittest.TestCase):
    def test_names_come_from_the_checked_api(self):
        table = Path(__file__).with_name("godot_names.txt")
        with table.open() as source:
            members, emit = runtime_names.select(source)
        self.assertEqual(members["objectIDSelected"], "object_id_selected")
        self.assertEqual(members["treeEntered"], "tree_entered")
        self.assertNotIn("tree_entered", members)
        self.assertEqual(members["data"], "data")  # JSON.data can hold a signal.
        self.assertEqual(emit[:2], ("Object", "emit_signal"))
        self.assertTrue(emit[2].isdigit())
