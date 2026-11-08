#!/usr/bin/env python3
"""Negative real-ELF gates for the explicit libc-memory import profile.

SPDX-License-Identifier: Apache-2.0
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ("rearm", "sla", "clang", "fixture", "stub-dir"):
        p.add_argument("--" + key, required=True)
    p.add_argument("--out", type=Path, default=Path("build/libc-negative"))
    p.add_argument("--allocator-fixture")
    args = p.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("validation", root / "tools/validate_e2e.py")
    validation = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validation)
    report = {"passed": False, "checks": []}

    def rejected(name, source, expected, runtime="libc-memory", bias="0x10000000"):
        output = args.out / (name + ".cpp")
        output.unlink(missing_ok=True)
        command = [args.rearm, "--input", str(source), "--sla", args.sla,
                   "--load-bias", bias, "--entry", "fixture_entry", "--output", str(output)]
        if runtime:
            command += ["--runtime", runtime]
        result = subprocess.run(command, text=True, capture_output=True, timeout=30)
        ok = result.returncode != 0 and expected in result.stderr and not output.exists()
        report["checks"].append({"name": name, "passed": ok, "command": command,
            "input_sha256": hashlib.sha256(Path(source).read_bytes()).hexdigest(),
            "exit_code": result.returncode, "stderr": result.stderr})
        if not ok:
            raise RuntimeError(f"negative profile gate {name} failed: {result.stderr}")

    try:
        rejected("profile-disabled", args.fixture, "missing DT_NEEDED 'libc.so'", runtime=False)
        source = args.out / "unknown.c"
        source.write_text("extern unsigned rearm_unknown_import(unsigned); unsigned fixture_entry(unsigned x){return rearm_unknown_import(x);}\n")
        elf = args.out / "unknown.so"
        report["unknown_build"] = validation.run_measured([args.clang, "--target=armv7a-linux-gnueabi", "-marm", "-mfloat-abi=soft",
            "-nostdlib", "-fPIC", "-shared", "-fuse-ld=lld", str(source), "-L" + args.stub_dir,
            "-Wl,--no-as-needed", "-l:libc.so", "-o", str(elf)])
        rejected("unknown-import", elf, "Unresolved ELF symbol 'rearm_unknown_import'")
        fixture = validation.ElfFixture(Path(args.fixture).read_bytes(), defer_unresolved=True)
        first_page = min(at for at, _, size, _ in fixture.segments if size) & -4096
        collision_bias = 0xffd00000 - first_page
        rejected("callback-overlap", args.fixture, "overlaps libc-memory callback arena", bias=hex(collision_bias))
        rejected("stack-overlap", args.fixture, "overlaps reserved guest stack", bias=hex(0x7fef0000-first_page))
        rejected("sentinel-overlap", args.fixture, "overlaps reserved return sentinel", bias=hex(0xffff0000-first_page))
        if args.allocator_fixture:
            rejected("allocator-profile-disabled", args.allocator_fixture, "Unresolved ELF symbol")
            allocator = validation.ElfFixture(Path(args.allocator_fixture).read_bytes(), defer_unresolved=True)
            first = min(at for at, _, size, _ in allocator.segments if size) & -4096
            for arena, name in ((0x60000000,"heap"),(0x70000000,"errno")):
                rejected(name+"-overlap", args.allocator_fixture, "overlaps libc-basic heap/errno arena",
                         runtime="libc-basic", bias=hex(arena-first))
        report["passed"] = True
    finally:
        validation.write_report(args.out / "metrics.json", report)
    print(json.dumps({"passed": report["passed"], "checks": len(report["checks"])}))


if __name__ == "__main__":
    main()
