/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_LAYERS_H
#define W202_LAYERS_H

#include "w202_rotation.h"

/* r6p0 layer register layout, also used by the W205 vendor driver. */
#define W202_LAY_STRIDE		0x40
#define W202_LAY_ADDR		0x30
#define W202_LAY_CTRL		0x40
#define W202_LAY_DST_SIZE	0x44
#define W202_LAY_SRC_SIZE	0x48
#define W202_LAY_PITCH		0x4c
#define W202_LAY_POS		0x50
#define W202_LAY_ALPHA		0x54
#define W202_LAY_CROP		0x60

#define W202_LAY_PRIMARY	0
#define W202_LAY_OVERLAY	1
#define W202_LAY_CURSOR		2

#define W202_LAY_CONST_ALPHA	(1u << 2)
#define W202_LAY_COMBO_ALPHA	(1u << 3)
#define W202_LAY_ARGB8888	(3u << 4)
#define W202_LAY_RGB565		(4u << 4)
#define W202_LAY_PREMULT	(1u << 16)
#define W202_LAY_ROTATION_SHIFT	20

static inline unsigned int w202_pair(unsigned int lo, unsigned int hi)
{
	return lo | (hi << 16);
}

#endif
