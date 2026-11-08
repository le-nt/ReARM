# Building and validating ReARM

The reproducible baseline is Ubuntu 24.04 x86_64, Clang 18, and Python 3.12.
Windows users can use that distribution under WSL2. Native Windows build work
may use equivalent tools, but it does not count as executing the Linux baseline.

## Bootstrap

Install the host prerequisites once:

```sh
sudo apt-get update
sudo apt-get install -y git clang-18 lld-18 cmake ninja-build zlib1g-dev python3-venv nodejs
bash tools/bootstrap.sh
```

Bootstrap uses `dependencies.lock.json`, creates ordinary local dependency
checkouts, and verifies their exact commit IDs. It refuses modified or
differently pinned existing dependency checkouts rather than resetting them.
It checks the vendored ARM SLA digest and the Emscripten SDK archive SHA-256
before installation. Compiler, SDK, and Python package versions are recorded in
`build/evidence/toolchain.txt`. Host apt packages and the runner image are not
content-addressed; their versions remain part of the recorded environment.

SLEIGH is built against an explicit, unpatched Ghidra source checkout at the
locked upstream commit. This avoids the packaging project's default mutable
tag fetch. Only the SLEIGH library is built; the original M0 compiled ARM SLA is
retained and its checksum is checked. No Ghidra desktop installation is needed.

Set `REARM_JOBS=2` on a memory-constrained machine. The default is four build
jobs. Set `REARM_INSTALL_BROWSER=0` to defer the Chromium download; a browser
test must then be reported as unavailable rather than passed.

## Tests

```sh
ctest --test-dir build/recomp --output-on-failure --no-tests=error
source build/venv/bin/activate
source third_party/emsdk/emsdk_env.sh
python -m playwright install --with-deps chromium
python tools/validate_e2e.py \
  --rearm build/recomp/rearm \
  --sla third_party/sla/ARM/ARM7_le.sla \
  --clang clang-18 --cxx clang++-18 \
  --emxx third_party/emsdk/upstream/emscripten/em++ \
  --node node --browser
```

The end-to-end command compiles public C fixtures as ARM/Thumb ELF, translates
them through SLEIGH to C++, builds native and WASM executables, and compares
executed state with the test-only Unicorn oracle. It records stage times and
artifact sizes from the first emitter. A compiler invocation completing without
executing and comparing its output is not an end-to-end pass. Consult
`TEST_RESULTS.md` for commands actually executed and their current outcomes;
this file describes the intended repeatable commands, not a claim that CI ran.

The original Linux baseline bootstrap and public compiler suites at commit
`ff7d79d9a61efae289d53224eebecdb20d89e5ef` executed successfully in
[GitHub Actions run 42365430843](https://github.com/le-nt/ReARM/actions/runs/42365430843).
That is execution evidence for that revision, not a claim that every R01 gate
or later bootstrap edit has passed. Current results remain in `TEST_RESULTS.md`.

All fixtures used by public CI are source-built. Proprietary clients, generated
translations of those clients, assets, secrets, and private traces must remain
outside public artifacts. The original M0 tools remain available as reference.

## Native Windows fallback

When WSL is unavailable, `tools/bootstrap.ps1` provisions workspace-local LLVM
MinGW, embedded test Python, static zlib, pinned SLEIGH/Ghidra and emsdk. Node,
Git, CMake, CTest and Windows `tar.exe` must already be available. Archive URLs and
SHA-256 values are pinned in `dependencies.lock.json`; no global tool install is
performed. The Node downloader avoids this host's failing Schannel/.NET path,
requires HTTPS, verifies cached files, and publishes new downloads only after
their checksum passes. A changed upstream `get-pip.py` fails its pinned digest;
it is never silently accepted.

```powershell
./tools/bootstrap.ps1
# Reuse a previously provisioned workspace cache:
./tools/bootstrap.ps1 -ToolsRoot C:/path/to/toolchain -VerifyOnly
node --test tools/download.test.mjs
```

`-VerifyOnly` checks existing archive hashes, dependency revisions, required
files, Python package versions and compiler/SDK executable probes. It neither
downloads nor rebuilds, and does not certify an end-to-end translation. The full
script runs the native build and CTest; add `-InstallBrowser` to provision
Chromium. Fresh provisioning has not yet been rerun through this script; the
verification path was executed against the current workspace's working tools.

For manual Emscripten calls on this host, use the SDK's full Python rather than
the embedded test Python (whose `_pth` excludes script siblings):

```powershell
$env:EMSDK_PYTHON = "$PWD/third_party/emsdk/python/3.13.3_64bit/python.exe"
$env:EM_CONFIG = "$PWD/third_party/emsdk/.emscripten"
```

The script sets these for its process. The embedded Python remains the isolated
test/oracle interpreter. Linux baseline acceptance remains a separate test gate.

## Sharing a checkout between Windows and Linux

Keep native CMake trees, SDK installations and SDK configs separate. Bootstrap
refuses a build tree created by the other host and refuses to overwrite the
other host's SDK binaries/configuration. Defaults remain compatible with the CI
and existing Windows checkout; these overrides support coexistence:

| Linux environment variable | Windows PowerShell parameter | Default |
|---|---|---|
| `REARM_DEPENDENCY_ROOT` | `-DependencyRoot` | `third_party` |
| `REARM_SLEIGH_BUILD_ROOT` | `-SleighBuildRoot` | selected dependency root `/sleigh/build` |
| `REARM_EMSDK_ROOT` | `-SdkRoot` | selected dependency root `/emsdk` |
| `REARM_BUILD_ROOT` | `-BuildRoot` | `build/recomp` |
| `REARM_PYTHON_ENV` | test Python lives under `-ToolsRoot` | `build/venv` on Linux |

For example, Linux alongside an existing Windows build:

```sh
REARM_SLEIGH_BUILD_ROOT="$PWD/build/linux-sleigh" \
REARM_BUILD_ROOT="$PWD/build/linux" \
REARM_EMSDK_ROOT="$PWD/build/linux-emsdk" \
REARM_PYTHON_ENV="$PWD/build/linux-venv" bash tools/bootstrap.sh
```

Use `-SleighBuildRoot build/windows-sleigh -BuildRoot build/windows
-SdkRoot build/windows-emsdk` for the reverse situation. Set overrides to absolute
paths when invoking build tools from another directory. `-VerifyOnly` also rejects
a missing configuration instead of allowing an SDK probe to generate one.
Configuration stays at the selected SDK root `/.emscripten`, matching the pinned
emsdk activation behavior; choose a separate SDK root to isolate both binaries
and configuration.

## Preserved M0 pthread toolchain smoke

To validate explicit libc memory/string imports through real ELF PLT relocations:

```sh
python tools/validate_libc.py --rearm build/recomp/rearm \
  --sla third_party/sla/ARM/ARM7_le.sla --clang clang-18 --cxx clang++-18 \
  --emxx third_party/emsdk/upstream/emscripten/em++ --browser
python tools/check_libc_profile.py --rearm build/recomp/rearm \
  --sla third_party/sla/ARM/ARM7_le.sla --clang clang-18 \
  --fixture build/libc-e2e/arm-O2/libfixture.so --stub-dir build/libc-e2e
```

`--runtime libc-memory` explicitly supplies the `libc.so` dependency and ten
function imports: memcpy, memmove, memset, memcmp, strlen, strnlen, strcmp,
strncmp, strcpy and strncpy. Generated code requires the repository include
path plus `runtime/jni32.cpp`, `runtime/aapcs32.cpp`, `runtime/libc_memory32.cpp`
and `runtime/libc_imports32.cpp`. The compiler/test harness handles these links.
The generated link-metadata libc.so is never loaded or executed; the production
runner uses checked guest memory services, and Unicorn uses an independent
Python implementation. Unknown strong imports, function/data type conflicts and
callback arena collisions fail. This is a bounded integer/pointer ABI subset,
not a complete Bionic runtime. Heap allocation, errno, file/thread/network and
other services remain separate work. Memory transfers default to16MiB and
string scans to1MiB; over-limit requests produce errors, not successful truncation.
JNI and libc profiles can coexist in generated code, but combined profile
execution is not yet acceptance-tested by this harness.

`validate_libc.py --allocator` selects the distinct `--runtime libc-basic`
profile. This adds malloc/calloc/realloc/free and Android `__errno` to the ten
memory functions. Generated programs additionally link `runtime/heap32.cpp` and
`runtime/libc_allocator32.cpp`. The heap is a bounded16MiB arena at0x60000000;
errno cells use guest metadata storage at0x70000000 and remain distinct by
guest thread identity. Allocator metadata defaults to65536 live blocks.
OOM and checked size overflow return null and set ENOMEM; success/free preserve
errno. Zero-size malloc/calloc reserve a minimum block, and realloc(p,0) frees p.
These are explicit initial policies, not a promise of matching every Android
allocator's layout or undefined-input behavior. The independent ARM oracle uses
an occupancy bitmap rather than the production free-range algorithm.

The Android ABI references are Bionic's
[thread-specific __errno declaration](https://android.googlesource.com/platform/bionic/+/bd8773d19d96cd8794f944ce5f93f4feb43e386b/libc/include/errno.h)
and [ENOMEM constant](https://android.googlesource.com/platform/bionic/+/557b1c4dfe0b0d38891b57fb9dfea44aa511013f/libc/kernel/uapi/asm-generic/errno-base.h).
Guest pthreads, TLS register access and thread lifecycle remain separate work;
per-thread errno unit coverage does not establish R17 compatibility.

`--codegen functions` is an optional generated layout: each statically lowered
instruction has a small non-inlined C++ function, called by the same bounded
iterative dispatcher. `--codegen monolithic` remains the default. The function
layout reduces the optimizer's single-function problem, but may trade guest
runtime performance and code size for compilation time. This is not musttail,
an interpreter, or acceptance of the R31 optimization milestone. Validation tools
`validate_e2e.py`, `validate_link.py` and `validate_libc.py` forward this option.

The public JNI guest bridge can be exercised separately:

```sh
python tools/validate_jni.py --rearm build/recomp/rearm \
  --sla third_party/sla/ARM/ARM7_le.sla --clang clang-18 --cxx clang++-18 \
  --emxx third_party/emsdk/upstream/emscripten/em++ --browser
```

This builds ARM/Thumb O0/O2 shared libraries and compares registers, flags and
observed memory against Unicorn with an independently implemented finite JNI
test service. `--jni onload` supplies a guest JavaVM pointer to the chosen entry
with reserved r1=0. Generated code in this mode requires `-I <ReARM>` and linking
`runtime/jni32.cpp runtime/aapcs32.cpp`; normal freestanding output remains a
single translation unit. Fixed JNI data/callback arenas reject ELF overlaps.
This explicit host subset does not load Java classes or execute Java bytecode.
Unsupported callbacks and undeclared class/method requirements fail visibly.

For registered native callbacks, add `--registered --stack` to `validate_jni.py`.
This builds an opposite-mode native method with six jint arguments, including
nonzero stack words, and executes four deterministic input values per build.
The fixture uses `-mfloat-abi=soft` to isolate integer ABI behavior; this gate
does not validate VFP/NEON execution.

The compiler accepts repeated explicit declarations:

```text
--jni onload --jni-class fixture/Bridge
--jni-native 'fixture/Bridge|transform|(IIIIII)I|static'
--jni-invoke 'fixture/Bridge|transform|(IIIIII)I'
```

After a successful JNI1.6 OnLoad return, `--jni-invoke` resolves the method that
guest RegisterNatives installed, creates a local class reference, and starts
the guest function with JNIEnv*, jclass and jint arguments from runner argv.
The current invocation subset is one static native method, 0–62 jint arguments,
and jint/void result. Unspecified arguments default to zero. Declared instance
methods may be registered but cannot yet be invoked by this runner. Float,
64-bit, object argument, Java method and general Java VM execution are deferred;
unsupported invocation profiles fail during translation. Only explicitly
declared, already initialized host classes are exposed.

```sh
python tools/check_pthreads.py \
  --emcc third_party/emsdk/upstream/emscripten/emcc --node node --browser
python -m unittest discover -s tools -p 'test_check_pthreads.py' -v
```

This compiles unchanged `tests/m0/hello_pthread.c` with Emscripten pthreads,
places the program's main function on a worker, then executes Node and Chromium.
The local browser server sends COOP/COEP headers. Passing requires four distinct
workers, `counter=400000 OK`, exit status zero, and browser cross-origin isolation.
It writes `build/m0-pthreads/report.json` with hashes and executed output. Both
backends passed locally using Emscripten 6.0.12; this smoke verifies host WASM
threading, not statically recompiled ARM pthread semantics or R17 compatibility.
On Windows use `emcc.exe` and the SDK Python/config variables above; configure
`PLAYWRIGHT_BROWSERS_PATH` only when using a nondefault browser cache.
