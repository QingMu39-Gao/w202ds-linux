/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test of the same optional-request selector used by native DRM. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../w202_boot_text.h"

int main(void)
{
	const unsigned int good[20] = {
		2, 2, 0, 0xb0000000, 0x9e4000, 9216000, 1920, 1200, 7680, 2,
		1920, 1200, 120, 84, 1640, 960, 0, 90, 4, 1,
	};
	struct pj4edk2_fbcon_viewport view;
	unsigned int v[20], i;

	/* Normal boots and truncated/extended descriptors select normal fbdev. */
	assert(!w202_boot_viewport_select(&view, NULL, 0));
	assert(!w202_boot_viewport_select(&view, good, 19));
	assert(!w202_boot_viewport_select(&view, good, 21));
	assert(w202_boot_viewport_select(&view, good, 20));
	assert(view.x == 128 && view.y == 92);
	assert(view.width == 1624 && view.height == 944);


	/* Reject incompatible ABI, addresses, formats and overflowing rectangles. */
	for (i = 0; i < 20; i++) {
		memcpy(v, good, sizeof(v));
		v[i] = ~0u;
		assert(!w202_boot_viewport_select(&view, v, 20));
	}
	memcpy(v, good, sizeof(v));
	v[14] = 0;
	assert(!w202_boot_viewport_select(&view, v, 20));
	v[10] = 1280; v[11] = 800;
	v[12] = 80; v[13] = 56; v[14] = 1093; v[15] = 640;
	assert(!w202_boot_viewport_select(&view, v, 20));
	puts("PASS: absent, malformed, overflow, native landscape and rejected legacy display requests");
	return 0;
}
