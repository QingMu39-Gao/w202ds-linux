/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../w202_inherited_display.h"

static struct w202_inherited_display valid(void)
{
	struct w202_inherited_display s = {
		.version = 0x600, .control = 1, .panel = 0x078004b0,
		.blend = 0x078004b0, .layers = 1, .address = 0xb0000000,
		.layer_control = 0x00100034, .source = 0x04b00780,
		.destination = 0x04b00780, .stride = 1920, .alpha = 255,
		.vau_control = 8, .dpi = 0x10000, .h_timing = 0x09c0a004,
		.v_timing = 0x58001c08, .host_reset = 1, .host_mode = 2,
		.format = 12, .packet = 1200, .hblank = 0x1003a,
		.line_time = 554, .vblank = 0x807180, .vactive = 1920,
		.hs_clock = 1, .phy_control = 7, .phy_status = 0x1f02, .lanes = 3,
	};
	memcpy(s.dsc, w202_inherited_dsc, sizeof(s.dsc));
	return s;
}

int main(void)
{
	struct w202_inherited_display good = valid(), s;
	unsigned int mode = 99, i;

	assert(!w202_inherited_display_check(&good, &mode) && mode == 0);
	s = good;
	s.v_timing = s.dsc[5] = 0x12001c08;
	s.vblank = 0x807120;
	assert(!w202_inherited_display_check(&s, &mode) && mode == 1);
	/* Reject every corrupted critical field, including another valid-looking
	 * framebuffer that is not owned by the reserved handoff aperture. */
	for (i = 0; i < sizeof(s) / sizeof(unsigned int); i++) {
		unsigned int *fields = (unsigned int *)&s;

		s = good;
		fields[i] ^= i == 1 ? 2 : 1;
		/* RUN and unrelated status/control bits are deliberately not proof
		 * of scanout; the driver must observe fresh IRQs separately. */
		if (i == 31) /* PHY status: flip lock, not an unrelated status bit. */
			fields[i] = good.phy_status ^ 2;
		if (i == 7) /* Layer format, not an unrelated enable bit. */
			fields[i] = good.layer_control ^ 0x10;
		if (i == 18) /* DPI video enable. */
			fields[i] = good.dpi ^ 0x10000;
		if (i == 26) /* Reject unsupported line times, not accepted rounding. */
			fields[i] = 555;
		assert(w202_inherited_display_check(&s, &mode));
	}
	puts("inherited 60/90 Hz scanout and incompatible-state rejection passed");
	return 0;
}
