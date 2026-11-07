/* SPDX-License-Identifier: Apache-2.0 */
typedef unsigned int u32;
volatile u32 fixture_output[16];
u32 fixture_data[4] = {0x12345678u, 0xffffffffu, 0x80000000u, 19u};
static u32 local_data[4] = {3u, 5u, 7u, 11u};

/* Default-visible data and private pointers exercise both symbol-based GOT
 * bindings and base-relative pointer relocation in a real PIC shared object. */
u32 *volatile fixture_data_pointer = fixture_data;
static u32 *volatile local_data_pointer = local_data;

__attribute__((noinline)) static u32 step(u32 value) {
    return (value << 7) ^ (value >> 25) ^ 0xa55aa55au;
}
static u32 (*volatile local_callback)(u32) = step;

__attribute__((noinline)) u32 fixture_entry(u32 seed) {
    u32 *first = fixture_data_pointer;
    u32 *second = local_data_pointer;
    first[seed & 3] ^= seed;
    u32 value = seed;
    for (u32 i = 0; i != 16; ++i) {
        /* The direct call site also supplies a discoverable code entry point. */
        value = step(value + first[i & 3]);
        value = local_callback(value ^ second[i & 3]);
        fixture_output[i] = value;
    }
    return value;
}
