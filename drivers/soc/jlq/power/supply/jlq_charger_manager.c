// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This driver enables to monitor battery health and control charger
 * during suspend-to-mem.
 * Charger manager depends on other devices. Register this later than
 * the depending devices.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/of.h>
#include <linux/thermal.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/extcon.h>
#include <linux/delay.h>
#include <soc/jlq/jlq_extcon.h>

#include <linux/extcon-provider.h>
#include <jlq_cm_voter.h>
#include <jlq_charger_manager.h>


/*
 * Default temperature threshold for charging.
 * Every temperature units are in tenth of centigrade.
 */
#define CM_DEFAULT_RECHARGE_TEMP_DIFF	50
#define CM_DEFAULT_CHARGE_TEMP_MAX	500

static const char *const default_event_names[] = {
	[CM_EVENT_UNKNOWN] = "Unknown",
	[CM_EVENT_BATT_FULL] = "Battery Full",
	[CM_EVENT_BATT_IN] = "Battery Inserted",
	[CM_EVENT_BATT_OUT] = "Battery Pulled Out",
	[CM_EVENT_BATT_OVERHEAT] = "Battery Overheat",
	[CM_EVENT_BATT_COLD] = "Battery Cold",
	[CM_EVENT_EXT_PWR_IN_OUT] = "External Power Attach/Detach",
	[CM_EVENT_CHG_START_STOP] = "Charging Start/Stop",
	[CM_EVENT_OTHERS] = "Other battery events"
};

#define SW_THERMAL_CLIENT "soft-thermal"
#define SYSFS_CLIENT "sysfs"
#define INIT_CLIENT "INIT"
#define DURATION_CLIENT "duration"
#define SW_JEITA_CLIENT "soft-Jeita-thermal"
#define SW_RECHG_CLIENT "soft-recharge"
#define SW_STEPCHG_CLIENT "step-charge"
#define USB_TYPE_CLIENT "USB-plugin-type"
#define ADAPTER_DET_CLIENT "Adapter-det"
#define COMMON_ENABLE_CLIENT "Common-enable"
#define USB_OTG_CLIENT "OTG"
#define USB_USER_FORCE_CLIENT    "User-Force"
#define USB_CHARGER_MAX_CLIENT    "chargers-max"
#define JCM_THERM_LEVEL_CLIENT    "ThermalLevel"
#define DRVINIT_CLIENT "DrvInit"

#define JCM_DEBUG_BAT_FAKE_SOC_ERRVAL  50
#define JCM_DEBUG_BAT_ID_LOW  2000
#define JCM_DEBUG_BAT_ID_HIGH  14000
/*
 * Regard CM_JIFFIES_SMALL jiffies is small enough to ignore for
 * delayed works so that we can run delayed works with CM_JIFFIES_SMALL
 * without any delays.
 */
#define	CM_JIFFIES_SMALL	(2)

/* If y is valid (> 0) and smaller than x, do x = y */
#define CM_MIN_VALID(x, y) ((((y) > 0) && ((x) > (y))) ? (y) : (x))
/*
 * Regard CM_RTC_SMALL (sec) is small enough to ignore error in invoking
 * rtc alarm. It should be 2 or larger
 */
#define CM_RTC_SMALL		(2)

#define UEVENT_BUF_SIZE		32


static void jcm_fullbatt_handler(struct jlq_charger_manager *cm);
static bool jcm_is_full_charged(struct jlq_charger_manager *cm);
static int jcm_update_soc_restart(struct jlq_charger_manager *cm);

struct device_node *of_jlq_battery_profile_get_best_profile(
		const struct device_node *batterydata_container_node,
		int batt_id_kohm, const char *batt_type);

/**
 * jcm_is_vbus_ok - Returns true if the Vbus is OK.
 * @cm: the Charger Manager representing the charger.
 */
#if 1
static bool jcm_is_vbus_ok(struct jlq_charger_manager *cm)
{
	int ret;
	union power_supply_propval val;
	ret = jcm_get_charger_psy_prop(cm->charger->chips,
		POWER_SUPPLY_PROP_PRESENT, &val);
	if (ret < 0) {
		return false;
	}
	return !!val.intval;
}
#else
static bool jcm_is_vbus_ok(struct jlq_charger_manager *cm)
{
	return atomic_read(&cm->charger->attached);
}
#endif

static int jcm_get_gauge_psy_prop(struct jlq_charger_manager *cm,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct power_supply *psy;
	struct jlq_charger_regulator *charger;
	int ret;

	charger = cm->charger;
	if (!cm->inited)
		return -EBUSY;
	if (charger->no_battery)
		return -ENODEV;

	psy = power_supply_get_by_name(charger->psy_fuel_gauge);
	if (IS_ERR_OR_NULL(psy)) {
		dev_dbg(cm->dev, "Cannot find power supply- \"%s\"\n",
				charger->psy_fuel_gauge);
		return -ENODEV;
	}
	ret = power_supply_get_property(psy, psp,
			val);
	if (ret < 0) {
		dev_dbg(cm->dev, "Cannot read psp:%d value from %s\n",
			psp, charger->psy_fuel_gauge);
		power_supply_put(psy);
		return ret;
	}
	power_supply_put(psy);
	return ret;
}
#ifndef QCOM_BATT_UNIFY_SYSFS
static int jcm_get_charger_psy_prop(struct jlq_charger_chip *chip,
	enum power_supply_property psp,
	union power_supply_propval *val)
#else
int jcm_get_charger_psy_prop(struct jlq_charger_chip *chip,
	enum power_supply_property psp,
	union power_supply_propval *val)
#endif
{
	struct power_supply *psy;
	int ret;

	if (!chip || !chip->charger_psy)
		return -ENODEV;
	if (!chip->cm->inited)
		return -EBUSY;
	psy = power_supply_get_by_name(chip->charger_psy);
	if (!psy || IS_ERR(psy)) {
		dev_dbg(chip->cm->dev, "Cannot find power supply- \"%s\"\n",
				chip->charger_psy);
		return -ENODEV;
	}
	ret = power_supply_get_property(psy, psp,
			val);
	if (ret) {
		dev_dbg(chip->cm->dev, "Cannot read psp:%d value from %s\n",
			psp, chip->charger_psy);
	}
	power_supply_put(psy);
	return ret;
}

#ifndef QCOM_BATT_UNIFY_SYSFS
static int jcm_set_charger_psy_prop(struct jlq_charger_chip *chip,
	enum power_supply_property psp,
	union power_supply_propval *val)
#else
int jcm_set_charger_psy_prop(struct jlq_charger_chip *chip,
	enum power_supply_property psp,
	union power_supply_propval *val)
#endif
{
	struct power_supply *psy;
	int ret;

	if (!chip || !chip->charger_psy)
		return -ENODEV;
	if (!chip->cm->inited)
		return -EBUSY;
	psy = power_supply_get_by_name(chip->charger_psy);
	if (!psy || IS_ERR(psy)) {
		dev_dbg(chip->cm->dev, "Cannot find power supply- \"%s\"\n",
				chip->charger_psy);
		return -ENODEV;
	}
	ret = power_supply_set_property(psy, psp,
			val);
	if (ret) {
		dev_dbg(chip->cm->dev, "Cannot read psp:%d value from %s\n",
			psp, chip->charger_psy);
	}
	power_supply_put(psy);
	return ret;
}


/**
 * is_batt_present - See if the battery presents in place.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_batt_present(struct jlq_charger_manager *cm)
{
	union power_supply_propval val;
	struct jlq_charger_chip *chip;
	struct power_supply *psy;
	bool present = false;
	int i, ret;

	switch (cm->charger->battery_present) {
	case CM_BATTERY_PRESENT:
		present = true;
		break;
	case CM_NO_BATTERY:
		break;
	case CM_FUEL_GAUGE:
		psy = power_supply_get_by_name(cm->charger->psy_fuel_gauge);
		if (!psy)
			break;

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT,
				&val);
		if (ret == 0 && val.intval)
			present = true;
		power_supply_put(psy);
		break;
	case CM_CHARGER_STAT:
		chip = cm->charger->chips;
	for (i = 0; i < cm->charger->num_chips; i++, chip++) {
			if (!chip->charger_psy)
				continue;

			psy = power_supply_get_by_name(chip->charger_psy);
			if (!psy) {
				dev_dbg(cm->dev, "Cannot find power supply. \"%s\"\n",
				chip->charger_psy);
				continue;
			}
			ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_PRESENT, &val);
			power_supply_put(psy);
			if (ret == 0 && val.intval) {
				present = true;
				break;
			}
		}
		break;
	}

	return present;
}

static int jcm_get_batt_soc(struct jlq_charger_manager *cm)
{
	int ret;
	int bat_soc;
	union power_supply_propval val;

	if (!is_batt_present(cm)) {
		/* There is no battery. Assume 100% */
//		bat_soc = 100;
		return -EAGAIN;
	}

	ret = jcm_get_gauge_psy_prop(cm, POWER_SUPPLY_PROP_CAPACITY, &val);
	if (ret < 0)
		return -EAGAIN;
	bat_soc = val.intval;

	if (bat_soc > 100)
		bat_soc = 100;
	if (bat_soc <= 0)
		bat_soc = 0;

	return bat_soc;
}
static int jcm_get_msoc(struct jlq_charger_manager *cm)
{
	static int timeout_cnt;

	if (cm->soc_update.msoc < 0) {
		while (cm->soc_update.msoc < 0 && timeout_cnt < 75) {
			//wait fuel gauge init done Max 15s while fisrt time.
			msleep(200);
			jcm_update_soc_restart(cm);
			timeout_cnt++;
		}
	}
	return cm->soc_update.msoc < 0 ? 99 : cm->soc_update.msoc;
}

static int jcm_get_syssoc(struct jlq_charger_manager *cm)
{
	static int timeout_cnt;

	while (cm->soc_update.sys_soc < 0 && timeout_cnt < 5) {
		//wait fuel gauge init done Max 1s while fisrt time.
		msleep(200);
		jcm_update_soc_restart(cm);
		timeout_cnt++;
	}
	return cm->soc_update.msoc;
}

/**
 * is_ext_pwr_online - See if an external power source is attached to charge
 * @cm: the Charger Manager representing the battery.
 *
 * Returns true if at least one of the chargers of the battery has an external
 * power source attached to charge the battery regardless of whether it is
 * actually charging or not.
 */
static bool is_ext_pwr_online(struct jlq_charger_manager *cm)
{
	union power_supply_propval val;
	struct jlq_charger_chip *chip;
	struct power_supply *psy;
	bool online = false;
	int i, ret;

	/* If at least one of them has one, it's yes. */
	chip = cm->charger->chips;
	for (i = 0; i < cm->charger->num_chips; i++, chip++) {
		psy = power_supply_get_by_name(chip->charger_psy);
		if (!psy) {
			dev_dbg(cm->dev, "Cannot find power supply \"%s\"\n",
					chip->charger_psy);
			continue;
		}

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE,
				&val);
		power_supply_put(psy);
		if (ret == 0 && val.intval) {
			online = true;
			break;
		}
	}

	return online;
}

/**
 * jcm_get_batt_uV - Get the voltage level of the battery
 * @cm: the Charger Manager representing the battery.
 * @uV: the voltage level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int jcm_get_batt_uV(struct jlq_charger_manager *cm, int *uV)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->charger->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	power_supply_put(fuel_gauge);
	*uV = val.intval;
	return 0;
}

/**
 * jcm_get_batt_ocv_uV - Get the open circuit voltage level of the battery
 * @cm: the Charger Manager representing the battery.
 * @uV: the voltage level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int jcm_get_batt_ocv_uV(struct jlq_charger_manager *cm, int *uV)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->charger->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_OCV, &val);
	power_supply_put(fuel_gauge);
	if (ret < 0)
		return ret;
	if (val.intval < JCM_CUTOFF_BAT_OCV_MIN)
		return -EINVAL;
	*uV = val.intval;
	return ret;
}


/**
 * jcm_get_ibus_uA - Get the current of the USB
 * @cm: the Charger Manager representing the USB.
 * @uA: the current  returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int jcm_get_ibus_uA(struct jlq_charger_manager *cm, int *uA)
{
	union power_supply_propval val;
	int ret;

	ret = jcm_get_charger_psy_prop(cm->charger->chips,
		POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret)
		return ret;
	*uA = val.intval;
	return 0;
}

/**
 * jcm_get_vbus_uV - Get the Volt of the USB
 * @cm: the Charger Manager representing the USB.
 * @uV: the voltage  returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int jcm_get_vbus_uV(struct jlq_charger_manager *cm, int *uV)
{
	union power_supply_propval val;
	int ret;

	ret = jcm_get_charger_psy_prop(cm->charger->chips,
		POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (ret)
		return ret;
	*uV = val.intval;
	return 0;
}

/**
 * jcm_get_vreg_uV - Get the Volt of the vreg
 * @cm: the Charger Manager representing the USB.
 * @uV: the voltage  returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int jcm_get_vreg_uV(struct jlq_charger_manager *cm, int *uV)
{
	union power_supply_propval val;
	int ret;

	ret = jcm_get_charger_psy_prop(cm->charger->chips,
		POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
	if (ret)
		return ret;
	*uV = val.intval;
	return 0;
}

/**
 * jcm_get_batt_uA - Get the current of the battery
 * @cm: the Charger Manager representing the battery.
 * @uA: the current  returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int jcm_get_batt_uA(struct jlq_charger_manager *cm, int *uA)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->charger->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*uA = -1 * val.intval;
	return 0;
}

static int jcm_charging_status(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger;
	int ret;
	int batt_capacity;
	union power_supply_propval val;

	charger = cm->charger;
	if (!cm->inited)
		return -EBUSY;

	batt_capacity = jcm_get_msoc(cm);
	if (jcm_is_full_charged(cm)  && batt_capacity > 95)
	{
		return POWER_SUPPLY_STATUS_FULL;
	}

	//begin gerrit 208763
	if (batt_capacity == 0)
		return POWER_SUPPLY_STATUS_DISCHARGING;
	//end gerrit 208763

	if (jcmvote_get_client_vote(
			charger->charger_disable_votable,SW_RECHG_CLIENT) && batt_capacity > 95)
	{
		return POWER_SUPPLY_STATUS_FULL;
	}
//	ret = jcm_get_msoc(cm);
//	if (ret == 100)
//		return POWER_SUPPLY_STATUS_FULL;

	ret = jcm_get_charger_psy_prop(cm->charger->chips,
		POWER_SUPPLY_PROP_STATUS, &val);
	if (!ret && val.intval == POWER_SUPPLY_STATUS_FULL && batt_capacity > 95)
		return POWER_SUPPLY_STATUS_FULL;

	if (!jcm_is_vbus_ok(cm))
		return POWER_SUPPLY_STATUS_DISCHARGING;

	if (!cm->charger_enabled)
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_CHARGING;
}

/**
 * jcm_is_charging - Returns true if the battery is being charged.
 * @cm: the Charger Manager representing the battery.
 */
static bool jcm_is_charging(struct jlq_charger_manager *cm)
{
	/* If there is no battery, it cannot be charged */
	if (!is_batt_present(cm))
		return false;
	if (!cm->charger_enabled || !jcm_is_vbus_ok(cm))
		return false;
	return true;
}

static int jcm_get_control_limt_level(struct jlq_charger_manager *cm)
{
	return cm->charger->thermal_level;
}

/**
 * jcm_is_vbus_ok - Returns true if the Vbus is OK.
 * @cm: the Charger Manager representing the charger.
 */
static int jcm_set_control_limt_level(struct jlq_charger_manager *cm,int level)
{
	struct jlq_charger_regulator *charger = cm->charger;
	if (level < 0 || level >= charger->thermal_level_cnt)
		return -EINVAL;
	charger->thermal_level = level;

	dev_info(cm->dev, "Set thermal level:%d", charger->thermal_level);
	if (level == charger->thermal_level_cnt) {
		jcmvote(charger->charger_disable_votable,
			JCM_THERM_LEVEL_CLIENT, true, 0);
		return 0;
	}
	jcmvote(charger->fastchg_current_votable,
		JCM_THERM_LEVEL_CLIENT, true, charger->thermal_mitigation[level]);
	jcmvote(charger->charger_disable_votable, JCM_THERM_LEVEL_CLIENT, false, 1);
	return 0;
}
static int jcm_get_control_limt_max_level(struct jlq_charger_manager *cm,int *max_level)
{
	*max_level = cm->charger->thermal_level_cnt;
	return 0;
}

static int jcm_set_chg_type(struct jlq_charger_regulator *charger, int chg_type)
{
	mutex_lock(&charger->lock);
	charger->chg_type = chg_type;
	mutex_unlock(&charger->lock);
	return 0;
}

/**
 * jcm_is_full_charged - Returns true if the battery is fully charged.
 * @cm: the Charger Manager representing the battery.
 */
static bool jcm_is_full_charged(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;
	union power_supply_propval val;
//	struct power_supply *fuel_gauge = NULL;
	bool is_full = false;
	int ret = 0;
	int uV;
	int soc;

	/* If there is no battery, it cannot be charged */
	if (!is_batt_present(cm))
		return false;

#if 0
	fuel_gauge = power_supply_get_by_name(cm->charger->psy_fuel_gauge);
	if (!fuel_gauge)
		return false;
	if (charger->battery.fullbatt_full_capacity > 0) {
		val.intval = 0;

		/* Not full if capacity of fuel gauge isn't full */
		ret = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CHARGE_NOW, &val);
		if (!ret && val.intval >
				charger->battery.fullbatt_full_capacity) {
			is_full = true;
			goto out;
		}
	}
#endif
	soc = jcm_get_batt_soc(cm);
	ret = jcm_get_charger_psy_prop(charger->chips,
				POWER_SUPPLY_PROP_STATUS, &val);
	if (!ret && val.intval == POWER_SUPPLY_STATUS_FULL && soc > 95) {
		is_full = true;
		goto out;
	}

	/* Full, if it's over the fullbatt voltage */
	if (!charger->soc_policy && charger->battery.fullbatt_uV > 0) {
		ret = jcm_get_batt_ocv_uV(cm, &uV);
		if (!ret && uV >= charger->battery.fullbatt_uV) {
			is_full = true;
			goto out;
		}
	}
out:
//	power_supply_put(fuel_gauge);
	return is_full;
}

/*
 * try_charger_enable - Enable/Disable chargers altogether
 * @cm: the Charger Manager representing the battery.
 * @enable: true: enable / false: disable
 *
 * Note that Charger Manager keeps the charger enabled regardless whether
 * the charger is charging or not (because battery is full or no external
 * power source exists) except when CM needs to disable chargers forcibly
 * because of emergency causes; when the battery is overheated or too cold.
 */
static int try_charger_enable(
	struct jlq_charger_manager *cm,
	const char *client, bool enable)
{
	int err = 0;
	struct jlq_charger_regulator *charger = cm->charger;

	/* Ignore if it's redundant command */
//	if (enable == cm->charger_enabled)
//		return 0;

	if (enable) {
/*
 * Save start time of charging to limit
 * maximum possible charging time.
 */
		cm->charging_start_time = ktime_to_ms(ktime_get());
		cm->charging_end_time = 0;
		if (client)
			jcmvote(charger->charger_disable_votable, client, false, 1);
		else
			jcmvote(charger->charger_disable_votable, COMMON_ENABLE_CLIENT, false, 1);
//		if (charger->regulator_input)
//			regulator_enable(charger->regulator_input);

	} else {
/*
 * Save end time of charging to maintain fully charged state
 * of battery after full-batt.
 */
		cm->charging_start_time = 0;
		cm->charging_end_time = ktime_to_ms(ktime_get());
//		if (charger->regulator_input)
//			regulator_disable(charger->regulator_input);

		if (client)
			jcmvote(charger->charger_disable_votable, client, true, 0);
		else
			jcmvote(charger->charger_disable_votable, COMMON_ENABLE_CLIENT, true, 0);
	}
//	if (!err)
//		cm->charger_enabled = enable;

	return err;
}

/*
 * uevent_notify - Let users know something has changed.
 * @cm: the Charger Manager representing the battery.
 * @event: the event string.
 *
 * If @event is null, it implies that uevent_notify is called
 * by resume function. When called in the resume function, cm->jcm_suspended
 * should be already reset to false in order to let uevent_notify
 * notify the recent event during the suspend to users. While
 * suspended, uevent_notify does not notify users, but tracks
 * events so that uevent_notify can notify users later after resumed.
 */
static void uevent_notify(struct jlq_charger_manager *cm, const char *event)
{
	static char env_str[UEVENT_BUF_SIZE + 1] = "";
	static char env_str_save[UEVENT_BUF_SIZE + 1] = "";

	if (cm->jcm_suspended) {
		/* Nothing in suspended-event buffer */
		if (env_str_save[0] == 0) {
			if (!strncmp(env_str, event, UEVENT_BUF_SIZE))
				return; /* status not changed */
			strncpy(env_str_save, event, UEVENT_BUF_SIZE);
			return;
		}

		if (!strncmp(env_str_save, event, UEVENT_BUF_SIZE))
			return; /* Duplicated. */
		strncpy(env_str_save, event, UEVENT_BUF_SIZE);
		return;
	}

	if (event == NULL) {
		/* No messages pending */
		if (!env_str_save[0])
			return;

		strncpy(env_str, env_str_save, UEVENT_BUF_SIZE);
		kobject_uevent(&cm->dev->kobj, KOBJ_CHANGE);
		env_str_save[0] = 0;

		return;
	}

	/* status not changed */
	if (!strncmp(env_str, event, UEVENT_BUF_SIZE))
		return;

	/* save the status and notify the update */
	strncpy(env_str, event, UEVENT_BUF_SIZE);
	kobject_uevent(&cm->dev->kobj, KOBJ_CHANGE);

	dev_dbg(cm->dev, "%s\n", event);
}

/**
 * fullbatt_vchk - Check voltage drop some times after "FULL" event.
 * @work: the work_struct appointing the function
 *
 * If a user has designated "fullbatt_vchkdrop_ms/uV" values with
 * charger_desc, Charger Manager checks voltage drop after the battery
 * "FULL" event. It checks whether the voltage has dropped more than
 * fullbatt_vchkdrop_uV by calling this function after fullbatt_vchkrop_ms.
 */
static void fullbatt_vchk(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct jlq_charger_manager *cm = container_of(dwork,
			struct jlq_charger_manager, fullbatt_vchk_work);
	struct jlq_charger_regulator *charger = cm->charger;
	int batt_stat, err, diff;
	bool rechg_flag = false;
	time64_t cur_time;

	if (!charger->soft_recharge_flag)
		return;

	/* remove the appointment for fullbatt_vchk */
	cm->fullbatt_vchk_jiffies_at = 0;

	if ((charger->battery.rechg_volt_base && !charger->battery.fullbatt_vchkdrop_uV) ||
		(!charger->battery.rechg_volt_base && !charger->battery.fullbatt_rechgsoc) ||
			!charger->fullbatt_vchkdrop_ms)
		return;
	cur_time = ktime_get_boottime_seconds();

	if (charger->battery.rechg_volt_base) {
		err = jcm_get_batt_ocv_uV(cm, &batt_stat);
		if (err) {
			dev_err(cm->dev, "%s: jcm_get_batt_uV error(%d)\n", __func__, err);
			goto rescan_chk;
		}
		diff = charger->battery.fullbatt_uV - batt_stat;
		if (diff < 0)
			goto rescan_chk;
		if (diff < charger->battery.fullbatt_vchkdrop_uV) {
			dev_dbg(cm->dev, "VBATT dropped %dmV after full-batt %dseconds\n",
				diff / 1000, cur_time - charger->fullbatt_term_time_at);
			goto rescan_chk;
		}
		rechg_flag = true;
		dev_dbg(cm->dev, "VBATT dropped %dmV after full-batt %dseconds\n",
			diff / 1000, cur_time - charger->fullbatt_term_time_at);
	} else {
		batt_stat = jcm_get_syssoc(cm);
		if (batt_stat < 0) {
			dev_err(cm->dev, "%s: jcm_get_syssoc error(%d)\n", __func__, batt_stat);
			goto rescan_chk;
		}
		if (batt_stat > charger->battery.fullbatt_rechgsoc) {
			dev_dbg(cm->dev, "VBATT dropped to %d%% after full-batt %dseconds\n",
				batt_stat, cur_time - charger->fullbatt_term_time_at);
			goto rescan_chk;
		}
		rechg_flag = true;
		dev_dbg(cm->dev, "VBATT dropped to %d%% after full-batt %dseconds\n",
			batt_stat, cur_time - charger->fullbatt_term_time_at);
	}
	if (rechg_flag &&
			charger->fullbatt_term_time_at + charger->recharge_hold_sec <= cur_time) {
		charger->fullbatt_term_time_at = 0;
		dev_err(cm->dev, "Recharging\n");
		jcmvote(cm->charger->charger_disable_votable, SW_RECHG_CLIENT, false, 1);
		uevent_notify(cm, "Recharging");
		return;
	}
rescan_chk:
	cm->fullbatt_vchk_jiffies_at = jiffies + msecs_to_jiffies(charger->fullbatt_vchkdrop_ms);
	schedule_delayed_work(&cm->fullbatt_vchk_work,
			msecs_to_jiffies(charger->fullbatt_vchkdrop_ms));

}

/*
 * check_charging_duration - Monitor charging/discharging duration
 * @cm: the Charger Manager representing the battery.
 *
 * If whole charging duration exceed 'charging_max_duration_ms',
 * cm stop charging to prevent overcharge/overheat. If discharging
 * duration exceed 'discharging _max_duration_ms', charger cable is
 * attached, after full-batt, cm start charging to maintain fully
 * charged state for battery.
 */
static int check_charging_duration(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;
	u64 curr = ktime_to_ms(ktime_get());
	u64 duration;
	int ret = false;

	if (!charger->battery.charging_max_duration_ms &&
			!charger->battery.discharging_max_duration_ms)
		return ret;

	if (cm->charger_enabled) {
		duration = curr - cm->charging_start_time;

		if (duration > charger->battery.charging_max_duration_ms) {
			dev_info(cm->dev, "Charging duration exceed %ums\n",
				charger->battery.charging_max_duration_ms);
			uevent_notify(cm, "Discharging");
			jcmvote(cm->charger->charger_disable_votable, DURATION_CLIENT, true, 0);
			ret = true;
		}
	} else if (is_ext_pwr_online(cm) && !cm->charger_enabled) {
		duration = curr - cm->charging_end_time;
		if (duration > charger->battery.discharging_max_duration_ms &&
				is_ext_pwr_online(cm)) {
			dev_info(cm->dev, "Discharging duration exceed %ums\n",
				charger->battery.discharging_max_duration_ms);
			uevent_notify(cm, "duration Recharging");
			jcmvote(cm->charger->charger_disable_votable, DURATION_CLIENT, false, 1);
			ret = true;
		}
	}

	return ret;
}

static int jcm_get_battery_temperature_by_psy(struct jlq_charger_manager *cm,
					int *temp)
{
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->charger->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_TEMP,
				(union power_supply_propval *)temp);
	power_supply_put(fuel_gauge);

	return ret;
}

static int jcm_get_battery_temperature(struct jlq_charger_manager *cm,
					int *temp)
{
	int ret;
	if (cm->charger->fake_temp < JCM_FAKE_TEMP_ERRVAL) {
		*temp =  cm->charger->fake_temp;
		return 0;
	}

	if (cm->charger->not_measure_battery_temp)
		return -ENODEV;

	/* if-else continued from CONFIG_THERMAL */
	ret = jcm_get_battery_temperature_by_psy(cm, temp);
	return ret;
}

static int jcm_check_thermal_status(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;
	int temp, upper_limit, lower_limit;
	int ret = 0;

	ret = jcm_get_battery_temperature(cm, &temp);
	if (ret < 0) {
/*
 *FIXME:
 * No information of battery temperature might
 * occur hazardous result. We have to handle it
 * depending on battery type.
 */
		dev_err(cm->dev, "Failed to get battery temperature\n");
		return 0;
	}

	upper_limit = charger->battery.temp_max;
	lower_limit = charger->battery.temp_min;

	if (cm->emergency_stop) {
		upper_limit -= charger->battery.temp_diff;
		lower_limit += charger->battery.temp_diff;
	}

	if (temp > upper_limit)
		ret = CM_EVENT_BATT_OVERHEAT;
	else if (temp < lower_limit)
		ret = CM_EVENT_BATT_COLD;

	return ret;
}

static void jcm_sw_jeita_work(struct work_struct *work)
{
	struct jlq_charger_manager *cm = container_of(work,
				struct jlq_charger_manager, swjeita_chk_work);
	struct jlq_charger_regulator *charger;
	struct jlq_charger_mangaer_battery_profile *battery;
	static struct jlq_charger_mangaer_step_rang *last_rang = NULL;
	struct jlq_charger_mangaer_step_rang *step_rang;
	int volt;
	int temp;
	int bat_ovp = 0;
	int ret = 0;
	int current_set, volt_set, real_volt;

	charger = cm->charger;
	battery = &charger->battery;
	if (!battery->sw_jeita_enabled)
		return;
	ret = jcm_get_battery_temperature(cm, &temp);
	if (ret < 0)
		return;
	ret = jcm_get_batt_uV(cm, &volt);
	if (ret < 0)
		return;
	ret = jcm_get_vreg_uV(cm, &real_volt);
	if (ret < 0)
		return;

	step_rang = battery->sw_jeita_rangs;
	current_set = step_rang->current_ua;
	volt_set = step_rang->float_volt_uv;
	for (step_rang = battery->sw_jeita_rangs;
			step_rang->low_threshold != 0 ||
			step_rang->high_threshold != 0;
			step_rang++) {
		if (temp > step_rang->low_threshold &&
				temp <= step_rang->high_threshold) {
			current_set = step_rang->current_ua;
			volt_set = step_rang->float_volt_uv;
			break;
		}
	}
	if (step_rang->low_threshold == 0 &&
				step_rang->high_threshold == 0) {
		dev_err(cm->dev, "SW JEITA out of Jeita Rangs. Stop Charge!");
		jcmvote(charger->fastchg_current_votable, SW_JEITA_CLIENT, false, current_set);
		jcmvote(charger->fastchg_volt_votable, SW_JEITA_CLIENT, false, volt_set);
		jcmvote(charger->charger_disable_votable, SW_JEITA_CLIENT, true, 0);
		last_rang = step_rang;
		return;
	}
	if ((last_rang != NULL) &&
		((last_rang->float_volt_uv / VREG_STEP) != (real_volt / VREG_STEP)) &&
		((charger->battery.fastchg_volt_uv / VREG_STEP) == (real_volt / VREG_STEP))) {
		bat_ovp++;
	}
	if (last_rang != step_rang) {
		dev_info(cm->dev, "SW JEITA Select:Rang[%d ~ %d] %duA, %duV",
			step_rang->low_threshold, step_rang->high_threshold,
			current_set, volt_set);
		if (bat_ovp) {
			jcmvote(charger->fastchg_volt_votable, SW_JEITA_CLIENT, true,
						charger->battery.fastchg_volt_uv);
			jcmvote(charger->charger_disable_votable, SW_JEITA_CLIENT, true, 0);
			dev_err(cm->dev, "update vreg/charger-enable status for BAT-OVP[temp change]!!!");
		}
		jcmvote(charger->fastchg_volt_votable, SW_JEITA_CLIENT, true, volt_set);
		jcmvote(charger->fastchg_current_votable, SW_JEITA_CLIENT, true, current_set);
		jcmvote(charger->charger_disable_votable, SW_JEITA_CLIENT, false, 1);
	} else if (volt <= charger->battery.batovp_recharge_uv) {
		if (bat_ovp) {
			jcmvote(charger->fastchg_volt_votable, SW_JEITA_CLIENT, true,
					charger->battery.fastchg_volt_uv);
			jcmvote(charger->charger_disable_votable, SW_JEITA_CLIENT, true, 0);
			dev_err(cm->dev, "update vreg/charger-enable status for BAT-OVP[vbat change]!!!");
		}
		jcmvote(charger->fastchg_volt_votable, SW_JEITA_CLIENT, true, volt_set);
		jcmvote(charger->fastchg_current_votable, SW_JEITA_CLIENT, true, current_set);
		jcmvote(charger->charger_disable_votable, SW_JEITA_CLIENT, false, 1);
	} else {
		jcmvote(charger->fastchg_volt_votable, SW_JEITA_CLIENT, true, volt_set);
		jcmvote(charger->fastchg_current_votable, SW_JEITA_CLIENT, true, current_set);
		jcmvote(charger->charger_disable_votable, SW_JEITA_CLIENT, false, 1);
	}

	last_rang = step_rang;
}

static int jcm_sw_thermal_chk(struct jlq_charger_manager *cm)
{
	int temp_alrt;
	temp_alrt = jcm_check_thermal_status(cm);
	if (temp_alrt) {
		jcmvote(cm->charger->charger_disable_votable, SW_THERMAL_CLIENT, true, 0);
		if (!cm->temp_alrt_stat) {
			dev_dbg(cm->dev, "[%s] assert TempArlt,Chg Stop.", SW_THERMAL_CLIENT);
			cm->temp_alrt_stat = true;
		}
		return temp_alrt;
	}
	jcmvote(cm->charger->charger_disable_votable, SW_THERMAL_CLIENT, false, 1);
	if (cm->temp_alrt_stat) {
		dev_dbg(cm->dev, "[%s] deassert TempArlt,Chg Resume.", SW_THERMAL_CLIENT);
		cm->temp_alrt_stat = false;
	}
	schedule_work(&cm->swjeita_chk_work);
	return 0;
}

#define JCM_INPUT_VOLT_ADJUST_RETRY  5
static void jcm_input_volt_adjust_work(struct work_struct *work)
{
	struct jlq_charger_regulator *charger = container_of(work,
				struct jlq_charger_regulator, input_volt_work.work);
	struct jlq_charger_manager *cm = charger->cm;
	int input_volt;
	int ret;
	int retry_cnt;
	union power_supply_propval val;
	struct power_supply *psy;

	// sgm41542 not support adc, so skip return from this function 
	psy = power_supply_get_by_name(charger->chips->charger_psy);
	if(psy)
	{
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_MODEL_NAME, &val);
		power_supply_put(psy);
		if(!ret && (strncmp(val.strval, "sgm4154x", 6) == 0) )
		{
			//dev_err(cm->dev, "%s POWER_SUPPLY_PROP_MODEL_NAME: %s !\n", __func__, val.strval);
			dev_err(cm->dev, "%s SKIP for sgm4154x charger ic!\n", __func__);
			jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT, true, JCM_USB_HVDCP_MAX_CHG_CUR_UA);
			jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT, true, JCM_USB_HVDCP_MAX_CUR_UA);
			charger->input_volt_rt = JCM_HVDCP_QC20_DEFAULT_VBUS_VOLT;
			return;
		}
	}
	// skip sgm41542 end.

	charger->input_volt_rt = JCM_VBUS_NORMAL_VOLT;
	pm_stay_awake(cm->dev);
	if (charger->chg_type != POWER_SUPPLY_TYPE_USB_HVDCP) {
		regulator_set_voltage(charger->regulator_input, 100000, 100000);  //relase D+D-
		goto errout;
	}
	dev_dbg(cm->dev, "Set VBus voltage :%d uV.\n", charger->input_volt_target);
	if (!charger->regulator_input && charger->input_regulator_name) {
		charger->regulator_input = regulator_get(cm->dev,
					charger->input_regulator_name);
		if (IS_ERR_OR_NULL(charger->regulator_input)) {
			dev_err(cm->dev, "Cannot find charger INPUT regulator(%s)\n",
				charger->input_regulator_name);
			charger->regulator_input = NULL;
			goto errout;
		}
	}

	if (!charger->regulator_input)
		goto errout;
	ret = regulator_set_voltage(charger->regulator_input,
		charger->input_volt_target, charger->input_volt_target);
	if (ret < 0) {
		dev_err(cm->dev, "set input voltage[%d]: return %d\n",
				charger->input_volt_target, ret);
	} else {
		msleep(500);
	}
	for (retry_cnt = 0; retry_cnt < JCM_INPUT_VOLT_ADJUST_RETRY; retry_cnt++) {
		input_volt = regulator_get_voltage(charger->regulator_input);
		if (input_volt == -EINVAL)
			goto errout;
		if (input_volt > 0 && abs(input_volt - charger->input_volt_target) < 800000)
			break;

		dev_dbg(cm->dev, "Set VBus voltage failed input_vol t:%d[t:%d]uV. Retry:%d\n",
			input_volt, charger->input_volt_target, retry_cnt);
		msleep(800);
	}
	if (retry_cnt >= JCM_INPUT_VOLT_ADJUST_RETRY) {
		/* If set VBus voltage failed, relase D+D-
		* Set Vbus power ability as DCP.
		*/
		jcm_set_chg_type(charger, POWER_SUPPLY_USB_TYPE_DCP);
		dev_dbg(cm->dev, "Set VBus voltage failed input_vol t:%d[t:%d]uV.\n",
			input_volt, charger->input_volt_target);
		regulator_set_voltage(charger->regulator_input, JCM_VBUS_NORMAL_VOLT, JCM_VBUS_NORMAL_VOLT);  //relase D+D-
		jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT, true, JCM_USB_DCP_MAX_CUR_UA * 12 / 10);
		jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT, true, JCM_USB_DCP_MAX_CUR_UA);
		goto out;
	}
	dev_dbg(cm->dev, "Set VBus voltage success input_volt:%d[t:%d]uV.\n",
		input_volt, charger->input_volt_target);
	jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT, true, JCM_USB_HVDCP_MAX_CHG_CUR_UA);
	jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT, true, JCM_USB_HVDCP_MAX_CUR_UA);
	charger->input_volt_rt = input_volt;
	goto out;
errout:
	jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT, true, JCM_USB_DCP_MAX_CUR_UA * 12 / 10);
	jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT, true, JCM_USB_DCP_MAX_CUR_UA);
out:
	pm_relax(cm->dev);
	return;
}
#if 0
static void jcm_hvdcp_monitor(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;
	int ibus;
	int input_volt;
	int ret;
	int power;
	int input_volt_target = charger->input_volt_target;

	if (charger->chg_type != POWER_SUPPLY_TYPE_USB_HVDCP ||
			!jcm_is_vbus_ok(cm))
		return ;

	ret = jcm_get_ibus_uA(cm, &ibus);
	if (ret < 0)
		return;
/*
* adjust hvdcp input voltage enough interval. default:1Min
*/
	if (ktime_before(ktime_get(),
			ktime_add_ms(charger->last_input_adjust, JCM_HVDCP_MONITOR_INTERV_MS)))
		return;
	input_volt = regulator_get_voltage(charger->regulator_input);
	if (input_volt < 0)
		return;
	power = (input_volt / 1000) * (ibus / 1000);

/*
* if power < 5W, swich to 5V charging.
* if power > 6W, swich to 9V charging.
* others Keep
*/
	if (charger->input_volt_target == JCM_HVDCP_QC20_DEFAULT_VBUS_VOLT &&
			power < (JCM_HVDCP_SW_5V_CUR_MA * (JCM_VBUS_NORMAL_VOLT / 1000))) {
		charger->input_volt_target = JCM_VBUS_NORMAL_VOLT;
	}else if(charger->input_volt_target == JCM_VBUS_NORMAL_VOLT &&
			power > (JCM_HVDCP_SW_HVDCP_CUR_MA * (JCM_VBUS_NORMAL_VOLT / 1000))) {
		charger->input_volt_target = JCM_HVDCP_QC20_DEFAULT_VBUS_VOLT;
	}
	if (input_volt_target != charger->input_volt_target &&
			abs(input_volt - charger->input_volt_target) > 800000) {
		dev_err(cm->dev, "IBus:%duA Input switch  to %duV",
			ibus, charger->input_volt_target);
		cancel_delayed_work(&charger->input_volt_work);
		charger->last_input_adjust = ktime_get();
		schedule_delayed_work(&charger->input_volt_work, 0);
	}
	return;
}

#endif
/*
* jcm_adapter_monitor
* Lower the current on the usb,
* avoid some adapter heating because Current exceeds the standard.
* Dec ICL when vbus <= 4.6V and ibus >  800mA.
* Mainly for DCP.
*/
static void jcm_adapter_monitor(struct jlq_charger_manager *cm)
{
	int input_volt = 0;
	int input_cur = 0;
	int input_set = 0;
	int input_temp = 0;
	int i;
	int ret;
	struct jlq_charger_regulator *charger = cm->charger;

	if (!jcm_is_vbus_ok(cm) || !charger->chargers_inited ||
			!charger->fg_inited)
		return ;

	ret = jcm_get_vbus_uV(cm, &input_volt);
	if (ret < 0 || input_volt == 0)
		return ;
	if (input_volt > JCM_ADP_DET_VBUS_LOW_UV)
		return ;

	for (i = 0, input_cur = 0; i < JCM_ADP_DET_IBUS_AVG; i++) {
		ret = jcm_get_ibus_uA(cm, &input_temp);
		if (ret < 0)
			return ;
		input_cur += input_temp;
		msleep(50);
	}
	input_cur /= JCM_ADP_DET_IBUS_AVG;
	if (input_cur < JCM_ADP_DET_IBUS_LOW_UA)
		return ;
	input_set = input_cur - (input_cur * JCM_ADP_DET_DEC_PERCENT / 100);

	if (charger->chg_type == POWER_SUPPLY_TYPE_USB_HVDCP &&
			input_set < JCM_USB_HVDCP_MIN_CUR_UA) {
		input_set = JCM_USB_HVDCP_MIN_CUR_UA;
	} else if (charger->chg_type == POWER_SUPPLY_TYPE_USB_DCP &&
			input_set < JCM_USB_DCP_MIN_CUR_UA) {
		input_set = JCM_USB_DCP_MIN_CUR_UA;
	} else if (input_set < JCM_USB_DEFAULT_MAX_CUR_UA) {
		input_set = JCM_USB_DEFAULT_MAX_CUR_UA;
	}

	ret = jcm_get_vbus_uV(cm, &input_volt);
	if (ret < 0 || input_volt == 0)
		return ;
	if (input_volt > JCM_ADP_DET_VBUS_LOW_UV)
		return ;
	jcmvote(cm->charger->input_current_limit_votable, ADAPTER_DET_CLIENT, true, input_set);
	dev_info(cm->dev, "Decrease off %d%% input current Limt ,%d uA->%d uA",
		JCM_ADP_DET_DEC_PERCENT, input_cur, input_set);
}

/*
* jcm_bat_soc_monitor
* Monitor the Battery SOC
*/
static void jcm_bat_soc_monitor(struct jlq_charger_manager *cm)
{
	int bat_soc;
	int ret;
	int batt_uV;
	int ibus;
	int temp;
	static int charger_calibrated = 0;
	struct jlq_charger_regulator *charger = cm->charger;
	union power_supply_propval val;

	if (charger->debug_fake_soc)
		return;

	if (charger->fake_soc >= 0 && charger->fake_soc <= 100)
		bat_soc = charger->fake_soc;
	else
		bat_soc = jcm_get_batt_soc(cm);

	ret = jcm_get_batt_ocv_uV(cm, &batt_uV);
	if (ret < 0)
		return;

	ret = jcm_get_ibus_uA(cm, &ibus);
	if (ret < 0)
		return;

	ret = jcm_get_battery_temperature(cm, &temp);
	if (ret < 0)
		return;

	if (charger->charge_ir_comp_off) {
		/*monitor the SOC for OFF charger IR Compensation.*/
		if ((bat_soc < JCM_CHARGE_IR_COMP_OFF_SOC_LEVEL) &&
			(temp < JCM_CHARGE_IR_COMP_OFF_TEMP) && !charger_calibrated) {
			charger_calibrated = val.intval = 1;
			jcm_set_charger_psy_prop(&charger->chips[0], POWER_SUPPLY_PROP_CALIBRATE, &val);
			dev_info(cm->dev, "SOC < %d temp < %d Open charger_calibrated. turn on IR-COMP\n",
				JCM_CHARGE_IR_COMP_OFF_SOC_LEVEL, JCM_CHARGE_IR_COMP_OFF_TEMP);
		} else if ((bat_soc >= JCM_CHARGE_IR_COMP_OFF_SOC_LEVEL) &&
				   (temp < JCM_CHARGE_IR_COMP_OFF_TEMP) && charger_calibrated) {
			charger_calibrated = val.intval = 0;
			jcm_set_charger_psy_prop(&charger->chips[0], POWER_SUPPLY_PROP_CALIBRATE, &val);
			dev_info(cm->dev, "SOC >= %d temp < %d Close charger_calibrated. turn off IR-COMP\n",
				JCM_CHARGE_IR_COMP_OFF_SOC_LEVEL, JCM_CHARGE_IR_COMP_OFF_TEMP);
		} else if ((temp >= JCM_CHARGE_IR_COMP_OFF_TEMP) &&
				   (batt_uV < JCM_CHARGE_IR_COMP_OFF_OCV) && charger_calibrated) {
			charger_calibrated = val.intval = 0;
			jcm_set_charger_psy_prop(&charger->chips[0], POWER_SUPPLY_PROP_CALIBRATE, &val);
			dev_info(cm->dev, "TEMP >= %d OCV < %d Close charger_calibrated. turn off IR-COMP\n",
				JCM_CHARGE_IR_COMP_OFF_TEMP, JCM_CHARGE_IR_COMP_OFF_OCV);
		} else if ((temp >= JCM_CHARGE_IR_COMP_OFF_TEMP) &&
				   (batt_uV >= JCM_CHARGE_IR_COMP_OFF_OCV) &&
				   (ibus >= JCM_CHARGE_IR_COMP_OFF_IBAT) && charger_calibrated) {
			charger_calibrated = val.intval = 0;
			jcm_set_charger_psy_prop(&charger->chips[0], POWER_SUPPLY_PROP_CALIBRATE, &val);
			dev_info(cm->dev, "TEMP >= %d OCV >= %d ibus >= %dClose charger_calibrated. turn off IR-COMP\n",
				JCM_CHARGE_IR_COMP_OFF_TEMP, JCM_CHARGE_IR_COMP_OFF_OCV, JCM_CHARGE_IR_COMP_OFF_IBAT);
		} else {}
	}
	return;
}

static void jcm_step_charge_work(struct work_struct *work)
{
	struct jlq_charger_manager *cm = container_of(work,
				struct jlq_charger_manager, step_chg_work);
	struct jlq_charger_regulator *charger;
	struct jlq_charger_mangaer_battery_profile *battery;
	struct jlq_charger_mangaer_step_rang *step_rang;
	int mV;
	int ret = 0;
	int current_set, volt_set;

	charger = cm->charger;
	battery = &charger->battery;
	if (!battery->step_chg_enabled)
		return;

	ret = jcm_get_batt_ocv_uV(cm, &mV);
	if (ret < 0)
		return;

	mV /= 1000;
	step_rang = battery->step_chg_rangs;
	current_set = step_rang->current_ua;
	volt_set = step_rang->float_volt_uv;
	for (step_rang = battery->step_chg_rangs;
			step_rang->low_threshold != 0 ||
			step_rang->high_threshold != 0;
			step_rang++) {
		if (mV >= step_rang->low_threshold &&
				mV < step_rang->high_threshold) {
			current_set = step_rang->current_ua;
			volt_set = step_rang->float_volt_uv;
			break;
		}
	}
	if (step_rang->low_threshold == 0 &&
			step_rang->high_threshold == 0) {
		jcmvote(charger->fastchg_current_votable, SW_STEPCHG_CLIENT, false, current_set);
		jcmvote(charger->fastchg_volt_votable, SW_STEPCHG_CLIENT, false, volt_set);
		jcmvote(charger->charger_disable_votable, SW_STEPCHG_CLIENT, true, 0);
		return;
	}
	jcmvote(charger->fastchg_current_votable, SW_STEPCHG_CLIENT, true, current_set);
	jcmvote(charger->fastchg_volt_votable, SW_STEPCHG_CLIENT, true, volt_set);
	jcmvote(charger->charger_disable_votable, SW_STEPCHG_CLIENT, false, 1);
}

static int jcm_calc_sys_soc(struct jlq_charger_manager *cm)
{
	jcm_soc_update_t *soc_update = &cm->soc_update;
	struct jlq_charger_regulator *charger = cm->charger;
	int ret;
	int bat_soc;
	int sys_soc;
	int bat_uv;
	int in_cutoff = 0;

	if (charger->fake_soc != JCM_FAKE_SOC_ERRVAL) {
		sys_soc = charger->fake_soc;
		goto out;
	}

	if (charger->debug_fake_soc) {
		sys_soc = JCM_DEBUG_BAT_FAKE_SOC_ERRVAL;
		goto out;
	}

	bat_soc = jcm_get_batt_soc(cm);

	soc_update->bat_soc = bat_soc;
	if (bat_soc < 0)
		return -ENODEV;

	if (bat_soc > soc_update->ssoc_full) {
		sys_soc = 100;
	} else if (bat_soc > soc_update->ssoc_cutoff) {
		sys_soc = (bat_soc - soc_update->ssoc_cutoff) * 1000;
		sys_soc /= (soc_update->ssoc_full - soc_update->ssoc_cutoff);
		/*rounding*/
		sys_soc += 5;
		sys_soc /= 10;
	}

	if (charger->soc_policy && bat_soc <= soc_update->ssoc_cutoff) {
		if (cm->cutoff_cnt >= JCM_CUTOFF_CNT_MAX) {
			sys_soc = 0;
		} else {
			sys_soc = 1;
			cm->cutoff_cnt++;
		}
		in_cutoff = 1;
	} else if (!charger->soc_policy) {
		ret = jcm_get_batt_ocv_uV(cm, &bat_uv);
		if (!ret && bat_uv <= JCM_CUTOFF_BAT_OCV) {
			if (cm->cutoff_cnt >= JCM_CUTOFF_CNT_MAX) {
				sys_soc = 0;
			} else {
				sys_soc = 1;
				cm->cutoff_cnt++;
			}
			in_cutoff = 1;
		}
	}
	if (!in_cutoff)
		cm->cutoff_cnt = 0;
	else
		dev_emerg(cm->dev, "In cut off status(%d).", cm->cutoff_cnt);

out:
	soc_update->sys_soc = sys_soc;
	dev_dbg(cm->dev, "[%s] bat_soc:%d sys_soc:%d",
		__func__, soc_update->bat_soc, sys_soc);
	return 0;
}

static void jcm_update_soc_work(struct work_struct *work)
{
	struct jlq_charger_manager *cm = container_of(work,
			struct jlq_charger_manager, soc_update_work.work);
	jcm_soc_update_t *soc_update = &cm->soc_update;
//	int charging;
	int next_interval = JCM_SOC_UPDATE_INTERV_NORMAL;
	int bat_current = 0;
	int bat_uv;
	int status = JCM_SOC_UPDATE_STATUS_QUIET;
	int ret;

	ret = jcm_calc_sys_soc(cm);
	if (ret < 0) {
		status = JCM_SOC_UPDATE_STATUS_INIT_FAST_QUERY;
//		dev_info(cm->dev, "Get SOC failed.");
		goto done ;
	}
//	charging = jcm_is_charging(cm);
	ret = jcm_get_batt_uA(cm, &bat_current);
//	dev_info(cm->dev, "[%s] bat_soc:%d sys_soc:%d msoc:%d charging:%d", __func__,
//		soc_update->bat_soc, soc_update->sys_soc, soc_update->msoc, charging);
	dev_dbg(cm->dev, "[%s] bat_soc:%d sys_soc:%d msoc:%d", __func__,
			soc_update->bat_soc, soc_update->sys_soc, soc_update->msoc);


	if (soc_update->sys_soc == soc_update->msoc) {
		/*
		*SYSSoc equ MSoc.
		*/
		goto done ;
	}
	if (soc_update->msoc < 0)
		 soc_update->msoc = soc_update->sys_soc;
#if 0
	if ((soc_update->sys_soc > soc_update->msoc && !charging) ||
		(soc_update->sys_soc < soc_update->msoc && bat_current > 0)) {
		/*
		*  SYSSoc above MSoc,but not charging.then display last MSoc.
		*  if SYSSoc above MSoc,but not charging. then display last MSoc.
		*/
		goto done ;
	}
#endif

	if (abs(soc_update->sys_soc - soc_update->msoc) >
			JCM_SOC_UPDATE_VERY_FAST_VARY) {
		/*if SYSSoc vary very great ,then expedite display.
		*/
		status = JCM_SOC_UPDATE_STATUS_VERY_FAST;
	} else if (abs(soc_update->sys_soc - soc_update->msoc) >
			JCM_SOC_UPDATE_FAST_VARY) {
		/*if SYSSoc vary great ,then expedite display.
		*/
		status = JCM_SOC_UPDATE_STATUS_FAST;
	} else if (abs(soc_update->sys_soc - soc_update->msoc) > 1) {
		status = JCM_SOC_UPDATE_STATUS_NORMAL;
	}
#if 1
	if (cm->charger->fake_soc != JCM_FAKE_SOC_ERRVAL) {
		soc_update->msoc = soc_update->sys_soc;
	} else if (soc_update->sys_soc > soc_update->msoc)
		soc_update->msoc++;
	else if (soc_update->sys_soc < soc_update->msoc)
		soc_update->msoc--;
#else
	soc_update->msoc = soc_update->sys_soc;
#endif
#ifdef QCOM_BATT_UNIFY_SYSFS
	if(cm->shutdown_delay_en) {
		ret = jcm_get_batt_uV(cm, &bat_uv);
		if(soc_update->msoc == 0) {
			if (!ret && (bat_uv > JCM_CUTOFF_BAT_UV)) {
				cm->shutdown_delay = !jcm_is_vbus_ok(cm);
				soc_update->msoc = 1;
			} else {
				cm->shutdown_delay = 0;
			}
		} else {
			cm->shutdown_delay = 0;
		}
		if(cm->last_shutdown_delay != cm->shutdown_delay) {
			cm->last_shutdown_delay = cm->shutdown_delay;
			generate_xm_charge_uvent(cm);
		}
	}
#endif
	power_supply_changed(cm->charger_psy);
done:
	if(status != JCM_SOC_UPDATE_STATUS_INIT_FAST_QUERY) {
		if (soc_update->sys_soc != soc_update->msoc) {
			if (soc_update->sys_soc >= 95 ||
				soc_update->sys_soc <= 5) {
				status = JCM_SOC_UPDATE_STATUS_VERY_FAST;
			} else if (soc_update->sys_soc >= 90 ||
				soc_update->sys_soc <= 10) {
				status = JCM_SOC_UPDATE_STATUS_FAST;
			}
		}
		if (jcm_is_full_charged(cm) && !cm->charger_enabled) {
			status = JCM_SOC_UPDATE_STATUS_NORMAL;
		}
		if (abs(bat_current / 1000) > 2000 &&
			status < JCM_SOC_UPDATE_STATUS_VERY_FAST) {
			status = JCM_SOC_UPDATE_STATUS_VERY_FAST;
		} else if (abs(bat_current / 1000) > 1000 &&
			status < JCM_SOC_UPDATE_STATUS_FAST) {
			status = JCM_SOC_UPDATE_STATUS_FAST;
		}

	}
	switch(status) {
		case JCM_SOC_UPDATE_STATUS_FAST:
			next_interval = JCM_SOC_UPDATE_INTERV_FAST;
			break;
		case JCM_SOC_UPDATE_STATUS_VERY_FAST:
			next_interval = JCM_SOC_UPDATE_INTERV_VERY_FAST;
			break;
		case JCM_SOC_UPDATE_STATUS_INIT_FAST_QUERY:
			next_interval = 1000;
			break;
		default:
			next_interval = JCM_SOC_UPDATE_INTERV_NORMAL;
			break;
	}
	if (status != JCM_SOC_UPDATE_STATUS_INIT_FAST_QUERY)
		dev_dbg(cm->dev, "[%s] status:%d batsoc:%d syssoc:%d  msoc:%d next_interval:%d", __func__,
			status, soc_update->bat_soc, soc_update->sys_soc, soc_update->msoc, next_interval);
	soc_update->status = status;
	soc_update->last_polling = jiffies;
	soc_update->next_polling = jiffies + msecs_to_jiffies(next_interval);
	schedule_delayed_work(&cm->soc_update_work, msecs_to_jiffies(next_interval));
}

static int jcm_update_soc_suspend(struct jlq_charger_manager *cm)
{
	jcm_soc_update_t *soc_update = &cm->soc_update;
	unsigned long next_polling_ms;

	next_polling_ms = jiffies_to_msecs(soc_update->next_polling
				- jiffies);
	if (time_is_before_eq_jiffies(soc_update->next_polling) ||
		msecs_to_jiffies(next_polling_ms) < CM_JIFFIES_SMALL) {
		jcm_update_soc_work(&cm->soc_update_work.work);
	}
	cancel_delayed_work_sync(&cm->soc_update_work);
	switch (soc_update->status){
		case JCM_SOC_UPDATE_STATUS_QUIET:
			next_polling_ms = JCM_SOC_UPDATE_INTERV_SLEEP - jiffies_to_msecs(
				(jiffies - soc_update->last_polling));
		break;
		default:
			next_polling_ms = jiffies_to_msecs(soc_update->next_polling
						- jiffies);
		break;
	}
	dev_dbg(cm->dev, "[%s]:next_polling_ms:%dms", __func__, next_polling_ms);
	return next_polling_ms;
}

static int jcm_update_soc_resume(struct jlq_charger_manager *cm)
{
	unsigned long now = jiffies + msecs_to_jiffies(1000);
	unsigned long delay;
	jcm_soc_update_t *soc_update = &cm->soc_update;

	if (time_before_eq(now, soc_update->next_polling))
		delay = jiffies_to_msecs(soc_update->next_polling - now);
	else
		delay = 0;

	if (delay > cm->jcm_suspend_duration_ms) {
		delay -= cm->jcm_suspend_duration_ms;
		schedule_delayed_work(&cm->soc_update_work,  msecs_to_jiffies(delay));
		dev_dbg(cm->dev, "[%s]:delay:%dms", __func__, delay);
	} else {
		delay = 0;
		schedule_delayed_work(&cm->soc_update_work, 0);
	}
	return 0;
}

static int jcm_update_soc_init(struct jlq_charger_manager *cm)
{
	jcm_soc_update_t *soc_update = &cm->soc_update;
	struct jlq_charger_mangaer_battery_profile *profile =
		&cm->charger->battery;

	soc_update->ssoc_cutoff = profile->cutoff_batt_soc;
	if(soc_update->ssoc_cutoff < 0 ||
			soc_update->ssoc_cutoff > 20)
		soc_update->ssoc_cutoff = 2;

	soc_update->ssoc_full = profile->fullbatt_soc;
	if (soc_update->ssoc_full < 80 ||
			soc_update->ssoc_full > 100)
		soc_update->ssoc_full = 95;

	soc_update->catoff_uv = profile->cutoff_batt_uV;
	if(soc_update->catoff_uv < 0 ||
			soc_update->catoff_uv > 10000000)
		soc_update->catoff_uv = 3560000;
	soc_update->full_uv = profile->fullbatt_uV;
	if(soc_update->full_uv < 0 ||
			soc_update->full_uv > 10000000)
		soc_update->full_uv = 4400000;
	INIT_DELAYED_WORK(&cm->soc_update_work, jcm_update_soc_work);
	return 0;
}

static int jcm_update_soc_restart(struct jlq_charger_manager *cm)
{
	cancel_delayed_work_sync(&cm->soc_update_work);
	jcm_update_soc_work(&cm->soc_update_work.work);
	return 0;
}
#ifndef QCOM_BATT_UNIFY_SYSFS
static int jcm_update_msoc_force_flush(struct jlq_charger_manager *cm)
#else
int jcm_update_msoc_force_flush(struct jlq_charger_manager *cm)
#endif
{
	jcm_soc_update_t *soc_update = &cm->soc_update;
	soc_update->msoc = -EINVAL;
	jcm_update_soc_restart(cm);
	return 0;
}

static void jcm_charging_dbg_record_work(struct work_struct *work)
{
	struct jlq_charger_regulator *charger = container_of(work,
				struct jlq_charger_regulator, dbg_pr_work.work);
	struct jlq_charger_manager *cm = charger->cm;
	const char *dischg_client;
	const char *fcc_client;
	int dischg_stat;
	jcm_soc_update_t *soc_update = &cm->soc_update;
	int bat_ocv, bat_volt, cur_now;
	int bat_soc, sys_soc, msoc;
	int charge_now = 0, charge_full = 0, chg_stat;
	int ibus, usb_input_volt;
	int fcc, fcv;
	int ret;
	union power_supply_propval val;

	jcm_get_batt_ocv_uV(cm, &bat_ocv);
	jcm_get_batt_uV(cm, &bat_volt);
	jcm_get_batt_uA(cm, &cur_now);
	bat_soc = soc_update->bat_soc;
	sys_soc = soc_update->sys_soc;
	msoc = soc_update->msoc;
	ret = jcm_get_gauge_psy_prop(cm, POWER_SUPPLY_PROP_CHARGE_NOW, &val);
	if (!ret)
		charge_now = val.intval;

	ret = jcm_get_gauge_psy_prop(cm, POWER_SUPPLY_PROP_CHARGE_FULL, &val);
	if (!ret)
		charge_full = val.intval;

	chg_stat = jcm_is_charging(cm);
	dischg_client = jcmvote_get_effective_client(charger->charger_disable_votable);
	dischg_stat = jcmvote_get_effective_result(charger->charger_disable_votable);
	fcc_client =  jcmvote_get_effective_client(charger->fastchg_current_votable);
	usb_input_volt = regulator_get_voltage(charger->regulator_input);
	jcm_get_ibus_uA(cm, &ibus);
	fcc = charger->set_current;
	fcv = charger->set_volt;
	pr_emerg("JCMREC: OCV:%d Volt:%d Cur:%d BSoc:%d Ssoc:%d Msoc:%d Cnow:%d Cfull:%d Chg:%d ibus:%d vbus:%d fcv:%d fcc:%d fccEf:%s ChgS:%d ChgEf:%s",
		bat_ocv, bat_volt, cur_now, bat_soc, sys_soc, msoc,charge_now, charge_full,
		chg_stat, ibus, usb_input_volt, fcv, fcc, fcc_client, !dischg_stat, dischg_client);
	if (charger->dbg_work_s != 0 && charger->dbg_work_s < 600)
		schedule_delayed_work(&charger->dbg_pr_work, msecs_to_jiffies(charger->dbg_work_s * 1000));
	return ;
}

/*
 * _jcm_cm_monitor - Monitor the temperature and return true for exceptions.
 * @cm: the Charger Manager representing the battery.
 *
 * Returns true if there is an event to notify for the battery.
 * (True if the status of "emergency_stop" changes)
 */
static bool _jcm_cm_monitor(struct jlq_charger_manager *cm)
{

	jcm_sw_thermal_chk(cm);
/*
 * Check temperature whether overheat or cold.
 * If temperature is out of range normal state, stop charging.
 */
	if (check_charging_duration(cm)) {
		dev_dbg(cm->dev,
			"Charging/Discharging duration is out of range\n");
/*
 * Check dropped voltage of battery. If battery voltage is more
 * dropped than fullbatt_vchkdrop_uV after fully charged state,
 * charger-manager have to recharge battery.
 */
	}
//	jcm_hvdcp_monitor(cm);
//	jcm_adapter_monitor(cm);
	jcm_bat_soc_monitor(cm);
	if (jcm_is_full_charged(cm) && cm->charger_enabled) {
		if (cm->charger->soft_recharge_flag) {
			jcm_fullbatt_handler(cm);
//			return true;
		}
	}
	if (cm->charger->battery.step_chg_enabled)
		schedule_work(&cm->step_chg_work);
	return true;
}

static void jcm_monitor_task(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	int ret;
	struct jlq_charger_manager *cm =
		container_of(dwork, struct jlq_charger_manager, jcm_monitor);

	if (!cm->pm_states_awake) {
		pm_stay_awake(cm->dev);
		cm->pm_states_awake = true;
	}
	ret = _jcm_cm_monitor(cm);
	if (jcm_is_vbus_ok(cm)) {
		cm->next_polling = jiffies +
				msecs_to_jiffies(cm->charger->polling_interval_ms);
		schedule_delayed_work(dwork,
				msecs_to_jiffies(cm->charger->polling_interval_ms));
	}
	if (!jcm_is_charging(cm) & cm->pm_states_awake) {
		pm_relax(cm->dev);
		cm->pm_states_awake = false;
	}
}

static int jcm_get_cali(struct jlq_charger_manager *cm)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->charger->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge,
		POWER_SUPPLY_PROP_CALIBRATE, &val);
	return ret;
}

/**
 * jcm_fullbatt_handler - Event handler for CM_EVENT_BATT_FULL
 * @cm: the Charger Manager representing the battery.
 */
static void jcm_fullbatt_handler(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;

	if ((charger->battery.rechg_volt_base && !charger->battery.fullbatt_vchkdrop_uV) ||
		(!charger->battery.rechg_volt_base && !charger->battery.fullbatt_rechgsoc) ||
			!charger->fullbatt_vchkdrop_ms)
		goto out;

	if (cm->jcm_suspended)
		device_set_wakeup_capable(cm->dev, true);

	if (charger->soft_recharge_flag) {
		charger->fullbatt_term_time_at = ktime_get_boottime_seconds();
		dev_err(cm->dev, "Battery Full Stop Charged\n");
		jcmvote(charger->charger_disable_votable, SW_RECHG_CLIENT, true, 0);
		cm->fullbatt_vchk_jiffies_at = jiffies + msecs_to_jiffies(
							charger->fullbatt_vchkdrop_ms);
		schedule_delayed_work(&cm->fullbatt_vchk_work,
				msecs_to_jiffies(charger->fullbatt_vchkdrop_ms));
	}
	// check the fg cali status
	jcm_get_cali(cm);
out:
	dev_dbg(cm->dev, "EVENT_HANDLE: Battery Fully Charged\n");
	uevent_notify(cm, default_event_names[CM_EVENT_BATT_FULL]);
}

/**
 * jcm_battout_handler - Event handler for CM_EVENT_BATT_OUT
 * @cm: the Charger Manager representing the battery.
 */
static void jcm_battout_handler(struct jlq_charger_manager *cm)
{
	if (cm->jcm_suspended)
		device_set_wakeup_capable(cm->dev, true);

	if (!is_batt_present(cm)) {
		dev_info(cm->dev, "Battery Pulled Out!\n");
		uevent_notify(cm, default_event_names[CM_EVENT_BATT_OUT]);
	} else
		uevent_notify(cm, "Battery Reinserted?");

}

/**
 * misc_event_handler - Handler for other events
 * @cm: the Charger Manager representing the battery.
 * @type: the Charger Manager representing the battery.
 */
static void misc_event_handler(struct jlq_charger_manager *cm,
			enum jcm_event_types type)
{
	if (cm->jcm_suspended)
		device_set_wakeup_capable(cm->dev, true);

	cancel_delayed_work_sync(&cm->jcm_monitor);
	schedule_work(&cm->jcm_monitor.work);
	uevent_notify(cm, default_event_names[type]);
}

/*
 * jcm_notify_event - charger driver notify Charger Manager of charger event
 * @psy: pointer to instance of charger's power_supply
 * @type: type of charger event
 * @msg: optional message passed to uevent_notify function
 */
void jcm_notify_event(struct jlq_charger_manager *cm, enum jcm_event_types type, char *msg)
{
	switch (type) {
	case CM_EVENT_BATT_FULL:
		jcm_fullbatt_handler(cm);
		break;
	case CM_EVENT_BATT_OUT:
		jcm_battout_handler(cm);
		break;
	case CM_EVENT_BATT_IN:
	case CM_EVENT_EXT_PWR_IN_OUT ... CM_EVENT_CHG_START_STOP:
		misc_event_handler(cm, type);
		break;
	case CM_EVENT_UNKNOWN:
	case CM_EVENT_OTHERS:
		uevent_notify(cm, msg ? msg : default_event_names[type]);
		break;
	default:
		dev_err(cm->dev, "%s: type not specified\n", __func__);
		break;
	}
}

static int jcm_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct jlq_charger_manager *cm = power_supply_get_drvdata(psy);
	struct jlq_charger_regulator *charger = cm->charger;
	struct power_supply *fuel_gauge = NULL;
	int temp_val = 0;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		temp_val = jcm_charging_status(cm);
		val->intval = temp_val < 0 ? POWER_SUPPLY_STATUS_UNKNOWN : temp_val;
		break;
/*	case POWER_SUPPLY_PROP_HEALTH:
		if (cm->emergency_stop > 0)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (cm->emergency_stop < 0)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;*/
	case POWER_SUPPLY_PROP_PRESENT:
		if (is_batt_present(cm))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = jcm_get_batt_uV(cm, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = jcm_get_batt_uA(cm, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		ret = jcm_get_gauge_psy_prop(cm, POWER_SUPPLY_PROP_TEMP_AMBIENT, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = jcm_get_battery_temperature(cm, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
//		if (!is_batt_present(cm)) {
//			/* There is no battery. Assume 100% */
//			val->intval = 100;
//			break;
//		}
		ret = jcm_get_msoc(cm);
		if (ret < 0) {
			break;
		}
		val->intval = ret;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (is_ext_pwr_online(cm))
			val->intval = 1;
		else
			val->intval = 0;

		//begin gerrit 208763
		ret = jcm_get_msoc(cm);
		if (ret == 0)
			val->intval = 0;
		//end gerrit 208763
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
	case POWER_SUPPLY_PROP_HEALTH:
		ret = jcm_get_gauge_psy_prop(cm, psp, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = jcm_get_gauge_psy_prop(cm, POWER_SUPPLY_PROP_CHARGE_NOW, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = jcm_get_control_limt_level(cm);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		ret = jcm_get_control_limt_max_level(cm, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_CHARGE_TYPE, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX: //cv
		ret = jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_VOLTAGE_MAX, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, val);
		break;
	case POWER_SUPPLY_PROP_QUICK_CHARGE_TYPE:
		ret = jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_QUICK_CHARGE_TYPE, val);
		break;
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		ret = jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_PRECHARGE_CURRENT, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		ret = jcm_get_gauge_psy_prop(cm, POWER_SUPPLY_PROP_TECHNOLOGY, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, val);
		break;
	default:
		return -EINVAL;
	}
	if (fuel_gauge)
		power_supply_put(fuel_gauge);

	return ret;
}
static int jcm_set_property(
	struct power_supply *psy, enum power_supply_property psp,
	const union power_supply_propval *val)
{
	int ret = 0;
	struct jlq_charger_manager *cm = power_supply_get_drvdata(psy);
	switch(psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		ret = jcm_set_control_limt_level(cm, val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int jcm_prop_is_writeable(
	struct power_supply *psy, enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		break;
	}
	return 0;
}

#define NUM_CHARGER_PSY_OPTIONAL	(4)
static enum power_supply_property default_charger_props[] = {
	/* Guaranteed to provide */
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
//	POWER_SUPPLY_PROP_VOLTAGE_MAX, //cv
//	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_QUICK_CHARGE_TYPE,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
#ifdef CONFIG_THERMAL
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
#endif
};

static const struct power_supply_desc psy_default = {
	.name = "jcmbattery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = default_charger_props,
	.num_properties = ARRAY_SIZE(default_charger_props),
	.get_property = jcm_get_property,
	.set_property = jcm_set_property,
	.property_is_writeable = jcm_prop_is_writeable,
	.no_thermal = true,
};

/**
 * jcm_setup_timer - For in-suspend monitoring setup wakeup alarm
 *		    for suspend_again.
 *
 * Returns true if the alarm is set for Charger Manager to use.
 * Returns false if
 *	jcm_setup_timer fails to set an alarm,
 *	jcm_setup_timer does not need to set an alarm for Charger Manager,
 *	or an alarm previously configured is to be used.
 */
static bool jcm_setup_timer(struct jlq_charger_manager *cm)
{
	unsigned int wakeup_ms = UINT_MAX;
	unsigned int fbchk_ms = 0;
	unsigned int soc_update_ms = 0;
	int timer_req = 0;

	if (jcm_is_vbus_ok(cm)) {
		if (time_after(cm->next_polling, jiffies))
			wakeup_ms = CM_MIN_VALID(wakeup_ms,
				jiffies_to_msecs(cm->next_polling - jiffies));

		/* fullbatt_vchk is required. setup timer for that */
		if (cm->fullbatt_vchk_jiffies_at) {
			fbchk_ms = jiffies_to_msecs(cm->fullbatt_vchk_jiffies_at
						- jiffies);
			if (time_is_before_eq_jiffies(
				cm->fullbatt_vchk_jiffies_at) ||
				msecs_to_jiffies(fbchk_ms) < CM_JIFFIES_SMALL) {
				fullbatt_vchk(&cm->fullbatt_vchk_work.work);
				fbchk_ms = 0;
			}
		}
		wakeup_ms = CM_MIN_VALID(wakeup_ms, fbchk_ms);

	/* Skip if polling is not required for this CM */
		timer_req++;
		wakeup_ms = CM_MIN_VALID(wakeup_ms, cm->charger->polling_interval_ms);
	}
	soc_update_ms = jcm_update_soc_suspend(cm);
	wakeup_ms = CM_MIN_VALID(wakeup_ms, soc_update_ms);
	timer_req++;
	dev_dbg(cm->dev, "[%s]:wakeup_ms:%dms timer_req:%d",
		__func__, wakeup_ms, timer_req);
	cancel_delayed_work(&cm->charger->dbg_pr_work);

	if (timer_req) {
		ktime_t now, add;

		/*
		 * Set alarm with the polling interval (wakeup_ms)
		 * The alarm time should be NOW + CM_RTC_SMALL or later.
		 */
		if (wakeup_ms == UINT_MAX ||
			wakeup_ms < CM_RTC_SMALL * MSEC_PER_SEC)
			wakeup_ms = 2 * CM_RTC_SMALL * MSEC_PER_SEC;

		now = ktime_get_boottime();
		dev_dbg(cm->dev,"Charger Manager wakeup timer: %u ms,Mow %u ns\n", wakeup_ms, now);
		add = ktime_set(wakeup_ms / MSEC_PER_SEC,
				(wakeup_ms % MSEC_PER_SEC) * NSEC_PER_MSEC);
		alarm_start(&cm->jcm_timer, ktime_add(now, add));

		cm->jcm_suspend_duration_ms = wakeup_ms;

		return true;
	}
	return false;
}


static void  jcm_set_current_steper(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct jlq_charger_manager *cm = container_of(dwork,
			struct jlq_charger_manager, steper_current_work);
	int i;
	struct jlq_charger_regulator *charger = cm->charger;
	struct jlq_charger_chip *chip;
	int set_done = 1;
	int ret;

	mutex_lock(&charger->lock);
	for (i = 0; i < charger->num_chips; i++) {
		chip = &charger->chips[i];
		if (chip->set_uA == chip->now_uA) {
			continue;
		} else if (chip->now_uA > chip->set_uA) {
			chip->now_uA = chip->set_uA;
			if (!chip->cable_regulator) {
			chip->cable_regulator = regulator_get(cm->dev, chip->chip_regulator_name);
			if (IS_ERR_OR_NULL(chip->cable_regulator))
				chip->cable_regulator = NULL;
			}
			if (chip->cable_regulator) {
				dev_dbg(cm->dev, "JCM Set(%s) current to %d uA.",
					chip->chip_regulator_name, chip->now_uA);
				ret = regulator_set_current_limit(chip->cable_regulator,
					chip->now_uA, chip->now_uA);
				if (ret < 0)
					dev_info(cm->dev, "Set charger(%s) current Failed(%d).", chip->chip_regulator_name, ret);
			}
		} else {
			if (charger->steper_val > 0  && charger->steper_intv_ms > 0 &&
					chip->now_uA + charger->steper_val < chip->set_uA ){
				set_done = 0;
				chip->now_uA += charger->steper_val;
			} else {
				chip->now_uA = chip->set_uA;
			}
			if (!chip->cable_regulator) {
				chip->cable_regulator = regulator_get(cm->dev, chip->chip_regulator_name);
				if (IS_ERR_OR_NULL(chip->cable_regulator))
					chip->cable_regulator = NULL;
			}
			if (chip->cable_regulator) {
				dev_dbg(cm->dev, "JCM Set(%s) current to %d uA.",
					chip->chip_regulator_name, chip->now_uA);
				ret = regulator_set_current_limit(chip->cable_regulator,
					chip->now_uA, chip->now_uA);
				if (ret < 0)
					dev_info(cm->dev, "Set charger(%s) current Failed(%d).", chip->chip_regulator_name, ret);
			}
		}
	}
	if (!set_done)
		schedule_delayed_work(&cm->steper_current_work, charger->steper_intv_ms * HZ / 1000);
	mutex_unlock(&charger->lock);
}

int jcm_allot_chips_set_current(struct jlq_charger_manager *cm, int total_current)
{
	int i;
	int current_remain;
	int current_max;
	int arg_num = 0;
	int arg_persent = 100;
//	int ret;

	struct jlq_charger_regulator *charger;
	struct jlq_charger_chip *chip;

	charger = cm->charger;
	current_max = current_remain = total_current;
	mutex_lock(&charger->lock);
	for (i = 0; i < charger->num_chips; i++) {
		chip = &charger->chips[i];
		if (chip->current_percent <= 0 || chip->current_percent >= 100) {
			chip->current_percent = 0;
			arg_num++;
		} else
			arg_persent -= chip->current_percent;

	}
	if (arg_persent < 0)
		arg_num = charger->num_chips;

	for (i = 0; i < charger->num_chips; i++) {
		chip = &charger->chips[i];
		if (chip->current_percent || arg_num == charger->num_chips) {
			chip->set_uA = current_max * 100 / chip->current_percent;
			if (chip->set_uA > chip->max_uA)
				chip->set_uA = chip->max_uA;

			if (chip->set_uA > current_remain)
				chip->set_uA = current_remain;

			current_remain -= chip->set_uA;
		} else {
			chip->set_uA = current_max/arg_num;
			if (chip->set_uA > chip->max_uA)
				chip->set_uA = chip->max_uA;

			if (chip->set_uA > current_remain)
				chip->set_uA = current_remain;

			current_remain -= chip->set_uA;
		}
		if (current_remain <= 0)
			break;

	}
	if (current_remain > 0) {
		for (i = 0; i < charger->num_chips; i++) {
			chip = &charger->chips[i];
			if (chip->set_uA + current_remain >= chip->max_uA) {
				current_remain -= (chip->max_uA - chip->set_uA);
				chip->set_uA = chip->max_uA;
			} else {
				chip->set_uA += current_remain;
				current_remain = 0;
				break;
			}
			if (current_remain <= 0)
				break;
		}
	}
	mutex_unlock(&charger->lock);
	schedule_delayed_work(&cm->steper_current_work, charger->steper_intv_ms * HZ / 1000);
	return 0;
}

int jcm_charger_disable(
	struct jcmvotable *votable,
	void *data,
	int disabled,
	const char *client)
{
	struct jlq_charger_manager *cm = (struct jlq_charger_manager *)data;
	struct jlq_charger_regulator *charger;
	struct jlq_charger_chip *chip;
	int i;
	int volt;
	int ret;

	charger = cm->charger;
	chip = charger->chips;
	if (cm->charger_enabled == !disabled || !cm->inited)
		return 0;

	cm->charger_enabled = !disabled;
	if (disabled) {
		dev_info(cm->dev, "JCM stop charge[%s].", client);
		mutex_lock(&charger->lock);
		cancel_delayed_work(&cm->steper_current_work);
		for (i = 0; i < charger->num_chips; i++, chip++) {
			if (!chip->cable_regulator) {
				chip->cable_regulator = regulator_get(cm->dev, chip->chip_regulator_name);
				if (IS_ERR_OR_NULL(chip->cable_regulator))
					chip->cable_regulator = NULL;
			}
			if (chip->cable_regulator && !cm->charger_enabled) {
				chip->now_uA = JCM_USB_DEFAULT_MIN_CUR_UA;
				ret = regulator_set_current_limit(chip->cable_regulator,
						chip->now_uA, chip->now_uA);
				dev_dbg(cm->dev, "JCM stop charge. Set(%s) current %d uA.",
					chip->chip_regulator_name, chip->now_uA);
				ret = regulator_disable(chip->cable_regulator);
				if (ret < 0)
					dev_info(cm->dev, "Disable charger(%s) Failed(%d).",
						chip->chip_name, ret);
			}
		}
		mutex_unlock(&charger->lock);
	} else {
		dev_info(cm->dev, "JCM Start charge[%s].", client);
		volt = charger->set_volt;
		jcm_allot_chips_set_current(cm, charger->set_current);
		mutex_lock(&charger->lock);
		for (i = 0; i < charger->num_chips; i++, chip++) {
			if (!chip->cable_regulator) {
				chip->cable_regulator = regulator_get(cm->dev, chip->chip_regulator_name);
				if (IS_ERR_OR_NULL(chip->cable_regulator))
					chip->cable_regulator = NULL;
			}
			if (chip->cable_regulator) {
				dev_dbg(cm->dev, "JCM chip[%s] Volt:%d.",
					chip->chip_regulator_name, volt);
				ret = regulator_set_voltage(chip->cable_regulator,  volt - 16000, volt);
				if (ret < 0)
					dev_info(cm->dev, "Set charger(%s) voltage Failed(%d).", chip->chip_name, ret);
				if (!regulator_is_enabled(chip->cable_regulator)) {
					ret = regulator_enable(chip->cable_regulator);
					if (ret < 0)
						dev_info(cm->dev, "Enable charger(%s) Failed(%d).", chip->chip_name, ret);
				}
			} else {
				dev_info(cm->dev, "NO Charger Regulator (%s).", chip->chip_name);
			}
		}
		mutex_unlock(&charger->lock);
	}
	jcm_update_soc_restart(cm);
	return 0;
}

int jcm_set_fast_chg_current(
	struct jcmvotable *votable,
	void *data,
	int curr,
	const char *client)
{
	struct jlq_charger_manager *cm = (struct jlq_charger_manager *)data;
	struct jlq_charger_regulator *charger;

	charger = cm->charger;
	charger->set_current = curr;
	if (!cm->charger_enabled)
		return 0;

	dev_info(cm->dev, "JCM Set charge current:%d uA.[%s]", curr, client);
	return jcm_allot_chips_set_current(cm, curr);
}

int jcm_set_fast_chg_volt(
	struct jcmvotable *votable,
	void *data,
	int volt,
	const char *client
)
{
	struct jlq_charger_manager *cm = (struct jlq_charger_manager *)data;
	struct jlq_charger_regulator *charger;
	struct jlq_charger_chip *chip;
	int i;

	charger = cm->charger;
	charger->set_volt = volt;
	dev_info(cm->dev, "JCM Set charge VOLT:%d uV.[%s]", volt, client);
	if (!cm->charger_enabled)
		return 0;

	chip = charger->chips;
	for (i = 0; i < charger->num_chips; i++, chip++) {
		if (!chip->cable_regulator) {
			chip->cable_regulator = regulator_get(cm->dev, chip->chip_regulator_name);
			if (IS_ERR(chip->cable_regulator))
				chip->cable_regulator = NULL;
		}
		if (chip->cable_regulator)
			regulator_set_voltage(chip->cable_regulator, volt - 16000, volt);
	}
	return 0;
}

int jcm_set_input_cur_limt(
	struct jcmvotable *votable,
	void *data,
	int icl,
	const char *client
)
{
	struct jlq_charger_manager *cm = (struct jlq_charger_manager *)data;
	struct jlq_charger_regulator *charger = cm->charger;

	if (!charger->input_regulator_name)
		return 0;
	if (!charger->regulator_input) {
		charger->regulator_input = regulator_get(cm->dev,
					charger->input_regulator_name);
		if (IS_ERR_OR_NULL(charger->regulator_input)) {
			dev_err(cm->dev, "Cannot find charger INPUT regulator(%s)\n",
				charger->input_regulator_name);
			charger->regulator_input = NULL;
			return -ENODEV;
		}
	}
	regulator_set_current_limit(charger->regulator_input, icl + (50 *1000), icl + (99 *1000));
	return 0;
}

/**
 * charger_extcon_usb_device_notifier - receive the state of charger chip
 *			when registered chip is attached or detached.
 *
 * @self: the notifier block of the charger_extcon_notifier.
 * @event: the chip state.
 * @ptr: the data pointer of notifier block.
 */
static int charger_vbus_state_changed(struct jlq_charger_regulator *charger,bool vbus_ok)
{
	struct jlq_charger_manager *cm = charger->cm;
	bool last_vbus_stat = !!atomic_read(&charger->attached);

	dev_dbg(cm->dev, "Get vbus state changed.(%d)\n", vbus_ok);
/*
 * The newly state of charger chip.
 * If chip is attached, chip->attached is true.
 */
	if (!vbus_ok) {
		cancel_delayed_work(&cm->fullbatt_vchk_work);
		jcmvote(charger->charger_disable_votable, SW_RECHG_CLIENT, false, 1);
		cm->charger->fullbatt_term_time_at = 0;
	}
	atomic_set(&charger->attached, !!vbus_ok);
//	if (charger->attached && charger->otg_regulator)
//		regulator_disable(charger->otg_regulator);
/*
* cancel full battery chk,if plug out.
*/
/*
 * Setup monitoring to check battery state
 * when charger chip is attached.
 */
	try_charger_enable(charger->cm, NULL, !!vbus_ok);

	if (last_vbus_stat != vbus_ok)
		jcm_update_soc_restart(cm);
	if (atomic_read(&charger->attached)) {
		cancel_delayed_work_sync(&cm->jcm_monitor);
		schedule_work(&cm->jcm_monitor.work);
	}
	return NOTIFY_DONE;
}

static int charger_extcon_usb_type_notifier(struct notifier_block *self,
			unsigned long event, void *ptr)
{
	struct jlq_charger_regulator *charger =
		container_of(self, struct jlq_charger_regulator, usb_type_nb);
	bool vbus_ok = false;

	cancel_delayed_work(&charger->input_volt_work);
	charger->input_volt_target = 5000000;
	jcmvote(charger->input_current_limit_votable, ADAPTER_DET_CLIENT,
		false, JCM_USB_DEFAULT_MAX_CUR_UA);
	if (extcon_get_state(charger->extcon_dev, EXTCON_CHG_USB_CDP) > 0) {
		jcm_set_chg_type(charger, POWER_SUPPLY_TYPE_USB_CDP);
		vbus_ok = true;
		jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT,
			true, JCM_USB_CDP_MAX_CUR_UA * 12 / 10);
		jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT,
			true, JCM_USB_CDP_MAX_CUR_UA);
	} else if (extcon_get_state(charger->extcon_dev, EXTCON_CHG_USB_FAST) > 0) {
		//Hold D+D- to 5V.some charge chip can't set qc2.0 immediate.
//		regulator_set_voltage(charger->regulator_input, 5000000, 5000000);
		charger->input_volt_target = charger->qc20_vbus_volt_setting;
		jcm_set_chg_type(charger, POWER_SUPPLY_TYPE_USB_HVDCP);
		vbus_ok = true;
		jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DEFAULT_MAX_CUR_UA * 12 / 10);
		jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DEFAULT_MAX_CUR_UA);
		/*If not raised input voltage, set input power ability as DCP.*/
		charger->last_input_adjust = ktime_get();
//		schedule_delayed_work(&charger->input_volt_work, 2 * HZ);
		schedule_delayed_work(&charger->input_volt_work, msecs_to_jiffies(200));
	} else if (extcon_get_state(charger->extcon_dev, EXTCON_CHG_USB_DCP) > 0) {
		jcm_set_chg_type(charger, POWER_SUPPLY_TYPE_USB_DCP);
		vbus_ok = true;
		jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DCP_MAX_CUR_UA * 12 / 10);
		jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DCP_MAX_CUR_UA);
	} else if (extcon_get_state(charger->extcon_dev, EXTCON_CHG_USB_SDP) > 0) {
		jcm_set_chg_type(charger, POWER_SUPPLY_TYPE_USB);
		vbus_ok = true;
		jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DEFAULT_MAX_CUR_UA * 12 / 10);
		jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DEFAULT_MAX_CUR_UA);
	} else if (extcon_get_state(charger->extcon_dev, JLQ_EXTCON_CHG_USB_FLOAT) > 0) {
		jcm_set_chg_type(charger, POWER_SUPPLY_TYPE_USB_FLOAT);
		vbus_ok = true;
		jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT,
			true, JCM_USB_FLOAT_MAX_CUR_UA * 12 / 10);
		jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT,
			true, JCM_USB_FLOAT_MAX_CUR_UA);
	} else {
		vbus_ok = false;
		jcm_set_chg_type(charger, POWER_SUPPLY_TYPE_UNKNOWN);
		jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DEFAULT_MAX_CUR_UA * 12 / 10);
		jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT,
			true, JCM_USB_DEFAULT_MAX_CUR_UA);
	}
	if (!vbus_ok) {
		regulator_set_voltage(charger->regulator_input, 100000, 100000);	//relase D+D-
	}

	charger_vbus_state_changed(charger, vbus_ok);
	dev_dbg(charger->cm->dev, "JCM Set charger->chg_type =[%d].", charger->chg_type);
	return 0;
}

static int charger_extcon_usb_id_notifier(struct notifier_block *self,
			unsigned long event, void *ptr)
{
	struct jlq_charger_regulator *charger =
		container_of(self, struct jlq_charger_regulator, usb_otg_nb);

	if (!charger->otg_regulator_name) {
		   dev_err(charger->cm->dev, "Not Set OTG regulator\n");
		   return 0;
	 }

	if (!charger->otg_regulator) {
		charger->otg_regulator = regulator_get(charger->cm->dev,
					charger->otg_regulator_name);
		if (IS_ERR_OR_NULL(charger->otg_regulator)) {
			dev_err(charger->cm->dev, "Cannot find  OTG regulator(%s)\n",
				charger->otg_regulator_name);
			charger->otg_regulator = NULL;
			return -ENODEV;
		}
		if(regulator_is_enabled(charger->otg_regulator))
			regulator_disable(charger->otg_regulator);
		charger->otg_enabled = regulator_is_enabled(charger->otg_regulator);
	}

	if (extcon_get_state(charger->extcon_id_dev, EXTCON_USB_HOST) > 0) {
		jcmvote(charger->charger_disable_votable, USB_OTG_CLIENT, true, 0);
		regulator_set_current_limit(charger->otg_regulator,
			charger->otg_boost_current , charger->otg_boost_current);
		regulator_set_voltage(charger->otg_regulator,
			charger->otg_boost_volt , charger->otg_boost_volt);
		if (charger->otg_regulator && !charger->otg_enabled ) {
			regulator_enable(charger->otg_regulator);
		}
		charger->otg_enabled = true;
		dev_err(charger->cm->dev, "OTG mode detect disable charge.");
	} else {
		jcmvote(charger->charger_disable_votable, USB_OTG_CLIENT, false, 1);
		if (charger->otg_regulator && charger->otg_enabled)
			regulator_disable(charger->otg_regulator);
		charger->otg_enabled = false;
		regulator_set_current_limit(charger->otg_regulator,
			JCM_OTG_DEFAULT_BOOST_CURRENT , JCM_OTG_DEFAULT_BOOST_CURRENT);
		regulator_set_voltage(charger->otg_regulator,
			JCM_OTG_DEFAULT_BOOST_VOLT , JCM_OTG_DEFAULT_BOOST_VOLT);
		dev_err(charger->cm->dev, "OTG mode remove enable charge.");
	}
//	dev_info(charger->cm->dev, "JCM Set charger->otg_enabled =.[%d]", charger->otg_enabled);
	return 0;
}

static struct extcon_dev *jcm_charger_extcon_scan(
	struct jlq_charger_manager *cm,
	const char *prop_name
)
{
	int ret;
	int i;
	struct extcon_dev *edev;

	const char *extcon_names[4];
	ret = of_property_read_string_array(cm->dev->of_node, prop_name,
			(const char**)extcon_names, 4);
	if (ret < 0)
		return ERR_PTR(ret);

	for (i = 0; i < ret; i++){
		edev = extcon_get_extcon_dev(extcon_names[i]);
		if (!IS_ERR_OR_NULL(edev))
			return edev;
	}
	return NULL;
}
/**
 * charger_extcon_init - register external connector to use it
 *			as the charger chip
 *
 * @cm: the Charger Manager representing the battery.
 * @chip: the Charger chip representing the external connector.
 */
static int charger_extcon_init(struct jlq_charger_manager *cm,
		struct jlq_charger_regulator *charger)
{
	int ret;

/*
 * Charger manager use Extcon framework to identify
 * the charger chip among various external connector
 * chip (e.g., TA, USB, MHL, Dock).
 */
//	INIT_WORK(&charger->extcon_usb_wq, charger_extcon_work);
//	charger->extcon_dev = extcon_get_extcon_dev(charger->extcon_name);
    if (charger->extcon_dev)
		return 0;
	charger->extcon_dev = extcon_get_edev_by_phandle(cm->dev, 0);
	if (IS_ERR_OR_NULL(charger->extcon_dev)) {
		charger->extcon_dev = jcm_charger_extcon_scan(cm, "usb-dev-extcons");
		if (IS_ERR_OR_NULL(charger->extcon_dev)) {
			charger->extcon_dev = NULL;
			dev_err(cm->dev, "Cannot find extcon_dev");
			return -ENODEV;
		}
	}
#if 0
	charger->usb_nb.notifier_call = charger_extcon_usb_device_notifier;
	ret = extcon_register_notifier(charger->extcon_dev, EXTCON_USB, &charger->usb_nb);
	if (ret < 0) {
		pr_info("Cannot register notifier EXTCON_USB for %s\n",
			extcon_get_edev_name(charger->extcon_dev));
//		return ret;
	}
#endif
#if 1
	charger->usb_type_nb.notifier_call = charger_extcon_usb_type_notifier;
	ret = devm_extcon_register_notifier_all(cm->dev,
			charger->extcon_dev, &charger->usb_type_nb);
	if (ret < 0) {
		pr_info("Cannot register extcon notifier for %s\n",
			extcon_get_edev_name(charger->extcon_dev));
	}
#else

	ret = extcon_register_notifier(charger->extcon_dev, EXTCON_CHG_USB_SDP, &charger->usb_type_nb);
	if (ret < 0) {
		pr_info("Cannot register notifier EXTCON_CHG_USB_SDP for %s\n",
			extcon_get_edev_name(charger->extcon_dev));
	}
	ret = extcon_register_notifier(charger->extcon_dev, EXTCON_CHG_USB_CDP, &charger->usb_type_nb);
	if (ret < 0) {
		pr_info("Cannot register notifier EXTCON_CHG_USB_SDP for %s\n",
			extcon_get_edev_name(charger->extcon_dev));
	}
	ret = extcon_register_notifier(charger->extcon_dev, EXTCON_CHG_USB_DCP, &charger->usb_type_nb);
	if (ret < 0) {
		pr_info("Cannot register notifier EXTCON_CHG_USB_DCP for %s\n",
			extcon_get_edev_name(charger->extcon_dev));
	}
	ret = extcon_register_notifier(charger->extcon_dev, EXTCON_CHG_USB_FAST, &charger->usb_type_nb);
	if (ret < 0) {
		pr_info("Cannot register notifier EXTCON_CHG_USB_FAST for %s\n",
			extcon_get_edev_name(charger->extcon_dev));
	}
	ret = extcon_register_notifier(charger->extcon_dev, JLQ_EXTCON_CHG_USB_FLOAT, &charger->usb_type_nb);
	if (ret < 0) {
		pr_info("Cannot register notifier JLQ_EXTCON_CHG_USB_FLOAT for %s\n",
			extcon_get_edev_name(charger->extcon_dev));
	}
#endif
	charger_extcon_usb_type_notifier(&charger->usb_type_nb,0,NULL);

	charger->extcon_id_dev = extcon_get_edev_by_phandle(cm->dev, 1);
	if (IS_ERR_OR_NULL(charger->extcon_id_dev)) {
		charger->extcon_id_dev = jcm_charger_extcon_scan(cm, "usb-id-extcons");
		if (IS_ERR_OR_NULL(charger->extcon_id_dev)) {
			charger->extcon_id_dev = NULL;
			dev_err(cm->dev, "Cannot find extcon_id");
			return 0;
		}
	}
	charger->usb_otg_nb.notifier_call = charger_extcon_usb_id_notifier;
	ret = extcon_register_notifier(charger->extcon_id_dev, EXTCON_USB_HOST, &charger->usb_otg_nb);
	if (ret < 0) {
		pr_err("Cannot register notifier EXTCON_USB_HOST for %s\n",
			extcon_get_edev_name(charger->extcon_id_dev));
		ret = 0;
	}
//	extcon_sync(charger->extcon_id_dev,EXTCON_USB_HOST);
	charger_extcon_usb_id_notifier(&charger->usb_otg_nb,0,NULL);
	return ret;
}

/**
 * charger_manager_init_chargers - Register extcon device to receive state
 *				of charger chip.
 * @cm: the Charger Manager representing the battery.
 *
 * This function support EXTCON(External Connector) subsystem to detect the
 * state of charger chips for enabling or disabling charger(regulator) and
 * select the charger chip for charging among a number of external chip
 * according to policy of H/W board.
 */
static int charger_manager_init_chargers(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;
	int ret;
	int i;
	int max_uA;
	struct jlq_charger_chip *chip;

	if (charger->input_regulator_name && !charger->regulator_input) {
		charger->regulator_input = regulator_get(cm->dev,
					charger->input_regulator_name);
		if (IS_ERR_OR_NULL(charger->regulator_input)) {
			dev_info(cm->dev, "Cannot find charger INPUT regulator(%s)\n",
				charger->input_regulator_name);
			charger->regulator_input = NULL;
			return -ENODEV;
		}
		regulator_set_current_limit(charger->regulator_input,
			JCM_USB_DEFAULT_MAX_CUR_UA, JCM_USB_DEFAULT_MAX_CUR_UA);
		regulator_enable(charger->regulator_input);
	} else {
		dev_info(cm->dev, "Not Set ChargerIN regulator\n");
	}
	INIT_DELAYED_WORK(&charger->input_volt_work, jcm_input_volt_adjust_work);
	if (charger->otg_regulator_name && !charger->otg_regulator) {
		charger->otg_regulator = regulator_get(cm->dev,
					charger->otg_regulator_name);
		if (IS_ERR_OR_NULL(charger->otg_regulator)) {
			dev_err(cm->dev, "Cannot find  OTG regulator(%s)\n",
				charger->otg_regulator_name);
			charger->otg_regulator = NULL;
			return -ENODEV;
		} else if (regulator_is_enabled(charger->otg_regulator)) {
			regulator_disable(charger->otg_regulator);
		}
		charger->otg_enabled = regulator_is_enabled(charger->otg_regulator);
	} else {
		dev_info(cm->dev, "Not Set OTG regulator\n");
	}
	charger->cm = cm;
	max_uA = 0;
	chip = charger->chips;
	for (i = 0; i < charger->num_chips; i++, chip++) {
		chip->cable_regulator = regulator_get(cm->dev,
					chip->chip_regulator_name);
		if (IS_ERR_OR_NULL(chip->cable_regulator)) {
			dev_err(cm->dev, "Cannot find charger chip regulator(%s)\n",
				chip->chip_regulator_name);
				chip->cable_regulator = NULL;
			return -ENODEV;
		}
		cm->charger_enabled |= regulator_is_enabled(chip->cable_regulator);
		max_uA += chip->max_uA;
		chip->cm = cm;
	}
	charger->fastchg_current_votable = create_jcmvotable(
				"fastchg-current", JCMVOTE_MIN, jcm_set_fast_chg_current, cm);
	if (!charger->fastchg_current_votable) {
		dev_err(cm->dev, "Creat vote(%s) failed.\n",
			"fastchg-current");
		return -ENOMEM;
	}

	charger->fastchg_volt_votable = create_jcmvotable(
				"fastchg-volt", JCMVOTE_MIN, jcm_set_fast_chg_volt, cm);
	if (!charger->fastchg_volt_votable) {
		dev_err(cm->dev, "Creat vote(%s) failed.\n",
			"fastchg-volt");
		destroy_jcmvotable(charger->fastchg_current_votable);
		return -ENOMEM;
	}
	charger->input_current_limit_votable = create_jcmvotable(
				"input-CL", JCMVOTE_MIN, jcm_set_input_cur_limt, cm);
	if (!charger->input_current_limit_votable) {
		dev_err(cm->dev, "Creat vote(%s) failed.\n",
			"input-CL");
		destroy_jcmvotable(charger->fastchg_current_votable);
		destroy_jcmvotable(charger->fastchg_volt_votable);
		return -ENOMEM;
	}

	charger->charger_disable_votable = create_jcmvotable(
				"disable_charge", JCMVOTE_SET_ANY, jcm_charger_disable, cm);
	if (!charger->charger_disable_votable) {
		destroy_jcmvotable(charger->fastchg_current_votable);
		destroy_jcmvotable(charger->fastchg_volt_votable);
		destroy_jcmvotable(charger->input_current_limit_votable);
		dev_err(cm->dev, "Creat vote(%s) failed.\n",
			"disable_charge");
		return -ENOMEM;
	}

	if (charger->fg_inited == false) {
		dev_err(cm->dev, "fg uninited,set drv init current limited. \n");
		jcmvote(charger->fastchg_current_votable, DRVINIT_CLIENT, true, JCM_USB_DEFAULT_MAX_CUR_UA);
	}
	jcmvote(charger->fastchg_volt_votable, USB_CHARGER_MAX_CLIENT, true, cm->charger->battery.fastchg_volt_uv);
	jcmvote(charger->fastchg_current_votable, USB_TYPE_CLIENT, true, JCM_USB_DEFAULT_MAX_CUR_UA);
	jcmvote(charger->fastchg_current_votable, USB_CHARGER_MAX_CLIENT, true, cm->charger->battery.fastchg_current_ua);
	jcmvote(charger->charger_disable_votable, "INIT", false, 1);
	jcmvote(charger->input_current_limit_votable, USB_TYPE_CLIENT, true, JCM_USB_DEFAULT_MAX_CUR_UA);
	cm->inited = true;
	jcmvote_rerun_election(charger->charger_disable_votable);
	ret = charger_extcon_init(cm, charger);
	if (ret < 0) {
		dev_err(cm->dev, "Cannot initialize extcon(%s)\n",
			charger->extcon_name);
		return ret;
	}
	return 0;
}

/* help function of sysfs node to control charger(regulator) */
static ssize_t charger_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr, struct jlq_charger_regulator, attr_input_name);

	return sprintf(buf, "%s\n", charger->input_regulator_name);
}

static ssize_t charger_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr, struct jlq_charger_regulator, attr_state);
	int state = 0;

	state = charger->cm->charger_enabled;
//	state = regulator_is_enabled(charger->regulator_input);

	return sprintf(buf, "%s\n", state ? "enabled" : "disabled");
}

static ssize_t charger_batid_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr, struct jlq_charger_regulator, attr_res_id);

	return sprintf(buf, "%d\n", charger->batt_id_ohm);
}

static ssize_t charger_force_disable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_force_disable);

	return sprintf(buf, "%d\n", charger->externally_control);
}

static ssize_t charger_force_disable_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct jlq_charger_regulator *charger
		= container_of(attr, struct jlq_charger_regulator,
					attr_force_disable);
	int ret;
	int force_disable;

	ret = kstrtoint(buf, 0, &force_disable);
	if (ret < 0) {
		ret = -EINVAL;
		return ret;
	}

	charger->externally_control = !!force_disable;
	if (!force_disable) {
		jcmvote(charger->charger_disable_votable, USB_USER_FORCE_CLIENT, false, 0);
	}else{
		jcmvote(charger->charger_disable_votable, USB_USER_FORCE_CLIENT, true, 1);
	}

	return count;
}

static ssize_t jcm_fake_soc_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_fake_soc);
	if (charger->fake_soc >= JCM_FAKE_SOC_ERRVAL)
		return sprintf(buf, "%s\n", "NULL");
	return sprintf(buf, "%d\n", charger->fake_soc);
}

static ssize_t jcm_fake_soc_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct jlq_charger_regulator *charger
		= container_of(attr, struct jlq_charger_regulator,
					attr_fake_soc);
	int ret;
	int fake_soc;

	ret = kstrtoint(buf, 0, &fake_soc);
	if (ret < 0) {
		ret = -EINVAL;
		charger->fake_soc = JCM_FAKE_SOC_ERRVAL;
		jcm_update_msoc_force_flush(charger->cm);
		return ret;
	}
	if (fake_soc >= JCM_FAKE_SOC_ERRVAL)
		charger->fake_soc = JCM_FAKE_SOC_ERRVAL;
	else
		charger->fake_soc = fake_soc;
	jcm_update_msoc_force_flush(charger->cm);
	return count;
}


static ssize_t jcm_dbg_work_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_dbg_work);

	return sprintf(buf, "%d\n", charger->dbg_work_s);
}

static ssize_t jcm_dbg_work_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_dbg_work);
	int ret;
	unsigned int dbg_work_s;

	ret = kstrtoint(buf, 0, &dbg_work_s);
	if (ret < 0) {
		charger->dbg_work_s = 0;
		return  -EINVAL;
	}
	if (dbg_work_s >= 600)
		charger->dbg_work_s = 0;
	else
		charger->dbg_work_s = dbg_work_s;

	cancel_delayed_work(&charger->dbg_pr_work);
	if (charger->dbg_work_s) {
		schedule_delayed_work(&charger->dbg_pr_work, msecs_to_jiffies(1000));
	}
	return count;
}

static ssize_t jcm_fake_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_fake_temp);
	if (charger->fake_temp >= JCM_FAKE_TEMP_ERRVAL)
		return sprintf(buf, "%s\n", "NULL");
	return sprintf(buf, "%d\n", charger->fake_temp);
}

static ssize_t jcm_fake_temp_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct jlq_charger_regulator *charger
		= container_of(attr, struct jlq_charger_regulator,
					attr_fake_temp);
	int ret;
	int fake_temp;

	ret = kstrtoint(buf, 0, &fake_temp);
	if (ret < 0) {
		ret = -EINVAL;
		schedule_work(&charger->cm->psys_status_change_work);
		charger->fake_temp = JCM_FAKE_TEMP_ERRVAL;
		return ret;
	}
	if (fake_temp >= JCM_FAKE_TEMP_ERRVAL)
		charger->fake_temp = JCM_FAKE_TEMP_ERRVAL;
	else
		charger->fake_temp = fake_temp;
	schedule_work(&charger->cm->psys_status_change_work);
	return count;
}

static ssize_t jcm_sysfs_fcv_show(
	struct device *dev,struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_fcv);
	return sprintf(buf, "%d\n", charger->set_volt);
}

static ssize_t jcm_sysfs_fcv_effective_show(
	struct device *dev,struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_fcv_eff);

	return sprintf(buf, "%s\n",
		jcmvote_get_effective_client(charger->fastchg_volt_votable));
}

static ssize_t jcm_sysfs_fcc_show(
	struct device *dev,struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_fcc);

	return sprintf(buf, "%d\n", charger->set_current);
}

static ssize_t jcm_sysfs_fcc_effective_show(
	struct device *dev,struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_fcc_eff);

	return sprintf(buf, "%s\n",
		jcmvote_get_effective_client(charger->fastchg_current_votable));
}

static ssize_t jcm_sysfs_chg_disable_effective_show(
	struct device *dev,struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_chg_en_eff);

	return sprintf(buf, "%s\n",
		jcmvote_get_effective_client(charger->charger_disable_votable));
}

static ssize_t jcm_sysfs_battery_type_show(
	struct device *dev,struct device_attribute *attr, char *buf)
{
	struct jlq_charger_regulator *charger
		= container_of(attr,
			struct jlq_charger_regulator, attr_battery_type);

	return sprintf(buf, "%s\n",charger->battery.battery_name);
}

/**
 * charger_manager_prepare_sysfs - Prepare sysfs entry for each charger
 * @cm: the Charger Manager representing the battery.
 *
 * This function add sysfs entry for charger(regulator) to control charger from
 * user-space. If some development board use one more chargers for charging
 * but only need one charger on specific case which is dependent on user
 * scenario or hardware restrictions, the user enter 1 or 0(zero) to '/sys/
 * class/power_supply/battery/charger.[index]/externally_control'. For example,
 * if user enter 1 to 'sys/class/power_supply/battery/charger.[index]/
 * externally_control, this charger isn't controlled from charger-manager and
 * always stay off state of regulator.
 */
static int charger_manager_prepare_sysfs(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;
	int i = 0;

	charger->attr_grp.name = "charger";
	charger->attr_grp.attrs = charger->attrs;
	charger->sysfs_groups[0] = &charger->attr_grp;

	charger->attrs[i++] = &charger->attr_input_name.attr;
	sysfs_attr_init(&charger->attr_input_name.attr);
	charger->attr_input_name.attr.name = "input_name";
	charger->attr_input_name.attr.mode = 0444;
	charger->attr_input_name.show = charger_name_show;

	charger->attrs[i++] = &charger->attr_state.attr;
	sysfs_attr_init(&charger->attr_state.attr);
	charger->attr_state.attr.name = "state";
	charger->attr_state.attr.mode = 0444;
	charger->attr_state.show = charger_state_show;

	charger->attrs[i++] = &charger->attr_force_disable.attr;
	sysfs_attr_init(&charger->attr_force_disable.attr);
	charger->attr_force_disable.attr.name = "force_disable";
	charger->attr_force_disable.attr.mode = 0644;
	charger->attr_force_disable.show
			= charger_force_disable_show;
	charger->attr_force_disable.store
			= charger_force_disable_store;

	charger->attrs[i++] = &charger->attr_res_id.attr;
	sysfs_attr_init(&charger->attr_res_id.attr);
	charger->attr_res_id.attr.name = "resistance_id";
	charger->attr_res_id.attr.mode = 0444;
	charger->attr_res_id.show = charger_batid_show;

	charger->attrs[i++] = &charger->attr_fake_temp.attr;
	sysfs_attr_init(&charger->attr_fake_temp.attr);
	charger->attr_fake_temp.attr.name = "fake_temp";
	charger->attr_fake_temp.attr.mode = 0644;
	charger->attr_fake_temp.show
			= jcm_fake_temp_show;
	charger->attr_fake_temp.store
			= jcm_fake_temp_store;

	charger->attrs[i++] = &charger->attr_fake_soc.attr;
	sysfs_attr_init(&charger->attr_fake_soc.attr);
	charger->attr_fake_soc.attr.name = "fake_soc";
	charger->attr_fake_soc.attr.mode = 0644;
	charger->attr_fake_soc.show
			= jcm_fake_soc_show;
	charger->attr_fake_soc.store
			= jcm_fake_soc_store;

	charger->attrs[i++] = &charger->attr_dbg_work.attr;
	sysfs_attr_init(&charger->attr_dbg_work.attr);
	charger->attr_dbg_work.attr.name = "dbg_work_intv";
	charger->attr_dbg_work.attr.mode = 0644;
	charger->attr_dbg_work.show
			= jcm_dbg_work_show;
	charger->attr_dbg_work.store
			= jcm_dbg_work_store;

	charger->attrs[i++] = &charger->attr_fcv.attr;
	sysfs_attr_init(&charger->attr_fcv.attr);
	charger->attr_fcv.attr.name = "fcv";
	charger->attr_fcv.attr.mode = 0444;
	charger->attr_fcv.show
			= jcm_sysfs_fcv_show ;

	charger->attrs[i++] = &charger->attr_fcv_eff.attr;
	sysfs_attr_init(&charger->attr_fcv_eff.attr);
	charger->attr_fcv_eff.attr.name = "fcv_eff";
	charger->attr_fcv_eff.attr.mode = 0444;
	charger->attr_fcv_eff.show
			= jcm_sysfs_fcv_effective_show;

	charger->attrs[i++] = &charger->attr_fcc.attr;
	sysfs_attr_init(&charger->attr_fcc.attr);
	charger->attr_fcc.attr.name = "fcc";
	charger->attr_fcc.attr.mode = 0444;
	charger->attr_fcc.show
			= jcm_sysfs_fcc_show ;

	charger->attrs[i++] = &charger->attr_fcc_eff.attr;
	sysfs_attr_init(&charger->attr_fcc_eff.attr);
	charger->attr_fcc_eff.attr.name = "fcc_eff";
	charger->attr_fcc_eff.attr.mode = 0444;
	charger->attr_fcc_eff.show
			= jcm_sysfs_fcc_effective_show;

	charger->attrs[i++] = &charger->attr_chg_en_eff.attr;
	sysfs_attr_init(&charger->attr_chg_en_eff.attr);
	charger->attr_chg_en_eff.attr.name = "chg_en_eff";
	charger->attr_chg_en_eff.attr.mode = 0444;
	charger->attr_chg_en_eff.show
			= jcm_sysfs_chg_disable_effective_show;

	charger->attrs[i++] = &charger->attr_battery_type.attr;
	sysfs_attr_init(&charger->attr_battery_type.attr);
	charger->attr_battery_type.attr.name = "battery_type";
	charger->attr_battery_type.attr.mode = 0444;
	charger->attr_battery_type.show
			= jcm_sysfs_battery_type_show;

	charger->attrs[i++] = NULL;
	return 0;
}

static int jcm_init_thermal_data(struct jlq_charger_manager *cm)
{
	struct jlq_charger_regulator *charger = cm->charger;

	if (!cm->charger->not_measure_battery_temp) {
		/* NOTICE : Default allowable minimum charge temperature is 0 */
		if (!charger->battery.temp_max)
			charger->battery.temp_max = CM_DEFAULT_CHARGE_TEMP_MAX;
		if (!charger->battery.temp_diff)
			charger->battery.temp_diff = CM_DEFAULT_RECHARGE_TEMP_DIFF;
	}
	return 0;
}

int jlqcm_read_range_data_from_node(struct device_node *node,
		const char *prop_str, struct jlq_charger_mangaer_step_rang *ranges,
		int max_threshold, u32 max_volt, u32 max_current)
{
	int rc = 0, i, length, per_tuple_length, tuples;

	if (!node || !prop_str || !ranges) {
		pr_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		pr_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;
	per_tuple_length = sizeof(struct jlq_charger_mangaer_step_rang) / sizeof(u32);
	if (length % per_tuple_length) {
		pr_err("%s length (%d) should be multiple of %d\n",
				prop_str, length, per_tuple_length);
		return -EINVAL;
	}
	tuples = length / per_tuple_length;

	rc = of_property_read_u32_array(node, prop_str,
			(u32 *)ranges, length);
	if (rc) {
		pr_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	for (i = 0; i < tuples; i++) {
		if (ranges[i].low_threshold >
				ranges[i].high_threshold) {
			pr_err("%s thresholds should be in ascendant ranges\n",
						prop_str);
			rc = -EINVAL;
			goto clean;
		}

		if (i != 0) {
			if (ranges[i - 1].high_threshold >
					ranges[i].low_threshold) {
				pr_err("%s thresholds should be in ascendant ranges\n",
							prop_str);
				rc = -EINVAL;
				goto clean;
			}
		}

		if (ranges[i].low_threshold > max_threshold)
			ranges[i].low_threshold = max_threshold;
		if (ranges[i].high_threshold > max_threshold)
			ranges[i].high_threshold = max_threshold;
		if (ranges[i].float_volt_uv > max_volt)
			ranges[i].float_volt_uv = max_volt;
		if (ranges[i].current_ua > max_current)
			ranges[i].current_ua = max_current;
	}

	return rc;
clean:
	memset(ranges, 0, tuples * sizeof(struct jlq_charger_mangaer_step_rang));
	return rc;
}


static int of_cm_parse_battery_profile(
	struct jlq_charger_regulator *charger,
	struct device_node *bat_np)
{
	struct jlq_charger_mangaer_battery_profile *profile = &charger->battery;
	int len;
	int i;

	of_property_read_string(bat_np, "battery-name", &profile->battery_name);
	of_property_read_u32(bat_np, "batt-id-kohm", &profile->bat_id_kohm);
	of_property_read_u32(bat_np, "term-current", &profile->termination_current);
	of_property_read_u32(bat_np, "recharge-drop-mv",
					&profile->fullbatt_vchkdrop_uV);
	profile->fullbatt_vchkdrop_uV *= 1000;
	profile->fullbatt_rechgsoc = JCM_RECHG_SOC_DEFAULT;
	of_property_read_u32(bat_np, "recharge-drop-soc",
					&profile->fullbatt_rechgsoc);
	profile->rechg_volt_base =
		of_property_read_bool(bat_np, "recharge_base_volt");
	of_property_read_u32(bat_np, "fullbatt-voltage", &profile->fullbatt_uV);
	of_property_read_u32(bat_np, "cutoffbatt-voltage", &profile->cutoff_batt_uV);
	of_property_read_u32(bat_np, "fullbatt-soc", &profile->fullbatt_soc);
	of_property_read_u32(bat_np, "cutoffbatt-soc", &profile->cutoff_batt_soc);

	of_property_read_u32(bat_np, "fullbatt-capacity", &profile->fullbatt_full_capacity);
	profile->fullbatt_full_capacity *= 1000;
	of_property_read_u32(bat_np, "fullbatt-energy", &profile->fullbatt_full_enery);
	profile->fullbatt_full_enery *= 1000;
	of_property_read_u32(bat_np, "fastchg-current-ua", &profile->fastchg_current_ua);
	of_property_read_u32(bat_np, "fastchg-volt-uv", &profile->fastchg_volt_uv);

	of_property_read_u32(bat_np, "battery-cold", &profile->temp_min);
	if (of_get_property(bat_np, "battery-cold-in-minus", NULL))
		profile->temp_min *= -1;
	of_property_read_u32(bat_np, "battery-hot", &profile->temp_max);
	of_property_read_u32(bat_np, "battery-temp-diff", &profile->temp_diff);

	of_property_read_u32(bat_np, "charging-max-time-ms",
				&profile->charging_max_duration_ms);
	of_property_read_u32(bat_np, "discharging-max-time-ms",
				&profile->discharging_max_duration_ms);
	profile->step_chg_enabled = of_property_read_bool(bat_np, "step-chg-enabled");
	if (profile->step_chg_enabled && of_find_property(bat_np, "step-chg-ranges", &len)) {
		profile->step_chg_rangs =
			kcalloc(len/sizeof(struct jlq_charger_mangaer_step_rang) + 1,
				sizeof(struct jlq_charger_mangaer_step_rang), GFP_KERNEL);
		jlqcm_read_range_data_from_node(bat_np, "step-chg-ranges",
				profile->step_chg_rangs, profile->fullbatt_uV,
				profile->fastchg_volt_uv, profile->fastchg_current_ua);
		for ( i = 0; i < len / sizeof(struct jlq_charger_mangaer_step_rang) + 1; i++){
			pr_debug("[%s]step-chg-ranges: Rang[%d ~ %d] %duA, %duV",
				__func__, profile->step_chg_rangs[i].low_threshold,
				profile->step_chg_rangs[i].high_threshold,
				profile->step_chg_rangs[i].current_ua, profile->step_chg_rangs[i].float_volt_uv);
		}
	} else {
		profile->step_chg_enabled = 0;
	}

	profile->sw_jeita_enabled = of_property_read_bool(bat_np, "sw-jeita-enabled");
	if (profile->sw_jeita_enabled && of_find_property(bat_np, "sw-jeita-ranges", &len)) {
		profile->sw_jeita_rangs =
			kcalloc(len/sizeof(struct jlq_charger_mangaer_step_rang) + 1,
				sizeof(struct jlq_charger_mangaer_step_rang), GFP_KERNEL);
		jlqcm_read_range_data_from_node(bat_np, "sw-jeita-ranges",
				profile->sw_jeita_rangs, 800,
				profile->fastchg_volt_uv, profile->fastchg_current_ua);
		profile->batovp_recharge_uv =
			profile->sw_jeita_rangs[(len / sizeof(struct jlq_charger_mangaer_step_rang) - 1)].float_volt_uv - 100000;
		for ( i = 0; i < len / sizeof(struct jlq_charger_mangaer_step_rang) + 1; i++){
			pr_debug("[%s]sw-jeita-ranges: Rang[%d ~ %d] %duA, %duV",
				__func__, profile->sw_jeita_rangs[i].low_threshold,
				profile->sw_jeita_rangs[i].high_threshold,
				profile->sw_jeita_rangs[i].current_ua, profile->sw_jeita_rangs[i].float_volt_uv);
		}
	} else {
		profile->sw_jeita_enabled = 0;
	}
	return 0;
}

static struct jlq_charger_regulator *of_cm_parse_desc(struct device *dev)
{
	struct jlq_charger_regulator *charger;
	struct device_node *np = dev->of_node;
	u32 poll_mode = CM_POLL_DISABLE;
	u32 battery_stat = CM_NO_BATTERY;
	/* battery charger regulators */
	struct jlq_charger_chip *chips;
	struct device_node *child;
	struct device_node *_child;
	int prop_len;

	charger = devm_kzalloc(dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return ERR_PTR(-ENOMEM);

	mutex_init(&charger->lock);
	of_property_read_string(np, "cm-name", &charger->psy_name);
	of_property_read_u32(np, "cm-poll-mode", &poll_mode);
	charger->polling_mode = poll_mode;

	of_property_read_u32(np, "cm-poll-interval",
				&charger->polling_interval_ms);
	if(charger->polling_interval_ms < 10000)
		charger->polling_interval_ms = 10000;

	of_property_read_string(np, "cm-fuel-gauge", &charger->psy_fuel_gauge);

	of_property_read_string(np, "cm-thermal-zone", &charger->thermal_zone);
	charger->soft_recharge_flag =
		of_property_read_bool(np, "cm-soft-recharge");
	if (of_property_read_u32(np, "cm-recharge-hold-sec",
			&charger->recharge_hold_sec) < 0)
		charger->recharge_hold_sec = 1200;
	if (of_property_read_u32(np, "cm-fullbatt-vchk-sec",
				&charger->fullbatt_vchkdrop_ms) < 0)
		charger->fullbatt_vchkdrop_ms = 0;
	charger->fullbatt_vchkdrop_ms *= 1000;
	if (of_property_read_u32(np, "cm-batid-pu", &charger->batid_pu_ohm) < 0)
		charger->batid_pu_ohm = 1000000;

	of_property_read_u32(np, "cm-battery-stat", &battery_stat);
	charger->battery_present = battery_stat;
	charger->sysfs_groups = devm_kcalloc(dev, 2,
				sizeof(*charger->sysfs_groups), GFP_KERNEL);
	if (!charger->sysfs_groups)
		return ERR_PTR(-ENOMEM);
	if (of_property_read_u32(np, "cm-steper-intv-ms", &charger->steper_intv_ms) < 0)
		charger->steper_intv_ms = 0;

	if (of_property_read_u32(np, "cm-steper-current-ma", &charger->steper_val) < 0)
		charger->steper_val = 300 * 1000;
	else
		charger->steper_val *= 1000;
	charger->debug_fake_soc = of_property_read_bool(np, "cm-debug-fake-soc");
	charger->no_battery = of_property_read_bool(np, "jlq,batteryless-platform");
	charger->soc_policy = of_property_read_bool(np, "cm-soc-policy");

	of_property_read_string(np, "cm-input-regulator",
			&charger->input_regulator_name);
	of_property_read_string(np, "cm-charger-extcon",
			&charger->extcon_name);
	of_property_read_string(np, "otg_regulator",
			&charger->otg_regulator_name);
	if (of_property_read_u32(np, "otg-boost-current-ua",
			&charger->otg_boost_current) < 0)
		charger->otg_boost_current = JCM_OTG_DEFAULT_BOOST_CURRENT;
	if (of_property_read_u32(np, "otg-boost-volt-uv",
			&charger->otg_boost_volt) < 0)
		charger->otg_boost_volt = JCM_OTG_DEFAULT_BOOST_VOLT;
	if (of_property_read_u32(np, "qc20_vbus_volt_setting",
			&charger->qc20_vbus_volt_setting) < 0)
		charger->qc20_vbus_volt_setting = JCM_HVDCP_QC20_DEFAULT_VBUS_VOLT;
	charger->charge_ir_comp_off =
		!of_property_read_bool(np, "cm-no-off-ir-comp");

	prop_len = of_property_count_u32_elems(np, "jlq,thermal-mitigation");
	if (prop_len < 0 )
		prop_len = 0;
	charger->thermal_mitigation =  devm_kcalloc(dev, prop_len + 1,
			sizeof(int), GFP_KERNEL);
	if (!charger->thermal_mitigation)
		return ERR_PTR(-ENOMEM);

	charger->thermal_level_cnt = prop_len + 1;
	if (prop_len) {
		of_property_read_u32_array(np, "jlq,thermal-mitigation",
				charger->thermal_mitigation, prop_len);
	}
	dev_dbg(dev, "cm-name:%s", charger->psy_name);
	dev_info(dev, "polling_interval_ms:%d soft_recharger:%d Min:%d sec, fullbatt chk: %dms",
		charger->polling_interval_ms, charger->soft_recharge_flag,
		charger->recharge_hold_sec, charger->fullbatt_vchkdrop_ms);
	dev_dbg(dev, "cm-fuel-gauge:%s", charger->psy_fuel_gauge);
	dev_dbg(dev, "thermal-mitigation Size:%d, HVDCP QC20 Vbus volt:%d uV",
		charger->thermal_level_cnt, charger->qc20_vbus_volt_setting);
	dev_dbg(dev, "otg_boost_current:%d uA, otg_boost_volt:%d uV ",
		charger->otg_boost_current, charger->otg_boost_volt);
	dev_dbg(dev, "cm-steper-intv-ms:%d, cm-steper-current-ma:%d",
		charger->steper_intv_ms, charger->steper_val / 1000);
	dev_dbg(dev, "cm-batid-adc-pu:%d ", charger->batid_pu_ohm);
	dev_dbg(dev, "cm-fuel-gauge:%s ", charger->psy_fuel_gauge);
	dev_dbg(dev, "cm-charger-extcon:%s ", charger->extcon_name);
	dev_dbg(dev, "otg_regulator:%s ", charger->otg_regulator_name);
	dev_dbg(dev, "charge_ir_comp_off:%d ", charger->charge_ir_comp_off);
	/* charger chips */
	child = of_get_child_by_name(np, "chargers");
	if (!child) {
		return ERR_PTR(-ENOPARAM);
	}
	charger->num_chips = of_get_child_count(child);
	if (charger->num_chips) {
		chips = devm_kcalloc(dev,
				charger->num_chips,
				sizeof(*chips),
				GFP_KERNEL);

		charger->chips = chips;
		for_each_child_of_node(child, _child) {
			of_property_read_string(_child, "chg-name",
				&chips->chip_name);
			of_property_read_string(_child, "power-supply",
				&chips->charger_psy);
			of_property_read_string(_child,
				"cm-regulator-name", &chips->chip_regulator_name);
			of_property_read_u32(_child,
				"cm-chip-min",
				&chips->min_uA);
			of_property_read_u32(_child,
				"cm-chip-max",
				&chips->max_uA);
			dev_dbg(dev, "chip_name:%s ", chips->chip_name);
			dev_dbg(dev, "chip_regulator_name:%s ", chips->chip_regulator_name);
			dev_dbg(dev, "power-supply:%s ", chips->charger_psy);
			dev_dbg(dev, "cm-chip-current:%d ~%d ua", chips->min_uA, chips->max_uA);
			chips++;
		}
	}
	return charger;
}

static inline struct jlq_charger_regulator *jcm_get_drv_data(struct platform_device *pdev)
{
	if (pdev->dev.of_node)
		return of_cm_parse_desc(&pdev->dev);
	return dev_get_platdata(&pdev->dev);
}

static int jcm_psy_match(struct jlq_charger_manager *cm, struct power_supply *psy)
{
	int i;
	struct jlq_charger_chip *chip;
	struct jlq_charger_regulator *charger;

	charger = cm->charger;
	if (!strcmp(psy->desc->name, charger->psy_fuel_gauge))
		return 1;

	chip = cm->charger->chips;
	for (i = 0; i < charger->num_chips; i++, chip++) {
		if (!strcmp(psy->desc->name, chip->charger_psy))
			return 1;
	}
	return 0;
}

/* thermal cooling device callbacks */
static int jcm_get_max_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long *state)
{
	struct jlq_charger_manager *cm;

	cm = (struct jlq_charger_manager *)tcd->devdata;
	return jcm_get_control_limt_max_level(cm, (int *)state);
}

static int jcm_get_cur_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long *state)
{
	struct jlq_charger_manager *cm;

	cm = (struct jlq_charger_manager *)tcd->devdata;
	*state = (unsigned long)jcm_get_control_limt_level(cm);
	return 0;
}

static int jcm_set_cur_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long state)
{
	struct jlq_charger_manager *cm;
	cm = (struct jlq_charger_manager *)tcd->devdata;

	return jcm_set_control_limt_level(cm, (int)state);
}

static const struct thermal_cooling_device_ops jcm_tcd_ops = {
	.get_max_state = jcm_get_max_charge_cntl_limit,
	.get_cur_state = jcm_get_cur_charge_cntl_limit,
	.set_cur_state = jcm_set_cur_charge_cntl_limit,
};

static int jcm_register_cooler(struct jlq_charger_manager *cm)
{
	cm->tcd = thermal_of_cooling_device_register(cm->dev->of_node,
						(char *)"JlqChargerManager",
						cm, &jcm_tcd_ops);
	return PTR_ERR_OR_ZERO(cm->tcd);
}


static void jcm_psys_status_changed(struct work_struct *work)
{
	struct jlq_charger_manager *jcm =
		container_of(work, struct jlq_charger_manager, psys_status_change_work);
//	jcm_notify_event(jcm);
	power_supply_changed(jcm->charger_psy);
}

static int jcm_psy_notify_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct power_supply *psy = (struct power_supply *)data;
	struct jlq_charger_manager *jcm =
		container_of(nb, struct jlq_charger_manager, psy_nb);
	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if (jcm_psy_match(jcm, psy))
		schedule_work(&jcm->psys_status_change_work);
	return NOTIFY_OK;
}

static int jcm_register_psy_notify(struct jlq_charger_manager *cm)
{
	struct notifier_block *nb;

	INIT_WORK(&cm->psys_status_change_work, jcm_psys_status_changed);
	nb = &cm->psy_nb;
	nb->notifier_call = jcm_psy_notify_cb;
	return power_supply_reg_notifier(nb);
}

static enum alarmtimer_restart jcm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct jlq_charger_manager *cm = container_of(alarm,
			struct jlq_charger_manager, jcm_timer);

	cm->jcm_timer_set = false;
	return ALARMTIMER_NORESTART;
}

static void jlq_charger_manage_delay_init(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct jlq_charger_manager *cm = container_of(dwork,
			struct jlq_charger_manager, probe_delay_work);
	struct jlq_charger_regulator *charger = cm->charger;
	struct jlq_charger_chip *chip;
	struct power_supply *charger_psy;
	struct power_supply *fg_psy;
	int ret, i = 0;

	/* Check if charger's supplies are present at probe */
	if (!charger->chargers_inited) {
		chip = charger->chips;
		for (i = 0; i < charger->num_chips; i++, chip++) {
			charger_psy = power_supply_get_by_name(chip->charger_psy);
			if (IS_ERR_OR_NULL(charger_psy)) {
//				dev_err(cm->dev, "Wait for Charger PSY \"%s\"\n",
//					chip->charger_psy);
					cancel_delayed_work(&cm->probe_delay_work);
					schedule_delayed_work(&cm->probe_delay_work, msecs_to_jiffies(3000));
				return;
			}
			power_supply_put(charger_psy);
		}

		ret = jcm_init_thermal_data(cm);
		if (ret) {
			dev_err(cm->dev, "Failed to initialize thermal data\n");
			cm->charger->not_measure_battery_temp = true;
		}

		charger->chargers_inited = true;
		/* Register extcon device for charger chip */
		ret = charger_manager_init_chargers(cm);
		if (ret < 0) {
			dev_err(cm->dev, "Cannot initialize extcon device\n");
			schedule_delayed_work(&cm->probe_delay_work, msecs_to_jiffies(1000));
			/* Remove notifier block if only edev exists */
			if (charger->regulator_input) {
				regulator_put(charger->regulator_input);
				charger->regulator_input = NULL;
			}
			power_supply_unregister(cm->charger_psy);
			return;
		}

		jcm_register_psy_notify(cm);

		schedule_delayed_work(&cm->soc_update_work, 0);
		schedule_delayed_work(&cm->jcm_monitor, msecs_to_jiffies(500));
		INIT_DELAYED_WORK(&charger->dbg_pr_work, jcm_charging_dbg_record_work);
		schedule_delayed_work(&charger->dbg_pr_work, msecs_to_jiffies(1000));
	}
	if (!charger->fg_inited && charger->psy_fuel_gauge) {
		fg_psy = power_supply_get_by_name(charger->psy_fuel_gauge);
		if (IS_ERR_OR_NULL(fg_psy)) {
//			dev_err(cm->dev, "Wait for Fg PSY \"%s\"\n",
//				charger->psy_fuel_gauge);
			cancel_delayed_work(&cm->probe_delay_work);
			schedule_delayed_work(&cm->probe_delay_work, msecs_to_jiffies(3000));
			return;
		}
		charger->fg_inited = true;
		if (charger->fastchg_current_votable != NULL) {
			dev_dbg(cm->dev, "fg inited done,remove drv init current limited. \n");
			jcmvote(charger->fastchg_current_votable, DRVINIT_CLIENT, false, 0);
		}
		power_supply_put(fg_psy);
		return;
	}
	return;
}

static int jcm_batid_permillage2ohm(struct jlq_charger_regulator *charger, int permillage)
{
	int ohm;
	int other_permillage;
	other_permillage = 1000 - permillage;
	ohm = permillage * charger->batid_pu_ohm;
	ohm /= other_permillage;
	return  ohm;
}

static int jlq_charger_manager_probe(struct platform_device *pdev)
{
	struct jlq_charger_regulator *charger = jcm_get_drv_data(pdev);
	struct jlq_charger_manager *cm;
	int ret;
	struct iio_channel *batt_id_iio = NULL;
	int battid_permillage;
	int battid_ohm = 0;
	struct power_supply_config psy_cfg = {};
	struct device_node	*batt_node;

	if (IS_ERR(charger)) {
		dev_err(&pdev->dev, "No platform data (charger) found\n");
		return PTR_ERR(charger);
	}
	cm = devm_kzalloc(&pdev->dev, sizeof(*cm), GFP_KERNEL);
	if (!cm)
		return -ENOMEM;

	cm->inited = false;

	/* matching Best battery profile*/
	batt_id_iio = iio_channel_get(&pdev->dev, "Bat-ID");
	if (!IS_ERR_OR_NULL(batt_id_iio)) {
		ret = iio_read_channel_processed(batt_id_iio, &battid_permillage);
		if (ret < 0 || battid_permillage == 0) {
			battid_ohm = 330001;
			return -EPROBE_DEFER;
		} else {
			battid_ohm = jcm_batid_permillage2ohm(charger, battid_permillage);
		}
		dev_dbg(&pdev->dev, "battid_permillage:%d -> battid_ohm:%d ohm\n",
			battid_permillage, battid_ohm);
	} else {
		dev_emerg(&pdev->dev, "No Found batt_id_iio chan.\n");
		battid_ohm = 330000;
	}
	if (battid_ohm >= JCM_DEBUG_BAT_ID_LOW &&
			battid_ohm <= JCM_DEBUG_BAT_ID_HIGH) {
		charger->debug_battery = true;
		charger->debug_fake_soc = true;
		dev_warn(&pdev->dev, "Debug battery detected.\n");
	}
	charger->fastchg_current_votable = NULL;
	charger->chargers_inited = false;
	charger->fg_inited = false;
	charger->fake_temp = JCM_FAKE_TEMP_ERRVAL;
	charger->fake_soc = JCM_FAKE_SOC_ERRVAL;
	charger->dbg_work_s = 30;

	batt_node = of_get_child_by_name(pdev->dev.of_node, "batterys");
	if (!batt_node) {
		return -ENOPARAM;
	}
	cm->best_battery_nd =
		of_jlq_battery_profile_get_best_profile(
				batt_node, battid_ohm / 1000, NULL);
	ret = of_cm_parse_battery_profile(charger, cm->best_battery_nd);
	if (ret < 0)
		return ret;

	charger->batt_id_ohm = battid_ohm;
//	}
	/* Basic Values. Unspecified are Null or 0 */
	cm->dev = &pdev->dev;
	cm->charger = charger;

	/* Initialize alarm timer */
	if (alarmtimer_get_rtcdev()) {
		alarm_init(&cm->jcm_timer, ALARM_BOOTTIME, jcm_timer_func);
	} else {
		cm->jcm_timer.function = NULL;
		dev_emerg(&pdev->dev, "jcm_timer not inited.\n");
	}

/*
 *Some of the following do not need to be errors.
 *Users may intentionally ignore those features.
 */
	if (charger->soft_recharge_flag) {
		if (charger->battery.fullbatt_uV == 0) {
			dev_info(&pdev->dev,
			"Ignoring full-battery voltage threshold as it is not supplied\n");
		}
		if ((charger->battery.rechg_volt_base && !charger->battery.fullbatt_vchkdrop_uV) ||
			(!charger->battery.rechg_volt_base && !charger->battery.fullbatt_rechgsoc) ||
				!charger->fullbatt_vchkdrop_ms) {
			dev_info(&pdev->dev,
				"Disabling full-battery voltage drop checking mechanism as it is not supplied\n");
			charger->fullbatt_vchkdrop_ms = 0;
			charger->battery.fullbatt_vchkdrop_uV = 0;
			charger->battery.rechg_volt_base = 0;
			charger->battery.fullbatt_rechgsoc = 0;
		}
		if (charger->battery.fullbatt_soc == 0) {
			dev_info(&pdev->dev,
			"Ignoring full-battery soc(state of charge) threshold as it is not supplied\n");
		}
		if (charger->battery.fullbatt_full_capacity == 0) {
			dev_info(&pdev->dev,
			"Ignoring full-battery full capacity threshold as it is not supplied\n");
		}
		if (!charger->battery.charging_max_duration_ms ||
				!charger->battery.discharging_max_duration_ms) {
			dev_info(&pdev->dev,
			"Cannot limit charging duration checking mechanism to prevent overcharge/overheat and control discharging duration\n");
			charger->battery.charging_max_duration_ms = 0;
			charger->battery.discharging_max_duration_ms = 0;
		}
		if (cm->charger->polling_mode != CM_POLL_DISABLE &&
			(charger->polling_interval_ms == 0 ||
			msecs_to_jiffies(charger->polling_interval_ms) <= CM_JIFFIES_SMALL)) {
			dev_dbg(&pdev->dev,
				"polling_interval_ms is too small\n");
			return -EINVAL;
		}
	}

	if (!charger->psy_fuel_gauge) {
		dev_err(&pdev->dev, "No fuel gauge power supply defined\n");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&cm->probe_delay_work, jlq_charger_manage_delay_init);
	INIT_DELAYED_WORK(&cm->steper_current_work, jcm_set_current_steper);

	platform_set_drvdata(pdev, cm);

	memcpy(&cm->charger_psy_desc, &psy_default, sizeof(psy_default));

	if (!charger->psy_name)
		strncpy(cm->psy_name_buf, psy_default.name, PSY_NAME_MAX);
	else
		strncpy(cm->psy_name_buf, charger->psy_name, PSY_NAME_MAX);
	cm->charger_psy_desc.name = cm->psy_name_buf;

	/* Allocate for psy properties because they may vary */
	cm->charger_psy_desc.properties =
		devm_kcalloc(&pdev->dev,
			    ARRAY_SIZE(default_charger_props) +
				NUM_CHARGER_PSY_OPTIONAL,
			    sizeof(enum power_supply_property), GFP_KERNEL);
	if (!cm->charger_psy_desc.properties)
		return -ENOMEM;

	memcpy(cm->charger_psy_desc.properties, default_charger_props,
		sizeof(enum power_supply_property) * ARRAY_SIZE(default_charger_props));
	cm->charger_psy_desc.num_properties = psy_default.num_properties;

	/* Register sysfs entry for charger(regulator) */
	ret = charger_manager_prepare_sysfs(cm);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Cannot prepare sysfs entry of regulators\n");
		return ret;
	}
#ifdef QCOM_BATT_UNIFY_SYSFS
	cm->shutdown_delay_en = 1;
	cm->shutdown_delay = 0;
	cm->last_shutdown_delay = 0;
	qcom_battery_sysfs_init(charger);
#endif
	psy_cfg.drv_data = cm;
	psy_cfg.of_node = cm->best_battery_nd;
	psy_cfg.attr_grp = charger->sysfs_groups;
	cm->charger_psy = power_supply_register(cm->dev,
						&cm->charger_psy_desc,
						&psy_cfg);
	if (IS_ERR(cm->charger_psy)) {
		dev_dbg(cm->dev, "Cannot register charger-manager psy with name \"%s\"\n",
			cm->charger_psy_desc.name);
		return PTR_ERR(cm->charger_psy);
	}
	ret = jcm_register_cooler(cm);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Cannot Register Cooler device.\n");
	}
	dev_dbg(cm->dev, "register charger-manager psy with name \"%s\"\n",
		cm->charger_psy_desc.name);

	cm->soc_update.msoc = -EAGAIN;

	jcm_update_soc_init(cm);
	INIT_DELAYED_WORK(&cm->fullbatt_vchk_work, fullbatt_vchk);
	INIT_WORK(&cm->step_chg_work, jcm_step_charge_work);
	INIT_WORK(&cm->swjeita_chk_work, jcm_sw_jeita_work);
	INIT_DELAYED_WORK(&cm->jcm_monitor, jcm_monitor_task);

/*
 * Charger-manager is capable of waking up the systme from sleep
 * when event is happened through jcm_notify_event()
 */
	device_init_wakeup(&pdev->dev, true);
	device_set_wakeup_capable(&pdev->dev, false);

	schedule_delayed_work(&cm->probe_delay_work, 2 * HZ);

	return 0;

}

static int charger_manager_remove(struct platform_device *pdev)
{
	struct jlq_charger_manager *cm = platform_get_drvdata(pdev);
	cancel_delayed_work_sync(&cm->jcm_monitor);
	cancel_delayed_work_sync(&cm->fullbatt_vchk_work);
	cancel_delayed_work_sync(&cm->soc_update_work);
	cancel_work_sync(&cm->step_chg_work);
	cancel_work_sync(&cm->swjeita_chk_work);
	try_charger_enable(cm, NULL, false);
	power_supply_unregister(cm->charger_psy);
	return 0;
}

static int jcm_suspend_noirq(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		device_set_wakeup_capable(dev, false);
		return -EAGAIN;
	}

	return 0;
}

static bool jcm_need_to_awake(struct jlq_charger_manager *cm)
{

	if (jcm_is_charging(cm))
		return true;

	return false;
}

static int jcm_suspend_prepare(struct device *dev)
{
	struct jlq_charger_manager *cm = dev_get_drvdata(dev);

	if (jcm_need_to_awake(cm))
		return -EBUSY;

	if(!cm->jcm_timer.function) {
		if (alarmtimer_get_rtcdev()) {
			alarm_init(&cm->jcm_timer, ALARM_BOOTTIME, jcm_timer_func);
			dev_dbg(cm->dev, "[%s]:reinit jcm_timer Success", __func__);
		} else {
			dev_dbg(cm->dev, "[%s]:reinit jcm_timer Failed.", __func__);
			return -EINVAL;
		}
	}
	if (!cm->jcm_suspended)
		cm->jcm_suspended = true;

	cm->jcm_timer_set = jcm_setup_timer(cm);

	if (cm->jcm_timer_set) {
		cancel_delayed_work_sync(&cm->jcm_monitor);
		cancel_delayed_work(&cm->fullbatt_vchk_work);
	}

	return 0;
}

static void jcm_suspend_complete(struct device *dev)
{
	struct jlq_charger_manager *cm = dev_get_drvdata(dev);

	if (cm->jcm_suspended)
		cm->jcm_suspended = false;

	if (cm->jcm_timer_set) {
		ktime_t remain;

		alarm_cancel(&cm->jcm_timer);
		cm->jcm_timer_set = false;
		remain = alarm_expires_remaining(&cm->jcm_timer);
		cm->jcm_suspend_duration_ms -= ktime_to_ms(remain);
		dev_dbg(cm->dev, "[%s]:jcm_suspend_duration_ms:%dms remain:%dms",
			__func__, cm->jcm_suspend_duration_ms, ktime_to_ms(remain));
	}
	if (atomic_read(&cm->charger->attached))
		schedule_work(&cm->jcm_monitor.work);

	/* Re-enqueue delayed work (fullbatt_vchk_work) */
	if (cm->fullbatt_vchk_jiffies_at) {
		unsigned long delay = 0;
		unsigned long now = jiffies + CM_JIFFIES_SMALL;

		if (time_before_eq(now, cm->fullbatt_vchk_jiffies_at)) {
			delay = (unsigned long)((long)(cm->fullbatt_vchk_jiffies_at)
				- (long)now);
			delay = jiffies_to_msecs(delay);
		} else {
			delay = 0;
		}

/*
 * Account for cm->jcm_suspend_duration_ms with assuming that
 * timer stops in suspend.
 */
		if (delay > cm->jcm_suspend_duration_ms)
			delay -= cm->jcm_suspend_duration_ms;
		else
			delay = 0;

		dev_dbg(cm->dev, "jcm_suspend_duration_ms:%dms delay:%dms",
			cm->jcm_suspend_duration_ms, delay);
		cm->fullbatt_vchk_jiffies_at =
			jiffies + msecs_to_jiffies(delay);
		schedule_delayed_work(&cm->fullbatt_vchk_work,
				  msecs_to_jiffies(delay));
	}
	/* Re-enqueue delayed work (soc update) */
	jcm_update_soc_resume(cm);
	if (cm->charger->dbg_work_s)
		schedule_delayed_work(&cm->charger->dbg_pr_work, 0);
	device_set_wakeup_capable(cm->dev, false);
}

static const struct platform_device_id charger_manager_id[] = {
	{ "charger-manager", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, charger_manager_id);

static const struct of_device_id charger_manager_match[] = {
	{
		.compatible = "jlq,charger-manager",
	},
	{},
};

static const struct dev_pm_ops charger_manager_pm = {
	.prepare	= jcm_suspend_prepare,
	.suspend_noirq	= jcm_suspend_noirq,
	.complete	= jcm_suspend_complete,
};

static struct platform_driver charger_manager_driver = {
	.driver = {
		.name = "jlq-charger-manager",
		.pm = &charger_manager_pm,
		.of_match_table = charger_manager_match,
	},
	.probe = jlq_charger_manager_probe,
	.remove = charger_manager_remove,
	.id_table = charger_manager_id,
};

static int __init charger_manager_init(void)
{
	return platform_driver_register(&charger_manager_driver);
}
module_init(charger_manager_init);

static void __exit charger_manager_cleanup(void)
{
	platform_driver_unregister(&charger_manager_driver);
}
module_exit(charger_manager_cleanup);

MODULE_SOFTDEP("pre: qcom-adc5");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("Charger Manager");
MODULE_LICENSE("GPL");
