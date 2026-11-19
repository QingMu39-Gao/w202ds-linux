/* SPDX-License-Identifier: MIT */
#include "modes.h"

.section .text,"ax"
.global lk_logo_selected, lk_logo_fastboot_enter, lk_logo_clear
.global lk_logo_text

/* Record the real mode before dispatching, then run the original log. */
lk_logo_selected:
    stp x0, x1, [sp, #-32]!
    stp x2, x30, [sp, #16]
    mov w0, w1
    bl lk_logo_update
    ldp x2, x30, [sp, #16]
    ldp x0, x1, [sp], #32
    b lk_log_mode

/* This entry can be reached without the native Fastboot splash handler. */
lk_logo_fastboot_enter:
    stp x0, x30, [sp, #-16]!
    mov w0, #LK_MODE_FASTBOOT
    bl lk_logo_update
    ldp x0, x30, [sp], #16
    b lk_log_fastboot

lk_logo_clear:
    adr x16, lk_logo_visible
    str wzr, [x16]
    b lk_lcd_flush

/* The native warning screen in logo_display prints text without a bitmap. */
lk_logo_text:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    bl lk_warning_line
    ldp x29, x30, [sp], #16
    b lk_logo_present

/* Native callers that show an explicit mode before the selector runs, or
 * enter Fastboot after a different mode was selected. Forward all args. */
.macro mode_hook name, mode, original
    .global \name
\name:
    adr x16, lk_logo_mode
    mov w17, #\mode
    str w17, [x16]
    b \original
.endm

mode_hook lk_logo_low_bitmap, LK_MODE_LOW_BATTERY, lk_draw_bitmap
mode_hook lk_logo_no_battery_bitmap, LK_MODE_NO_BATTERY, lk_draw_bitmap
mode_hook lk_logo_charge_bitmap, LK_MODE_CHARGE, lk_draw_bitmap
mode_hook lk_logo_charge_splash, LK_MODE_CHARGE, lk_draw_splash
mode_hook lk_logo_fastboot_splash, LK_MODE_FASTBOOT, lk_draw_splash

.section .note.GNU-stack,"",%progbits
