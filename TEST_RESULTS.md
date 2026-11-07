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

## PIC, linked-library and BSS checkpoint — 2026-11-07

- 50 ELF tests,18 real multi-DSO linker cases,47 IR verifier cases pass on Windows.
  Native Linux builds pass; CTest fixture generation initially failed because LLD
  was absent. Workspace-local Ubuntu LLD18.1.3 was extracted without sudo; all
  Linux CTest suites then passed. Hosted CI already installs lld-18 explicitly.
- 39 reference/oracle/harness tests,5 inventory tests,6 M0-pthread-result validation
  tests and5 downloader tests pass.
- Shared PIC: ARM/Thumb O0/O2 at0x10000000 and0x20000000,4 seeds each:
  **32 cases /96 native+Node+Chromium comparisons PASS**. Real relocations include
  RELATIVE, GLOB_DAT, ABS32; linked fixtures additionally exercise JUMP_SLOT.
- Multi-DSO constructors:4 seeds across native/Node/Chromium PASS with initialized
  memory/full guest-state comparison. Native no-initialization/swapped-bias4cases pass.
- BSS:4MiB mapping,16 cases /48 backend comparisons PASS. Generated C++ sizes
  33,322/47,947/28,930/40,561 bytes, all under the128KiB regression gate.
- Existing baseline rerun after constructor integration:16 cases /48 comparisons PASS.
- Preserved M0 Emscripten pthread smoke runs under Node and isolated Chromium:
  four distinct workers, counter400000 OK, exit0. This does not establish R17.
- Prior hosted clean Linux runs42365389357 and42365430843 at ff7d79d succeeded.
  The extended workflow is a new gate and must be observed independently.

## JNI32 host foundation

- Added bounded guest-pointer JNI 1.6 tables, thread attachment, local/global
  references, exception state, explicit class/method registry and transactional
  RegisterNatives. Unsupported functions trap with their slot/function identity.
- 38 JNI host tests pass on Windows and Linux, including eight concurrent host
  threads. All five CTest suites pass on both hosts after integration.
- This is a host subset only: generated guest calls, class loading/bytecode,
  Java method execution and real client JNI_OnLoad remain unverified.
- Next: AAPCS32 callback adapter and public ARM/Thumb JNI_OnLoad execution through
  the generated static dispatcher, followed by native/Node/Chromium checks.
