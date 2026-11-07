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
