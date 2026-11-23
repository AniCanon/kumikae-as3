import unittest

from by_name_gate import failures


class GateTests(unittest.TestCase):
    def test_compares_each_distribution_and_each_mode(self):
        times = {"node/read": {"native": [8, 9], "interpreter": [5, 6], "C#": [4, 5]}}
        self.assertEqual(len(failures(times)), 2)
        times["node/read"]["native"] = [5, 6]
        self.assertEqual(failures(times), [])

    def test_proven_local_compares_to_typed_in_each_mode(self):
        one = {"native": [5, 6], "interpreter": [5, 6], "C#": [5, 6]}
        times = {"proven/read": one, "typed/read": {**one, "native": [2, 3]}}
        self.assertEqual(len(failures(times)), 1)


if __name__ == "__main__":
    unittest.main()
