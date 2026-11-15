// SPDX-License-Identifier: GPL-2.0-only
/* Bridge the PMIC's fresh PHY-owned BC1.2 result to Linux TCPM. */
#include "controller.h"

void w202_typec_charger_state(struct w202_typec *pd, bool sink)
{
	if (pd->usb_phy)
		usb_phy_set_charger_state(pd->usb_phy,
			sink ? USB_CHARGER_PRESENT : USB_CHARGER_ABSENT);
}

int w202_typec_charger_type(struct w202_typec *pd)
{
	if (!pd->usb_phy ||
	    READ_ONCE(pd->usb_phy->chg_state) != USB_CHARGER_PRESENT)
		return POWER_SUPPLY_USB_TYPE_UNKNOWN;
	switch (READ_ONCE(pd->usb_phy->chg_type)) {
	case SDP_TYPE: return POWER_SUPPLY_USB_TYPE_SDP;
	case DCP_TYPE: return POWER_SUPPLY_USB_TYPE_DCP;
	case CDP_TYPE: return POWER_SUPPLY_USB_TYPE_CDP;
	default: return POWER_SUPPLY_USB_TYPE_UNKNOWN;
	}
}

int w202_typec_charger_current(struct tcpc_dev *tcpc)
{
	struct w202_typec *pd = tcpc_to_w202(tcpc);
	u32 state, debug;
	int type, ret;

	ret = regmap_read(pd->regmap, pd->typec_base + SC27XX_TYPEC_STATUS, &state);
	if (!ret)
		ret = regmap_read(pd->regmap, pd->typec_base + SC27XX_TYPEC_DBG1, &debug);
	if (ret || (state & SC27XX_STATE_MASK) != 2 || !(debug & SC27XX_TYPEC_VBUS_OK))
		return 0;
	type = w202_typec_charger_type(pd);
	/* TCPM invokes this only for default Rp. Explicit Rp and negotiated
	 * PD/PPS keep their standard TCPM path. An absent/old PHY supplies
	 * no fresh BC1.2 result, so it cannot raise the 500 mA fallback.
	 */
	return type == POWER_SUPPLY_USB_TYPE_DCP ||
		type == POWER_SUPPLY_USB_TYPE_CDP ? 1500 : 500;
}
