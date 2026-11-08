# ReARM implementation progress

Active goal: implement the approved R01–R32 reconstruction roadmap. Major target:
authentic statically recompiled ARM32 Roblox gameplay against an authorized local server.

Branch: `codex/rearm-reconstruction`. Original M0: `b55568513f697465c6ad22339d1b40b1aa4e4ac1`.

## Current checkpoint

- R01 in progress: portable Windows toolchain and pinned SLEIGH/Emscripten installed and executed;
  Linux bootstrap and CI prepared but not yet executed.
- R02/R03 in progress: 39 deterministic ELF loader/relocation tests pass, including optional
  static symbols; real ARM and Thumb ELF fixtures load and translate.
- Early end-to-end checkpoint PASSED on Windows: source-built ARM/Thumb at O0/O2, four
  seeds each, translated native + Node WASM + Chromium WASM all match independent Unicorn.
  This is 16 fixture cases and 48 full-state backend comparisons. Evidence:
  `tests/evidence/early-e2e-windows.json` (public synthetic fixtures only).
- SLEIGH lifting, initial instruction IR, static integer C++ emitter and bounded iterative
  dispatch are working for the exercised subset. No production interpreter or decoder.
- Next: IR verification, condition-flag and indirect/interworking fixtures, larger seeded
  validation, Linux CI execution and measured performance expansion.
- Full R02/R03 acceptance is not yet claimed; broader real-linker and fuzz coverage remains.

## Expanded verified checkpoint

- Four suites (baseline, condition flags, indirect/interworking, Thumb IT regression)
  pass64 input/build cases across Windows-native, Node-WASM and Chromium-WASM:
  **192 full-state comparisons** against Unicorn.
- Native Linux SLEIGH/compiler builds and CTest pass under Ubuntu24.04 WSL/Clang18.
  Linux-generated execution matches the same64 hashed reference cases:
  **256 total backend comparisons** across both host platforms.
- Integrated IR verifier:47 tests pass. ELF suite39, Python harness22,
  downloader5 and unsupported-SVC rejection2 cases also pass.
- Fixed the Thumb IT noflow context leak; the minimized regression remains in Git.
- Next actions now follow the vertical-slice integration strategy below.

## Shared-library vertical slice

- ET_DYN PIC fixtures execute at two load biases:32 input/build cases match
  independent ARM references on native, Node-WASM and Chromium-WASM backends.
- Eager multi-DSO linking resolves actual function/data imports and PLT targets.
  Dependency-first constructors execute before the selected entry; initialized
  memory and complete final state match the independent reference on all3backends.
- CTest now includes50 ELF tests,18 real multi-DSO cases and47 IR verifier cases.
- The4MiB BSS regression passes16 cases on native/Node/Chromium; generated source
  stays below48KiB by allocating BSS at runtime rather than emitting zero tokens.
- Preserved M0 pthread toolchain smoke passes Node+Chromium with counter400000;
  this is not translated ARM pthread support.
- Client inventory tooling records supplied versions/provenance, hashes, imports,
  relocations, JNI exports and assets without executing/extracting client code.
- User confirmed authorized Roblox inputs are not available yet. Continue compiler
  and runtime foundations; do not repeatedly request inputs or download substitutes.
- Next: finish and validate JNI32 host foundation, connect guest ABI trampolines,
  exercise public JNI_OnLoad fixtures, and extend linking/import diagnostics.

## Environment and blockers

- WSL was initially denied under restricted permissions. After the permission change,
  Ubuntu24.04 WSL native builds/tests/execution pass. Fresh bootstrap and hosted CI
  remain distinct unverified gates.
- Python 3.12.10, Ninja 1.13.0 and LLVM MinGW 20261104 were installed in the enclosing workspace.
- Emscripten 6.0.12 and SLEIGH v12.2 built/executed successfully.
- Local execution requires this computer to remain awake and online; no always-on host verified.
- No Roblox binaries are present or needed for current general-purpose compiler work.
- Git CLI writes now work after the permission change; the earlier credential-store
  failure is resolved. Verified commits are pushed only to the development branch.
- Published initial checkpoint: `4ce418b675d201d048c6a78fb85c625f59671740`.
- After the user cleared the diagnostic Goal and requested the original objective,
  the reconstruction Goal was set back on this same chat; the service confirms active.
  Existing commits and progress were preserved. Account checks confirmed usage allowed.

Never mark a milestone passed without its actual acceptance evidence.

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
