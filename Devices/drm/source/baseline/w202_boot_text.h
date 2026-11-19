/* SPDX-License-Identifier: GPL-2.0-only */
/* Optional board contract selector; Linux fbcon supplies text and raster. */
#ifndef W202_BOOT_TEXT_H
#define W202_BOOT_TEXT_H
#include "../pj4edk-boot-display/pj4edk-boot-display-core.h"

static inline int w202_boot_viewport_select(struct pj4edk2_fbcon_viewport *view,
                                           const unsigned int *v,
                                           unsigned int count)
{
    return v && count == PJ4EDK2_DISPLAY_CELLS && v[10] == 1920 &&
        v[11] == 1200 && pj4edk_boot_display_layout(v) &&
        pj4edk2_fbcon_viewport(v, view);
}
#endif
