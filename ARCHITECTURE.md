# Implemented architecture

This describes the reconstruction checkpoint, not all planned functionality.
See `TEST_RESULTS.md` for executed evidence and `PLAN.md` for future gates.

## ELF image and relocations

`recomp/elf/elf_image.hpp` exposes `rearm::elf::Image::load/parse`, sparse PT_LOAD
segments, symbol/relocation metadata, checked guest-address reads, symbol lookup,
and batch relocation. Guest addresses are 32-bit values, never host pointers.
File offset and virtual address are distinct; valid BSS is zero-filled. Section
metadata is optional for mapping; dynamic metadata supplies dynamic symbols and
REL records. The current relocation subset is NONE, ABS32, REL32, GLOB_DAT,
JUMP_SLOT and RELATIVE. A resolver callback supplies external symbol addresses.
The batch validates/resolves before mutating bytes and rejects reapplication.

The `rearm_elf` CMake library is independently buildable with
`REARM_BUILD_SLEIGH_TOOLS=OFF`. `elf_tests` exercises 39 deterministic loader and
relocation cases. Those tests passed locally; they do not establish complete
Android linker compatibility.

## SLEIGH integration and static emission

The original `sla_probe` remains an M0 smoke tool. Its file-offset assumption is
not the production loader contract. New `recomp/lift` code adapts the ELF image
to SLEIGH and captures instructions, raw p-code operations, varnodes, register
definitions and userop names. LOAD/STORE space operands are represented by stable
space identity, avoiding serialization of host AddrSpace pointers.

The new discovery worklist follows direct p-code edges and function-symbol seeds
with ARM/Thumb mode and context state. SLEIGH parser lookahead may be padded, but
the actual consumed instruction bytes must be executable and mapped. Unknown
indirect destinations remain explicit dispatcher checks. This code passes the
early translated execution gate on the documented public fixture subset.

The C++ SLEIGH library builds from pinned sources. The emitter statically lowers
integer p-code to explicit C++ operations with byte-addressed overlapping register
and temporary storage. Instruction-local branches become labels; guest transfers
use bounded iterative dispatch indexed by address and ISA state. Guest memory uses
sparse mapped segments and a separate guest stack. Unsupported operations fail
with guest-address diagnostics. The integrated integer-subset IR verifier checks
operation shapes, register slices, internal CFG edges and definite temporary-byte
initialization;47 verifier tests pass. The full IR milestone remains open.

## Independent validation and measurements

Public freestanding C/linker fixtures cover loops, calls, arithmetic, branches,
stack access and observable guest memory at ARM/Thumb O0/O2. A separate Python ELF
fixture reader and Unicorn oracle do not import the production ELF implementation.
The entry contract specifies seed registers, guest stack and a return sentinel.
JSON comparisons include every general register, meaningful CPSR bits and the
observed output range. Sixteen cases now pass across native generated execution,
Node WASM and Chromium WASM against Unicorn (48 full-state backend comparisons).

`tools/validate_e2e.py` orchestrates source compilation, the compiler CLI, native
and Emscripten compilation, Node/optional Chromium execution and state comparison.
It records elapsed stage time, available sampled process-tree RSS, artifact
size/hash and mismatches. That instrumentation starts with the first emitter;
scalability qualification remains R31.

## Build boundary

Native compiler processes use C++20. Generated C++ is intended for native Clang
and Emscripten, with no runtime ARM/p-code interpretation. The pinned Linux
bootstrap supplies SLEIGH/Ghidra/emsdk and oracle/browser test packages. Native
Windows tests use portable LLVM MinGW. Native Linux Clang18 builds, CTest and64
generated execution comparisons also pass under WSL. The early translated E2E has passed
for the public fixtures. Linux baseline, Android runtime bridges and Roblox
compatibility are not yet verified. Detailed public metrics are in tests/evidence/.
