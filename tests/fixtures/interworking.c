/* SPDX-License-Identifier: Apache-2.0 */
typedef unsigned int u32;
typedef u32 (*Callback)(u32);
volatile u32 fixture_output[16];

__attribute__((noinline, target("arm"))) u32 arm_step(u32 value) {
    return (value ^ 0xa55aa55au) + 17;
}

__attribute__((noinline, target("thumb"))) u32 thumb_step(u32 value) {
    return (value << 3) ^ (value >> 2) ^ 0x13579bdfu;
}

/* Volatile prevents the compiler from replacing these guest indirect calls
 * with direct calls. Both target functions are reachable through direct calls
 * as well, so this tests dispatch separately from unknown-target discovery. */
static Callback volatile callback = arm_step;

__attribute__((noinline)) u32 fixture_entry(u32 seed) {
    u32 value = arm_step(seed);
    value = thumb_step(value);
    for (u32 i = 0; i < 16; ++i) {
        callback = (i & 1) ? arm_step : thumb_step;
        value = callback(value + i);
        fixture_output[i] = value;
    }
    return value;
}
