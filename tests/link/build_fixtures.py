#!/usr/bin/env python3
"""Build public ARM DSOs with real ELF linker metadata; no Android SDK required.

SPDX-License-Identifier: Apache-2.0
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    args.out.mkdir(parents=True, exist_ok=True)
    common = [args.clang, "--target=armv7a-linux-gnueabi", "-march=armv7-a",
              "-mfloat-abi=soft", "-fuse-ld=lld", "-nostdlib", "-fPIC", "-shared",
              "-O1", "-Wl,--hash-style=sysv,--pack-dyn-relocs=none",
              "-Wl,-z,max-page-size=4096,-z,common-page-size=4096"]
    commands = []
    for name, mode, dependency in [("provider", "thumb", None), ("consumer", "arm", "provider"),
                                   ("interposer", "arm", "provider"), ("weak", "arm", None),
                                   ("strong", "thumb", None), ("hidden_consumer", "arm", "provider")]:
        output = args.out / f"lib{name}.so"
        command = common + [f"-m{mode}", str(source / f"{name}.c"),
                            f"-Wl,-soname,lib{name}.so", "-o", str(output)]
        if dependency:
            command += [f"-L{args.out}", "-Wl,--no-as-needed", f"-l{dependency}"]
        subprocess.run(command, check=True)
        commands.append({"command": command, "sha256": hashlib.sha256(output.read_bytes()).hexdigest()})
    (args.out / "build-manifest.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
    print(f"Built {len(commands)} public ARM shared-library fixtures")


if __name__ == "__main__":
    main()
