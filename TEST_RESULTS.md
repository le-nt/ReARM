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

## JNI guest callback slice

- The generated static dispatcher now bridges ARM AAPCS integer/pointer calls to
  the JNI32 host, with guest-only pointers and ARM/Thumb return interworking.
- 13 adapter tests and38 host tests pass on Windows and Linux; all six CTest
  suites pass. The39 Python reference/harness tests and a linked-library native
  regression also pass after emitter changes.
- Public ARM/Thumb O0/O2 JNI_OnLoad fixtures pass native, Node-WASM and Chromium-WASM:
  four builds,12 full-state comparisons against Unicorn plus an independent finite
  JNI reference service. Exact source/ELF/output hashes and timing/size/RSS evidence
  are in tests/evidence/jni-onload-windows.json.
- The optional --jni onload profile requires linking the runtime sources. It
  supplies a JavaVM pointer and reserved=null, not a complete Android process.
- Earlier expanded hosted CI42368507523 at92bc85b passed; new JNI workflow coverage
  must be observed separately after pushing.
- Next: explicit host class/method declarations and a RegisterNatives guest fixture,
  then native registration dispatch and measured Android import bridges. No client
  binaries are available; no authentic Roblox initialization is claimed.

- Linux-native JNI guest validation additionally passed all four ARM/Thumb O0/O2 builds using Clang18 and pinned Unicorn2.1.4 in a workspace venv; see tests/evidence/jni-onload-linux.json.

## Registered native method vertical slice

- Explicit --jni-class/--jni-native declarations expose only named host classes
  and native methods; malformed descriptors, duplicates, undeclared classes and
  unsupported invocation signatures fail during translation (23 profile tests).
- --jni-invoke resolves guest RegisterNatives output after successful JNI1.6
  OnLoad, creates a class local reference, marshals jint words into r2/r3 and an
  aligned guest stack, then runs the registered ARM/Thumb target statically.
- Host native-call frames protect call boundaries, release nested local frames
  on return, preserve global/outer refs and reject detach during active calls.
  The JNI host suite now has42 passing tests; the AAPCS bridge retains13 tests.
- Seven CTest suites pass on Windows and Linux. The39 reference harness tests,
  original JNI smoke regression and real linked-library native regression pass.
- Six-jint callback fixtures cover opposite-mode native functions, nonzero stack
  arguments and four deterministic inputs at ARM/Thumb O0/O2. Final evidence is
  recorded under tests/evidence/jni-registration-*.json after successful runs.
- Hosted JNI smoke CI42435271119 at d3d7358 passed. Registered callback CI is new
  coverage and must be observed independently after this checkpoint is pushed.
- Next actionable task: minimal explicit Bionic import bridge profile, beginning
  with bounded guest memory/string routines and real imported-ELF fixtures.
  Keep unknown imports fatal. Add allocation/error/ABI behavior from measured
  fixtures before attempting larger Android library initialization.
- No authorized Roblox binaries/assets are available; compiler/runtime work
  continues independently. The Goal service last reported usageLimited, not
  active; manual execution continued without creating or replacing the Goal.

## Bounded libc memory service foundation

- Added guest memcpy/memmove/memset/memcmp, strlen/strnlen, strcmp/strncmp and
  strcpy/strncpy semantics over the checked guest-memory interface. No host
  address is accepted as a guest pointer. Limits fail rather than truncate;
  zero-count operations make no access and undefined overlaps are diagnosed.
- 11 service tests pass on Windows and Linux, including1700 seeded differential
  operations per host against standard C memory/string routines plus bounds,
  write-protection, null, wrap, terminator, padding and overlap regressions.
- All eight CTest suites pass on both platforms; strict Windows compiler warnings
  pass for the new service and tests. These are host component tests, not ARM
  differential cases and not the R09 100000-case gate.
- Import resolution, generated callback dispatch, heap/errno and Bionic ABI
  compatibility are not implemented by this service. Next: connect a deliberately
  small external-import whitelist to real public ELF imports and compare ARM,
  native and WASM execution before claiming imported-libc support.

## Explicit libc imports: tested vertical slice

- --runtime libc-memory explicitly binds ten memory/string function imports and
  libc.so dependency metadata to checked guest services. No host library loader,
  ARM decoder or production interpreter is introduced. Unknown strong imports,
  function/data conflicts and callback address collisions remain fatal.
- Real PT_LOAD/DT_NEEDED/PLT fixtures exercise all ten imports at ARM/Thumb O0/O2.
  Four seeds/build match independent ARM reference on native/Node/Chromium:
  16 cases /48 comparisons PASS. Linux-native additionally passes16 cases.
- Expanded native-only differential run passes64 inputs/build (256 cases).
  The first four inputs repeat the backend gate; do not count them twice when
  reporting unique cases. Evidence: tests/evidence/libc-imports-*.json.
- Nine CTest suites pass on Windows and Linux, including7 import-ABI/error cases.
  39 Python reference tests, strict import-bridge compilation,3 negative real-ELF
  profile gates, and a native/Node/Chromium registered-JNI regression pass.
- Hosted CI42436842635 at cf51073 passed. The new imported-libc workflow must
  be observed separately after push.
- Standing user instruction: always continue feasible approved work after tested
  checkpoints without requesting another continue. Existing Goal remains intact;
  service usageLimited still prevents claiming verified unattended continuation.
- Next: bounded guest heap allocation/reallocation/free with overflow, ownership,
  alignment and reuse tests; wire only verified allocator imports into a distinct
  runtime profile. No complete Bionic or authentic Roblox startup is claimed.

## Bounded guest heap foundation

- Added eight-byte-aligned guest arena allocation, zeroed allocation, realloc,
  free/coalescing and bounded metadata. No host pointer crosses the guest ABI.
- 13 heap tests pass on Windows and Linux, including1000 seeded allocation
  traces, eight concurrent host threads, overflow/OOM, invalid/double/interior
  frees, zero-size policy, moving/in-place realloc and guest-write rollback.
- All ten CTest suites pass on both hosts; strict Windows heap compile passes.
  This is host component coverage, not yet imported ARM allocator execution.
- Next: a separate explicit libc-basic profile with malloc/calloc/realloc/free
  imports and guest-thread errno storage; validate a real imported-ELF fixture
  before claiming allocator ABI compatibility.

## Imported heap and errno vertical slice

- Explicit libc-basic profile adds malloc/calloc/realloc/free and __errno to
  tested memory imports. Heap addresses, limits, zero-size policy and guest-thread
  errno cells are explicit; unknown imports remain errors.
- Public ARM/Thumb O0/O2 allocation fixtures pass16 input/build cases on native,
  Node-WASM and Chromium-WASM (48 comparisons), plus16 Linux-native comparisons.
  The independent ARM allocator oracle uses an occupancy bitmap; production uses
  ordered free ranges. Evidence: tests/evidence/allocator-imports-*.json.
- Nine allocator bridge tests pass, including errno isolation/preservation, checked
  overflow/OOM, invalid frees, metadata overlap and ABI guards. Eight real-ELF
  negative gates pass on Windows and Linux. Stack/sentinel overlap is now rejected
  before translation, preventing accidental shadow mappings/early termination.
- Added bounded JNI modified UTF-8 codec:7 known-vector/invalid-input/bound tests
  and1000 seeded UTF-16 round trips pass on both hosts. This is a string-codec
  foundation, not yet NewString/GetString guest JNI support or Java differential proof.
- All12 CTest suites pass on Windows and Linux;39 reference harness tests pass.
- Linux Clang18 native allocator fixture compilation takes74–131seconds versus
  13–23seconds for Windows Clang23; source output is272–405KiB. Record this early
  scaling concern without claiming R31. Translation remains around0.1seconds.
- Next: test an optional emitter layout with small static instruction functions
  to measure compile-time scaling while preserving the existing dispatcher;
  then wire JNI string functions and test real guest string lifecycles.
- Authentic Roblox initialization is not established. Authorized inputs remain
  unavailable, and no private service or production 2x2/Pizda was modified.

## Optional static instruction function layout

- --codegen functions places statically lowered instructions in small non-inlined
  C++ functions, retaining the same bounded iterative dispatcher and guest state.
  --codegen monolithic remains the default. Neither layout interprets p-code.
- Allocator16 cases and flags16 cases plus registered-JNI4 cases match independent
  ARM reference on Windows-native, Node-WASM and Chromium-WASM (108 comparisons).
  Linux-native allocator16 cases additionally pass;124 comparisons in this gate.
- For exact same ARM O2 ELF, observed native compile changed130.7s→12.0s on
  Linux Clang18 and23.4s→8.2s on Windows Clang23. These are single observations
  with uncontrolled concurrent load, not a general speedup guarantee. Default
  monolithic output was regenerated and its SHA256 remains byte-identical.
- All12 CTest suites and39 Python harness tests pass. Optional layout evidence:
  tests/evidence/instruction-functions.json. New hosted CI coverage is pending.
- Guest execution throughput, source/code-size tradeoffs and millions-instruction
  translation still need performance gates. Do not mark R31 or musttail complete.
- Next: actual JNI NewString/NewStringUTF, length and region functions, using the
  tested modified UTF-8 codec, explicit guest buffers and real guest stack args.

## Guest JNI string region slice

- Added NewString/NewStringUTF, UTF16 and modified-UTF8 length queries, and both
  region-copy functions. ABI slots are independently measured from OpenJDK17.
  Region calls marshal a real fifth argument from the guest stack.
- java/lang/String is intrinsic. Payloads preserve embedded NUL and surrogate
  units; regions write only requested content, without an added terminator.
  Invalid ranges raise pending StringIndexOutOfBoundsException without writing.
- Shared string storage follows reference lifetimes and a16MiB default live-byte
  budget; deleting refs/native frames releases storage when the last owner drops.
- JNI host suite now51 tests; all12 CTest suites pass on Windows and Linux.
  39 Python harness tests and strict JNI host build pass.
- Public ARM/Thumb O0/O2 string fixtures match independent ARM reference on native,
  Node-WASM and Chromium-WASM (12 comparisons) plus Linux-native4 comparisons.
  Python standard Unicode codecs independently supply the reference conversion;
  no guest result is precomputed. Evidence: tests/evidence/jni-strings-*.json.
- GetStringChars/GetStringUTFChars and corresponding release functions remain
  explicit unsupported callbacks. Next: bounded copied-buffer leases, proper
  release validation/reuse and guest lifecycle tests before widening JNI support.

## Copied JNI string leases

- GetStringChars/GetStringUTFChars return actual guest copies and isCopy=true.
  UTF8 copies include a terminator; UTF16 copies preserve explicit code-unit length.
  Release checks object identity, encoding and acquisition state, and supports
  valid global aliases on another attached thread and pending-exception cleanup.
- Reusable copy pool defaults to512KiB/4096 buffers. Unreleased copies retain the
  object and live-string budget. Attached guest identities are bounded to4096.
- JNI host suite now62 tests;2000 acquire/release cycles reuse guest storage
  without advancing the allocator after the first copy. OOM recovery, wrong/stale
  releases, immutable source storage and metadata/output guards are covered.
- All12 CTest suites pass on Windows and Linux;39 Python reference tests and
  strict host compilation pass. Copied-string ARM/Thumb O0/O2 fixtures pass
  native/Node/Chromium (12 comparisons) plus Linux-native4 comparisons.
  Evidence: tests/evidence/jni-string-leases-*.json.
- CI now lets a running checkpoint finish while keeping only the newest pending
  checkpoint per ref. Continuous engineering no longer cancels every long test run.
- Next: primitive JNI arrays and bounded region access, followed by a combined
  JNI/allocator initialization fixture. Critical string pinning and Java method
  execution remain explicit unsupported calls; no Roblox startup is claimed.
