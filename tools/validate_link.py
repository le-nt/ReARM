#!/usr/bin/env python3
"""Compare real linked ARM DSOs and constructors on native/WASM backends.

SPDX-License-Identifier: Apache-2.0
Inputs are prebuilt source fixtures, with explicit module paths and load biases.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

_spec = importlib.util.spec_from_file_location("validate_e2e", Path(__file__).with_name("validate_e2e.py"))
validation = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(validation)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rearm", required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--load-bias", type=lambda x: int(x, 0), default=0x10000000)
    parser.add_argument("--dependency", action="append", default=[], help="PATH@LOAD_BIAS; repeat in main-first lookup order")
    parser.add_argument("--sla", required=True)
    parser.add_argument("--entry", default="fixture_entry")
    parser.add_argument("--observe", default="fixture_output:64")
    parser.add_argument("--initialize", choices=("yes", "no"), default="yes")
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--emxx")
    parser.add_argument("--node", default="node")
    parser.add_argument("--browser", action="store_true")
    parser.add_argument("--native-only", action="store_true")
    parser.add_argument("--jni-onload", action="store_true", help="finite public JNI fixture validation profile")
    parser.add_argument("--jni-registered", action="store_true", help="public registered static native callback fixture")
    parser.add_argument("--jni-stack", action="store_true", help="six-jint registered native fixture with stack arguments")
    parser.add_argument("--runtime", choices=("libc-memory", "libc-basic"))
    parser.add_argument("--codegen", choices=("monolithic", "functions"), default="monolithic")
    parser.add_argument("--cases", type=int, default=4)
    parser.add_argument("--max-instructions", type=int, default=1000000)
    parser.add_argument("--out", type=Path, default=Path("build/link-e2e"))
    args = parser.parse_args()
    if args.jni_stack and not args.jni_registered:
        raise validation.FixtureError("--jni-stack requires --jni-registered")
    extra_args = [7, 0xFFFFFFFF, 0x80000000, 13, 29] if args.jni_stack else []
    signature = "(" + "I" * (1 + len(extra_args)) + ")I"
    if args.jni_registered and not args.jni_onload:
        raise validation.FixtureError("--jni-registered requires --jni-onload")
    case_seeds = validation.seeds(args.cases)
    if not args.native_only and not args.emxx:
        raise validation.FixtureError("--emxx is required unless --native-only is selected")
    if args.browser and args.native_only:
        raise validation.FixtureError("--browser cannot be combined with --native-only")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    if args.runtime and (args.jni_onload or args.dependency):
        raise validation.FixtureError("libc oracle currently validates a standalone main DSO without JNI")
    libc_oracle = None
    if args.runtime:
        spec = importlib.util.spec_from_file_location("libc_reference", Path(__file__).with_name("allocator_reference.py" if args.runtime=="libc-basic" else "libc_reference.py"))
        service = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(service)
        libc_oracle = service.ReferenceBasic if args.runtime=="libc-basic" else service.ReferenceLibc
        fixture = validation.ElfFixture(args.input.read_bytes(), args.load_bias, defer_unresolved=True)
        if fixture.needed != ["libc.so"]:
            raise validation.FixtureError("libc oracle fixture must declare exactly DT_NEEDED libc.so")
        fixture.bind(libc_oracle().resolve)
    else:
        fixture = validation.load_fixture(args.input, args.load_bias, args.dependency)
    observed = [validation.parse_range(args.observe, fixture)]
    generated = args.out / "linked.cpp"
    native = args.out / ("linked.exe" if sys.platform == "win32" else "linked")
    module = args.out / "linked.js"
    report_path = args.out / "metrics.json"
    report = {"schema_version": 1, "complete": False, "passed": False, "steps": [], "cases": [], "artifacts": [],
              "modules": [{"path": str(args.input), "load_bias": args.load_bias,
                           "sha256": hashlib.sha256(args.input.read_bytes()).hexdigest()}]}
    for dependency in args.dependency:
        path, bias = validation.dependency_spec(dependency)
        report["modules"].append({"path": str(path), "load_bias": bias, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})

    def checkpoint():
        validation.write_report(report_path, report)

    def stage(name, command, artifacts=()):
        report["active"] = {"stage": name, "command": command}
        checkpoint()
        result = validation.run_measured(command)
        result["stage"] = name
        if name == "translate":
            result["translation"] = validation.translation_metrics(result["stdout"], result["elapsed_seconds"])
        report["steps"].append(result)
        for path in artifacts:
            contents = path.read_bytes()
            report["artifacts"].append({"path": str(path), "size_bytes": len(contents), "sha256": hashlib.sha256(contents).hexdigest()})
        checkpoint()
        return result

    checkpoint()
    try:
        command = [args.rearm, "--input", str(args.input), "--load-bias", hex(args.load_bias), "--entry", args.entry,
                   "--observe", args.observe, "--output", str(generated), "--sla", args.sla, "--initialize", args.initialize,
                   "--max-instructions", str(args.max_instructions), "--codegen", args.codegen]
        for dependency in args.dependency:
            command += ["--dependency", dependency]
        if args.jni_onload:
            command += ["--jni", "onload"]
        if args.runtime:
            command += ["--runtime", args.runtime]
        if args.jni_registered:
            command += ["--jni-class", "fixture/Bridge", "--jni-native", f"fixture/Bridge|transform|{signature}|static",
                        "--jni-invoke", f"fixture/Bridge|transform|{signature}"]
        stage("translate", command, (generated,))
        runtime = (["-I", str(root), str(root / "runtime/jni32.cpp"), str(root / "runtime/aapcs32.cpp")] if args.jni_onload or args.runtime else [])
        if args.runtime:
            runtime += [str(root / "runtime/libc_memory32.cpp"), str(root / "runtime/libc_imports32.cpp")]
        if args.runtime=="libc-basic":
            runtime += [str(root / "runtime/heap32.cpp"), str(root / "runtime/libc_allocator32.cpp")]
        stage("native_compile", [args.cxx, str(generated), *runtime, "-std=c++20", "-O2", "-o", str(native)], (native,))
        if not args.native_only:
            stage("wasm_compile", [args.emxx, str(generated), *runtime, "-std=c++20", "-O2", "-sMODULARIZE=1",
                  "-sEXPORT_NAME=createRearmModule", "-sENVIRONMENT=node,web", "-sEXIT_RUNTIME=0", "-sINVOKE_RUN=0",
                  "-sEXPORTED_RUNTIME_METHODS=callMain", "-sALLOW_MEMORY_GROWTH=1", "-o", str(module)],
                  (module, module.with_suffix(".wasm")))
        for seed in case_seeds:
            report["active"] = {"stage": "reference", "seed": seed}
            checkpoint()
            host = libc_oracle() if libc_oracle else None
            if args.jni_onload:
                spec = importlib.util.spec_from_file_location("jni_reference", Path(__file__).with_name("jni_reference.py"))
                service = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(service)
                host = service.ReferenceJni(args.jni_registered, extra_args)
            expected = validation.reference_state(fixture, args.entry, observed, args.max_instructions, seed, args.initialize == "yes", host)
            if args.jni_onload:
                trace = [1030, 4, 219, 6, 228, 17, 228]
                trace += [6, 113, 215, 4, 4] if args.jni_registered else [4]
                if host.trace != trace:
                    raise validation.FixtureError(f"unexpected JNI reference trace: {host.trace}")
                expected["reference"]["jni_slots"] = host.trace
            if libc_oracle:
                if set(host.trace) != set(host.names):
                    raise validation.FixtureError(f"libc fixture did not execute all declared imports: {host.trace}")
                expected["reference"]["libc_calls"] = host.trace
            case = {"seed": seed, "reference": expected, "backends": {}}
            report["cases"].append(case)
            commands = [("native", [str(native), str(seed), *map(str, extra_args)])]
            if not args.native_only:
                commands.append(("node", [args.node, str(root / "tests/reference/run_wasm.cjs"), str(module), str(seed), *map(str, extra_args)]))
            for backend, command in commands:
                measured = stage(backend, command)
                actual = validation.parse_runner_output(measured["stdout"])
                differences = validation.compare_states(expected, actual)
                case["backends"][backend] = {"actual": actual, "differences": differences, "passed": not differences}
                checkpoint()
            if args.browser:
                report["active"] = {"stage": "chromium", "seed": seed}
                checkpoint()
                actual, elapsed = validation.browser_state(module, seed, extra_args)
                differences = validation.compare_states(expected, actual)
                case["backends"]["chromium"] = {"actual": actual, "differences": differences, "passed": not differences,
                                                   "elapsed_seconds": elapsed}
                checkpoint()
        backends = {"native"} if args.native_only else {"native", "node"}
        if args.browser:
            backends.add("chromium")
        report["complete"] = len(report["cases"]) == len(case_seeds) and all(set(case["backends"]) == backends for case in report["cases"])
        report["passed"] = report["complete"] and all(backend["passed"] for case in report["cases"] for backend in case["backends"].values())
        report.pop("active", None)
    except BaseException as error:
        report["failure"] = {"type": type(error).__name__, "message": str(error)}
        if isinstance(error, validation.PipelineStepError):
            report["failure"]["process"] = error.result
        raise
    finally:
        checkpoint()
    print(json.dumps({"passed": report["passed"], "cases": len(report["cases"]), "report": str(report_path)}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (validation.FixtureError, OSError, ValueError) as error:
        print(f"validate_link: {error}", file=sys.stderr)
        raise SystemExit(2)
