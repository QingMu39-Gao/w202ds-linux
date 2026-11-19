/* SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include <stdint.h>
#include "modes.h"

/* Symbols from the pinned W202DS LK. The decoder and lcd_enable use the
 * same framebuffer, with 16-bit pixels even though LK's bpix code is 8. */
extern const uint8_t lk_font[][16];
extern const char *const lk_mode_names[LK_MODE_LIMIT];
extern const volatile uint8_t lk_lcd_enabled, lk_lcd_bpix;
extern const volatile uint16_t lk_lcd_width, lk_lcd_height;
extern const volatile uint32_t lk_lcd_pitch;
extern const volatile uintptr_t lk_lcd_base;
extern void lk_lcd_flush(void);

#define FB_ADDRESS UINT32_C(0xb0000000)
#define FB_WIDTH 1200
#define FB_HEIGHT 1920
#define DPU_REG(offset) (*(const volatile uint32_t *)(0x31000000ul + (offset)))

/* LK maps its image writable; these start at zero in the checked padding. */
volatile unsigned lk_logo_mode __attribute__((section(".data.splash"))) = 0;
volatile unsigned lk_logo_visible __attribute__((section(".data.splash"))) = 0;

static bool paint_mode(unsigned mode)
{
    const char *p = "LK MODE: ";
    const char *label = mode == LK_MODE_LOW_BATTERY ? "LOW BATTERY" :
                        mode == LK_MODE_NO_BATTERY ? "NO BATTERY" :
                        mode == 0 ? "DETECTING" :
                        mode < LK_MODE_LIMIT ? lk_mode_names[mode] : "UNKNOWN";
    if (!lk_lcd_enabled || lk_lcd_base != FB_ADDRESS || lk_lcd_bpix != 8 ||
        lk_lcd_width != FB_WIDTH || lk_lcd_height != FB_HEIGHT ||
        lk_lcd_pitch != FB_WIDTH * sizeof(uint16_t))
        return false;

    volatile uint16_t *fb = (void *)(uintptr_t)FB_ADDRESS;
    for (unsigned i = 0; i < 32; ++i) {
        if (i == sizeof("LK MODE: ") - 1)
            p = label;
        unsigned c = *p ? (uint8_t)*p++ : ' ';
        for (unsigned bit = 0; bit < 8; ++bit) {
            /* The LK layer rotates RGB565 by 180 degrees; the landscape
             * screen maps onto the portrait panel at 90 degrees. Map
             * landscape (x, y) to source (y, 1919 - x). Double the font. */
            unsigned x = 24 + (i * 8 + bit) * 2;
            volatile uint16_t *pixel = fb + (FB_HEIGHT - 1 - x) * FB_WIDTH + 24;
            for (unsigned row = 0; row < 16; ++row, pixel += 2) {
                uint16_t value = lk_font[c][row] & (0x80 >> bit) ? 0xffff : 0;
                pixel[0] = pixel[1] = pixel[-FB_WIDTH] = pixel[1 - FB_WIDTH] = value;
            }
        }
    }
    return true;
}

/* Replace the one cache flush at the successful native bitmap exit. */
void lk_logo_present(void)
{
    unsigned mode;
    do {
        mode = lk_logo_mode;
        lk_logo_visible = paint_mode(mode);
    } while (mode != lk_logo_mode);
    lk_lcd_flush();
}

/* A preloaded logo might already be on screen when selection finishes, or
 * when native Fastboot is entered after a failed resident load. */
void lk_logo_update(unsigned mode)
{
    if (lk_logo_mode == mode)
        return;
    lk_logo_mode = mode;
    if (lk_logo_visible && DPU_REG(0x2c) == 1 && DPU_REG(0x30) == FB_ADDRESS &&
        (DPU_REG(0x40) & 0x007000f0) == 0x00200040 &&
        DPU_REG(0x4c) == FB_WIDTH)
        lk_logo_present();
}
