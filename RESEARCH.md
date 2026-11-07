# Evidence and public references

## Microdroid provenance

User-confirmed project history: the supplied source is M0; Mollomm1 developed
Microdroid in October 2026; it translated ARM to C++ using Ghidra SLEIGH,
Emscripten and `[[clang::musttail]]`; original M12 was the first online build;
demonstrations included 2015, 2018 and 2021 clients; most original source is closed.
These are supplied historical statements, not independently verified test results.

The imported repository's sole original commit was
[`b555685`](https://github.com/le-nt/ReARM/commit/b55568513f697465c6ad22339d1b40b1aa4e4ac1).
Its README refers to an approved M0–M13 plan that is not in the imported tree.
Public repository/history searches and Microdroid/Mollomm1 roadmap searches did
not locate it. The [provided demo](https://microdroid.nodium.lol/?v=1) was
inaccessible through both available web research routes during investigation.
Unrelated AOSP and other projects share the Microdroid name; they are not evidence
of this recompiler's architecture. No original M1–M13 technical definitions are
inferred. ReARM R26 names an equivalent online outcome only.

## Primary implementation references

- [SLEIGH packaging](https://github.com/lifting-bits/sleigh): standalone C++
  library integration. The exact packaging/Ghidra/emsdk revisions and compiled
  SLA digest are in `dependencies.lock.json`.
- [Ghidra p-code reference](https://ghidra.re/ghidra_docs/languages/html/pcoderef.html)
  and [operations](https://ghidra.re/ghidra_docs/languages/html/pcodedescription.html):
  varnode spaces, byte widths and instruction-local control flow.
- [Arm ELF32 ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst),
  [AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst),
  [EHABI32](https://github.com/ARM-software/abi-aa/blob/main/ehabi32/ehabi32.rst):
  relocation, calling and guest unwind contracts.
- [Android ABIs](https://developer.android.com/ndk/guides/abis) and
  [Bionic linker](https://android.googlesource.com/platform/bionic/+/master/linker/linker_relocate.cpp):
  Android's softfp argument convention and linker behavior. The imported compiler
  spec's default hard-float/decompiler fixups are not production execution policy.
- [Clang musttail](https://clang.llvm.org/docs/AttributeReference.html#musttail):
  matching transfer signatures/lifetimes; iterative dispatch remains the fallback.
- [Emscripten pthreads](https://emscripten.org/docs/porting/pthreads.html),
  [networking](https://emscripten.org/docs/porting/networking.html), and
  [GL](https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html):
  worker/isolation, browser transport and graphics constraints.
- [JNI specification](https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html):
  guest table layouts and reference/callback lifecycle.
- [Unicorn](https://www.unicorn-engine.org/) and
  [QEMU](https://www.qemu.org/docs/master/user/main.html): test-only execution
  references. They are related implementations; use ARM hardware for disputed behavior.
- [SoftFloat](https://www.jhauser.us/arithmetic/SoftFloat.html),
  [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2),
  [Capstone](https://github.com/capstone-engine/capstone),
  [pyelftools](https://github.com/eliben/pyelftools), and
  [N64Recomp](https://github.com/N64Recomp/N64Recomp): reusable components or
  technical references, subject to their actual license terms and scope.

## Remaining evidence gaps

No exact authorized Roblox binaries/assets are present for current integration.
LARL is ABI/lifecycle reference code, not evidence of full client compatibility.
Each client needs version/hash/dependency inventory and an original-ARM baseline
on an authorized compatible local server. Documentation or an HTTP/socket success
must never substitute for actual rendering, replicated gameplay and reconnects.
