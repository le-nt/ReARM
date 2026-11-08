#!/usr/bin/env python3
"""Build public ARM/Thumb JNI_OnLoad DSOs and validate static native/WASM output.

SPDX-License-Identifier: Apache-2.0
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("clang", "cxx", "rearm", "sla"):
        parser.add_argument("--" + key, required=True)
    parser.add_argument("--emxx")
    parser.add_argument("--node", default="node")
    parser.add_argument("--browser", action="store_true")
    parser.add_argument("--native-only", action="store_true")
    parser.add_argument("--registered", action="store_true", help="register and invoke a static native method")
    parser.add_argument("--stack", action="store_true", help="exercise six-jint native registration and stack marshalling")
    parser.add_argument("--out", type=Path, default=Path("build/jni-e2e"))
    args = parser.parse_args()
    if args.stack and not args.registered:
        parser.error("--stack requires --registered")
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("validation", root / "tools/validate_e2e.py")
    validation = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validation)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    source = root / "tests/fixtures/jni_onload.c"
    report = {"schema_version": 1, "complete": False, "passed": False,
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "suites": []}
    try:
        for mode in ("arm", "thumb"):
            for optimization in ("O0", "O2"):
                out = args.out / f"{mode}-{optimization}"
                out.mkdir(parents=True, exist_ok=True)
                elf = out / "libjni.so"
                suite = {"mode": mode, "optimization": optimization}
                report["suites"].append(suite)
                suite["build"] = validation.run_measured([args.clang, "--target=armv7a-none-eabi", "-m" + mode,
                    "-" + optimization, "-mfloat-abi=soft", "-fPIC", "-nostdlib", "-fuse-ld=lld", "-Wl,-shared,-soname,libjni.so",
                    *(["-DREGISTER_NATIVE"] if args.registered else []), *(["-DREGISTER_STACK"] if args.stack else []), str(source), "-o", str(elf)])
                suite["elf_sha256"] = hashlib.sha256(elf.read_bytes()).hexdigest()
                command = [sys.executable, str(root / "tools/validate_link.py"), "--input", str(elf),
                    "--entry", "JNI_OnLoad", "--jni-onload", "--cases", "4" if args.registered else "1", "--rearm", args.rearm,
                    "--sla", args.sla, "--cxx", args.cxx, "--out", str(out / "execution"), "--node", args.node]
                if args.emxx:
                    command += ["--emxx", args.emxx]
                if args.registered:
                    command += ["--jni-registered"]
                if args.stack:
                    command += ["--jni-stack"]
                if args.native_only:
                    command += ["--native-only"]
                if args.browser:
                    command += ["--browser"]
                suite["validation"] = validation.run_measured(command)
                suite["result"] = json.loads((out / "execution/metrics.json").read_text())
                if not suite["result"]["passed"]:
                    raise RuntimeError(f"JNI validation failed for {mode}-{optimization}")
                validation.write_report(args.out / "metrics.json", report)
        report["complete"] = True
        report["passed"] = True
    except BaseException as error:
        report["failure"] = {"type": type(error).__name__, "message": str(error)}
        if isinstance(error, validation.PipelineStepError):
            report["failure"]["process"] = error.result
        raise
    finally:
        validation.write_report(args.out / "metrics.json", report)
    print(json.dumps({"passed": report["passed"], "suites": len(report["suites"]), "report": str(args.out / "metrics.json")}))


if __name__ == "__main__":
    main()
