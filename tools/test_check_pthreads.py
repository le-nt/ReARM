# SPDX-License-Identifier: Apache-2.0
"""Regression tests for truthful M0 pthread result validation."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("check_pthreads", Path(__file__).with_name("check_pthreads.py"))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class OutputTests(unittest.TestCase):
    def lines(self):
        return ["hello from pthread 2", "hello from pthread 0", "hello from pthread 3",
                "hello from pthread 1", "counter=400000 OK"]

    def test_real_worker_order_is_not_fixed(self):
        self.assertTrue(probe.check_output(self.lines(), 0)["passed"])

    def test_counter_alone_does_not_pass(self):
        with self.assertRaisesRegex(RuntimeError, "exactly four workers"):
            probe.check_output(["counter=400000 OK"], 0)

    def test_duplicate_worker_does_not_pass(self):
        with self.assertRaisesRegex(RuntimeError, "exactly four workers"):
            probe.check_output(self.lines() + ["hello from pthread 1"], 0)

    def test_bad_atomic_count_does_not_pass(self):
        with self.assertRaisesRegex(RuntimeError, "atomic counter"):
            probe.check_output(self.lines()[:-1] + ["counter=399999 FAIL"], 0)

    def test_missing_or_duplicate_counter_does_not_pass(self):
        for lines in (self.lines()[:-1], self.lines() + ["counter=400000 OK"]):
            with self.subTest(lines=lines), self.assertRaisesRegex(RuntimeError, "atomic counter"):
                probe.check_output(lines, 0)

    def test_nonzero_exit_does_not_pass(self):
        with self.assertRaisesRegex(RuntimeError, "exited with 1"):
            probe.check_output(self.lines(), 1)


if __name__ == "__main__":
    unittest.main()
