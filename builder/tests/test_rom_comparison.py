"""Location-hint tests use synthetic bytes only."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.compare_roms import compare
from emerald3ds_builder.recipe import Recipe


class ComparisonTests(unittest.TestCase):
    def test_relocations_missing_and_ambiguous_ranges_are_distinct(self):
        reference = bytes(range(128))
        candidate = b'\xff' * 64 + reference[:64] + b'\xff' * 64
        recipe = Recipe(engine_abi=0, rom_sha1='', release='test', entries=[
            {'path': 'graphics/test', 'ops': [['C', 0, 64], ['C', 64, 32], ['C', 96, 4], ['L', 0, 4]]}])
        report = compare(reference, candidate, recipe)
        stats = report['domains']['graphics']
        self.assertEqual(stats['matched_copy_bytes'], 64)
        self.assertEqual(stats['relocated_copy_bytes'], 64)
        self.assertEqual(stats['missing_copy_bytes'], 32)
        self.assertEqual(stats['short_copy_bytes'], 4)
        self.assertEqual(stats['other_operations'], 1)
        self.assertEqual(report['entries_with_missing_ranges'][0]['missing_ranges'],
                         [{'offset': 64, 'length': 32}])


if __name__ == '__main__':
    unittest.main()
