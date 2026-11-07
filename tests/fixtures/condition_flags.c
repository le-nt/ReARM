/* SPDX-License-Identifier: Apache-2.0 */
typedef unsigned int u32;
typedef signed int s32;
volatile u32 fixture_output[16];

/* Defined-width C expressions exercise carry/borrow, signed comparisons,
 * predication, and shifts at zero and 31 without C signed-overflow UB. */
__attribute__((noinline)) u32 fixture_entry(u32 seed) {
    u32 value = seed;
    for (u32 i = 0; i < 16; ++i) {
        u32 other = (0x80000000u >> (i & 7)) ^ (0xffffffffu - i);
        u32 sum = value + other;
        u32 carry = sum < value;
        u32 borrow = value < other;
        u32 overflow = ((value ^ sum) & (other ^ sum)) >> 31;
        u32 shift = (i & 1) ? 31 : 0;
        u32 rotated = (sum << shift) | (sum >> ((32 - shift) & 31));
        if ((s32)value < (s32)other)
            value = rotated ^ (carry | (borrow << 1) | (overflow << 2));
        else
            value = (value - other) + (carry << 3) + (borrow << 4);
        fixture_output[i] = value;
    }
    return value;
}
