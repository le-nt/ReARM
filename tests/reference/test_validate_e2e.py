# SPDX-License-Identifier: Apache-2.0
"""Dependency-free checks of the independent reference reader and comparator."""
import importlib.util
from pathlib import Path
import struct
import json
import tempfile
import types
import sys
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("validate_e2e", Path(__file__).resolve().parents[2] / "tools/validate_e2e.py")
validation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)


def elf_bytes():
    data = bytearray(0x104)
    data[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 40, 1, 0x10000, 52, 0, 0, 52, 32, 1, 40, 0, 0)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0x100, 0x10000, 0x10000, 4, 8, 5, 0x100)
    data[0x100:] = bytes.fromhex("1eff2fe1")
    return data


def state():
    return {"registers": [0] * 16, "cpsr": 0x10, "memory": [{"address": 0x11000, "bytes": "12345678"}]}


class FixtureReaderTests(unittest.TestCase):
    def test_offset_is_not_virtual_address(self):
        elf = validation.ElfFixture(bytes(elf_bytes()))
        self.assertEqual(elf.entry, 0x10000)
        self.assertEqual(elf.segments, [(0x10000, bytes.fromhex("1eff2fe1"), 8, 5)])

    def test_truncation_is_rejected(self):
        with self.assertRaises(validation.FixtureError):
            validation.ElfFixture(bytes(elf_bytes()[:-1]))

    def test_wrong_machine_is_rejected(self):
        data = elf_bytes()
        struct.pack_into("<H", data, 18, 3)
        with self.assertRaises(validation.FixtureError):
            validation.ElfFixture(bytes(data))

    def test_filesz_over_memsz_is_rejected(self):
        data = elf_bytes()
        struct.pack_into("<I", data, 52 + 20, 3)
        with self.assertRaises(validation.FixtureError):
            validation.ElfFixture(bytes(data))


class StateComparisonTests(unittest.TestCase):
    def test_identical_state(self):
        self.assertEqual(validation.compare_states(state(), state()), [])

    def test_register_mismatch(self):
        actual = state()
        actual["registers"][9] = 1
        self.assertIn("r9:", validation.compare_states(state(), actual)[0])

    def test_flags_and_mode_are_checked(self):
        for bit in (31, 30, 29, 28, 5):
            actual = state()
            actual["cpsr"] ^= 1 << bit
            self.assertTrue(validation.compare_states(state(), actual))

    def test_exception_level_is_ignored_by_default(self):
        actual = state()
        actual["cpsr"] = 0x13
        self.assertEqual(validation.compare_states(state(), actual), [])

    def test_missing_observed_memory_is_not_a_pass(self):
        actual = state()
        actual["memory"] = []
        self.assertTrue(validation.compare_states(state(), actual))

    def test_memory_byte_mismatch(self):
        actual = state()
        actual["memory"][0]["bytes"] = "12345679"
        self.assertIn("memory[0x00011003]", validation.compare_states(state(), actual)[0])

    def test_overlap_rejected(self):
        actual = state()
        actual["memory"].append({"address": 0x11000, "bytes": "12"})
        with self.assertRaises(validation.FixtureError):
            validation.canonical_state(actual)

    def test_register_range_rejected(self):
        actual = state()
        actual["registers"][0] = -1
        with self.assertRaises(validation.FixtureError):
            validation.canonical_state(actual)

    def test_seed_generation_reproducible(self):
        self.assertEqual(validation.seeds(4), [0, 1, 0xFFFFFFFF, 0x80000000])
        self.assertEqual(validation.seeds(20), validation.seeds(20))
        self.assertTrue(all(0 <= x < 2**32 for x in validation.seeds(100)))

    def test_wrong_state_shapes_have_diagnostics(self):
        for actual in ([], None, {**state(), "memory": {}}, {**state(), "memory": ["bad"]}):
            with self.assertRaises(validation.FixtureError):
                validation.canonical_state(actual)


class PipelineReliabilityTests(unittest.TestCase):
    def args(self, directory):
        return types.SimpleNamespace(rearm="rearm", clang="clang", cxx="clang++", emxx="em++", sla="ARM.sla",
                                     out=Path(directory), metrics_out=None, cases=1, modes=["arm"], optimizations=["2"],
                                     browser=False, max_instructions=1000000, node="node", fixture=None, cpsr_mask=validation.DEFAULT_CPSR_MASK)

    def test_zero_cases_rejected_before_subprocess(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(directory)
            args.cases = 0
            with mock.patch.object(validation, "run_measured") as run:
                with self.assertRaises(validation.FixtureError):
                    validation.pipeline(args)
                run.assert_not_called()

    def test_compile_failure_persists_process_evidence(self):
        failure = {"command": ["clang"], "exit_code": 17, "stdout": "partial output", "stderr": "intentional error"}
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(validation, "run_measured", side_effect=validation.PipelineStepError(failure.copy())):
                with self.assertRaises(validation.PipelineStepError):
                    validation.pipeline(self.args(directory))
            report = json.loads((Path(directory) / "metrics.json").read_text())
            self.assertFalse(report["complete"])
            self.assertFalse(report["passed"])
            self.assertEqual(report["steps"][0]["stdout"], "partial output")
            self.assertEqual(report["steps"][0]["stderr"], "intentional error")
            self.assertEqual(report["active"]["stage"], "arm_compile")

    def test_interrupt_preserves_completed_case(self):
        def interrupted(args, root, out, report, checkpoint):
            report["cases"].append({"fixture": "arm-o2", "seed": 0, "reference": state(),
                                    "backends": {"native": {"actual": state(), "passed": True}}})
            report["active"] = {"stage": "node"}
            checkpoint()
            raise KeyboardInterrupt()
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(validation, "run_pipeline_body", side_effect=interrupted):
                with self.assertRaises(KeyboardInterrupt):
                    validation.pipeline(self.args(directory))
            report = json.loads((Path(directory) / "metrics.json").read_text())
            self.assertEqual(report["cases"][0]["reference"]["registers"], [0] * 16)
            self.assertEqual(report["failure"]["type"], "KeyboardInterrupt")
            self.assertFalse(report["passed"])

    def test_incomplete_backend_set_cannot_pass(self):
        def incomplete(args, root, out, report, checkpoint):
            report["cases"].append({"backends": {"native": {"passed": True}}})
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(validation, "run_pipeline_body", side_effect=incomplete):
                self.assertEqual(validation.pipeline(self.args(directory)), 1)
            report = json.loads((Path(directory) / "metrics.json").read_text())
            self.assertFalse(report["complete"])
            self.assertFalse(report["passed"])

    def test_translation_rate_uses_reported_unique_count(self):
        metrics = validation.translation_metrics('{"instructions": 80, "pcode_ops": 320}', 2.0)
        self.assertEqual(metrics["instructions_per_second"], 40)
        self.assertEqual(metrics["pcode_ops_per_second"], 160)

    def test_translation_rate_not_fabricated(self):
        self.assertNotIn("instructions_per_second", validation.translation_metrics('{"instructions": "80"}', 2.0))
        self.assertNotIn("instructions_per_second", validation.translation_metrics('not JSON', 2.0))

    def test_command_requires_argv_array(self):
        for command in ("clang --version", {"program": "clang"}, [], [7]):
            with self.assertRaises(validation.FixtureError):
                validation.run_measured(command)

    def test_measured_process_output_and_failure(self):
        result = validation.run_measured([sys.executable, "-c", "print('measurement')"])
        self.assertEqual(result["stdout"].strip(), "measurement")
        self.assertEqual(result["exit_code"], 0)
        self.assertGreater(result["elapsed_seconds"], 0)
        with self.assertRaises(validation.PipelineStepError) as failure:
            validation.run_measured([sys.executable, "-c", "import sys; print('before failure'); sys.exit(9)"])
        self.assertEqual(failure.exception.result["exit_code"], 9)
        self.assertEqual(failure.exception.result["stdout"].strip(), "before failure")


if __name__ == "__main__":
    unittest.main()
