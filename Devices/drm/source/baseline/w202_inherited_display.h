/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_INHERITED_DISPLAY_H
#define W202_INHERITED_DISPLAY_H

/* Read-only compatibility check, shared with host tests. This accepts only
 * the reserved, unscaled landscape framebuffer used for W202DS handoff.
 * Register compatibility alone is insufficient: the caller must also prove
 * that clocks/PHY/reset are live and that new VSYNC interrupts arrive.
 */
struct w202_inherited_display {
	unsigned int version, control, interface, panel, blend;
	unsigned int layers, address, layer_control, source, destination;
	unsigned int stride, position, alpha, crop, scaling, enhancement;
	unsigned int vau_control, vau_start, dpi, h_timing, v_timing;
	unsigned int host_reset, host_mode, format, packet, hblank, line_time;
	unsigned int vblank, vactive, hs_clock, phy_control, phy_status, lanes;
	unsigned int dsc[23];
};

static const unsigned int w202_inherited_dsc[23] = {
	0x2000000b, 0x078004b0, 0x001400c8, 0x05000fa0,
	0x09c0a004, 0x58001c08, 0x306c8200, 0x00080202,
	0x0494050e, 0x10f01800, 0x003316b6, 0x382a1c0e,
	0x69625446, 0x7b797770, 0x00007e7d, 0x01000102,
	0x09be0940, 0x19fa19fc, 0x1a3819f8, 0x1ab61a78,
	0x2b342af6, 0x3b742b74, 0x00006bf4,
};

/* Returns NULL on compatibility; mode is 0 (60 Hz) or 1 (90 Hz). */
static inline const char *w202_inherited_display_check(
		const struct w202_inherited_display *s, unsigned int *mode)
{
	unsigned int i, vfp;

	if (s->version != 0x600 || (s->control & 2) || (s->interface & 3))
		return "DPU stopped or incompatible interface";
	if (s->panel != 0x078004b0 || s->blend != s->panel ||
	    s->layers != 1 || s->address != 0xb0000000 ||
	    (s->layer_control & 0x017007f0) != 0x00100030 ||
	    s->source != 0x04b00780 || s->destination != s->source ||
	    s->stride != 1920 || s->position || (s->alpha & 0xff) != 0xff ||
	    s->crop || s->scaling || s->enhancement)
		return "incompatible framebuffer or composition";
	if (s->vau_control != 8 || s->vau_start)
		return "VAU is not in physical bypass";
	if (!(s->dpi & (1U << 16)) || s->h_timing != 0x09c0a004)
		return "incompatible DPI timing";
	if (s->v_timing == 0x58001c08) {
		*mode = 0;
		vfp = 1408;
	} else if (s->v_timing == 0x12001c08) {
		*mode = 1;
		vfp = 288;
	} else {
		return "unsupported refresh timing";
	}
	if (s->host_reset != 1 || s->host_mode != 2 ||
	    (s->format & 0x3f) != 12 || s->packet != 1200 ||
	    s->hblank != 0x0001003a ||
	    (s->line_time != 553 && s->line_time != 554) ||
	    s->vblank != ((vfp & 0x3ff) | (28U << 10) | (8U << 20)) ||
	    s->vactive != 1920 || !(s->hs_clock & 1) ||
	    (s->phy_control & 7) != 7 || !(s->phy_status & 2) || s->lanes != 3)
		return "DSI/DPHY is not compatible video scanout";
	for (i = 0; i < 23; i++) {
		unsigned int expected = i == 5 ? s->v_timing : w202_inherited_dsc[i];

		if (s->dsc[i] != expected)
			return "incompatible DSC profile";
	}
	return 0;
}
#endif
