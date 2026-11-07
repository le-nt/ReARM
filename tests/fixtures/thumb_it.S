/* SPDX-License-Identifier: Apache-2.0 */
.syntax unified
.thumb
.text
.global fixture_entry
.type fixture_entry,%function
.thumb_func
fixture_entry:
    cmp r0, #0
    ite eq
    moveq r0, #7
    movne r0, #9
    movw r1, #:lower16:fixture_output
    movt r1, #:upper16:fixture_output
    str r0, [r1]
    bx lr
.size fixture_entry, .-fixture_entry

.bss
.balign 4
.global fixture_output
.type fixture_output,%object
fixture_output:
    .space 64
.size fixture_output, .-fixture_output
