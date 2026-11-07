/* Public, freestanding input for the ARM -> SLEIGH -> C++ -> WASM pipeline.
 * SPDX-License-Identifier: Apache-2.0
 * Build in ARM and Thumb modes, at -O0 and -O2. No host services are needed.
 */
typedef unsigned int u32;
_Static_assert(sizeof(u32) == 4, "fixture requires 32-bit unsigned int");

volatile u32 fixture_output[16];

__attribute__((noinline)) static u32 mix(u32 value, u32 salt) {
    value ^= salt + 0x9e3779b9u;
    value = (value << 5) | (value >> 27);
    return value + (salt ^ 0xa5a5a5a5u);
}

__attribute__((noinline)) u32 fixture_entry(u32 seed) {
    u32 value = 0x12345678u ^ seed;
    u32 sum = 0;
    for (u32 i = 0; i != 16; ++i) {
        value = mix(value, i + 7);
        if (value & 1u)
            sum += value;
        else
            sum ^= value;
        fixture_output[i] = sum;
    }
    return sum ^ value;
}
