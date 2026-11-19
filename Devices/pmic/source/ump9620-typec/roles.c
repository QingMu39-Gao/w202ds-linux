// SPDX-License-Identifier: GPL-2.0
/* UMP9620 CC/power roles. TCPM owns policy and the USB role-switch graph. */
#include <linux/regulator/consumer.h>
#include "controller.h"

int w202_typec_get_cc(struct tcpc_dev *tcpc, enum typec_cc_status *cc1,
		      enum typec_cc_status *cc2)
{
	struct w202_typec *pd = tcpc_to_w202(tcpc);
	enum typec_cc_status cc;
	u32 value;
	int ret = regmap_read(pd->regmap, pd->typec_base + SC27XX_TYPEC_STATUS, &value);

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;
	if (ret)
		return ret;
	/* W205 sc27xx_typec also treats state 8 (DEBUG_CABLE) as a sink.
	 * Report both Rp to TCPM as a debug accessory, without inventing a
	 * PD-capable cable or interpreting its accessory bits as current.
	 * State 6 needs VCONN, currently unsupported.
	 */
	switch (value & SC27XX_STATE_MASK) {
	case 8:
		*cc1 = TYPEC_CC_RP_DEF;
		*cc2 = TYPEC_CC_RP_DEF;
		return 0;
	case 2:
		switch (SC27XX_TYPEC_VBUS_CL(value)) {
		case 0: cc = TYPEC_CC_RP_DEF; break;
		case 1: cc = TYPEC_CC_RP_1_5; break;
		case 2: cc = TYPEC_CC_RP_3_0; break;
		default: return 0;
		}
		break;
	case 6:
		if (value & SC27XX_TYPEC_FINAL_SWITCH)
			*cc2 = TYPEC_CC_RA;
		else
			*cc1 = TYPEC_CC_RA;
		fallthrough;
	case 5:
		cc = TYPEC_CC_RD;
		break;
	default:
		return 0;
	}
	if (value & SC27XX_TYPEC_FINAL_SWITCH)
		*cc1 = cc;
	else
		*cc2 = cc;
	return 0;
}

int w202_typec_set_cc(struct tcpc_dev *tcpc, enum typec_cc_status cc)
{
	struct w202_typec *pd = tcpc_to_w202(tcpc);
	u32 mode, force = 0, cc_value;
	bool swapping;
	int ret;

	/* Source advertises default USB current, bounded to 500 mA. */
	if (cc != TYPEC_CC_OPEN && cc != TYPEC_CC_RD && cc != TYPEC_CC_RP_DEF)
		return -EOPNOTSUPP;
	mode = cc == TYPEC_CC_RP_DEF ? 1 : 0;
	mutex_lock(&pd->lock);
	if (pd->faulted && cc != TYPEC_CC_OPEN) {
		ret = -ESHUTDOWN;
		goto out;
	}
	swapping = pd->attached && cc != TYPEC_CC_OPEN &&
		((cc == TYPEC_CC_RD) != (pd->power_role == TYPEC_SINK));
	if (cc == TYPEC_CC_OPEN) {
		force = BIT(12) | BIT(14);
	} else if (swapping) {
		/* W205 CC switch values: 1=open, 2=Rd, 3=Rp. Keep the
		 * active CC connected during PR_SWAP; the other is open.
		 */
		cc_value = cc == TYPEC_CC_RD ? 2 : 3;
		force = pd->polarity == TYPEC_POLARITY_CC1 ?
			(cc_value << 12) | BIT(14) : BIT(12) | (cc_value << 14);
	}
	ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_PD_CFG,
				SC27XX_TYPEC_PD_NO_CHEK_DETACH,
				swapping ?
				SC27XX_TYPEC_PD_NO_CHEK_DETACH : 0);
	if (!ret)
		ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_SW_CFG,
					GENMASK(15, 12), force);
	if (!ret)
		ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_MODE,
					GENMASK(3, 0) | SC27XX_TYPEC_TRY_SRC_EN |
					SC27XX_TYPEC_TRY_SINK_EN,
					mode | SC27XX_TYPEC_RP_LEVEL(1));
	if (!ret && swapping)
		ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_WC_REG,
					SC27XX_TYPEC_PD_PR_SWAP, SC27XX_TYPEC_PD_PR_SWAP);
	if (ret) {
		/* TCPM does not check set_cc()'s return value. A partial swap
		 * must not leave detach suppressed or allow source enable.
		 * Keep cleanup pending even if the disconnect writes also fail.
		 */
		pd->cc_forced = true;
		regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_SW_CFG,
				   GENMASK(15, 12), BIT(12) | BIT(14));
		regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_PD_CFG,
				   SC27XX_TYPEC_PD_NO_CHEK_DETACH, 0);
		w202_pd_latch_fault(pd, "cc-role-io-error");
	} else {
		pd->cc_forced = swapping;
	}
out:
	mutex_unlock(&pd->lock);
	if (!ret && READ_ONCE(pd->ready))
		tcpm_cc_change(pd->port);
	return ret;
}

int w202_typec_start_toggling(struct tcpc_dev *tcpc, enum typec_port_type type,
			    enum typec_cc_status cc)
{
	struct w202_typec *pd = tcpc_to_w202(tcpc);
	int ret;

	if (type == TYPEC_PORT_SNK)
		return w202_typec_set_cc(tcpc, TYPEC_CC_RD);
	if (type == TYPEC_PORT_SRC)
		return w202_typec_set_cc(tcpc, TYPEC_CC_RP_DEF);
	if (type != TYPEC_PORT_DRP ||
	    (cc != TYPEC_CC_RD && cc != TYPEC_CC_RP_DEF))
		return -EINVAL;

	/* The PMIC debounces CC in hardware. TCPM's software DRP cadence
	 * switches modes before that debounce completes, preventing attach.
	 * Let the hardware alternate Rp/Rd and report the stable result by IRQ.
	 */
	mutex_lock(&pd->lock);
	if (pd->faulted) {
		ret = -ESHUTDOWN;
		goto out;
	}
	ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_PD_CFG,
				SC27XX_TYPEC_PD_NO_CHEK_DETACH, 0);
	if (!ret)
		ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_SW_CFG,
					GENMASK(15, 12), 0);
	if (!ret)
		ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_MODE,
					GENMASK(3, 0) | SC27XX_TYPEC_TRY_SRC_EN |
					SC27XX_TYPEC_TRY_SINK_EN,
					2 | SC27XX_TYPEC_RP_LEVEL(1) |
					SC27XX_TYPEC_TRY_SINK_EN);
	if (ret) {
		pd->cc_forced = true;
		regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_SW_CFG,
				   GENMASK(15, 12), BIT(12) | BIT(14));
		regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_PD_CFG,
				   SC27XX_TYPEC_PD_NO_CHEK_DETACH, 0);
		w202_pd_latch_fault(pd, "cc-toggle-io-error");
	} else {
		pd->cc_forced = false;
	}
out:
	mutex_unlock(&pd->lock);
	if (!ret && READ_ONCE(pd->ready))
		tcpm_cc_change(pd->port);
	return ret;
}

/* Caller holds pd->lock. Always permit shutdown, including a latched fault. */
int w202_typec_stop_source(struct w202_typec *pd)
{
	int ret;

	if (!pd->vbus_on)
		return 0;
	ret = regulator_disable(pd->vbus);
	if (!ret)
		pd->vbus_on = false;
	return ret;
}

int w202_typec_set_vbus(struct tcpc_dev *tcpc, bool on, bool charge)
{
	struct w202_typec *pd = tcpc_to_w202(tcpc);
	u32 debug;
	int ret = 0;

	mutex_lock(&pd->lock);
	if (!on) {
		ret = w202_typec_stop_source(pd);
	} else if (!pd->vbus_on) {
		if (charge || pd->faulted) {
			ret = -ESHUTDOWN;
			goto out;
		}
		ret = regmap_read(pd->regmap, pd->typec_base + SC27XX_TYPEC_DBG1, &debug);
		/* Never drive into an already-powered cable, including failed swaps. */
		if (!ret && (debug & SC27XX_TYPEC_VBUS_OK))
			ret = -EBUSY;
		if (!ret)
			ret = regulator_enable(pd->vbus);
		if (!ret)
			pd->vbus_on = true;
	}
out:
	mutex_unlock(&pd->lock);
	return ret;
}

int w202_typec_set_roles(struct tcpc_dev *tcpc, bool attached,
			enum typec_role power, enum typec_data_role data)
{
	struct w202_typec *pd = tcpc_to_w202(tcpc);
	u32 bits = (data == TYPEC_HOST ? SC27XX_PD_DATA_ROLE : 0) |
		(power == TYPEC_SOURCE ? SC27XX_PD_POWER_ROLE : 0);
	int ret;

	mutex_lock(&pd->lock);
	if (pd->faulted && attached) {
		ret = -ESHUTDOWN;
		goto out;
	}
	ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_CFG0,
				SC27XX_PD_SRC_SINK_MODE,
				power == TYPEC_SOURCE ? SC27XX_PD_SRC_SINK_MODE : 0);
	if (!ret)
		ret = regmap_update_bits(pd->regmap, pd->base + SC27XX_PD_HEAD_CFG,
					SC27XX_PD_POWER_ROLE | SC27XX_PD_DATA_ROLE, bits);
	if (!ret && (!attached || pd->cc_forced))
		ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_PD_CFG,
					SC27XX_TYPEC_PD_NO_CHEK_DETACH |
					(!attached ? SC27XX_TYPEC_PD_CONSTRACT : 0), 0);
	if (!ret && pd->cc_forced)
		ret = regmap_update_bits(pd->regmap, pd->typec_base + SC27XX_TYPEC_SW_CFG,
					GENMASK(15, 12), 0);
	if (!ret) {
		pd->cc_forced = false;
		pd->attached = attached;
		pd->power_role = power;
		if (!attached)
			pd->nr_source_caps = 0;
	}
out:
	mutex_unlock(&pd->lock);
	w202_typec_charger_state(pd, !ret && attached && power == TYPEC_SINK);
	return ret;
}
