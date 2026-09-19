/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo.Ham <myungjoo.ham@samsung.com>
 *
 * Charger Manager.
 * This framework enables to control and multiple chargers and to
 * monitor charging even in the context of suspend-to-RAM with
 * an interface combining the chargers.
 *
 */

#ifndef _CHARGER_MANAGER_H
#define _CHARGER_MANAGER_H

#include <linux/power_supply.h>
#include <linux/extcon.h>
#include <linux/alarmtimer.h>

#define QCOM_BATT_UNIFY_SYSFS

#define JCM_USB_DEFAULT_MIN_CUR_UA	(300*1000)
#if 1
#define JCM_USB_DEFAULT_MAX_CUR_UA	(500*1000)
#define JCM_USB_CDP_MAX_CUR_UA			(1500*1000)
#define JCM_USB_DCP_MAX_CUR_UA			(2000*1000)
#define JCM_USB_HVDCP_MAX_CUR_UA		(2000*1000)
#define JCM_USB_HVDCP_MAX_CHG_CUR_UA	(3000*1000)
#define JCM_USB_HVDCP_MIN_CUR_UA		(1500*1000)
#define JCM_USB_DCP_MIN_CUR_UA			(1000*1000)
#else
#define JCM_USB_DEFAULT_MAX_CUR_UA		(400*1000)
#define JCM_USB_CDP_MAX_CUR_UA			(1000*1000)
#define JCM_USB_DCP_MAX_CUR_UA			(1200*1000)
#define JCM_USB_HVDCP_MAX_CUR_UA		(1500*1000)
#define JCM_USB_HVDCP_MAX_CHG_CUR_UA	(2000*1000)
#endif
#define JCM_USB_FLOAT_MAX_CUR_UA		(1000*1000)


#define JCM_HVDCP_SW_5V_CUR_MA				1000
#define JCM_HVDCP_SW_HVDCP_CUR_MA			1200
#define JCM_VBUS_NORMAL_VOLT				5000000
#define JCM_HVDCP_QC20_DEFAULT_VBUS_VOLT	9000000
#define JCM_OTG_DEFAULT_BOOST_VOLT			5000000
#define JCM_OTG_DEFAULT_BOOST_CURRENT		500000
#define JCM_HVDCP_MONITOR_INTERV_MS			(60 * 1000)

#define JCM_ADP_DET_VBUS_LOW_UV			(4500 * 1000)
#define JCM_ADP_DET_IBUS_AVG			(4)
#define JCM_ADP_DET_IBUS_LOW_UA			(800 * 1000)
#define JCM_ADP_DET_DEC_PERCENT			(8)

#if 1
#define JCM_SOC_UPDATE_INTERV_VERY_FAST  (15 * 1000)
#define JCM_SOC_UPDATE_INTERV_FAST       (30 * 1000)
#define JCM_SOC_UPDATE_INTERV_NORMAL     (60 * 1000)
#define JCM_SOC_UPDATE_INTERV_SLEEP      (600 * 1000)
#else
#define JCM_SOC_UPDATE_INTERV_VERY_FAST  (5 * 1000)
#define JCM_SOC_UPDATE_INTERV_FAST       (15 * 1000)
#define JCM_SOC_UPDATE_INTERV_NORMAL     (30 * 1000)
#define JCM_SOC_UPDATE_INTERV_SLEEP      (45 * 1000)
#endif

#define JCM_SOC_UPDATE_VERY_FAST_VARY    (5)
#define JCM_SOC_UPDATE_FAST_VARY         (2)

#ifdef QCOM_BATT_UNIFY_SYSFS
#define JCM_CUTOFF_CNT_MAX	(0)
#define JCM_CUTOFF_BAT_UV	(3300 * 1000)
#else
#define JCM_CUTOFF_CNT_MAX	(3)
#endif

#define JCM_CUTOFF_BAT_OCV_MIN	(2000 * 1000)
#define JCM_CUTOFF_BAT_OCV	(3560 * 1000)

#define JCM_CHARGE_IR_COMP_OFF_SOC_LEVEL (98)
#define JCM_CHARGE_IR_COMP_OFF_TEMP (450)
#define JCM_CHARGE_IR_COMP_OFF_IBAT (650000)
#define JCM_CHARGE_IR_COMP_OFF_OCV (4000000)
#define JCM_FAKE_TEMP_ERRVAL  1001
#define JCM_FAKE_SOC_ERRVAL  1001

#define VREG_STEP  (32000)

#define JCM_RECHG_SOC_DEFAULT  (99)

enum JCM_SOC_UPDATE_STATUS_E {
	JCM_SOC_UPDATE_STATUS_QUIET = 0,
	JCM_SOC_UPDATE_STATUS_NORMAL,
	JCM_SOC_UPDATE_STATUS_FAST,
	JCM_SOC_UPDATE_STATUS_VERY_FAST,
	JCM_SOC_UPDATE_STATUS_INIT_FAST_QUERY,
};

enum data_source {
	CM_BATTERY_PRESENT,
	CM_NO_BATTERY,
	CM_FUEL_GAUGE,
	CM_CHARGER_STAT,
};

enum polling_modes {
	CM_POLL_DISABLE = 0,
	CM_POLL_ALWAYS,
	CM_POLL_EXTERNAL_POWER_ONLY,
	CM_POLL_CHARGING_ONLY,
};

enum jcm_event_types {
	CM_EVENT_UNKNOWN = 0,
	CM_EVENT_BATT_FULL,
	CM_EVENT_BATT_IN,
	CM_EVENT_BATT_OUT,
	CM_EVENT_BATT_OVERHEAT,
	CM_EVENT_BATT_COLD,
	CM_EVENT_EXT_PWR_IN_OUT,
	CM_EVENT_CHG_START_STOP,
	CM_EVENT_OTHERS,
};

/**
 * struct jlq_charger_chip
 * @extcon_name: the name of extcon device.
 * @name: the name of charger cable(external connector).
 * @extcon_dev: the extcon device.
 * @wq: the workqueue to control charger according to the state of
 *	charger cable. If charger cable is attached, enable charger.
 *	But if charger cable is detached, disable charger.
 * @nb: the notifier block to receive changed state from EXTCON
 *	(External Connector) when charger cable is attached/detached.
 * @attached: the state of charger cable.
 *	true: the charger cable is attached
 *	false: the charger cable is detached
 * @charger: the instance of struct jlq_charger_regulator.
 * @cm: the Charger Manager representing the battery.
 */
struct jlq_charger_chip {
	const char *chip_name;
	const char *chip_regulator_name;
	struct jlq_charger_regulator *parent_charger;
	struct regulator *cable_regulator;
	const char *charger_psy;
	struct notifier_block psy_nb;

	/*
	 * Set min/max current of regulator to protect over-current issue
	 * according to a kind of charger cable when cable is attached.
	 */
	int min_uA;
	int max_uA;
	int set_uA;
	int now_uA;
	int current_percent;

	struct jlq_charger_manager *cm;
};

struct jlq_charger_mangaer_step_rang {
	int low_threshold;
	int high_threshold;
	int float_volt_uv;
	int current_ua;
};

struct jlq_charger_mangaer_battery_profile {
	const char *battery_name;
	int bat_id_kohm;
	int termination_current;

	bool rechg_volt_base;
	unsigned int fullbatt_vchkdrop_uV;
	unsigned int fullbatt_rechgsoc;
	unsigned int fullbatt_uV;
	unsigned int fullbatt_soc;
	unsigned int fullbatt_full_capacity;
	unsigned int fullbatt_full_enery;
	unsigned int cutoff_batt_soc;
	unsigned int cutoff_batt_uV;

	int fastchg_volt_uv;  //快速充电电压
	int fastchg_current_ua; //快速充电电流
	int batovp_recharge_uv;
	int step_chg_enabled;
	struct jlq_charger_mangaer_step_rang *step_chg_rangs;

	int sw_jeita_enabled;
	struct jlq_charger_mangaer_step_rang *sw_jeita_rangs;

	int temp_min;
	int temp_max;
	int temp_diff;

	u32 charging_max_duration_ms;
	u32 discharging_max_duration_ms;

};

typedef struct jcm_soc_update_s {
	unsigned long last_polling;
	unsigned long next_polling;
	int bat_soc;
	int ssoc_cutoff;
	int ssoc_full;
	int catoff_uv;
	int full_uv;
	int sys_soc;
	int msoc;
	int status;
}jcm_soc_update_t;

/**
 * struct jlq_charger_regulator
 * @psy_name: the name of power-supply-class for charger manager
 * @polling_mode:
 *	Determine which polling mode will be used
 * @fullbatt_vchkdrop_ms:
 * @fullbatt_vchkdrop_uV:
 *	Check voltage drop after the battery is fully charged.
 *	If it has dropped more than fullbatt_vchkdrop_uV after
 *	fullbatt_vchkdrop_ms, CM will restart charging.
 * @fullbatt_uV: voltage in microvolt
 *	If VBATT >= fullbatt_uV, it is assumed to be full.
 * @fullbatt_soc: state of Charge in %
 *	If state of Charge >= fullbatt_soc, it is assumed to be full.
 * @fullbatt_full_capacity: full capacity measure
 *	If full capacity of battery >= fullbatt_full_capacity,
 *	it is assumed to be full.
 * @polling_interval_ms: interval in millisecond at which
 *	charger manager will monitor battery health
 * @battery_present:
 *	Specify where information for existence of battery can be obtained
 * @psy_charger_stat: the names of power-supply for chargers
 * @num_charger_regulator: the number of entries in charger_regulators
 * @charger_regulators: array of charger regulators
 * @psy_fuel_gauge: the name of power-supply for fuel gauge
 * @thermal_zone : the name of thermal zone for battery
 * @temp_min : Minimum battery temperature for charging.
 * @temp_max : Maximum battery temperature for charging.
 * @temp_diff : Temperature difference to restart charging.
 * @measure_battery_temp:
 *	true: measure battery temperature
 *	false: measure ambient temperature
 * @charging_max_duration_ms: Maximum possible duration for charging
 *	If whole charging duration exceed 'charging_max_duration_ms',
 *	cm stop charging.
 * @discharging_max_duration_ms:
 *	Maximum possible duration for discharging with charger cable
 *	after full-batt. If discharging duration exceed 'discharging
 *	max_duration_ms', cm start charging.
 */
struct jlq_charger_regulator {
	const char *psy_name;
	const char *input_regulator_name;
	const struct attribute_group **sysfs_groups;
	const char *psy_fuel_gauge;
	const char *thermal_zone;
	const char *extcon_name;
	const char *otg_regulator_name;
	int batid_pu_ohm;
	int qc20_vbus_volt_setting;

	bool fg_inited;
	bool chargers_inited;
	enum polling_modes polling_mode;
	unsigned int polling_interval_ms;
	enum data_source battery_present;
	bool not_measure_battery_temp;
	bool soft_recharge_flag;
	int recharge_hold_sec;
	time64_t fullbatt_term_time_at;
	unsigned int fullbatt_vchkdrop_ms;
	int externally_control;
	struct jlq_charger_mangaer_battery_profile battery;

	int batt_id_ohm;
	/* The name of regulator for charging */
	struct regulator *regulator_input;
	/* charger never on when system is on */
	struct jcmvotable *input_current_limit_votable;
	struct jcmvotable *fastchg_current_votable;
	struct jcmvotable *fastchg_volt_votable;
	struct jcmvotable *charger_disable_votable;
	int set_current;
	int set_volt;
	/* The charger-manager use Extcon framework */
	struct extcon_dev *extcon_dev;
	struct extcon_dev *extcon_id_dev;
	struct delayed_work input_volt_work;
	ktime_t last_input_adjust;
	int input_volt_target;
	int input_volt_rt;

//	struct work_struct extcon_usb_wq;
	struct notifier_block usb_nb;
	struct notifier_block usb_type_nb;
	struct notifier_block usb_otg_nb;
	enum power_supply_type chg_type;
	/*
	 * Store constraint information related to current limit,
	 * each cable have different condition for charging.
	 */
	int num_chips;
	/*the main charge chip must always locat the first chip.*/
	struct jlq_charger_chip *chips;
	struct regulator *otg_regulator;
	/* The state of charger cable */
	atomic_t attached;
	bool otg_enabled;
	bool soc_policy;
	bool debug_fake_soc;
	bool debug_battery;
	bool no_battery;
	bool charge_ir_comp_off;
	int otg_boost_volt;
	int otg_boost_current;
	int thermal_level;
	int thermal_level_cnt;
	int *thermal_mitigation;
	struct mutex lock;
	struct attribute_group attr_grp;
	struct device_attribute attr_input_name;
	struct device_attribute attr_state;
	struct device_attribute attr_force_disable;
	struct device_attribute attr_res_id;
	struct device_attribute attr_fake_temp;
	struct device_attribute attr_fake_soc;
	struct device_attribute attr_dbg_work;
	struct device_attribute attr_fcc;
	struct device_attribute attr_fcc_eff;
	struct device_attribute attr_fcv;
	struct device_attribute attr_fcv_eff;
	struct device_attribute attr_chg_en_eff;
	struct device_attribute attr_battery_type;
	struct attribute *attrs[16];
	struct jlq_charger_manager *cm;
#ifdef QCOM_BATT_UNIFY_SYSFS
	struct class qcom_batt_unify_class;
#endif
	int steper_intv_ms;
	int steper_val;
	int fake_temp;
	int fake_soc;

	unsigned int dbg_work_s;
	struct delayed_work dbg_pr_work;
};

#define PSY_NAME_MAX	30

/**
 * struct jlq_charger_manager
 * @entry: entry for list
 * @dev: device pointer
 * @desc: instance of charger_desc
 * @fuel_gauge: power_supply for fuel gauge
 * @charger_stat: array of power_supply for chargers
 * @tzd_batt : thermal zone device for battery
 * @charger_enabled: the state of charger
 * @fullbatt_vchk_jiffies_at:
 *	jiffies at the time full battery check will occur.
 * @fullbatt_vchk_work: work queue for full battery check
 * @emergency_stop:
 *	When setting true, stop charging
 * @psy_name_buf: the name of power-supply-class for charger manager
 * @charger_psy: power_supply for charger manager
 * @status_save_ext_pwr_inserted:
 *	saved status of external power before entering suspend-to-RAM
 * @status_save_batt:
 *	saved status of battery before entering suspend-to-RAM
 * @charging_start_time: saved start time of enabling charging
 * @charging_end_time: saved end time of disabling charging
 */
struct jlq_charger_manager {
	struct list_head entry;
	struct device *dev;
	struct jlq_charger_regulator *charger;
	struct device_node *best_battery_nd;

#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzd_batt;
	struct thermal_cooling_device *tcd;
#endif
	struct delayed_work probe_delay_work;
	struct notifier_block psy_nb;
	struct work_struct psys_status_change_work;
	struct delayed_work steper_current_work;

	unsigned long fullbatt_vchk_jiffies_at;
	struct delayed_work jcm_monitor;
	struct delayed_work fullbatt_vchk_work;
	struct work_struct swjeita_chk_work;
	struct work_struct step_chg_work;
	struct delayed_work soc_update_work;
	bool charger_enabled;
	int emergency_stop;
	bool inited;
	bool temp_alrt_stat;
	bool pm_states_awake;

	char psy_name_buf[PSY_NAME_MAX + 1];
	struct power_supply_desc charger_psy_desc;
	struct power_supply *charger_psy;
	u64 charging_start_time;
	u64 charging_end_time;
	bool cm_suspended;
	struct delayed_work cm_monitor_work; /* init at driver add */
	/* About in-suspend (suspend-again) monitoring */
	struct alarm jcm_timer;

	jcm_soc_update_t soc_update;
	bool jcm_suspended;
	bool jcm_timer_set;
	int cutoff_cnt;
#ifdef QCOM_BATT_UNIFY_SYSFS
	int shutdown_delay_en;
	unsigned int shutdown_delay;
	unsigned int last_shutdown_delay;
#endif
	unsigned long jcm_suspend_duration_ms;

	/* About normal (not suspended) monitoring */
	unsigned long polling_jiffy; /* ULONG_MAX: no polling */
	unsigned long next_polling; /* Next appointed polling time */

};

#ifdef QCOM_BATT_UNIFY_SYSFS
int qcom_battery_sysfs_init(struct jlq_charger_regulator *charger);
int jcm_update_msoc_force_flush(struct jlq_charger_manager *cm);
int jcm_get_charger_psy_prop(struct jlq_charger_chip *chip,
	enum power_supply_property psp,
	union power_supply_propval *val);
int jcm_set_charger_psy_prop(struct jlq_charger_chip *chip,
	enum power_supply_property psp,
	union power_supply_propval *val);
void generate_xm_charge_uvent(struct jlq_charger_manager *cm);
#endif
extern void jcm_notify_event(struct jlq_charger_manager *cm,
				enum jcm_event_types type, char *msg);
#endif /* _CHARGER_MANAGER_H */
