/* SPDX-License-Identifier: Apache-2.0 */
typedef unsigned int u32;
volatile u32 fixture_output[16];
volatile unsigned char large_bss[4 * 1024 * 1024];

__attribute__((noinline)) u32 fixture_entry(u32 seed) {
    /* Exercise zero initialization and both ends of a large guest BSS mapping. */
    u32 zero = large_bss[0] | large_bss[sizeof(large_bss) - 1];
    large_bss[0] = (unsigned char)seed;
    large_bss[sizeof(large_bss) - 1] = (unsigned char)(seed >> 8);
    u32 value = zero + large_bss[0] + large_bss[sizeof(large_bss) - 1];
    for (u32 i = 0; i < 16; ++i) fixture_output[i] = value + i;
    return value;
}
