import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('mapping', Path(__file__).resolve().parents[1] / 'case_mapping.py')
mapping = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mapping)

class CaseMappingTest(unittest.TestCase):
    def test_case_navigation_preserves_source_and_method(self):
        case = {'source': 'tests/All.as', 'cases': [
            {'source': 'tests/Course Test.as', 'suite': 'CourseTest', 'name': 'gap jump', 'method': 'gapJump'}]}
        self.assertEqual(mapping.location(Path('/project'), case, 'CourseTest', 'gap jump'),
                         'file:///project/tests/Course%20Test.as?method=gapJump')

    def test_duplicate_or_invalid_mappings_fail(self):
        good = {'source':'tests/T.as', 'suite':'T', 'name':'one', 'method':'one'}
        for cases in [[good,good], [dict(good,method='bad()')], [dict(good,name='a::b')]]:
            with self.assertRaises(ValueError): mapping.validate({'cases':cases})

    def test_parameter_cases_can_share_one_method(self):
        mapping.validate({'cases': [
            {'source': 'T.as', 'suite': 'T', 'name': 'empty', 'method': 'score'},
            {'source': 'T.as', 'suite': 'T', 'name': 'existing', 'method': 'score'}]})
