#!/usr/bin/env python3
import importlib.machinery
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[1] / 'tests/reference_input.py'
module = importlib.machinery.SourceFileLoader('hall_input', str(source)).load_module()


class DebounceTests(unittest.TestCase):
    def test_quick_close_open_cancels(self):
        d = module.Debounce(2)
        self.assertFalse(d.update(True, 0))
        self.assertFalse(d.update(False, 1.9))
        self.assertFalse(d.update(False, 3))

    def test_stable_close_and_immediate_open(self):
        d = module.Debounce(2)
        self.assertFalse(d.update(True, 10))
        self.assertFalse(d.update(True, 11.99))
        self.assertTrue(d.update(True, 12))
        self.assertFalse(d.update(False, 12.01))

    def test_duplicate_active_does_not_extend_deadline(self):
        d = module.Debounce(2)
        for t in [0, .2, .8, 1.5]:
            self.assertFalse(d.update(True, t))
        self.assertTrue(d.update(True, 2))

    def test_reclose_starts_a_fresh_deadline(self):
        d = module.Debounce(2)
        d.update(True, 0)
        d.update(False, 1)
        self.assertFalse(d.update(True, 1.1))
        self.assertFalse(d.update(True, 2))
        self.assertTrue(d.update(True, 3.1))

    def test_zero_delay_preserves_switch_semantics(self):
        d = module.Debounce(0)
        self.assertTrue(d.update(True, 0))
        self.assertFalse(d.update(False, .001))


if __name__ == '__main__':
    unittest.main()
