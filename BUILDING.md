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
