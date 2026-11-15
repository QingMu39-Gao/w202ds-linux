// SPDX-License-Identifier: GPL-2.0-only
/* Caller locks both chargers. Save/restore only bits owned by OTG. */
#include "charger.h"

int w202_bq_otg_exit_locked(struct w202_bq *main, struct w202_bq *slave)
{
	static const u8 regs[] = { 0, 2, 5, 6 };
	static const u8 masks[] = { BIT(7), BIT(7), GENMASK(5, 4), GENMASK(5, 4) };
	int ret, first = 0, i;

	ret = w202_bq_update_locked(main, 1, OTG_ENABLE | CHARGE_ENABLE | WATCHDOG_STROBE, 0);
	if (ret) {
		/* An unconfirmed write may still be sourcing. Keep slave isolated. */
		main->otg_active = true;
		return ret;
	}
	main->otg_active = false;
	if (!main->otg_saved_valid)
		return 0;
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = w202_bq_update_locked(main, regs[i], masks[i], main->otg_saved[i] & masks[i]);
		if (!first && ret)
			first = ret;
	}
	/* Only reconnect the slave input after boost-off was read back. */
	ret = w202_bq_update_locked(slave, 0, BIT(7), main->otg_saved[4] & BIT(7));
	if (!first && ret)
		first = ret;
	if (!first)
		main->otg_saved_valid = false;
	return first;
}

int w202_bq_otg_enter_locked(struct w202_bq *main, struct w202_bq *slave)
{
	static const u8 regs[] = { 0, 2, 5, 6 };
	int ret, rollback, i;

	if (main->otg_active)
		return 0;
	if (main->otg_saved_valid) {
		ret = w202_bq_otg_exit_locked(main, slave);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = i2c_smbus_read_byte_data(main->client, regs[i]);
		if (ret < 0)
			return ret;
		main->otg_saved[i] = ret;
	}
	ret = i2c_smbus_read_byte_data(slave->client, 0);
	if (ret < 0)
		return ret;
	main->otg_saved[4] = ret;
	main->otg_saved_valid = true;
	ret = w202_bq_stop_locked(main);
	if (!ret)
		ret = w202_bq_stop_locked(slave);
	if (!ret)
		ret = w202_bq_update_locked(slave, 0, BIT(7), BIT(7));
	if (!ret)
		ret = w202_bq_update_locked(main, 0, BIT(7), 0);
	/* W205 BQ register map: boost limit 0=500 mA, boost voltage 1=5 V.
	 * Disable the charge watchdog; expiry resets the charging profile.
	 */
	if (!ret)
		ret = w202_bq_update_locked(main, 5, GENMASK(5, 4), 0);
	if (!ret)
		ret = w202_bq_update_locked(main, 2, BIT(7), 0);
	if (!ret)
		ret = w202_bq_update_locked(main, 6, GENMASK(5, 4), BIT(4));
	if (!ret) {
		main->otg_active = true;
		ret = w202_bq_update_locked(main, 1, OTG_ENABLE | CHARGE_ENABLE | WATCHDOG_STROBE,
					  OTG_ENABLE);
	}
	if (!ret)
		return 0;
	rollback = w202_bq_otg_exit_locked(main, slave);
	return rollback ?: ret;
}
