// SPDX-License-Identifier: GPL-2.0-only
/* Board VBUS regulator. Pair lock precedes main/slave locks; no TCPM setters. */
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include "charger.h"
#include "otg-fault.h"

static DEFINE_MUTEX(pair_lock);
static struct w202_bq *main_charger, *slave_charger;

static int battery_ready(void)
{
	int temp, voltage, ret;

	ret = w202_bq_battery_property(POWER_SUPPLY_PROP_TEMP, &temp);
	if (!ret)
		ret = w202_bq_battery_property(POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage);
	if (ret)
		return ret;
	return temp < 100 || temp > 430 || voltage < 3600000 ||
		voltage > 4430000 ? -ERANGE : 0;
}

static int vbus_enable(struct regulator_dev *rdev)
{
	struct w202_bq *main = rdev_get_drvdata(rdev), *slave;
	int ret;

	mutex_lock(&pair_lock);
	slave = slave_charger;
	if (!slave || main != main_charger) {
		ret = -ENODEV;
		goto out;
	}
	ret = battery_ready();
	if (ret)
		goto out;
	mutex_lock(&main->lock);
	mutex_lock_nested(&slave->lock, SINGLE_DEPTH_NESTING);
	if (main->shutting_down || slave->shutting_down || main->suspended || slave->suspended ||
	    main->fault_latched || main->vendor_id != 5 || slave->vendor_id != 5) {
		ret = -ESHUTDOWN;
		goto unlock;
	}
	/* A watchdog reset of either chip could undo input isolation. */
	ret = i2c_smbus_read_byte_data(main->client, 5);
	if (ret < 0)
		goto unlock;
	if (ret & GENMASK(5, 4)) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = i2c_smbus_read_byte_data(slave->client, 5);
	if (ret < 0)
		goto unlock;
	if (ret & GENMASK(5, 4)) {
		ret = -EBUSY;
		goto unlock;
	}
	/* A live external input must never be driven by our boost converter. */
	ret = i2c_smbus_read_byte_data(main->client, 8);
	if (ret >= 0)
		ret = !main->otg_active && (ret & BIT(2)) ? -EBUSY :
			w202_bq_otg_enter_locked(main, slave);
unlock:
	mutex_unlock(&slave->lock);
	mutex_unlock(&main->lock);
out:
	mutex_unlock(&pair_lock);
	return ret;
}

int w202_bq_otg_shutdown(struct w202_bq *bq)
{
	int ret = 0;

	mutex_lock(&pair_lock);
	if (main_charger && slave_charger &&
	    (main_charger->otg_active || main_charger->otg_saved_valid)) {
		mutex_lock(&main_charger->lock);
		mutex_lock_nested(&slave_charger->lock, SINGLE_DEPTH_NESTING);
		ret = w202_bq_otg_exit_locked(main_charger, slave_charger);
		mutex_unlock(&slave_charger->lock);
		mutex_unlock(&main_charger->lock);
	}
	mutex_unlock(&pair_lock);
	return ret;
}

static int vbus_disable(struct regulator_dev *rdev)
{
	return w202_bq_otg_shutdown(rdev_get_drvdata(rdev));
}

static int vbus_is_enabled(struct regulator_dev *rdev)
{
	struct w202_bq *bq = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&bq->lock);
	ret = i2c_smbus_read_byte_data(bq->client, 1);
	mutex_unlock(&bq->lock);
	return ret < 0 ? ret : !!(ret & OTG_ENABLE);
}

static const struct regulator_ops vbus_ops = {
	.enable = vbus_enable,
	.disable = vbus_disable,
	.is_enabled = vbus_is_enabled,
};

static const struct regulator_desc vbus_desc = {
	.name = "otg-vbus", .of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE, .owner = THIS_MODULE,
	.ops = &vbus_ops, .fixed_uV = 5000000, .n_voltages = 1,
	.enable_time = 20000,
};

static void unregister_pair(void *data)
{
	struct w202_bq *bq = data;

	w202_bq_otg_shutdown(bq);
	mutex_lock(&pair_lock);
	if (bq == main_charger)
		main_charger = NULL;
	if (bq == slave_charger)
		slave_charger = NULL;
	mutex_unlock(&pair_lock);
}

int w202_bq_register_otg(struct w202_bq *bq)
{
	struct regulator_config config = { .dev = &bq->client->dev, .driver_data = bq };
	struct w202_bq **slot = bq->slave ? &slave_charger : &main_charger;
	int ret;

	if (bq->device_id != 0x14 || bq->vendor_id != 5)
		return 0;
	/* Take ownership with boost, charge and reset watchdog off on both chips. */
	mutex_lock(&bq->lock);
	ret = w202_bq_update_locked(bq, 1, OTG_ENABLE | CHARGE_ENABLE | WATCHDOG_STROBE, 0);
	if (!ret)
		ret = w202_bq_update_locked(bq, 5, GENMASK(5, 4), 0);
	mutex_unlock(&bq->lock);
	if (ret)
		return ret;
	mutex_lock(&pair_lock);
	ret = *slot ? -EBUSY : 0;
	if (!ret)
		*slot = bq;
	mutex_unlock(&pair_lock);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(&bq->client->dev, unregister_pair, bq);
	if (ret || bq->slave)
		return ret;
	bq->vbus = devm_regulator_register(&bq->client->dev, &vbus_desc, &config);
	return PTR_ERR_OR_ZERO(bq->vbus);
}

void w202_bq_otg_monitor(struct w202_bq *bq)
{
	bool failed = false;
	int ret, config, old_fault, fault;

	if (bq->slave || !READ_ONCE(bq->otg_active))
		return;
	ret = battery_ready();
	mutex_lock(&pair_lock);
	if (main_charger != bq || !slave_charger)
		goto out;
	mutex_lock(&bq->lock);
	mutex_lock_nested(&slave_charger->lock, SINGLE_DEPTH_NESTING);
	if (!bq->otg_active)
		goto unlock;
	/* Only the existing policy worker consumes latched REG09 faults. */
	config = i2c_smbus_read_byte_data(bq->client, 1);
	old_fault = i2c_smbus_read_byte_data(bq->client, 9);
	fault = i2c_smbus_read_byte_data(bq->client, 9);
	if (old_fault >= 0)
		bq->fault_history |= old_fault;
	if (fault >= 0)
		bq->fault_history |= fault;
	bq->fault = fault;
	if (ret || config < 0 || !(config & OTG_ENABLE) || (config & CHARGE_ENABLE) ||
	    w202_bq_otg_fault(old_fault, fault)) {
		bq->fault_latched = true;
		bq->health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		ret = w202_bq_otg_exit_locked(bq, slave_charger);
		failed = true;
		dev_err_ratelimited(&bq->client->dev,
			"OTG stopped: config=%d history=%d fault=%d off=%d\n",
			config, old_fault, fault, ret);
	}
unlock:
	mutex_unlock(&slave_charger->lock);
	mutex_unlock(&bq->lock);
out:
	mutex_unlock(&pair_lock);
	if (failed && !IS_ERR_OR_NULL(bq->vbus))
		regulator_notifier_call_chain(bq->vbus, REGULATOR_EVENT_FAIL, NULL);
}
