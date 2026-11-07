/* SPDX-License-Identifier: Apache-2.0 */
typedef unsigned int u32;
volatile u32 fixture_output[16];

/* No syscall bridge is implemented in the initial CPU-only profile. The
 * compiler must reject the SLEIGH userop rather than remove it as a no-op. */
__attribute__((noinline)) u32 fixture_entry(u32 seed) {
    __asm__ volatile("svc #0" ::: "memory");
    fixture_output[0] = seed;
    return seed;
}
