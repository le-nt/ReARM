#!/usr/bin/env python3
"""Build and differentially execute public ELF imports using libc-memory profile.

SPDX-License-Identifier: Apache-2.0
The generated libc.so is link metadata only. Its BX LR stubs are never loaded
or executed by the production runner or independent ARM reference.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("clang", "cxx", "rearm", "sla"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--emxx")
    parser.add_argument("--node", default="node")
    parser.add_argument("--browser", action="store_true")
    parser.add_argument("--native-only", action="store_true")
    parser.add_argument("--allocator", action="store_true")
    parser.add_argument("--cases", type=int, default=4)
    parser.add_argument("--out", type=Path, default=Path("build/libc-e2e"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("validation", root / "tools/validate_e2e.py")
    validation = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validation)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    source = root / "tests/fixtures/libc_imports.c"
    names = ("memcpy", "memmove", "memset", "memcmp", "strlen", "strnlen", "strcmp", "strncmp", "strcpy", "strncpy")
    if args.allocator:
        names += ("malloc", "calloc", "realloc", "free", "__errno")
    stub = args.out / "link-metadata.S"
    stub.write_text(".syntax unified\n.arm\n.text\n" + "".join(
        f".global {name}\n.type {name},%function\n{name}:\n bx lr\n.size {name},.-{name}\n" for name in names))
    report = {"schema_version": 1, "complete": False, "passed": False,
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "suites": []}
    common = [args.clang, "--target=armv7a-linux-gnueabi", "-mfloat-abi=soft", "-nostdlib", "-fPIC", "-shared", "-fuse-ld=lld",
              "-Wl,--hash-style=sysv,--pack-dyn-relocs=none", "-Wl,-z,max-page-size=4096,-z,common-page-size=4096"]
    try:
        report["link_metadata_build"] = validation.run_measured(common + [str(stub), "-Wl,-soname,libc.so", "-o", str(args.out / "libc.so")])
        for mode in ("arm", "thumb"):
            for optimization in ("O0", "O2"):
                out = args.out / f"{mode}-{optimization}"
                out.mkdir(parents=True, exist_ok=True)
                elf = out / "libfixture.so"
                suite = {"mode": mode, "optimization": optimization}
                report["suites"].append(suite)
                suite["build"] = validation.run_measured(common + ["-m" + mode, "-" + optimization, "-fno-builtin", *(["-DALLOCATOR_IMPORTS"] if args.allocator else []), str(source),
                    "-Wl,-soname,libfixture.so", "-L" + str(args.out), "-Wl,--no-as-needed", "-l:libc.so", "-o", str(elf)])
                suite["elf_sha256"] = hashlib.sha256(elf.read_bytes()).hexdigest()
                command = [sys.executable, str(root / "tools/validate_link.py"), "--input", str(elf), "--runtime", "libc-basic" if args.allocator else "libc-memory",
                    "--cases", str(args.cases), "--rearm", args.rearm, "--sla", args.sla, "--cxx", args.cxx,
                    "--out", str(out / "execution"), "--node", args.node]
                if args.emxx:
                    command += ["--emxx", args.emxx]
                if args.native_only:
                    command += ["--native-only"]
                if args.browser:
                    command += ["--browser"]
                suite["validation"] = validation.run_measured(command)
                suite["result"] = json.loads((out / "execution/metrics.json").read_text())
                if not suite["result"]["passed"]:
                    raise RuntimeError(f"libc validation failed for {mode}-{optimization}")
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
