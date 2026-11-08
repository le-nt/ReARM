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
