# ReARM

Independent reconstruction from the original Microdroid M0 skeleton. Development is on
`codex/rearm-reconstruction`; the imported M0 commit remains preserved.

The early static recompilation pipeline now executes public ARM/Thumb C fixtures as
native code and WASM (Node and Chromium), with complete observed-state comparisons
against Unicorn. See [TEST_RESULTS.md](TEST_RESULTS.md) for executed evidence and limits,
[BUILDING.md](BUILDING.md) for setup, and [PROGRESS.md](PROGRESS.md) for the next task.
Android/Roblox compatibility is not implemented yet. The approved R01–R32 roadmap is
in [PLAN.md](PLAN.md); initial E2E work is pulled forward without renumbering it.

## Original M0 README (historical reference)

Static recompiler that turns Android ARM32 shared libraries (`.so`) into Emscripten/WebAssembly,
plus a runtime of bridges (generic Android platform shims + per-game JNI bridges) so the game runs
in a browser. First target: the 2015 Roblox client (`libroblox.so` 2.217) hosted by `larl/`.

The full design and milestone roadmap (M0–M13) live in the approved plan; this file tracks layout,
build steps and milestone status.

## Layout

```
recomp/            C++20 recompiler (ELF loader, discovery, SLEIGH lifter, IR, C++ emitter)
runtime/           CpuState, dispatcher, atomics/kuser helpers, EHABI unwinder, userops
bridges/           generic Android platform bridges (libc, libm, libz, log, dl, egl, glesv2, ...)
games/roblox2217/  Roblox 2.217 manifest, hints and JNI bridge (port of larl/)
relay/             WebSocket <-> TCP/UDP relay for the self-hosted server
tests/             NDK test .so sources, device oracle (adb), differential runner
tools/             dev utilities (COOP/COEP dev server)
third_party/       emsdk, sleigh/libsla, vendored ARM .sla (versions in third_party/VERSIONS)
larl/              reference Android host (unchanged)
```

## Setup

```
# emsdk (pinned)
cd third_party/emsdk && ./emsdk install 6.0.12 && ./emsdk activate 6.0.12 && cd -

# libsla + ARM spec (Ghidra 12.2 via lifting-bits/sleigh v12.2)
cmake -S third_party/sleigh -B third_party/sleigh/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -Dsleigh_BUILD_EXTRATOOLS=OFF
ninja -C third_party/sleigh/build -j6 libsla.a sleigh sleigh_spec_ARM7_le
# compiled spec is vendored at third_party/sla/ARM/ARM7_le.sla

# recompiler tools
cmake -S recomp -B build/recomp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build/recomp
```

The machine has 15 GB RAM; keep `-j` around 6 for C++ builds.

## M0 checks

```
# SLEIGH decode of libroblox JNI_OnLoad (Thumb), with p-code
build/recomp/sla_probe third_party/sla/ARM/ARM7_le.sla \
  larl/versions_test/2.217/lib/armeabi/libroblox.so 0xc0fdf1 16

# emscripten pthreads under node
source third_party/emsdk/emsdk_env.sh
emcc -O2 -pthread -sPTHREAD_POOL_SIZE=4 -sEXIT_RUNTIME=1 tests/m0/hello_pthread.c -o build/m0/hello_pthread.js
node build/m0/hello_pthread.js

# ... and in a browser (needs COOP/COEP)
emcc -O2 -pthread -sPTHREAD_POOL_SIZE=4 tests/m0/hello_pthread.c -o build/m0/index.html
node tools/devserver.mjs build 8080   # open http://localhost:8080/m0/
```

## Notes

- SLEIGH `TMode` is a *flowing* context variable: a Thumb `blx <arm target>` commits TMode=0 from
  the target onwards, flipping the ISA of everything after it. The lifter owns the mode and
  repaints TMode over each instruction before decoding (`recomp/tools/sla_probe.cpp`); the IT-block
  (`condit`) and `LRset` commits are noflow and stay with SLEIGH.

## Status

| M | Status |
|---|---|
| 0 | done: emsdk 6.0.12, libsla + ARM7_le.sla, pthread hello in node, COOP/COEP dev server, SLEIGH vs capstone agree on JNI_OnLoad |
| 1 | next: ELF loader, static relocation, PLT/thunks, image blobs |
