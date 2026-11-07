# Early pipeline fixtures

`early_pipeline.c` is authored test source, not extracted game code. It exercises
calls, loops, branches, arithmetic, shifts, stack access, and observable guest
memory without libc. Compile each combination of `-marm`/`-mthumb` and `-O0`/`-O2`.

Example with an ARM-capable Clang and LLD:

```sh
clang --target=armv7-none-eabi -march=armv7-a -marm -O2 \
  -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib \
  -fuse-ld=lld -Wl,-T,tests/fixtures/early_pipeline.ld \
  tests/fixtures/early_pipeline.c -o build/fixtures/early-arm-o2.elf
python tools/validate_e2e.py --elf build/fixtures/early-arm-o2.elf \
  --reference-out build/fixtures/early-arm-o2.reference.json
```

The validator starts at ELF symbol `fixture_entry`, sets R0 to the case seed and
R1–R12 to zero, uses
SP `0x7ff00000`, LR `0xffff0000`, and a zero-filled 64-KiB stack below SP.
The entry symbol's low bit determines ARM/Thumb state. Returning to the sentinel
ends execution. `fixture_output` (64 bytes) is the default observed memory range.

The generated runner must report the same complete register array and observed
guest bytes. CPSR comparison defaults to NZCV and T; exception-level differences
are outside this fixture's contract. Raw reference JSON retains the full CPSR.
Reference instruction counts are diagnostics and are not compared to translated
host instruction counts.

See `python tools/validate_e2e.py --help` for comparison and optional build steps.

Full validation builds four source variants and checks four deterministic seeds
on each selected backend:

```sh
python tools/validate_e2e.py --rearm build/recomp/rearm \
  --clang clang --cxx clang++ \
  --emxx third_party/emsdk/upstream/emscripten/em++ \
  --sla third_party/sla/ARM/ARM7_le.sla --node node --browser
```

The JSON report records per-stage elapsed time, sampled peak process-tree RSS
when psutil is installed, artifact sizes/hashes, reference state, actual state,
and every state mismatch. Sampled RSS is not an exact operating-system high-water
mark. Browser wall time includes Chromium startup; it is not a guest execution
microbenchmark. Larger seeded suites use `--cases N` without changing production
code. The first four seeds are zero, one, UINT32_MAX, and the sign bit; subsequent
seeds use a fixed xorshift sequence.

Additional inputs use the same runner contract and can be selected with
`--fixture tests/fixtures/condition_flags.c` or
`--fixture tests/fixtures/interworking.c`. The latter deliberately mixes ARM and
Thumb functions in one ELF and performs volatile function-pointer calls. Its
targets also have direct call sites, separating indirect dispatch testing from
unknown-target discovery.

`thumb_it.S` is a minimal regression for SLEIGH IT decode-context leakage into
the following instruction. It returns 7 for seed zero and 9 otherwise, writes
the result to `fixture_output`, and must execute the following MOVW/MOVT
unconditionally. Select it with `--fixture tests/fixtures/thumb_it.S --modes thumb`.

`unsupported_svc.c` is a negative input. Verify rejection rather than execution:

```sh
python tests/reference/check_unsupported.py --rearm build/recomp/rearm \
  --clang clang --sla third_party/sla/ARM/ARM7_le.sla
```

Both ARM and Thumb must fail translation with the guest address, ISA mode, and
`software_interrupt` userop in the diagnostic. A successful translation is a
test failure for the initial CPU-only profile.
