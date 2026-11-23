import unittest

from by_name_gate import failures


class GateTests(unittest.TestCase):
    def test_outliers_do_not_hide_slower_medians(self):
        times = {"node/read": {"native": [1, 12, 13], "interpreter": [9, 10, 20], "C#": [9, 10, 20]}}
        self.assertEqual(len(failures(times, times)), 2)

    def test_tolerance_includes_the_boundary(self):
        times = {"node/read": {"native": [103], "interpreter": [100], "C#": [100]}}
        self.assertEqual(failures(times, times), [])

    def test_alias_compares_both_modes_with_main_and_managed(self):
        modes = {"native": [12], "interpreter": [12], "C#": [10]}
        main = {"native": [10], "interpreter": [10]}
        self.assertEqual(len(failures({"alias/write": modes}, {"alias/write": main})), 4)

    def test_proven_compares_with_its_typed_control(self):
        modes = {"native": [12], "interpreter": [12]}
        times = {"proven/read": modes, "typed/read": {"native": [10], "interpreter": [10]}}
        self.assertEqual(len(failures(times, times)), 2)


if __name__ == "__main__":
    unittest.main()
