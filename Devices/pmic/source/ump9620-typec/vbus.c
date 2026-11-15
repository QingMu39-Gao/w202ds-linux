// SPDX-License-Identifier: GPL-2.0-only
/* UMP9620 CC IRQs do not cover a later VBUS comparator transition. */
#include "controller.h"

static void vbus_poll(struct work_struct *work)
{
	struct w202_typec *pd = container_of(to_delayed_work(work),
					   struct w202_typec, vbus_work);
	bool changed = false, was_faulted;
	u32 debug;
	int ret;

	mutex_lock(&pd->lock);
	if (!READ_ONCE(pd->ready)) {
		mutex_unlock(&pd->lock);
		return;
	}
	was_faulted = pd->faulted;
	ret = regmap_read(pd->regmap, pd->typec_base + SC27XX_TYPEC_DBG1, &debug);
	if (ret) {
		w202_pd_latch_fault(pd, "vbus-comparator-io-error");
	} else {
		int present = !!(debug & SC27XX_TYPEC_VBUS_OK);

		changed = present != pd->vbus_seen;
		pd->vbus_seen = present;
	}
	mutex_unlock(&pd->lock);
	/* TCPM consumes these events on its own worker. Never invent VBUS
	 * from the requested source state or a successful regulator enable.
	 */
	if (ret && !was_faulted)
		tcpm_tcpc_reset(pd->port);
	else if (!ret && changed)
		tcpm_vbus_change(pd->port);
	if (READ_ONCE(pd->ready))
		mod_delayed_work(system_power_efficient_wq, &pd->vbus_work,
				 msecs_to_jiffies(100));
}

void w202_typec_vbus_init(struct w202_typec *pd)
{
	pd->vbus_seen = -1;
	INIT_DELAYED_WORK(&pd->vbus_work, vbus_poll);
}
