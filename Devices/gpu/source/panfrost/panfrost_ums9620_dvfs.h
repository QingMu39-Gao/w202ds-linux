/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PANFROST_UMS9620_DVFS_H
#define PANFROST_UMS9620_DVFS_H

struct ums_dvfs_ops {
	int (*get_voltage)(void *data);
	int (*set_voltage)(void *data, int uv);
	int (*set_index)(void *data, int index);
};

/* Voltage is established before raising the clock. On the way down the
 * clock must be confirmed first. An uncertain clock write retains the
 * higher voltage. A failed voltage reduction may leave the new, lower
 * clock running safely: return 1 to report that degraded success.
 */
static inline int ums_dvfs_transition(const struct ums_dvfs_ops *ops,
				      void *data, int index, int uv)
{
	int old_uv, ret;

	old_uv = ops->get_voltage(data);
	if (old_uv < 0)
		return old_uv;
	if (uv >= old_uv) {
		/* Also synchronize the hardware voltage slot at equal voltage:
		 * a previous failed regulator write may have changed the slot.
		 */
		ret = ops->set_voltage(data, uv);
		if (ret)
			return ret;
	}
	ret = ops->set_index(data, index);
	if (ret)
		return ret;
	if (uv < old_uv) {
		ret = ops->set_voltage(data, uv);
		if (ret)
			return ops->get_voltage(data) >= uv ? 1 : ret;
	}
	return 0;
}
#endif
