# Executed test evidence

## Initial ELF checkpoint — 2026-11-07

Environment: Windows, LLVM MinGW 20261104, Clang 23.1.5, C++20.

```text
clang++ -std=c++20 -Wall -Wextra -Werror -Irecomp recomp/elf/elf_image.cpp tests/elf/elf_image_test.cpp -o elf_tests.exe
elf_tests.exe
PASS 30 ELF loader/relocation tests
```

The executable compiled and exited zero. This validates deterministic synthetic ELF and
relocation cases only. SLEIGH compilation, generated C++, WASM execution, Linux builds,
browser execution and Roblox compatibility have not yet been tested at this checkpoint.

## Early end-to-end checkpoint — 2026-11-07

- SLEIGH v12.2 / Ghidra 12.2 native static library built with LLVM MinGW.
- Current ELF suite: **39 cases PASS**, also run through CTest.
- Python reference/comparator/reliability suite: **22 tests PASS**.
- ARM source fixtures compiled with `clang --target=armv7-none-eabi` in ARM and Thumb,
  at `-O0` and `-O2`, then lifted by the production C++ SLEIGH integration.
- Generated self-contained C++ compiled by native Clang and Emscripten 6.0.12.
- Four seeds per variant: **16 cases PASS** across native execution, Node WASM and
  headless Chromium WASM (**48 backend comparisons**). Every case compares all 16
  registers, CPSR mask `0xf0000020`, and the 64-byte guest output buffer with Unicorn 2.1.4.
- Emitter uses static generated operations and iterative dispatch; no CPU emulator,
  p-code interpreter or runtime instruction decoder is in translated production output.

Representative command (paths resolved to installed local tools in the executed run):

```text
python tools/validate_e2e.py --rearm build/recomp/rearm.exe --clang clang --cxx clang++ --emxx third_party/emsdk/upstream/emscripten/em++.exe --sla third_party/sla/ARM/ARM7_le.sla --out build/e2e --cases 4 --browser
python -m unittest discover -s tests/reference -p test_*.py -v
ctest --test-dir build/recomp --output-on-failure
```

On Windows set `EMSDK_PYTHON` to the SDK's full Python executable and `EM_CONFIG`
to its `.emscripten` file; put the native LLVM DLL directory on PATH. Browser path
was workspace-local through `PLAYWRIGHT_BROWSERS_PATH`.

Public, path-sanitized timing/RSS/size evidence: `tests/evidence/early-e2e-windows.json`.
These are tiny fixtures, not a scalability benchmark. Native Linux execution, Android
runtime compatibility and Roblox execution remain unverified.

## Expanded checkpoint — 2026-11-07

- **39 ELF cases +47 IR verifier cases PASS** via CTest on Windows and Ubuntu24.04
  WSL (native Clang18.1.3), with separately built pinned SLEIGH libraries.
- **22 Python harness tests,5 downloader tests,2 unsupported-SVC rejection cases PASS**.
- Four suites (`early_pipeline`, `condition_flags`, `interworking`, `thumb_it`) cover
  ARM/Thumb build flags, O0/O2 and four seeds: **64 cases,192 Windows-native /
  Node-WASM /Chromium-WASM comparisons PASS** against Unicorn. The assembly regression
  remains Thumb code regardless of build-mode flag, so those variants repeat that case.
- The Linux compiler translated those exact hashed ELF fixtures; native Linux generated
  execution matched the saved independent ARM references in **64 cases**.
- Fixed the minimized Thumb IT-context regression. SLEIGH context ranges have an exclusive
  end; noflow state must be painted across `[address,address+1)`. Four-byte painting
  leaked IT state into the next instruction; an empty range erased predication.
- Public sanitized metrics: `tests/evidence/expanded-e2e-windows.json` and
  `tests/evidence/linux-native.json`. Full local reports remain ignored under build/.
- Workflow YAML parses; bootstrap shell syntax and Windows cache/tool probes pass.
  Fresh provisioning and hosted GitHub Actions remain unverified.

Repeat the earlier E2E command with `--fixture tests/fixtures/condition_flags.c`,
`--fixture tests/fixtures/interworking.c`, and `--fixture tests/fixtures/thumb_it.S`,
using a separate `--out` directory per suite.
