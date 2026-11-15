/* SPDX-License-Identifier: GPL-2.0-only */
/* Board acceptance policy; raster/text/damage live in the shared project. */
#ifndef W202_PJ4EDK_CORE_H
#define W202_PJ4EDK_CORE_H
#define pj4edk_terminal_init pj4edk_terminal_init_common
#include "common/include/pj4edk2_terminal.h"
#undef pj4edk_terminal_init
static int pj4edk_terminal_init(struct pj4edk_terminal *t, const unsigned int *v,
                               unsigned short *pixels, const unsigned char *font)
{
    if (!pj4edk2_display_validate_cells(v, PJ4EDK2_DISPLAY_CELLS) ||
        v[0] != 2 || v[2] || v[3] != 0xb0000000 ||
        v[4] != 0x9e4000 || v[5] != 9216000 || v[6] != 1920 ||
        v[7] != 1200 || v[8] != 7680 || v[9] != 3 ||
        !((v[10] == 1280 && v[11] == 800) ||
          (v[10] == 1920 && v[11] == 1200)) || v[16] != 0 ||
        v[17] != 90 || v[14] < 128 || v[15] < 80)
        return -1;
    return pj4edk_terminal_init_common(t,v,pixels,font);
}
#endif
