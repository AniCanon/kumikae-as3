import unittest

from run_bench import PACK_TOLERANCE, over_tolerance


class GateTests(unittest.TestCase):
    def test_pack_is_held_to_its_tolerance(self):
        self.assertIsNone(over_tolerance("pack", 100 * (1 + PACK_TOLERANCE), 100))
        self.assertIn("pack", over_tolerance("pack", 100 * (1 + PACK_TOLERANCE) + 1, 100))

    def test_other_rows_are_reported_only(self):
        self.assertIsNone(over_tolerance("check", 300, 100))
        self.assertIsNone(over_tolerance("run", 300, 100))


if __name__ == "__main__":
    unittest.main()
