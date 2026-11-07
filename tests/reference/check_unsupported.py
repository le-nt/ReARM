#!/usr/bin/env python3
"""Require deterministic compile-time diagnostics for unsupported SLEIGH userops.

SPDX-License-Identifier: Apache-2.0
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("validate_e2e", ROOT / "tools/validate_e2e.py")
validation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validation)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rearm", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--sla", required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/negative")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {"cases": [], "passed": False}
    for mode in ("arm", "thumb"):
        elf = args.out / f"unsupported_svc-{mode}.elf"
        generated = args.out / f"unsupported_svc-{mode}.cpp"
        compilation = validation.run_measured([
            args.clang, "--target=armv7-none-eabi", "-march=armv7-a", f"-m{mode}", "-O2",
            "-ffreestanding", "-nostdlib", "-fuse-ld=lld",
            f"-Wl,-T,{ROOT / 'tests/fixtures/early_pipeline.ld'}",
            str(ROOT / "tests/fixtures/unsupported_svc.c"), "-o", str(elf),
        ])
        command = [args.rearm, "--input", str(elf), "--entry", "fixture_entry", "--output", str(generated),
                   "--sla", args.sla, "--observe", "fixture_output:64"]
        try:
            rejection = validation.run_measured(command)
        except validation.PipelineStepError as error:
            rejection = error.result
        diagnostic = rejection["stderr"]
        passed = (rejection["exit_code"] != 0
                  and "unsupported userop software_interrupt" in diagnostic
                  and bool(re.search(r"guest 0x[0-9a-f]+ (ARM|Thumb)", diagnostic, re.IGNORECASE)))
        report["cases"].append({"mode": mode, "compile": compilation, "rejection": rejection, "passed": passed})
        (args.out / "metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    report["passed"] = len(report["cases"]) == 2 and all(case["passed"] for case in report["cases"])
    (args.out / "metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "cases": len(report["cases"]), "report": str(args.out / 'metrics.json')}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (validation.FixtureError, OSError) as error:
        print(f"check_unsupported: {error}", file=sys.stderr)
        raise SystemExit(2)
