/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef W202_OTG_FAULT_H
#define W202_OTG_FAULT_H

/* REG09 first returns latched history, then the current condition. A past
 * charge-input fault (CHRG_FAULT=01) after unplugging a charger is not a
 * boost fault. Preserve it for diagnostics without stopping a healthy OTG
 * output. All current faults and all other historical faults still stop it.
 */
static inline int w202_bq_otg_fault(int history, int fault_now)
{
	return history < 0 || fault_now < 0 || (history & ~0x10) || fault_now;
}

#endif
