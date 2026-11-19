/* SPDX-License-Identifier: GPL-2.0-only */
/* Board acceptance policy. fbcon layout and raster stay in the shared project. */
#ifndef W202_PJ4EDK_CORE_H
#define W202_PJ4EDK_CORE_H
#include "common/include/pj4edk2_fbcon_viewport.h"

static inline int pj4edk_boot_display_layout(const unsigned int *v)
{
    struct pj4edk2_fbcon_viewport view;

    return pj4edk2_fbcon_viewport(v, &view) &&
        v[0] == 2 && !v[2] && v[3] == 0xb0000000 &&
        v[4] == 0x9e4000 && v[5] == 9216000 && v[6] == 1920 &&
        v[7] == 1200 && v[8] == 7680 && v[9] == 2 &&
        ((v[10] == 1280 && v[11] == 800) ||
         (v[10] == 1920 && v[11] == 1200)) && !v[16] &&
        v[17] == 90 && v[14] >= 128 && v[15] >= 80;
}
#endif
