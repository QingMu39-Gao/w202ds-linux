// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../otg-fault.h"

int main(void)
{
	int history, current;

	/* Test all fault combinations: only an absent current fault with
	 * either no history or the unplug-related input fault may continue.
	 */
	for (history = 0; history < 256; history++)
		for (current = 0; current < 256; current++)
			assert(!!w202_bq_otg_fault(history, current) ==
				(current != 0 || (history != 0 && history != 0x10)));
	assert(w202_bq_otg_fault(-EIO, 0));
	assert(w202_bq_otg_fault(0, -EIO));
	assert(w202_bq_otg_fault(-EIO, -EIO));
	puts("PASS: all 65536 OTG fault pairs; old input loss preserved, current/protection/I/O faults stop");
	return 0;
}
