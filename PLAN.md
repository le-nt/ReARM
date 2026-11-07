# ReARM approved implementation roadmap

This records the existing approved R01–R32 roadmap, not a replacement roadmap.
The objective is working, independently validated ARM32/Thumb Android ELF →
SLEIGH p-code → normalized IR → generated C++ → executable WebAssembly, followed
by authentic Roblox engine initialization and authorized local-server gameplay.
Linux-native output uses the same guest semantics and Android bridge contracts.

## Execution priorities and current status

- Work exclusively on `codex/rearm-reconstruction`; preserve original M0 commit
  `b55568513f697465c6ad22339d1b40b1aa4e4ac1` and leave LARL/production 2x2 unchanged.
- Prioritize code, compilation, execution, and regression fixes over documents.
- Pull the minimum R02/R06/R08/R10/R12 work forward for an early real end-to-end
  checkpoint. Advanced relocations, FP, SIMD, and Android services do not block it.
- Develop independent components concurrently with explicit file ownership.
- Begin deterministic tests now, expand seeded differential cases progressively,
  and retain R09's 100,000-case gate without making it a prerequisite for R10.
- Measure translation time/throughput, generated size, build duration, process
  memory, WASM size, and execution time from the first working emitter. R31 is
  the major optimization gate, not the start of measurements.
- After tested checkpoints, commit meaningful changes, record exact evidence,
  and continue the next feasible task without routine approval requests.

Statuses: **Not started**, **In progress**, **Blocked**, **Implemented but
unverified**, **Passed with reproducible evidence**. A component test passing
does not automatically pass the larger milestone.

Current checkpoint: R01–R03, R06–R12 are in progress at varying subset coverage.
The ELF suite has 39 passing deterministic tests. The early E2E checkpoint passes:
16 ARM/Thumb O0/O2 input cases across native, Node WASM and Chromium WASM match
Unicorn (48 backend comparisons). R04–R05 and R13–R32 are not started as full milestones.
Later executed results and current work are authoritative in `PROGRESS.md` and
`TEST_RESULTS.md`; update this summary when a gate changes.

## Early end-to-end checkpoint

Compile the public C fixture as ARM and Thumb ELF at O0/O2. Load real PT_LOAD
segments, lift reachable instructions through Ghidra SLEIGH, emit reusable static
C++, compile native and with Emscripten, execute native/Node/Chromium outputs, and
compare all fixture-contract registers, meaningful CPSR bits, and observed guest
memory against Unicorn. Do not hardcode outputs. Record per-stage measurements.
Compilation alone, reference-only execution, or a production interpreter cannot
pass this checkpoint. Missing browser execution is a separate unpassed backend.

## Approved milestones and gates

Every row requires code in the named subsystem plus executed regression evidence.
Dependencies denote the full acceptance gate; the early checkpoint may use a
tested supported subset and must report that subset explicitly.

| ID | Implementation / subsystem | Dependencies | Completion test and expected result | Risk and fallback |
|---|---|---|---|---|
| R01 | Preserve M0; pin dependencies; bootstrap, CMake, CI | None | Clean Linux build, public SLEIGH decode, Node/Chromium pthread count 400000 | Dependency drift: exact revisions/digests and early failure |
| R02 | Validated ELF32 segment loader, `recomp/elf` | R01 | Correct unequal offset/vaddr, load bias, BSS, stripped ELF; malformed input rejected | Overflow/overlap: checked arithmetic and bounded allocations |
| R03 | Core ARM REL relocation/symbol engine | R02 | Hand-calculated and linker-built relocated bytes agree at two biases | Addend/Thumb-tag errors: per-type handlers; unknown types fail |
| R04 | Multi-module namespaces, symbols, constructors, pretranslated dl registry | R03 | Public multi-DSO function/data resolution and constructor ordering | Linker-version differences: explicit profiles and eager binding |
| R05 | Instruction/PREL31/packed relocations and TLS images | R04 | Positive, boundary, malformed and overflow tests per type; isolated TLS | Rare formats: inventory unsupported types without compatibility claim |
| R06 | ELF-backed SLEIGH raw lifter, `recomp/lift` | R02 | ARM/Thumb16/Thumb32/IT/BLX and invalid-fetch deterministic lifts | Decode-context leakage: isolated sessions and explicit context |
| R07 | Mixed-mode worklist CFG, switches, veneers, hints | R03, R06 | Expected edges recovered in mixed code/data and stripped fixtures | Unresolved indirects: explicit diagnostics, offline hints/rebuild |
| R08 | Normalized typed IR and verifier, `recomp/ir` | R06 | Internal branches, aliases, temporaries and userops preserve behavior; invalid IR rejected | Transformation bugs: retain raw p-code and minimal normalization |
| R09 | Differential CPU framework and minimization | R08 | Targeted tests then 100000 seeded defined cases; QEMU whole fixtures | Related oracle bugs: minimized replay and ARM hardware for disputes |
| R10 | Static C++ emitter, guest CPU/memory, minimal native runner | Minimum tested R07–R09 subset, then full gates | Generated public fixtures match reference; no production decoder/evaluator | C++ UB: fixed-width helpers, sanitizers, strict baseline |
| R11 | Bounded iterative/musttail dispatch and interworking | R10 | 10 million transfers, bounded host stack, equivalent indirect/callback/return behavior | Tail backend limitation: iterative mode, never recursive fallback |
| R12 | Emscripten worker host and artifact execution | R10; full gate R11 | Same supported fixtures match Linux/Node/Chromium; guest fault diagnostics | Missing tail-call support: feature-tested iterative artifact |
| R13 | VFP/NEON/FPSCR and architectural userops | R09, R12 | Aliases, lanes, NaNs, rounding, flags and conversions match reference | Host FP differences: SoftFloat and ARM-specific semantics |
| R14 | Android softfp ABI marshalling and bridge descriptors | R05, R12 | Register/stack/64-bit/aggregate/variadic/callback fixtures match ARM | Host layouts: typed guest memory helpers and handles |
| R15 | libc/libm/log/zlib/errno/time/loader services | R13, R14 | Each supported import and failure path exercised by NDK fixtures | Bionic drift: explicit profiles or authorized guest support libraries |
| R16 | Guest VFS, descriptors, asset access and persistence | R15 | Read/write/seek/stat/mmap/errors/persistence/path confinement pass | Browser quota: bounded storage and explicit errors |
| R17 | Guest pthreads, synchronization, TLS, atomics/kuser | R05, R13–R15 | Contention, TLS isolation, once/condvar/reservations/timeouts pass | Host races/deadlocks: conservative shared-memory model and replay mode |
| R18 | Guest EHABI, exceptions and setjmp/longjmp | R11, R15, R17 | Cross-module throws execute cleanups/catches; jumps restore guest state | Runtime personalities: pinned NDK profile; explicit unsupported errors |
| R19 | JNI/JavaVM tables, refs, registration and callbacks | R14, R17, R18 | Public JNI_OnLoad and attached-worker callbacks/ref/exception lifecycle pass | Missing Java surface: signature inventory and real implementations |
| R20 | EGL/GLES/WebGL2, input and UI queue | R16, R17, R19 | ARM graphics images, textures, shaders, multitouch and context recovery pass | Extensions: advertise only tested support; trace/replay diagnosis |
| R21 | Android audio adapters and PCM backends | R17, R19 | Expected samples/channels/callback timing and browser resume | Middleware: authorized companion recompilation or documented API bridge |
| R22 | Socket/DNS/poll and local WebSocket relay | R15, R17 | TCP partial reads, datagrams, errors, backpressure and reconnect tests pass | UDP timing: preserve boundaries and compare Linux direct transport |
| R23 | Exact original Roblox fixture inventories, `games` | R02–R07 | Version/architecture/hashes/assets/imports/relocations/provenance recorded | Missing inputs: mark client unavailable and continue public work |
| R24 | Original 2015 engine initialization/JNI host | R13–R23 | Repeatable native/graphics initialization without executed unresolved paths or required no-ops | Prepatched differences: original hashes and separate overlays/reference traces |
| R25 | Offline interactive 2015 engine scene | R24 | 10-minute scene with rendering/audio/input/clean shutdown across 10 launches | Timing: recorded events and minimal reproducer scene |
| R26 | **Major M12-equivalent online outcome** | R22, R25, authorized compatible server | Real join/replication/input observed by server and second participant; 30-minute session; 10 reconnects | Backend mismatch: establish original ARM session first; mocks do not pass |
| R27 | One exact authorized 2018 client profile | R23, R26 | Its initialization/offline/online ladder passes; 2015 stays passing | New middleware/code scale: isolated profiles and validated semantics |
| R28 | One exact authorized 2019 client profile | R27, its R23 inventory | Same ladder passes without earlier-client regressions | New assumptions: per-build traces and public reduced regressions |
| R29 | One exact authorized 2021 client profile | R28, its R23 inventory | Same ladder passes independently for ARM32 input | Missing/unsupported binary: precise blocker, no year-level inference |
| R30 | Complete Linux hosting and Android PIE process startup | R16–R22 | Public PIE/library suites and 2015 acceptance ladder pass natively | Android/glibc mismatch: retain guest Android bridges |
| R31 | Streaming caches, bounded shards/workers and resource telemetry | R12; final gate R29 | Recorded 6-vCPU/16-GiB baseline: 1M instructions analyze/emit ≤10min/4GiB; compile ≤1h/12GiB; 5M stress report | Optimizer blowups: split regions/TUs, adaptive jobs, no default full LTO |
| R32 | Release packaging, qualification, crash replay | R26–R31 | Clean public CI, available client suites, 24-hour soak; no unexplained retained growth >5% | Semantic drift: strict/optimized comparisons and revert individual fast paths |

## Durable continuation

`PROGRESS.md` records current commit, active work, blockers and one concrete next
action. `TEST_RESULTS.md` records exact commands/outcomes and evidence paths.
`AGENTS.md` governs future sessions; `ARCHITECTURE.md` describes implemented code;
`RESEARCH.md` separates sources from confirmed history; `KNOWN_ISSUES.md` states
actual limitations. Update these at meaningful tested checkpoints, not instead
of implementation. Never infer original M1–M13 definitions from these R numbers.
