/* SPDX-License-Identifier: GPL-2.0-only */
/* Text layout for the DRM client's bounded, rotated RGB565 boot window. */
#ifndef W202_BOOT_TEXT_H
#define W202_BOOT_TEXT_H

#define W202_TEXT_COLS 160
#define W202_TEXT_ROWS 50
#define W202_TEXT_W 12
#define W202_TEXT_H 24

#include "common/include/pj4edk2_terminal.h"
#define w202_boot_text pj4edk_terminal
#define w202_boot_text_put pj4edk_character

static inline int w202_boot_text_init(struct w202_boot_text *t, const unsigned int v[20])
{

	if (v[0] != 2 || v[1] != 2 || v[2] || v[3] != 0xb0000000 ||
	    v[4] != 0x9e4000 || v[5] != 9216000 || v[6] != 1920 ||
	    v[7] != 1200 || v[8] != 7680 || v[9] != 3 ||
	    v[10] != 1920 || v[11] != 1200 ||
	    v[12] > v[10] || v[13] > v[11] ||
	    v[14] > v[10]-v[12] || v[15] > v[11]-v[13] ||
	    v[14] < 128 || v[15] < 80 || v[16] ||
	    v[17] != 90 || v[18] != 4 || v[19] != 1)
		return -1;
    return pj4edk_terminal_layout(t,v,0,0);
}

/* This is an optional display request, never a prerequisite for normal KMS.
 * Callers decode OF cells first; an absent/malformed request selects fbdev.
 */
static inline int w202_boot_text_select(struct w202_boot_text *t,
				 const unsigned int *v, unsigned int count)
{
	return v && count == 20 && !w202_boot_text_init(t, v);
}

#endif
