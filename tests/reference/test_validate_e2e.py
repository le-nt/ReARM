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


def dynamic_elf(kind=23, symbol_index=0, symbol_section=1, symbol_info=0x11):
    """Sectionless ET_DYN; all oracle tables are explicit independent test bytes."""
    data = bytearray(0x300)
    data[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 3, 40, 1, 0x10000, 52, 0, 0, 52, 32, 2, 40, 0, 0)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0x100, 0x10000, 0x10000, 0x200, 0x300, 7, 0x100)
    tags = [(17, 0x100F0), (18, 8), (19, 8), (6, 0x100A0), (11, 16), (5, 0x100E0), (10, 8), (4, 0x10110), (0, 0)]
    struct.pack_into("<IIIIIIII", data, 84, 2, 0x120, 0x10020, 0x10020, len(tags) * 8, len(tags) * 8, 6, 4)
    for i, pair in enumerate(tags):
        struct.pack_into("<II", data, 0x120 + i * 8, *pair)
    struct.pack_into("<IIIBBH", data, 0x1B0, 1, 0x10010, 4, symbol_info, 0, symbol_section)
    data[0x1E0:0x1E8] = b"\0target\0"
    struct.pack_into("<II", data, 0x1F0, 0x10100, (symbol_index << 8) | kind)
    struct.pack_into("<I", data, 0x200, 0x10010)
    struct.pack_into("<IIII", data, 0x210, 1, 2, 1, 0)
    return data


def dynamic_tags(data, extra):
    """Append tags in the existing synthetic PT_DYNAMIC's reserved space."""
    for i, pair in enumerate(extra + [(0, 0)]):
        struct.pack_into("<II", data, 0x160 + i * 8, *pair)
    size = (8 + len(extra) + 1) * 8
    struct.pack_into("<II", data, 84 + 16, size, size)
    return data


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


class SharedFixtureTests(unittest.TestCase):
    def test_bias_rebases_segments_and_defined_symbols(self):
        for bias in (0x10000000, 0x20000000):
            elf = validation.ElfFixture(bytes(dynamic_elf()), bias)
            self.assertEqual(elf.entry, bias + 0x10000)
            self.assertEqual(elf.segments[0][0], bias + 0x10000)
            self.assertEqual(elf.address("target"), bias + 0x10010)

    def test_absolute_and_tls_symbols_keep_values(self):
        absolute = validation.ElfFixture(bytes(dynamic_elf(symbol_section=0xFFF1)), 0x10000000)
        self.assertEqual(absolute.address("target"), 0x10010)
        tls = validation.ElfFixture(bytes(dynamic_elf(symbol_info=0x16)), 0x10000000)
        self.assertEqual(tls.address("target"), 0x10010)

    def test_dynamic_rel_formulas(self):
        bias = 0x10000000
        expected = {23: bias + 0x10010, 2: bias + 0x10010 + 0x10010,
                    3: 0x10010 + 0x10010 - 0x10100, 21: bias + 0x10010, 22: bias + 0x10010}
        for kind, value in expected.items():
            with self.subTest(kind=kind):
                elf = validation.ElfFixture(bytes(dynamic_elf(kind, 0 if kind == 23 else 1)), bias)
                base, contents, memsz, _ = elf.segments[0]
                memory = bytearray(contents) + bytearray(memsz - len(contents))
                elf.relocate(lambda at, size: memory[at - base:at - base + size],
                             lambda at, data: memory.__setitem__(slice(at - base, at - base + len(data)), data))
                self.assertEqual(struct.unpack_from("<I", memory, 0x100)[0], value & 0xFFFFFFFF)

    def test_unknown_relocation_and_unresolved_symbol_rejected(self):
        with self.assertRaisesRegex(validation.FixtureError, "unsupported reference ARM relocation"):
            validation.ElfFixture(bytes(dynamic_elf(255)), 0x10000000)
        with self.assertRaisesRegex(validation.FixtureError, "unresolved reference dynamic symbol"):
            validation.ElfFixture(bytes(dynamic_elf(21, 1, symbol_section=0)), 0x10000000)

    def test_unresolved_weak_symbol_is_zero(self):
        elf = validation.ElfFixture(bytes(dynamic_elf(21, 1, symbol_section=0, symbol_info=0x21)), 0x10000000)
        self.assertEqual(elf.relocations[0]["symbol"], 0)

    def test_bias_alignment_and_overflow_rejected(self):
        for bias in (1, -1, 2**32):
            with self.assertRaises(validation.FixtureError):
                validation.ElfFixture(bytes(dynamic_elf()), bias)
        with self.assertRaises(validation.FixtureError):
            validation.ElfFixture(bytes(dynamic_elf()), 0xFFFF0000)

    def test_executable_nonzero_bias_rejected(self):
        with self.assertRaisesRegex(validation.FixtureError, "ET_EXEC"):
            validation.ElfFixture(bytes(elf_bytes()), 0x10000000)


class LinkedFixtureTests(unittest.TestCase):
    def module(self, name, bias, *, info=0x11, section=1, visibility=0, extra=()):
        data = dynamic_elf(21, 1, symbol_section=section, symbol_info=info)
        data[0x1BD] = visibility
        if extra:
            dynamic_tags(data, list(extra))
        return name, bytes(data), bias

    def test_main_first_strong_definition_preempts_default_visibility(self):
        fixture = validation.LinkedFixture([self.module("main", 0x10000000), self.module("provider", 0x20000000)])
        self.assertEqual([r["symbol"] for r in fixture.relocations], [0x10010010, 0x10010010])

    def test_strong_definition_beats_earlier_weak_definition(self):
        fixture = validation.LinkedFixture([self.module("main", 0x10000000, info=0x21), self.module("provider", 0x20000000)])
        self.assertEqual([r["symbol"] for r in fixture.relocations], [0x20010010, 0x20010010])

    def test_protected_definition_keeps_own_binding(self):
        fixture = validation.LinkedFixture([self.module("main", 0x10000000), self.module("provider", 0x20000000, visibility=3)])
        self.assertEqual([r["symbol"] for r in fixture.relocations], [0x10010010, 0x20010010])

    def test_hidden_definition_is_not_exported_to_consumer(self):
        with self.assertRaisesRegex(validation.FixtureError, "unresolved reference"):
            validation.LinkedFixture([self.module("main", 0x10000000, section=0), self.module("provider", 0x20000000, visibility=2)])

    def test_symbolic_definition_keeps_own_binding(self):
        fixture = validation.LinkedFixture([self.module("main", 0x10000000), self.module("provider", 0x20000000, extra=[(16, 0)])])
        self.assertEqual([r["symbol"] for r in fixture.relocations], [0x10010010, 0x20010010])

    def test_needed_dependency_initializes_first(self):
        fixture = validation.LinkedFixture([self.module("main", 0x10000000, extra=[(1, 1)]), self.module("target", 0x20000000)])
        self.assertEqual(fixture.init_order, [1, 0])

    def test_missing_dependency_and_overlapping_mappings_rejected(self):
        with self.assertRaisesRegex(validation.FixtureError, "missing reference DT_NEEDED"):
            validation.LinkedFixture([self.module("main", 0x10000000, extra=[(1, 1)])])
        with self.assertRaisesRegex(validation.FixtureError, "overlap"):
            validation.LinkedFixture([self.module("main", 0x10000000), self.module("provider", 0x10000000)])

    def test_dependency_spec_preserves_windows_path(self):
        path, bias = validation.dependency_spec(r"C:\fixture\libprovider.so@0x20000000")
        self.assertEqual(str(path), r"C:\fixture\libprovider.so")
        self.assertEqual(bias, 0x20000000)
        for invalid in ("libprovider.so", "libprovider.so@1", "@0x20000000"):
            with self.assertRaises(validation.FixtureError):
                validation.dependency_spec(invalid)

    @unittest.skipUnless(importlib.util.find_spec("unicorn"), "optional Unicorn package unavailable")
    def test_constructor_entry_register_reset_and_aggregate_budget(self):
        data = dynamic_tags(dynamic_elf(), [(12, 0x10000)])
        data[0x100:0x104] = bytes.fromhex("1eff2fe1")  # ARM bx lr.
        fixture = validation.ElfFixture(bytes(data), 0x10000000)
        state = validation.reference_state(fixture, "0x10010000", [], 2, seed=123)
        self.assertEqual(state["registers"][0], 123)
        self.assertEqual(state["reference"]["instructions"], 2)
        self.assertEqual(state["reference"]["initializers"], [0x10010000])
        with self.assertRaisesRegex(validation.FixtureError, "aggregate instruction budget"):
            validation.reference_state(fixture, "0x10010000", [], 1, seed=123)

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
                                     browser=False, max_instructions=1000000, node="node", fixture=None, shared=False,
                                     load_bias=0, dispatch="iterative", dependency=[], initialize="yes",
                                     cpsr_mask=validation.DEFAULT_CPSR_MASK)

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

    def test_report_replace_retries_and_preserves_pending(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "metrics.json"
            original = Path.replace
            attempts = 0
            def flaky(source, target):
                nonlocal attempts
                attempts += 1
                if attempts < 3:
                    raise PermissionError("reader lock")
                return original(source, target)
            with mock.patch.object(Path, "replace", flaky), mock.patch.object(validation.time, "sleep"):
                validation.write_report(destination, {"passed": True})
            self.assertEqual(attempts, 3)
            self.assertTrue(json.loads(destination.read_text())["passed"])
            with mock.patch.object(Path, "replace", side_effect=PermissionError("reader lock")) as replacement, mock.patch.object(validation.time, "sleep"):
                with self.assertRaisesRegex(validation.FixtureError, "pending report preserved"):
                    validation.write_report(destination, {"passed": False})
                self.assertEqual(replacement.call_count, 5)
            pending = list(Path(directory).glob("*.pending.json"))
            self.assertEqual(len(pending), 1)
            self.assertFalse(json.loads(pending[0].read_text())["passed"])


if __name__ == "__main__":
    unittest.main()
