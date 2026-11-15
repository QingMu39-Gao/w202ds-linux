// SPDX-License-Identifier: GPL-2.0-only
/* Host test: source capability must never be inferred from stale/invalid CC. */
#include <assert.h>
#include <stdio.h>
#include "../w202_usb_source.h"

int main(void)
{
	static const struct {
		unsigned int status;
		enum w202_usb_bc12_type type;
	} bc_results[] = {
		{ 0x804, W202_BC12_UNKNOWN },
		{ 0x824, W202_BC12_CDP },
		{ 0x844, W202_BC12_DCP },
		{ 0x864, W202_BC12_UNKNOWN },
		{ 0x884, W202_BC12_SDP },
		{ 0x8a4, W202_BC12_UNKNOWN },
		{ 0x8c4, W202_BC12_UNKNOWN },
		{ 0x8e4, W202_BC12_UNKNOWN },
	};
	unsigned int state, rp, extra, i, raw;

	assert(w202_usb_bc12_type(w202_usb_fresh_bc12_status(0x844, W202_BC12_UNKNOWN)) == W202_BC12_UNKNOWN);
	assert(w202_usb_source_current(w202_usb_fresh_bc12_status(0x844, W202_BC12_UNKNOWN), 1, 2, 0x100) == 500000);
	assert(w202_usb_source_current(w202_usb_fresh_bc12_status(0x884, W202_BC12_DCP), 1, 2, 0x100) == 1500000);
	assert(w202_usb_bc12_type(w202_usb_fresh_bc12_status(0x840, W202_BC12_DCP)) == W202_BC12_UNKNOWN);

	/* Exhaust every relevant UMP9620 status bit, including stale result,
	 * missing completion, removal and multiple type flags. Extra bits do
	 * not turn an ambiguous BC1.2 result into a recognized charger.
	 */
	for (extra = 0; extra <= 0xffff; ++extra) {
		if (extra & 0x8e4)
			continue;
		for (i = 0; i < sizeof(bc_results) / sizeof(bc_results[0]); ++i) {
			raw = extra | bc_results[i].status;
			assert(w202_usb_bc12_type(raw) == bc_results[i].type);
			assert(w202_usb_bc12_type(raw & ~4) == W202_BC12_UNKNOWN);
			assert(w202_usb_bc12_type(raw & ~0x800) == W202_BC12_UNKNOWN);
			assert(w202_usb_bc12_type(raw & ~0x804) == W202_BC12_UNKNOWN);
			assert(w202_usb_source_current(raw, 0, 0, 0) ==
				w202_usb_source_current(bc_results[i].status, 0, 0, 0));
		}
	}

	assert(w202_usb_source_current(0, 1, 0x82, 0x100) == 0);
	assert(w202_usb_source_current(4, 0, 0x82, 0x100) == 500000);
	assert(w202_usb_source_current(4, 1, 0x82, 0) == 500000);
	assert(w202_usb_source_current(4, 1, 0x82, 0x100) == 3000000);
	assert(w202_usb_source_current(4, 1, 0x42, 0x100) == 1500000);
	assert(w202_usb_source_current(4, 1, 2, 0x100) == 500000);
	assert(w202_usb_source_current(4, 1, 0xc2, 0x100) == 500000);
	assert(w202_usb_source_current(0x44, 0, 0, 0) == 500000); /* BC not done */
	assert(w202_usb_source_current(0x844, 0, 0, 0) == 1500000); /* DCP */
	assert(w202_usb_source_current(0x824, 0, 0, 0) == 1500000); /* CDP */
	assert(w202_usb_source_current(0x884, 0, 0, 0) == 500000); /* SDP */
	assert(w202_usb_source_current(0x8e4, 0, 0, 0) == 500000); /* ambiguous */
	for (state = 0; state < 32; state++) {
		if (state == 2)
			continue;
		for (rp = 0; rp < 4; rp++)
			assert(w202_usb_source_current(4, 1, state | (rp << 6), 0x100) == 500000);
	}
	/* A contract can be lower than Rp. PD can also grant more than default
	 * Rp; a non-PD provider cannot. Clamp an invalid oversized budget.
	 */
	assert(w202_usb_sink_budget(3000000, 1000000, 1) == 1000000);
	assert(w202_usb_sink_budget(500000, 3000000, 1) == 3000000);
	assert(w202_usb_sink_budget(500000, 3000000, 0) == 500000);
	assert(w202_usb_sink_budget(3000000, 500000, 0) == 500000);
	assert(w202_usb_sink_budget(3000000, 0, 1) == 0);
	assert(w202_usb_sink_budget(3000000, 6000000, 1) == 3000000);
	puts("USB source: 65536 BC1.2 status words, stale CC and TCPM budgets passed");
	return 0;
}
