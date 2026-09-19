/*
 * Fuelgauge battery driver
 *
 * Copyright (C) 2021 SinoWealth
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#define pr_fmt(fmt)	"[sh366101] %s(%d): " fmt, __func__, __LINE__
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <sh366101_fg.h>
#include <jlq_charger_manager.h>
#ifdef SH_FW_DEBUG
#include <linux/firmware.h>
#endif
//#include "../../../../i2c/busses/i2c-designware-core.h"

#define CHECK_RETURN(ret)      \
	if (ret < 0) { \
		pr_err("i2c transfer error, ret=%d", ret); \
		return ret; \
	} \

#ifdef CONFIG_JLQ_CHARGER_FG_INFO_DEBUG
    #define FG_INFO_DEBUG
#endif
#define I2C_DEBUG
#ifdef I2C_DEBUG
static u64 g_I2c_Transfer_Err_Cnt;
#endif

#define DEF_NOMINAL_VOLTAHE 385

#ifdef SH_FW_DEBUG
#define SH_AFI_FILE "sh_afi.bin"
static const struct firmware *sh_afi_firmware;
#endif

#define I2C_BASE 0x30706000
#define PINCTL_BASE 0x34501000
#define I2C_6_SCL_GPIO 470
#define I2C_6_SDA_GPIO 469
#define I2C_6_SCL_PINCTL 0xb0
#define I2C_6_SDA_PINCTL 0xac

static s32 fg_get_device_id(struct i2c_client *client);

enum sh_fg_reg_idx {
	SH_FG_REG_DEVICE_ID = 0,
	SH_FG_REG_CNTL,
	SH_FG_REG_INT,
	SH_FG_REG_STATUS,
	SH_FG_REG_SOC,
	SH_FG_REG_OCV,
	SH_FG_REG_VOLTAGE,
	SH_FG_REG_CURRENT,
	SH_FG_REG_TEMPERATURE_IN,
	SH_FG_REG_TEMPERATURE_EX,
	SH_FG_REG_BAT_RMC,
	SH_FG_REG_BAT_FCC,
	SH_FG_REG_RESET,
	SH_FG_REG_SOC_CYCLE,
	SH_FG_REG_DESIGN_CAPCITY, /* 20211108, Ethan */
	NUM_REGS,
};

static u32 sh366101_regs[NUM_REGS] = {
	CMDMASK_ALTMAC_R | 0x0001, /* DEVICE_ID */
	CMDMASK_ALTMAC_R | 0x0000, /* CNTL */
	CMDMASK_SINGLE | 0x6E, /* INT */
	0x06, /* STATUS */
	0x1C, /* SOC */
	0x64, /* OCV */
	0x04, /* VOLTAGE */
	0x10, /* CURRENT */
	0x1E, /* TEMPERATURE_IN */
	0x02, /* TEMPERATURE_EX */
	0x0C, /* BAT_RMC */
	0x0E, /* BAT_FCC */
	CMDMASK_ALTMAC_W | 0x41, /* RESET */
	0x1A, /* SOC_CYCLE */
	0x3C, /* SH_FG_REG_DESIGN_CAPCITY. 20211108, Ethan */
};

enum sh_fg_device {
	SH366101,
};

enum sh_fg_temperature_type {
	TEMPERATURE_IN = 0,
	TEMPERATURE_EX,
};

const unsigned char *device2str[] = {
	"sh366101",
};

enum battery_table_type {
	BATTERY_TABLE0 = 0,
	BATTERY_TABLE1,
	BATTERY_TABLE2,
	BATTERY_TABLE_MAX,
};

/* 20211112, Ethan. Charge Block */
enum sh_fg_charge_temper_range {
	TEMPER_RANGE_T1T2 = 0, /* value also used as block_chargingvoltage index */
	TEMPER_RANGE_T2T3 = 1,
	TEMPER_RANGE_T3T4 = 2,
	TEMPER_RANGE_T4T5 = 3,
	TEMPER_RANGE_T5T6 = 4,
	TEMPER_RANGE_BELOW_T1 = 5,
	TEMPER_RANGE_ABOVE_T6 = 6,
};

/* 20211112, Ethan. Charge Block */
enum sh_fg_charge_degrade_flag {
	DEGRADE_PHASE_0 = 0,
	DEGRADE_PHASE_1,
	DEGRADE_PHASE_2,
	DEGRADE_PHASE_3,
	DEGRADE_PHASE_4,
};

struct sh_fg_chip;

struct sh_fg_chip {
	struct device *dev;
	struct i2c_client *client;
	struct mutex i2c_rw_lock; /* I2C Read/Write Lock */
	struct mutex data_lock; /* Data Lock */
	struct mutex cali_lock; /* Cali Lock. 20220106, Ethan */
	u8 chip;
	u32 regs[NUM_REGS];
	s32 batt_id;
	s32 gpio_int;
	u32 irq;
	//struct notifier_block   nb;

	/* Status Tracking */
	bool batt_present;
	bool batt_fc; /* Battery Full Condition */
	bool batt_tc;	/* Battery Full Condition */
	bool batt_ot; /* Battery Over Temperature */
	bool batt_ut; /* Battery Under Temperature */
	bool batt_soc1;	/* SOC Low */
	bool batt_socp;	/* SOC Poor */
	bool batt_dsg; /* Discharge Condition*/
	s32 batt_soc;
	s32 batt_ocv;
	s32 batt_fcc; /* Full charge capacity */
	s32 fullbatt_vol;
	s32 batt_rmc; /* Remaining capacity */
	s32 batt_designcap; /* 20211108, Ethan */
	s32 batt_volt;
	s32 aver_batt_volt;
	s32 batt_temp;
	s32 sh_temp;
	s32 batt_curr;
	s32 is_charging; /* Charging informaion from charger IC */
	s32 batt_soc_cycle; /* Battery SOC cycle */

	s32 health;
	s32 recharge_vol;
	bool usb_present;
	bool batt_sw_fc;
	bool fast_mode;

	/* previous battery voltage current*/
	s32 p_batt_voltage;
	s32 p_batt_current;

	/* DT */
	bool en_temp_ex;
	bool en_temp_in;
	bool en_batt_det;
	s32 fg_irq_set;

	struct delayed_work monitor_work;
	struct delayed_work fg_init_work;
	//u64 last_update;
#ifdef FG_INFO_DEBUG
	bool is_debug_open;
#endif
	struct votable *fcc_votable;
	struct votable *fv_votable;
	struct votable *chg_dis_votable;

	enum sh_fg_charge_temper_range temper_range; /* 20211112, Ethan. Charge Block */
	enum sh_fg_charge_degrade_flag degrade_flag; /* 20211112, Ethan. Charge Block */
	s32 terminate_voltage;			     /* 20211112, Ethan. Termniate Voltage */
	s32 nominal_voltage;
	struct dentry *debug_root;
	struct power_supply *fg_psy;
#if !(IS_PACK_ONLY)
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct power_supply *bbc_psy;
#endif
	struct power_supply_desc fg_psy_d;
	bool is_suspend;
	bool force_updata;
	struct device_node *batt_np;
	const char *battery_profile_psy;
	void __iomem *i2c_bus_base;
	void __iomem *i2c_pinctl_base;
	bool is_calied;
	bool start_cali;
};

static s32 fg_gauge_enable_autocali(struct sh_fg_chip *sm, bool force_enable);
static s32 fg_gauge_check_autocali(struct sh_fg_chip *sm);
static s32 fg_reset_sh_i2cbus(struct sh_fg_chip *sm);
static s32 fg_gauge_seal(struct sh_fg_chip *sm);
static s32 fg_gauge_unseal(struct sh_fg_chip *sm);

static inline u32 sh_read32(u64 addr)
{
	return readl((void *)addr);
}

static inline void sh_write32(u64 addr, u32 v)
{
	writel(v, (void *)addr);
}

static s32 fg_init(struct i2c_client *client);

static int __fg_read_word(struct i2c_client *client, u8 reg, u16 *val)
{
	s32 ret;

	ret = i2c_smbus_read_word_data(client, reg); /* little endian */
	if (ret < 0) {
#ifdef I2C_DEBUG
		g_I2c_Transfer_Err_Cnt++;
#endif
		pr_err("i2c read word fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	*val = (u16)ret;

	return 0;
}

static int __fg_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;

	ret = i2c_smbus_write_word_data(client, reg, val); /* little endian */
	if (ret < 0) {
#ifdef I2C_DEBUG
		g_I2c_Transfer_Err_Cnt++;
#endif
		pr_err("i2c write word fail: can't write 0x%02X to reg 0x%02X\n",
				val, reg);
		return ret;
	}

	return 0;
}

static int fg_read_sbs_word(struct sh_fg_chip *sm, u32 reg, u16 *val)
{
	int ret = -1;

	pr_debug("start, reg=%08X", reg);

	mutex_lock(&sm->i2c_rw_lock);
	if ((reg & CMDMASK_ALTMAC_R) == CMDMASK_ALTMAC_R) {
		ret = __fg_write_word(sm->client, CMD_ALTMAC, (u16)reg);
		if (ret < 0)
			goto fg_read_sbs_word_end;

		HOST_DELAY(CMD_SBS_DELAY);
		ret = __fg_read_word(sm->client, CMD_ALTBLOCK, val);
	} else {
		ret = __fg_read_word(sm->client, (u8)reg, val);
	}
fg_read_sbs_word_end:
	mutex_unlock(&sm->i2c_rw_lock);

	return ret;
}

static int fg_write_sbs_word(struct sh_fg_chip *sm, u32 reg, u16 val)
{
	int ret;

	mutex_lock(&sm->i2c_rw_lock);
	ret = __fg_write_word(sm->client, (u8)reg, val);
	mutex_unlock(&sm->i2c_rw_lock);

	return ret;
}

//return -1: error; else return string valid length
static s32 print_buffer(char* str, s32 strlen, u8* buf, s32 buflen)
{
#define PRINT_BUFFER_FORMAT_LEN   3
	s32 i, j;

	if ((strlen <= 0) || (buflen <= 0))
		return -1;

	memset(str, 0, strlen * sizeof(char));

	j = min(buflen, strlen / PRINT_BUFFER_FORMAT_LEN);
	for (i = 0; i < j; i++)
		sprintf(&str[i * PRINT_BUFFER_FORMAT_LEN], "%02X ", buf[i]);

	return i * PRINT_BUFFER_FORMAT_LEN;
}

static s32 __fg_read_buffer(struct i2c_client *client, u8 reg, u8 length, u8 *val)
{
	static struct i2c_msg msg[2];

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(u8);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = length;

	return (s32)i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
}


static int fg_read_block(struct sh_fg_chip *sm, u32 reg, u8 length, u8 *val)
{
	int ret = -1;
	int i;
	u8 sum;
	u16 checksum;

	mutex_lock(&sm->i2c_rw_lock);
	if ((reg & CMDMASK_ALTMAC_R) == CMDMASK_ALTMAC_R) { /* 20211116, Ethan */
		ret = __fg_write_word(sm->client, CMD_ALTMAC, (u16)reg);
		if (ret < 0)
			goto fg_read_block_end;
		HOST_DELAY(CMD_SBS_DELAY);

		if (length > 32)
			length = 32;

		ret = __fg_read_buffer(sm->client, CMD_ALTBLOCK, length, val);
		if (ret < 0)
			goto fg_read_block_end;
		HOST_DELAY(CMD_SBS_DELAY);

		//check buffer
		ret = __fg_read_word(sm->client, CMD_ALTCHK, &checksum);
		if (ret < 0)
			goto fg_read_block_end;

		i = (checksum >> 8) - 4;
		if (i <= 0)
			goto fg_read_block_end;

		sum = (u8)(reg & 0xFF) + (u8)((reg >> 8) & 0xFF);
		while (i--)
			sum += val[i];
		sum = ~sum;
		if (sum != (u8)checksum)
			ret = -1;
		else
			ret = 0;
	} else {
		ret = __fg_read_buffer(sm->client, reg, length, val);
	}

fg_read_block_end:
	mutex_unlock(&sm->i2c_rw_lock);

	return ret;
}

//#ifdef FG_TEST
static s32 __fg_write_buffer(struct i2c_client *client, u8 reg, u8 length, u8 *val)
{
	static struct i2c_msg msg[1];
	static u8 write_buf[WRITE_BUF_MAX_LEN];
	s32 ret;

	if (!client->adapter)
		return -ENODEV;

	if ((length <= 0) || (length + 1 >= WRITE_BUF_MAX_LEN)) {
		pr_err("i2c write buffer fail: length invalid!");
		return -1;
	}

	memset(write_buf, 0, WRITE_BUF_MAX_LEN * sizeof(u8));
	write_buf[0] = reg;
	memcpy(&write_buf[1], val, length);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = write_buf;
	msg[0].len = sizeof(u8) * (length + 1);

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		pr_err("i2c write buffer fail: can't write reg 0x%02X\n", reg);
		return (s32)ret;
	}

	return 0;
}

/* 20211116, Ethan */
static int fg_write_block(struct sh_fg_chip *sm, u32 reg, u8 length, u8 *val)
{
	int ret;
	int i;
	u8 sum;
	u16 checksum;

	if (length > 32)
		length = 32;

	mutex_lock(&sm->i2c_rw_lock);
	if ((reg & CMDMASK_ALTMAC_W) == CMDMASK_ALTMAC_W) {
		ret = __fg_write_word(sm->client, CMD_ALTMAC, (u16)reg);
		if (ret < 0)
			goto fg_write_block_end;
		msleep(CMD_SBS_DELAY);

		ret = __fg_write_buffer(sm->client, CMD_ALTBLOCK, length, val);
		if (ret < 0)
			goto fg_write_block_end;
		msleep(CMD_SBS_DELAY);

		sum = (u8)reg + (u8)(reg >> 8);
		for (i = 0; i < length; i++)
			sum += val[i];
		sum = ~sum; /* 20220104, Ethan */
		checksum = length + 4;
		checksum = (checksum << 8) | sum;

		ret = __fg_write_word(sm->client, CMD_ALTCHK, checksum);
		if (ret < 0)
			goto fg_write_block_end;
	} else {
		ret = __fg_write_buffer(sm->client, (u8)reg, length, val);
	}
fg_write_block_end:
	mutex_unlock(&sm->i2c_rw_lock);

	return ret;
}
//#endif

struct sh_decoder;

struct sh_decoder {
	u8 addr;
	u8 reg;
	u8 length;
	u8 buf_first_val;
};

static s32 fg_decode_iic_read(struct sh_fg_chip *sm,
		struct sh_decoder *decoder, u8 *pBuf)
{
	static struct i2c_msg msg[2];
	u8 addr = IIC_ADDR_OF_2_KERNEL(decoder->addr);
	s32 ret;

	if (!sm->client->adapter)
		return -ENODEV;

	mutex_lock(&sm->i2c_rw_lock);

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].buf = &(decoder->reg);
	msg[0].len = sizeof(u8);
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = pBuf;
	msg[1].len = decoder->length;
	ret = (s32)i2c_transfer(sm->client->adapter, msg, ARRAY_SIZE(msg));
#ifdef I2C_DEBUG
	if (ret < 0)
		g_I2c_Transfer_Err_Cnt++;
#endif
	mutex_unlock(&sm->i2c_rw_lock);
	return ret;
}

static s32 fg_decode_iic_write(struct sh_fg_chip *sm, struct sh_decoder *decoder)
{
	static struct i2c_msg msg[1];
	static u8 write_buf[WRITE_BUF_MAX_LEN];
	u8 addr = IIC_ADDR_OF_2_KERNEL(decoder->addr);
	u8 length = decoder->length;
	s32 ret;

	if (!sm->client->adapter)
		return -ENODEV;

	if ((length <= 0) || (length + 1 >= WRITE_BUF_MAX_LEN)) {
		pr_err("i2c write buffer fail: length invalid!");
		return -1;
	}

	mutex_lock(&sm->i2c_rw_lock);
	memset(write_buf, 0, WRITE_BUF_MAX_LEN * sizeof(u8));
	write_buf[0] = decoder->reg;
	memcpy(&write_buf[1], &(decoder->buf_first_val), length);

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].buf = write_buf;
	msg[0].len = sizeof(u8) * (length + 1);

	ret = i2c_transfer(sm->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		pr_err("i2c write buffer fail: can't write reg 0x%02X\n", decoder->reg);
#ifdef I2C_DEBUG
	if (ret < 0)
		g_I2c_Transfer_Err_Cnt++;
#endif

	mutex_unlock(&sm->i2c_rw_lock);
	return (ret < 0) ? ret : 0;
}

static int fg_read_status(struct sh_fg_chip *sm)
{
	int ret;
	u16 flags1, cntl;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_CNTL], &cntl);
	if (ret < 0)
		return ret;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_STATUS], &flags1);
	if (ret < 0)
		return ret;

	pr_debug("cntl=0x%04X, bat_flags=0x%04X", cntl, flags1);
	mutex_lock(&sm->data_lock);
	sm->batt_present = !!(flags1 & FG_STATUS_BATT_PRESENT);
	sm->batt_ot = !!(flags1 & FG_STATUS_HIGH_TEMPERATURE);
	sm->batt_ut = !!(flags1 & FG_STATUS_LOW_TEMPERATURE);
	sm->batt_tc = !!(flags1 & FG_STATUS_TERM_SOC);
	sm->batt_fc = !!(flags1 & FG_STATUS_FULL_SOC);
	sm->batt_soc1 = !!(flags1 & FG_STATUS_LOW_SOC2);
	sm->batt_socp = !!(flags1 & FG_STATUS_LOW_SOC1);
	sm->batt_dsg = !!(flags1 & FG_OP_STATUS_CHG_DISCHG);
	mutex_unlock(&sm->data_lock);

	return 0;
}

static int fg_status_changed(struct sh_fg_chip *sm)
{
	sm->force_updata = true;
	cancel_delayed_work(&sm->monitor_work);
	schedule_delayed_work(&sm->monitor_work, msecs_to_jiffies(100));
	//power_supply_changed(sm->fg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t fg_irq_handler(int irq, void *dev_id)
{
	struct sh_fg_chip *sm = dev_id;

	if (sm->is_suspend || sm->start_cali)
		return IRQ_HANDLED;
	fg_status_changed(sm);

	return 0;
}

#ifdef FG_INFO_DEBUG
static s32 fg_read_gaugeinfo_block(struct sh_fg_chip *sm, bool is_force)
{
	static u8 buf[GAUGEINFO_LEN];
	static char str[GAUGESTR_LEN];
	int i, j = 0;
	int ret;
	u16 temp;
	static u64 jiffies_old;
	u64 jiffies_now = get_jiffies_64();
	s64 tick;

	if (sm->is_debug_open == false)
		return 0;

	if (is_force || jiffies_old == 0)
		goto FG_READ_INFO;

	tick = (s64)(jiffies_now - jiffies_old);
	if (tick < 0) {  //overflow
		tick = (s64)(U64_MAXVALUE - jiffies_old);
		tick += jiffies_now + 1;
	}
	tick /= HZ;
	if (tick < GAUGE_LOG_MIN_TIMESPAN)
		return 0;

FG_READ_INFO:
	jiffies_old = jiffies_now;

	/* Cali Info */
	ret = fg_read_block(sm, CMD_CALIINFO, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CALIINFO, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211111, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "Elasp=%d, ", tick);
	i += sprintf(&str[i], "Voltage=%d, ", (s16)BUF2U16_LT(&buf[12]));
	i += sprintf(&str[i], "Current=%d, ", (s16)BUF2U32_LT(&buf[8]));
	i += sprintf(&str[i], "TS1Temp=%d, ", (s16)(BUF2U16_LT(&buf[22]) - TEMPER_OFFSET));
	i += sprintf(&str[i], "IntTemper=%d, ", (s16)(BUF2U16_LT(&buf[18]) - TEMPER_OFFSET));
	j = max(i, j);
	pr_err("SH366101_GaugeLog: CMD_CALIINFO is %s", str);

	/* 20211116, Ethan. Charge Info */
#if ENABLE_CHGBLOCK
	ret = fg_read_block(sm, CMD_CHARGESTATUS, LEN_CHARGESTATUS, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CHARGESTATUS, ret = %d\n", ret);
		return ret;
	}
	ret = BUF2U16_BG(&buf[1]);
	ret |= ((u32)buf[0]) << 16;

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "ChgStatus=0x%06X, ", (u32)(ret & 0xFFFFFF));
	i += sprintf(&str[i], "DegradeFlag=0x%08X, ", BUF2U32_BG(&buf[3]));
#endif
	/* 20211116, Ethan. Term Volt */
#if ENABLE_TERMBLOCK
	ret = fg_read_block(sm, CMD_TERMINATEVOLT, LEN_TERMINATEVOLT, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read TERMINATEVOLT, ret = %d\n", ret);
		return ret;
	}

	i += sprintf(&str[i], "TermVolt=%d, ", BUF2U16_LT(&buf[0]));
	i += sprintf(&str[i], "TermVoltTime=%d, ", buf[2]);
#endif
	/* 20211123, Ethan */
	ret = fg_read_sbs_word(sm, CMD_CONTROLSTATUS, &temp);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_CONTROLSTATUS, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "ControlStatus=0x%04X, ", temp);

	ret = fg_read_sbs_word(sm, CMD_RUNFLAG, &temp);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_RUNFLAG, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "Flags=0x%04X, ", temp);

	ret = fg_read_sbs_word(sm, CMD_OEMFLAG, &temp); /* 20211126, Ethan */
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_OEMFLAG, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "OEMFLAG=0x%04X, ", temp);

	j = max(i, j);
	pr_err("SH366101_GaugeLog: CHARGESTATUS is %s", str);

	/* Lifetime Info. 20211126, Ethan */
	ret = fg_read_block(sm, CMD_LIFETIMEADC, LEN_LIFETIMEADC, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read LIFETIMEADC, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "LT_MaxVolt=%d, ", (s16)BUF2U16_BG(&buf[0]));
	i += sprintf(&str[i], "LT_MinVolt=%d, ", (s16)BUF2U16_BG(&buf[2]));
	i += sprintf(&str[i], "LT_MaxChgCUR=%d, ", (s16)BUF2U16_BG(&buf[4]));
	i += sprintf(&str[i], "LT_MaxDsgCUR=%d, ", (s16)BUF2U16_BG(&buf[6]));
	i += sprintf(&str[i], "LT_MaxTemper=%d, ", (s8)buf[8]);
	i += sprintf(&str[i], "LT_MinTemper=%d, ", (s8)buf[9]);
	i += sprintf(&str[i], "LT_MaxIntTemper=%d, ", (s8)buf[10]);
	i += sprintf(&str[i], "LT_MinIntTemper=%d, ", (s8)buf[11]);
	j = max(i, j);
	pr_err("SH366101_GaugeLog: LIFETIMEADC is %s", str);

	/* Gauge Info */
	ret = fg_read_block(sm, CMD_GAUGEINFO, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read GAUGEINFO, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "RunState=0x%08X, ", BUF2U32_LT(&buf[0]));
	i += sprintf(&str[i], "GaugeState=0x%08X, ", BUF2U32_LT(&buf[4]));
	i += sprintf(&str[i], "GaugeS2=0x%04X, ", BUF2U16_LT(&buf[8]));
	i += sprintf(&str[i], "WorkState=0x%04X, ", BUF2U16_LT(&buf[10]));
	i += sprintf(&str[i], "TimeInc=%d, ", buf[12]);
	i += sprintf(&str[i], "MainTick=%d, ", buf[13]);
	i += sprintf(&str[i], "SysTick=%d, ", buf[14]);
	i += sprintf(&str[i], "ClockH=%d, ", BUF2U16_LT(&buf[15]));
	i += sprintf(&str[i], "RamCheckT=%d, ", buf[17]);
	i += sprintf(&str[i], "AutoCaliT=%d, ", buf[18]);
	i += sprintf(&str[i], "LTHour=%d, ", buf[19]);
	i += sprintf(&str[i], "LTTimer=%d, ", buf[20]);
	i += sprintf(&str[i], "FlashT=%d, ", buf[21]);
	i += sprintf(&str[i], "LTFlag=0x%02X, ", buf[22]);
	i += sprintf(&str[i], "RSTS=0x%02X, ", buf[23]);
	j = max(i, j);
	pr_err("SH366101_GaugeLog: CMD_GAUGEINFO is %s", str);

	/* Gauge Block 2 */
	ret = fg_read_block(sm, CMD_GAUGEBLOCK2, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_GAUGEBLOCK2, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "QF1=0x%02X, ", buf[0]);
	i += sprintf(&str[i], "QF2=0x%02X, ", buf[1]);
	i += sprintf(&str[i], "PackQmax=%d, ", (s16)BUF2U16_BG(&buf[2]));
	i += sprintf(&str[i], "CycleCount=%d, ", BUF2U16_BG(&buf[14]));
	i += sprintf(&str[i], "QmaxCount=%d, ", BUF2U16_BG(&buf[16]));
	i += sprintf(&str[i], "QmaxCycle=%d, ", BUF2U16_BG(&buf[18]));
	i += sprintf(&str[i], "VatEOC=%d, ", (s16)BUF2U16_BG(&buf[4]));
	i += sprintf(&str[i], "IatEOC=%d, ", (s16)BUF2U16_BG(&buf[6]));
	i += sprintf(&str[i], "ChgVEOC=%d, ", (s16)BUF2U16_BG(&buf[8]));
	i += sprintf(&str[i], "AVILR=%d, ", (s16)BUF2U16_BG(&buf[10]));
	i += sprintf(&str[i], "AVPLR=%d, ", (s16)BUF2U16_BG(&buf[12]));
	i += sprintf(&str[i], "ModelCount=%d, ", BUF2U16_BG(&buf[20]));
	i += sprintf(&str[i], "ModelCycle=%d, ", BUF2U16_BG(&buf[22]));
	i += sprintf(&str[i], "VCTCount=%d, ", BUF2U16_BG(&buf[24]));
	i += sprintf(&str[i], "VCTCycle=%d, ", BUF2U16_BG(&buf[26]));
	i += sprintf(&str[i], "RelaxCycle=%d, ", BUF2U16_BG(&buf[28]));
	i += sprintf(&str[i], "RatioCycle=%d, ", BUF2U16_BG(&buf[30]));
	j = max(i, j);
	pr_err("SH366101_GaugeLog: GAUGEBLOCK2 is %s", str);

	/* Gauge Block 3 */
	ret = fg_read_block(sm, CMD_GAUGEBLOCK3, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_GAUGEBLOCK3, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "SFR_RC=%d, ", (s16)BUF2U16_BG(&buf[0]));
	i += sprintf(&str[i], "SFR_DCC=%d, ", (s16)BUF2U16_BG(&buf[2]));
	i += sprintf(&str[i], "SFR_ICC=%d, ", (s16)BUF2U16_BG(&buf[4]));
	i += sprintf(&str[i], "RCOffset=%d, ", (s16)BUF2U16_BG(&buf[6]));
	i += sprintf(&str[i], "C0DOD1=%d, ", (s16)BUF2U16_BG(&buf[8]));
	i += sprintf(&str[i], "PasCol=%d, ", (s16)BUF2U16_BG(&buf[10]));
	i += sprintf(&str[i], "PasEgy=%d, ", (s16)BUF2U16_BG(&buf[12]));
	i += sprintf(&str[i], "Qstart=%d, ", (s16)BUF2U16_BG(&buf[14]));
	i += sprintf(&str[i], "Estart=%d, ", (s16)BUF2U16_BG(&buf[16]));
	i += sprintf(&str[i], "FastTim=%d, ", buf[18]);
	i += sprintf(&str[i], "FILFLG=0x%02X, ", buf[19]);
	i += sprintf(&str[i], "StateTime=%d, ", BUF2U32_BG(&buf[20]));
	i += sprintf(&str[i], "StateHour=%d, ", BUF2U16_BG(&buf[24]));
	i += sprintf(&str[i], "StateSec=%d, ", BUF2U16_BG(&buf[26]));
	i += sprintf(&str[i], "OCVTim=%d, ", BUF2U16_BG(&buf[28]));
	i += sprintf(&str[i], "RaCalT1=%d, ", BUF2U16_BG(&buf[30]));
	j = max(i, j);
	pr_err("SH366101_GaugeLog: GAUGEBLOCK3 is %s", str);

	/* Gauge Block 4 */
	ret = fg_read_block(sm, CMD_GAUGEBLOCK4, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_GAUGEBLOCK4, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "GUInx=0x%02X, ", buf[0]);
	i += sprintf(&str[i], "GULoad=0x%02X, ", buf[1]);
	i += sprintf(&str[i], "GUStatus=0x%08X, ", BUF2U32_BG(&buf[2]));
	i += sprintf(&str[i], "EodLoad=%d, ", (s16)BUF2U16_BG(&buf[6]));
	i += sprintf(&str[i], "CRatio=%d, ", (s16)BUF2U16_BG(&buf[8]));
	i += sprintf(&str[i], "C0DOD0=%d, ", (s16)BUF2U16_BG(&buf[10]));
	i += sprintf(&str[i], "C0EOC=%d, ", (s16)BUF2U16_BG(&buf[12]));
	i += sprintf(&str[i], "C0EOD=%d, ", (s16)BUF2U16_BG(&buf[14]));
	i += sprintf(&str[i], "C0ACV=%d, ", (s16)BUF2U16_BG(&buf[16]));
	i += sprintf(&str[i], "ThemT=%d, ", (s16)BUF2U16_BG(&buf[18]));
	i += sprintf(&str[i], "Told=%d, ", (s16)BUF2U16_BG(&buf[20]));
	i += sprintf(&str[i], "Tout=%d, ", (s16)BUF2U16_BG(&buf[22]));
	i += sprintf(&str[i], "RCRaw=%d, ", (s16)BUF2U16_BG(&buf[24]));
	i += sprintf(&str[i], "FCCRaw=%d, ", (s16)BUF2U16_BG(&buf[26]));
	i += sprintf(&str[i], "RERaw=%d, ", (s16)BUF2U16_BG(&buf[28]));
	i += sprintf(&str[i], "FCERaw=%d, ", (s16)BUF2U16_BG(&buf[30]));
	j = max(i, j);
	pr_err("SH366101_GaugeLog: GAUGEBLOCK4 is %s", str);

	/* Gauge Block 5 */
	ret = fg_read_block(sm, CMD_GAUGEBLOCK5, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_GAUGEBLOCK5, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "IdealFCC=%d, ", (s16)BUF2U16_BG(&buf[0]));
	i += sprintf(&str[i], "IdealFCE=%d, ", (s16)BUF2U16_BG(&buf[2]));
	i += sprintf(&str[i], "FilRC=%d, ", (s16)BUF2U16_BG(&buf[4]));
	i += sprintf(&str[i], "FilFCC=%d, ", (s16)BUF2U16_BG(&buf[6]));
	i += sprintf(&str[i], "FSOC=%d, ",  buf[8]);
	i += sprintf(&str[i], "TrueRC=%d, ", (s16)BUF2U16_BG(&buf[9]));
	i += sprintf(&str[i], "TrueFCC=%d, ", (s16)BUF2U16_BG(&buf[11]));
	i += sprintf(&str[i], "RSOC=%d, ", buf[13]);
	i += sprintf(&str[i], "FilRE=%d, ", (s16)BUF2U16_BG(&buf[14]));
	i += sprintf(&str[i], "FilFCE=%d, ", (s16)BUF2U16_BG(&buf[16]));
	i += sprintf(&str[i], "FSOCW=%d, ", buf[18]);
	i += sprintf(&str[i], "TrueRE=%d, ", (s16)BUF2U16_BG(&buf[19]));
	i += sprintf(&str[i], "TrueFCE=%d, ", (s16)BUF2U16_BG(&buf[21]));
	i += sprintf(&str[i], "RSOCW=%d, ", buf[23]);
	i += sprintf(&str[i], "EquRC=%d, ", (s16)BUF2U16_BG(&buf[24]));
	i += sprintf(&str[i], "EquFCC=%d, ", (s16)BUF2U16_BG(&buf[26]));
	i += sprintf(&str[i], "EquRE=%d, ", (s16)BUF2U16_BG(&buf[28]));
	i += sprintf(&str[i], "EquFCE=%d, ", (s16)BUF2U16_BG(&buf[30]));
	j = max(i, j);
	pr_err("SH366101_GaugeLog: GAUGEBLOCK5 is %s", str);

	/* Gauge Block 6 */
	ret = fg_read_block(sm, CMD_GAUGEBLOCK6, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_GAUGEBLOCK6, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "GaugeS3=0x%04X, ", BUF2U16_BG(&buf[0]));
	i += sprintf(&str[i], "CCON1=0x%02X, ", buf[2]);
	i += sprintf(&str[i], "CCON2=0x%02X, ", buf[3]);
	i += sprintf(&str[i], "ModelS=0x%02X, ", buf[4]);
	i += sprintf(&str[i], "FGUpdate=0x%02X, ", buf[5]);
	i += sprintf(&str[i], "FMGrid=0x%02X, ", buf[6]);
	i += sprintf(&str[i], "ToggleCnt=%d, ", buf[7]);
	i += sprintf(&str[i], "ORUpdate=0x%02X, ", buf[8]);
	i += sprintf(&str[i], "UpState=0x%02X, ", buf[9]);
	i += sprintf(&str[i], "ChgVol=%d, ", (s16)BUF2U16_BG(&buf[10]));
	i += sprintf(&str[i], "TapCur=%d, ", (s16)BUF2U16_BG(&buf[12]));
	i += sprintf(&str[i], "ChgCur=%d, ", (s16)BUF2U16_BG(&buf[14]));
	i += sprintf(&str[i], "ChgRes=%d, ", (s16)BUF2U16_BG(&buf[16]));
	i += sprintf(&str[i], "PrevI=%d, ", (s16)BUF2U16_BG(&buf[18]));
	i += sprintf(&str[i], "DeltaC=%d, ", (s16)BUF2U16_BG(&buf[20]));
	i += sprintf(&str[i], "SOCJmpCnt=%d, ", buf[22]);
	i += sprintf(&str[i], "SOWJmpCnt=%d, ", buf[23]);
	i += sprintf(&str[i], "OcvVcell=%d, ", (s16)BUF2U16_BG(&buf[24]));
	i += sprintf(&str[i], "FGMeas=%d, ", (s16)BUF2U16_BG(&buf[26]));
	i += sprintf(&str[i], "FGPrid=%d, ", (s16)BUF2U16_BG(&buf[28]));
	i += sprintf(&str[i], "FastTime=%d, ", (s16)BUF2U16_BG(&buf[30]));
	j = max(i, j);
	pr_err("SH366101_GaugeLog: GAUGEBLOCK6 is %s", str);

	/* Gauge Fusion Model */
	ret = fg_read_block(sm, CMD_GAUGEBLOCK_FG, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_GAUGEBLOCK_FG, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "FusionModel=");
	for (ret = 0; ret < 15; ret++)
		i += sprintf(&str[i], "0x%04X ", BUF2U16_BG(&buf[ret * 2]));
	j = max(i, j); //20211115, Ethan
	pr_err("SH366101_GaugeLog: FusionModel is %s", str); //20211115, Ethan

	//20211111, Ethan
	/* CADC Info */
	ret = fg_read_block(sm, CMD_CADCINFO, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366101_GaugeLog: could not read CMD_CADCINFO, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	//20211116, Ethan. In case print twice
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "TEOFFSET=%d, ", (s16)BUF2U16_LT(&buf[0]));
	i += sprintf(&str[i], "UserOFFSET=%d, ", (s16)BUF2U16_LT(&buf[2]));
	i += sprintf(&str[i], "BoardOffset=%d, ", (s16)BUF2U16_LT(&buf[4]));
	i += sprintf(&str[i], "CADC25DEG=%d, ", (s16)BUF2U16_LT(&buf[6]));
	i += sprintf(&str[i], "CADCKR=%d, ", (s16)BUF2U16_LT(&buf[8]));
	i += sprintf(&str[i], "ChosenOffset=%d, ", (s16)BUF2U16_LT(&buf[10]));
	i += sprintf(&str[i], "TELiner=%d, ", (s16)BUF2U16_LT(&buf[12]));
	i += sprintf(&str[i], "UserLiner=%d, ", (s16)BUF2U16_LT(&buf[14]));
	i += sprintf(&str[i], "CurLiner=%d, ", (s16)BUF2U16_LT(&buf[16]));
	i += sprintf(&str[i], "COR=%d, ", (s16)BUF2U16_LT(&buf[18]));
	i += sprintf(&str[i], "CADC=%d, ", (s32)BUF2U32_LT(&buf[20]));
	i += sprintf(&str[i], "Current=%d, ", (s16)BUF2U16_LT(&buf[24]));
	i += sprintf(&str[i], "PackConfig=0x%04X, ", BUF2U16_LT(&buf[26]));
	j = max(i, j);
	pr_err("SH366101_GaugeLog: CADCINFO is %s", str);

	//j = max(i, j);
	//pr_err("SH366101_GaugeLog: FusionModel is %s", str);
	pr_err("SH366101_GaugeLog: max len=%d", j);

	ret = 0;

	/* fg_read_gaugeinfo_block_end: */
	return ret;
}
#endif

static s32 fg_read_soc(struct sh_fg_chip *sm)
{
	int ret;
	u16 data = 0;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_SOC], &data);
	if (ret < 0) {
		pr_err("could not read SOC, ret = %d\n", ret);
		return ret;
	}

	mutex_lock(&sm->data_lock);
	sm->batt_soc = (s32)data;
	mutex_unlock(&sm->data_lock);
	dev_dbg(sm->dev, "soc=%d\n", sm->batt_soc);

	return ret;
}

static u32 fg_read_ocv(struct sh_fg_chip *sm)
{
	int ret;
	u16 data = 0;
	u32 ocv;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_OCV], &data);
	if (ret < 0) {
		dev_err(sm->dev, "could not read OCV, ret = %d\n", ret);
		ocv = 4000 * MA_TO_UA;
	} else {
		ocv = data * MA_TO_UA;
	}
	mutex_lock(&sm->data_lock);
	sm->batt_ocv = ocv;
	mutex_unlock(&sm->data_lock);
	pr_debug("ocv=%d\n", sm->batt_ocv);
	return ret;
}

static s32 fg_read_temperature(struct sh_fg_chip *sm, enum sh_fg_temperature_type temperature_type)
{
	s32 ret;
	s32 temp = 0;
	u16 data = 0;

	if (temperature_type == TEMPERATURE_IN)
		temp = sm->regs[SH_FG_REG_TEMPERATURE_IN];
	else if (temperature_type == TEMPERATURE_EX)
		temp = sm->regs[SH_FG_REG_TEMPERATURE_EX];
	else
		return -EINVAL;

	ret = fg_read_sbs_word(sm, temp, &data);
	if (ret < 0) {
		dev_err(sm->dev, "could not read temperature, ret = %d\n", ret);
		return ret;
	}

	temp = (s32)data - 2731;
	dev_dbg(sm->dev, "fg read temp=%d\n", temp);
	mutex_lock(&sm->data_lock);
	if (temperature_type == TEMPERATURE_IN)
		sm->sh_temp = temp;
	else if (temperature_type == TEMPERATURE_EX)
		sm->batt_temp = temp;
	else
		ret = -EINVAL;
	mutex_unlock(&sm->data_lock);

	return ret;
}

static s32 fg_read_volt(struct sh_fg_chip *sm)
{
	s32 ret;
	s32 volt = 0;
	u16 data = 0;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_VOLTAGE], &data);
	if (ret < 0) {
		dev_err(sm->dev, "could not read voltage, ret = %d\n", ret);
		return ret;
	}
	volt = (s32)data * MA_TO_UA;

	/*cal avgvoltage*/
	mutex_lock(&sm->data_lock);
	sm->aver_batt_volt = (((sm->aver_batt_volt) * 4) + volt) / 5;
	sm->batt_volt = volt;
	mutex_unlock(&sm->data_lock);
	return ret;
}

static s32 fg_get_cycle(struct sh_fg_chip *sm)
{
	int ret;
	u16 data = 0;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_SOC_CYCLE], &data);
	if (ret < 0) {
		dev_err(sm->dev, "read cycle reg fail ret = %d\n", ret);
		data = 0;
	}

	mutex_lock(&sm->data_lock);
	sm->batt_soc_cycle = (s32)data;
	mutex_unlock(&sm->data_lock);
	return ret;
}

static s32 fg_read_current(struct sh_fg_chip *sm)
{
	int ret;
	u16 data = 0;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_CURRENT], &data);
	if (ret < 0) {
		dev_err(sm->dev, "could not read current, ret = %d\n", ret);
		return ret;
	}

	mutex_lock(&sm->data_lock);
	sm->batt_curr = (s32)((s16)data * MA_TO_UA);
	mutex_unlock(&sm->data_lock);
	if (sm->batt_curr > 3400000)
		dev_err(sm->dev, "batt current is too big batt_curr = %d\n",
			sm->batt_curr);
	return ret;
}

static s32 fg_read_fcc(struct sh_fg_chip *sm)
{
	int ret;
	u16 data = 0;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_BAT_FCC], &data);
	if (ret < 0) {
		dev_err(sm->dev, "could not read FCC, ret=%d\n", ret);
		return ret;
	}

	mutex_lock(&sm->data_lock);
	sm->batt_fcc = (s32)((s16)data * MA_TO_UA);
	mutex_unlock(&sm->data_lock);
	return ret;
}

static s32 fg_read_rmc(struct sh_fg_chip *sm)
{
	int ret;
	u16 data = 0;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_BAT_RMC], &data);
	if (ret < 0) {
		dev_err(sm->dev, "could not read RMC, ret=%d\n", ret);
		return ret;
	}

	mutex_lock(&sm->data_lock);
	sm->batt_rmc = (s32)((s16)data * MA_TO_UA);
	mutex_unlock(&sm->data_lock);
	return ret;
}

static s32 fg_read_designcap(struct sh_fg_chip *sm) /* 20211108, Ethan */
{
	int ret;
	u16 data = 0;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_DESIGN_CAPCITY], &data);
	if (ret < 0) {
		pr_err("could not read DesignCap, ret=%d\n", ret);
		return ret;
	}

	mutex_lock(&sm->data_lock);
	sm->batt_designcap = (s32)((s16)data * MA_TO_UA);
	mutex_unlock(&sm->data_lock);
	return ret;
}

#if 0
static s32 get_battery_status(struct sh_fg_chip *sm)
{
	union power_supply_propval ret = {0,};
	s32 rc;

	if (sm->batt_psy == NULL)
		sm->batt_psy = power_supply_get_by_name("battery");
	if (sm->batt_psy) {
		/* if battery has been registered, use the status property */
		rc = power_supply_get_property(sm->batt_psy, POWER_SUPPLY_PROP_STATUS, &ret);
		if (rc) {
			pr_err("Battery does not export status: %d\n", rc);
			return POWER_SUPPLY_STATUS_UNKNOWN;
		}
		return ret.intval;
	}

	/* Default to false if the battery power supply is not registered. */
	pr_err("battery power supply is not registered\n");
	return POWER_SUPPLY_STATUS_UNKNOWN;
}

static bool is_battery_charging(struct sh_fg_chip *sm)
{
	return get_battery_status(sm) == POWER_SUPPLY_STATUS_CHARGING;
}

static void fg_vbatocv_check(struct sh_fg_chip *sm)
{
	sm->p_batt_voltage = sm->batt_volt;
	sm->p_batt_current = sm->batt_curr;
}


static s32 fg_cal_carc (struct sh_fg_chip *sm)
{
	fg_vbatocv_check(sm);
	sm->is_charging = is_battery_charging(sm);

	return 1;
}
#endif

static s32 fg_get_batt_status(struct sh_fg_chip *sm)
{
	if (!sm->batt_present)
		return POWER_SUPPLY_STATUS_UNKNOWN;
	else if (sm->batt_fc)
		return POWER_SUPPLY_STATUS_FULL;
	else if (sm->batt_dsg)
		return POWER_SUPPLY_STATUS_DISCHARGING;
	else if (sm->batt_curr > 0)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_CHARGING;
}

static s32 fg_get_batt_capacity_level(struct sh_fg_chip *sm)
{
	if (!sm->batt_present)
		return POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
	else if (sm->batt_fc)
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (sm->batt_tc) /* [tc] always set when [fc] set */
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else if (sm->batt_socp) /* [soc1] always set when [socp] set */
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else if (sm->batt_soc1)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
}

static s32 fg_get_batt_health(struct sh_fg_chip *sm)
{
	if (!sm->batt_present)
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	else if (sm->batt_temp >= 600)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (sm->batt_temp >= 580 && sm->batt_ot < 600)
		return POWER_SUPPLY_HEALTH_HOT;
	else if (sm->batt_temp >= 450 && sm->batt_ot < 580)
		return POWER_SUPPLY_HEALTH_WARM;
	else if (sm->batt_temp >= 150 && sm->batt_ot < 450)
		return POWER_SUPPLY_HEALTH_GOOD;
	else if (sm->batt_temp >= 0 && sm->batt_ot < 150)
		return POWER_SUPPLY_HEALTH_COOL;
	else
		return POWER_SUPPLY_HEALTH_COLD;
}

static enum power_supply_property fg_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
};

static void fg_monitor_workfunc(struct work_struct *work);

static s32 fg_get_property(struct power_supply *psy, enum power_supply_property psp, union power_supply_propval *val)
{

	struct sh_fg_chip *sm = power_supply_get_drvdata(psy);
	s32 ret = 0;

	pr_debug("psp=%x", psp);

	switch (psp) {
   case POWER_SUPPLY_PROP_TYPE:
       val->intval = POWER_SUPPLY_TYPE_MAINS;
       break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = fg_get_batt_status(sm);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		if (sm->batt_ocv > 0)
			val->intval = sm->batt_ocv;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (sm->batt_volt > 0)
			val->intval = sm->batt_volt;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sm->batt_present;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = sm->batt_curr;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		if (sm->batt_soc >= 0)
			val->intval = sm->batt_soc;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = fg_get_batt_capacity_level(sm);
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = sm->batt_temp;
		break;

	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		val->intval = sm->sh_temp;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (sm->batt_fcc >= 0)
			val->intval = sm->batt_fcc;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = fg_get_batt_health(sm);
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW: /* 20211108, Ethan. */
		if (sm->batt_rmc >= 0)
			val->intval = sm->batt_rmc;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN: /* 20211108, Ethan. */
		if (sm->batt_designcap > 0)
			val->intval = sm->batt_designcap;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		if (sm->batt_designcap >= 0)
			val->intval = sm->batt_soc_cycle;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;

	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		if (sm->batt_designcap > 0)
			val->intval = (sm->batt_designcap / 100) * sm->nominal_voltage;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_ENERGY_FULL:
		if (sm->batt_fcc >= 0)
			val->intval = (sm->batt_fcc / 100) * sm->nominal_voltage;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_ENERGY_NOW:
		if (sm->batt_rmc >= 0)
			val->intval = (sm->batt_rmc / 100) * sm->nominal_voltage;
		else
			ret = -EINVAL;
		break;

	case POWER_SUPPLY_PROP_CALIBRATE:
		val->intval = sm->is_calied;
		if (sm->is_calied == false && sm->start_cali == false) {
			pm_stay_awake(sm->dev);
			sm->start_cali = true;
		}
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static s32 fg_set_property(struct power_supply *psy,
	enum power_supply_property prop, const union power_supply_propval *val)
{
#ifdef GF_GET_PROP /* 20211029, Ethan. */
	struct sh_fg_chip *sm = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_TEMP:
		sm->fake_temp = val->intval;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		sm->fake_soc = val->intval;
		power_supply_changed(sm->fg_psy);
		break;
	default:
		return -EINVAL;
	}
#endif
	return 0;
}

static s32 fg_prop_is_writeable(struct power_supply *psy, enum power_supply_property prop)
{
	s32 ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = 1;
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

static void fg_external_power_changed(struct power_supply *psy)
{
	struct sh_fg_chip *sm = power_supply_get_drvdata(psy);

	cancel_delayed_work(&sm->monitor_work);
	schedule_delayed_work(&sm->monitor_work, 0);
}

static s32 fg_psy_register(struct sh_fg_chip *sm)
{
	struct power_supply_config fg_psy_cfg = {};

	sm->fg_psy_d.name = "SH366101-0";
	sm->fg_psy_d.type = POWER_SUPPLY_TYPE_MAINS;
	sm->fg_psy_d.properties = fg_props;
	sm->fg_psy_d.num_properties = ARRAY_SIZE(fg_props);
	sm->fg_psy_d.get_property = fg_get_property;
	sm->fg_psy_d.set_property = fg_set_property;
	sm->fg_psy_d.external_power_changed = fg_external_power_changed;
	sm->fg_psy_d.property_is_writeable = fg_prop_is_writeable;

	fg_psy_cfg.drv_data = sm;
	fg_psy_cfg.num_supplicants = 0;

	sm->fg_psy = devm_power_supply_register(sm->dev, &sm->fg_psy_d, &fg_psy_cfg);
	if (IS_ERR(sm->fg_psy)) {
		pr_err("Failed to register fg_psy");
		return PTR_ERR(sm->fg_psy);
	}

	return 0;
}

static void fg_psy_unregister(struct sh_fg_chip *sm)
{
	power_supply_unregister(sm->fg_psy);
}

static ssize_t rm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	int ret, len;

	ret = fg_read_rmc(sm);
	if (ret)
		dev_err(sm->dev, "fg read rmc fail ret = %d\n", ret);
	len = snprintf(buf, MAX_BUF_LEN, "%d\n", sm->batt_rmc);

	return len;
}

static ssize_t fcc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	int ret, len;

	ret = fg_read_fcc(sm);
	if (ret)
		dev_err(sm->dev, "fg read fcc fail ret = %d\n", ret);
	len = snprintf(buf, MAX_BUF_LEN, "%d\n", sm->batt_fcc);

	return len;
}

static ssize_t batt_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	int ret, len;

	ret = fg_read_volt(sm);
	if (ret)
		dev_err(sm->dev, "fg read volt fail ret = %d\n", ret);
	len = snprintf(buf, MAX_BUF_LEN, "%d\n", sm->batt_volt);

	return len;
}

#ifdef I2C_DEBUG
static ssize_t i2c_transfer_err_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len;

	len = snprintf(buf, MAX_BUF_LEN, "i2c err times : %d\n", g_I2c_Transfer_Err_Cnt);

	return len;
}

/*
 * static ssize_t i2c_bus_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len;
	struct i2c_client *client = to_i2c_client(dev);
	//struct dw_i2c_dev *i2c_dev = i2c_get_adapdata(client->adapter);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	len = snprintf(buf, MAX_BUF_LEN, "read sm i2c IC_DATA_CMD 0x10: %x\n"
					"                        0x30: %x\n"
					"                        0x3c: %x\n"
					"                        0x38: %x\n"
					"                        0x34: %x\n"
					"                        0x2c: %x\n"
					"                        0x60: %x\n"
					"                        0x64: %x\n"
					"                        0x6c: %x\n"
					"                        0x70: %x\n",
					sh_read32(sm->i2c_bus_base + 0x10),
					sh_read32(sm->i2c_bus_base + 0x30),
					sh_read32(sm->i2c_bus_base + 0x3c),
					sh_read32(sm->i2c_bus_base + 0x38),
					sh_read32(sm->i2c_bus_base + 0x34),
					sh_read32(sm->i2c_bus_base + 0x2c),
					sh_read32(sm->i2c_bus_base + 0x60),
					sh_read32(sm->i2c_bus_base + 0x64),
					sh_read32(sm->i2c_bus_base + 0x6c),
					sh_read32(sm->i2c_bus_base + 0x70));
	return len;
}

static ssize_t i2c_bus_debug_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	//struct dw_i2c_dev *i2c_dev = i2c_get_adapdata(client->adapter);
	u32 val;
	u32 offset;
	u32 drect;
	int error;

	error = kstrtouint(buf, 16, &val);
	if (error)
		return error;

	drect = val & 0x0000ffff;
	offset = (val & 0xffff0000) >> 16;
	mutex_lock(&sm->i2c_rw_lock);
	if (drect == 0xffff) {
		pr_info("i2c bus read offset: 0x%x = 0x%x\n", offset,
			sh_read32(sm->i2c_bus_base + offset));
	} else {
		pr_info("write i2c bus offset : 0x%x == val : 0x%x\n",
			offset, val & 0x0000ffff);
		sh_write32(sm->i2c_bus_base + offset, val & 0x0000ffff);
	}
	mutex_unlock(&sm->i2c_rw_lock);
	return len;
}
*/
#endif
#ifdef FG_INFO_DEBUG
static ssize_t info_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len;
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	len = snprintf(buf, MAX_BUF_LEN, "sm info debug : %d\n", sm->is_debug_open);

	return len;
}

static ssize_t info_debug_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	if (val)
		sm->is_debug_open = true;
	else
		sm->is_debug_open = false;

	return len;
}
#endif

static ssize_t lifetime_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* Lifetime Info. */
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	int ret, len;
	char val[12];

	memset(val, 0, 12);
	ret = fg_read_block(sm, CMD_LIFETIMEADC, LEN_LIFETIMEADC, val);
	if (ret < 0) {
		pr_err("could not read LIFETIMEADC, ret = %d\n", ret);
		return ret;
	}

	len = snprintf(buf, MAX_BUF_LEN,
			"LT_MaxVolt : %d\n"
			"LT_MinVolt : %d\n"
			"LT_MaxChgCUR : %d\n"
			"LT_MaxDsgCUR : %d\n"
			"LT_MaxTemper : %d\n"
			"LT_MinTemper : %d\n"
			"LT_MaxIntTemper : %d\n"
			"LT_MinIntTemper : %d\n",
			(s16)BUF2U16_BG(&val[0]), (s16)BUF2U16_BG(&val[2]),
			(s16)BUF2U16_BG(&val[4]), (s16)BUF2U16_BG(&val[6]),
			(s8)val[8], (s8)val[9], (s8)val[10], (s8)val[11]);
	return len;
}

static ssize_t sh_cali_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);

	if ((val > 0) && (val < 3)) {
		cancel_delayed_work_sync(&sm->monitor_work);
		mdelay(50);

		if (val == 1)
			fg_gauge_enable_autocali(sm, false);
		else
			fg_gauge_enable_autocali(sm, true);
		pr_err("please shutdown to cali fg!!\n");
	}

	return len;
}

static ssize_t sh_cali_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	int len;

	if (!sm->is_calied)
		len = snprintf(buf, MAX_BUF_LEN,
				"FG is un-calibrated, need calibration!!\n");
	else
		len = snprintf(buf, MAX_BUF_LEN,
				"FG has been calibrated!!\n");

	return len;
}

static ssize_t sh_i2c_reset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);

	if (val > 0) {
		mutex_lock(&sm->i2c_rw_lock);
		fg_reset_sh_i2cbus(sm);
		mutex_unlock(&sm->i2c_rw_lock);
	}

	return len;
}

static DEVICE_ATTR_RO(rm);
static DEVICE_ATTR_RO(fcc);
static DEVICE_ATTR_RO(batt_volt);
#ifdef I2C_DEBUG
static DEVICE_ATTR_RO(i2c_transfer_err_cnt);
//static DEVICE_ATTR_RW(i2c_bus_debug);
#endif
#ifdef FG_INFO_DEBUG
static DEVICE_ATTR_RW(info_debug);
#endif
static DEVICE_ATTR_RO(lifetime_info);
static DEVICE_ATTR_RW(sh_cali);
static DEVICE_ATTR_WO(sh_i2c_reset);

static struct attribute *fg_attributes[] = {
	&dev_attr_rm.attr,
	&dev_attr_fcc.attr,
	&dev_attr_batt_volt.attr,
#ifdef I2C_DEBUG
	&dev_attr_i2c_transfer_err_cnt.attr,
	//&dev_attr_i2c_bus_debug.attr,
#endif
#ifdef FG_INFO_DEBUG
	&dev_attr_info_debug.attr,
#endif
	&dev_attr_lifetime_info.attr,
	&dev_attr_sh_cali.attr,
	&dev_attr_sh_i2c_reset.attr,
	NULL,
};

static const struct attribute_group fg_attr_group = {
	.attrs = fg_attributes,
};

#if ENABLE_CHGBLOCK
/* 20211112, Ethan. Charge Status */
static s32 fg_read_chargestatus(struct sh_fg_chip *sm)
{
	int ret;
	u8 buf[LEN_CHARGESTATUS];
	enum sh_fg_charge_temper_range temper_range;
	enum sh_fg_charge_degrade_flag degrade_flag;

	ret = fg_read_block(sm, CMD_CHARGESTATUS, LEN_CHARGESTATUS, buf);
	if (ret < 0) {
		pr_err("could not read Charge Status, ret=%d\n", ret);
		return ret;
	}

	switch (buf[0]) {
	case 0x01:
		temper_range = TEMPER_RANGE_BELOW_T1;
		break;
	case 0x02:
		temper_range = TEMPER_RANGE_T1T2;
		break;
	case 0x04:
		temper_range = TEMPER_RANGE_T2T3;
		break;
	case 0x08:
		temper_range = TEMPER_RANGE_T3T4;
		break;
	case 0x10:
		temper_range = TEMPER_RANGE_T4T5;
		break;
	case 0x20:
		temper_range = TEMPER_RANGE_T5T6;
		break;
	case 0x40:
		temper_range = TEMPER_RANGE_ABOVE_T6;
		break;
	default:
		pr_err("Charge Status Temper Range Error, value=0x%02X\n", buf[0]);
		return -1;
	}

	switch (buf[1] & MASK_CHARGESTATUS_DEGRADE) {
	case 0x00:
		degrade_flag = DEGRADE_PHASE_0;
		break;
	case 0x01:
		degrade_flag = DEGRADE_PHASE_1;
		break;
	case 0x02:
		degrade_flag = DEGRADE_PHASE_2;
		break;
	case 0x03:
		degrade_flag = DEGRADE_PHASE_3;
		break;
	case 0x04:
		degrade_flag = DEGRADE_PHASE_4;
		break;
	default:
		pr_err("Charge Status Degrade flag Error, value=0x%02X\n",
			(buf[1] & MASK_CHARGESTATUS_DEGRADE));
		return -1;
	}

	mutex_lock(&sm->data_lock);
	sm->temper_range = temper_range;
	sm->degrade_flag = degrade_flag;
	mutex_unlock(&sm->data_lock);
	return 0;
}

/* 20211112, Ethan. Charge Status */
static enum sh_fg_charge_temper_range fg_get_charge_temper_range(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	fg_read_chargestatus(sm);
	return sm->temper_range;
}

/* 20211112, Ethan. Charge Status */
static enum sh_fg_charge_degrade_flag fg_get_charge_degrade_flag(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	fg_read_chargestatus(sm);
	return sm->degrade_flag;
}

/* 20211112, Ethan. Charge Status */
static s32 fg_set_charging_voltage(struct device *dev,
	enum sh_fg_charge_temper_range temper_range, s32 charging_voltage)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	s32 ret;
	u8 buf[LEN_CHARGEVOLTAGES];

	/* check input */
	switch (temper_range) {
	case TEMPER_RANGE_T1T2:
	case TEMPER_RANGE_T2T3:
	case TEMPER_RANGE_T3T4:
	case TEMPER_RANGE_T4T5:
	case TEMPER_RANGE_T5T6:
		break;
	default:
		pr_err("error! temper_range input error");
		return -1;
	}

	if (charging_voltage <= 0) {
		pr_err("error! charging_voltage input error");
		return -1;
	}

	/* read block */
	ret = fg_read_block(sm, CMD_CHARGEVOLTAGES, LEN_CHARGEVOLTAGES, buf);
	if (ret < 0) {
		pr_err("error! Could not read charge-voltge block!, ret=%d", ret);
		return ret;
	}

	/* modify block. little endian */
	ret = ((s32)temper_range << 1);
	buf[ret] = (u8)(charging_voltage & 0xFF);
	buf[ret + 1] = (u8)((charging_voltage >> 8) & 0xFF);

	/* write block */
	ret = fg_write_block(sm, CMD_CHARGEVOLTAGES, LEN_CHARGEVOLTAGES, buf);
	if (ret < 0) {
		pr_err("error! Could not write charge-voltge block!, ret=%d", ret);
		return ret;
	}

	return 0;
}
#endif

#if ENABLE_TERMBLOCK
/* 20211112, Ethan. Termniate Voltage */
static s32 fg_read_terminate_voltage(struct sh_fg_chip *sm)
{
	int ret;
	u8 buf[LEN_TERMINATEVOLT];

	ret = fg_read_block(sm, CMD_TERMINATEVOLT, LEN_TERMINATEVOLT, buf);
	if (ret < 0) {
		pr_err("could not read Terminate Voltage, ret=%d\n", ret);
		return ret;
	}

	ret = (s32)(buf[0] | ((u16)buf[1] << 8));
	mutex_lock(&sm->data_lock);
	sm->terminate_voltage = ret * MA_TO_UA; /* 20211208, Ethan */
	mutex_unlock(&sm->data_lock);
	return 0;
}

/* 20211112, Ethan. Termniate Voltage */
static s32 fg_get_terminate_voltage(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	fg_read_terminate_voltage(sm);
	return sm->terminate_voltage;
}

/* 20211112, Ethan. Termniate Voltage */
static s32 fg_set_terminate_voltage(struct device *dev, s32 terminate_voltage)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	s32 ret;
	u8 buf[LEN_TERMINATEVOLT];

	/* check input */
	if (terminate_voltage <= 0) {
		pr_err("error! terminate_voltage input error");
		return -1;
	}

	/* read block */
	ret = fg_read_block(sm, CMD_TERMINATEVOLT, LEN_TERMINATEVOLT, buf);
	if (ret < 0) {
		pr_err("error! Could not read terminate_voltage block!, ret=%d", ret);
		return ret;
	}

	/* modify block. little endian */
	buf[0] = (u8)(terminate_voltage & 0xFF);
	buf[1] = (u8)((terminate_voltage >> 8) & 0xFF);

	/* write block */
	ret = fg_write_block(sm, CMD_TERMINATEVOLT, LEN_TERMINATEVOLT, buf);
	if (ret < 0) {
		pr_err("error! Could not write terminate_voltageblock!, ret=%d", ret);
		return ret;
	}

	return 0;
}
#endif

/* 20211113, Ethan */
static s32 fg_get_user_info(struct device *dev, s32 length, u8 *pbuf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	s32 ret;

	if (length > LEN_USERBUFFER)
		length = LEN_USERBUFFER;

	ret = fg_read_block(sm, CMD_USERBUFFER, length, pbuf);
	if (ret < 0) {
		pr_err("error! ret=%d", ret);
		return ret;
	}

	return 0;
}

/* 20211113, Ethan */
static s32 fg_set_user_info(struct device *dev, s32 length, u8 *pbuf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	s32 ret;

	if (length > LEN_USERBUFFER)
		length = LEN_USERBUFFER;

	ret = fg_write_block(sm, CMD_USERBUFFER, length, pbuf);
	if (ret < 0) {
		pr_err("fg_get_user_info error! ret=%d", ret);
		return ret;
	}

	return 0;
}

static s32 fg_check_ocv(struct sh_fg_chip *sm)
{
	if (sm == NULL || sm->fullbatt_vol <= 4200000)
		return 0;

	if (sm->batt_ocv >= sm->fullbatt_vol
		&& sm->batt_soc < 100) {
		pr_err("sh batt ocv is bigger than fullbatt vol(%d >= %d)",
			sm->batt_ocv, sm->fullbatt_vol);
		if (sm->batt_soc > 40)
			sm->batt_ocv = sm->fullbatt_vol - ((100 - sm->batt_soc) * 10000);
		else
			sm->batt_ocv = 3400000 + sm->batt_soc * 10000;
	}

	return 0;
}

static s32 fg_reset_sh_i2cbus(struct sh_fg_chip *sm)
{
	unsigned int v_sda, v_scl;

	v_sda = sh_read32(sm->i2c_pinctl_base + I2C_6_SDA_PINCTL);
	v_scl = sh_read32(sm->i2c_pinctl_base + I2C_6_SCL_PINCTL);
	gpio_request(I2C_6_SCL_GPIO, "i2c_6_scl");
	gpio_request(I2C_6_SDA_GPIO, "i2c_6_sda");
	gpio_direction_output(I2C_6_SCL_GPIO, 0);
	gpio_direction_output(I2C_6_SDA_GPIO, 0);
	msleep(2500);
	gpio_free(I2C_6_SCL_GPIO);
	gpio_free(I2C_6_SDA_GPIO);
	sh_write32(sm->i2c_pinctl_base + I2C_6_SDA_PINCTL, v_sda);
	sh_write32(sm->i2c_pinctl_base + I2C_6_SCL_PINCTL, v_scl);
	pr_err("reset sh i2c bus done!!\n");

	return 0;
}

static s32 fg_check_i2cbus(struct sh_fg_chip *sm)
{
	int ret = 0;
	int status = 0;

	ret = fg_get_device_id(sm->client);
	if (ret < 0) {
		mutex_lock(&sm->i2c_rw_lock);
		status = sh_read32(sm->i2c_bus_base + 0x70);
		if (status & 0x1)
			sh_write32(sm->i2c_bus_base + 0x10, 0x300);
		else if (ret == -121)
			fg_reset_sh_i2cbus(sm);

		mutex_unlock(&sm->i2c_rw_lock);
		pr_err("i2c bus status err = %d ; i2c status = 0x%x!!\n",
			ret, status);
	}
	return 0;
}

static s32 fg_refresh_status(struct sh_fg_chip *sm)
{
	bool last_batt_inserted = sm->batt_present;
	bool last_batt_fc = sm->batt_fc;
	bool last_batt_ot = sm->batt_ot;
	bool last_batt_ut = sm->batt_ut;
	s32 last_soc = sm->batt_soc;
	s32 last_curr = sm->batt_curr;

	int ret = 0;

	if (sm->is_suspend)
		return 0;

	ret = fg_read_status(sm);
	CHECK_RETURN(ret)
	dev_dbg(sm->dev, "batt_present=%d", sm->batt_present);

	if (!last_batt_inserted && sm->batt_present) { /* battery inserted */
		dev_err(sm->dev, "Battery inserted\n");
	} else if (last_batt_inserted && !sm->batt_present) { /* battery removed */
		dev_err(sm->dev, "Battery removed\n");
		sm->batt_soc = -ENODATA;
		sm->batt_fcc = -ENODATA;
		sm->batt_volt = -ENODATA;
		sm->batt_curr = -ENODATA;
		sm->batt_temp = -ENODATA;
		sm->sh_temp = -ENODATA;
		sm->batt_ocv = -ENODATA;
	}

	if (sm->batt_present) {
#ifdef FG_INFO_DEBUG
		ret = fg_read_gaugeinfo_block(sm, 0);
		CHECK_RETURN(ret)
#endif
		ret = fg_read_ocv(sm);
		CHECK_RETURN(ret)
		ret = fg_read_soc(sm);
		CHECK_RETURN(ret)
		ret = fg_read_volt(sm);
		CHECK_RETURN(ret)
		ret = fg_read_current(sm);
		CHECK_RETURN(ret)
		ret = fg_get_cycle(sm);
		CHECK_RETURN(ret)
		ret = fg_read_rmc(sm);
		CHECK_RETURN(ret)
		ret = fg_read_fcc(sm);
		CHECK_RETURN(ret)
		ret = fg_read_designcap(sm);
		CHECK_RETURN(ret)
		if (sm->en_temp_in) {
			ret = fg_read_temperature(sm, TEMPERATURE_IN);
			CHECK_RETURN(ret)
		}
		if (sm->en_temp_ex) {
			ret = fg_read_temperature(sm, TEMPERATURE_EX);
			CHECK_RETURN(ret)
		}

		ret = fg_check_ocv(sm);
		CHECK_RETURN(ret)

		if (last_batt_inserted && (last_soc == 0)
			&& (last_curr < 0) && (sm->batt_curr < 0))
			sm->batt_soc = 0;
#ifdef FG_INFO_DEBUG
		if (sm->is_debug_open) {
			pr_err("RSOC:%d, Volt:%d, OCV:%d, Current:%d, Temperature:%d\n",
				sm->batt_soc, sm->batt_volt, sm->batt_ocv,
				sm->batt_curr, sm->batt_temp);
			pr_err("RM:%d, FC:%d, FAST:%d\n",
				sm->batt_rmc, sm->batt_fcc, sm->fast_mode);
		} else {
			dev_dbg(sm->dev, "RSOC:%d, Volt:%d, Current:%d, Temperature:%d\n",
				sm->batt_soc, sm->batt_volt, sm->batt_curr, sm->batt_temp);
			dev_dbg(sm->dev, "RM:%d, FC:%d, FAST:%d\n", sm->batt_rmc,
				sm->batt_fcc, sm->fast_mode);
		}
#endif
		if (sm->force_updata)
			pr_info("sh fg force updata!\n");

		if ((last_soc != sm->batt_soc)
			|| (last_batt_inserted != sm->batt_present)
			|| (last_batt_fc != sm->batt_fc)
			|| (last_batt_ot != sm->batt_ot)
			|| (last_batt_ut != sm->batt_ut)
			|| (sm->force_updata == true)) {
			if (sm->fg_psy && sm->is_suspend == false) {
				sm->force_updata = false;
				power_supply_changed(sm->fg_psy);
			}
		}
	}

	return 0;
}

#define SH366101_FFC_TERM_WAM_TEMP              350
#define SH366101_COLD_TEMP_TERM                 0
#define BAT_FULL_CHECK_TIME                     1

static s32 fg_check_full_status(struct sh_fg_chip *sm)
{
	return 0;
}

static s32 fg_check_recharge_status(struct sh_fg_chip *sm)
{
	return 0;
}

static s32 fg_gauge_calibrate_board(struct sh_fg_chip *sm) /* 20211228, Ethan */
{
#define CALI_ERR_NONE 0
#define CALI_ERR_COMM -1
#define CALI_ERR_LARGE_CURR -2
#define CALI_ERR_CALIMODE -3
#define CAIL_ERR_USER_CORRUPT -4

#define WAIT_CALI_BOARD 1250
#define CURRENT_NO_LOAD (100 * MA_TO_UA) /* 20220101, Ethan */

#define LEN_ENTERCALI 6
#define CMD_ENTERCALI (CMDMASK_ALTMAC_W | 0xE014)
	u8 BUF_ENTERCALI[LEN_ENTERCALI] = { 0x45, 0x54, 0x43, 0x41, 0x4C, 0x49 };

#define LEN_EXITCALI 6
#define CMD_EXITCALI (CMDMASK_ALTMAC_W | 0xE015)
	u8 BUF_EXITCALI[LEN_EXITCALI] = { 0x65, 0x78, 0x63, 0x61, 0x6C, 0x69 };

#define LEN_CALIBOARD 9
#define CMD_CALIBOARD (CMDMASK_ALTMAC_W | 0xE016)
	u8 BUF_CALIBOARD[LEN_CALIBOARD] = { 0x43, 0x61, 0x4C, 0x69, 0x42, 0x6F, 0x61, 0x72, 0x64 };

#define FLAG_CALIED_FLAG 0x0155
#define LEN_CALIED_FLAG 2
#define CMD_CALIED_FLAG (CMDMASK_ALTMAC_W | 0x4042)
#define CMD_BOARDOFFSET (CMDMASK_ALTMAC_W | 0x4044)
#define VAL_MAX_BOARDOFFSET (2000)
	u8 BUF_CALIED_FLAG[32];

	s32 ret;
	u16 oemflag = 0;
	u32 oemflag_cali = 0;
	s32 retry_cnt;
	s16 boardoffset = 0;
	u16 boardflag = 0;

	ret = fg_read_current(sm);
	if (ret < 0) {
		pr_err("Comm error! cannot read 1st current!"); //20220104, Ethan
		ret = CALI_ERR_COMM;
		goto fg_gauge_calibrate_board_end;
	}
	if (abs(sm->batt_curr) >= CURRENT_NO_LOAD) {
		pr_err("current error! current too large!"); //20220104, Ethan
		ret = CALI_ERR_LARGE_CURR;
		goto fg_gauge_calibrate_board_end;
	}

	ret = fg_gauge_unseal(sm);
	if (ret < 0) {
		pr_err("Comm error! cannot 1st unseal!"); //20220104, Ethan
		ret = CALI_ERR_COMM;
		goto fg_gauge_calibrate_board_exitcali; /* retry may result ic in cali */
	}
	HOST_DELAY(CMD_SBS_DELAY);

	ret = fg_read_block(sm, CMD_BOARDOFFSET, 32, BUF_CALIED_FLAG);
	if (ret < 0) {
		pr_err("Comm error! cannot read 1st cali flag!"); //20220104, Ethan
		ret = CALI_ERR_COMM;
		goto fg_gauge_calibrate_board_exitcali; /* retry may result ic in cali */
	}
	boardoffset = (s16)BUF2U16_BG(BUF_CALIED_FLAG);

	for (retry_cnt = 0; retry_cnt < 5; retry_cnt++) {
		oemflag_cali = 0;
		oemflag = 0;
		boardflag = 0;
		HOST_DELAY(CMD_SBS_DELAY << 2);

		ret = fg_gauge_unseal(sm);
		if (ret < 0) {
			pr_err("Comm error! cannot 2nd unseal!"); //20220104, Ethan
			ret = CALI_ERR_COMM;
			goto fg_gauge_calibrate_board_exitcali; /* retry may result ic in cali */
		}
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_block(sm, CMD_CALIED_FLAG, 32, BUF_CALIED_FLAG);
		if (ret < 0) {
			pr_err("Comm error! cannot read 2nd cali flag!"); //20220104, Ethan
			ret = CALI_ERR_COMM;
			goto fg_gauge_calibrate_board_exitcali; /* retry may result ic in cali */
		}
		boardflag = BUF2U16_BG(BUF_CALIED_FLAG);
		if (boardflag == FLAG_CALIED_FLAG) {
			ret = CALI_ERR_NONE;
			goto fg_gauge_calibrate_board_exitcali; /* retry may result ic in cali */
		}
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_write_block(sm, CMD_ENTERCALI, LEN_ENTERCALI, BUF_ENTERCALI);
		if (ret < 0) {
			pr_err("Comm error! cannot enter cali mode!"); //20220104, Ethan
			ret = CALI_ERR_COMM;
			goto fg_gauge_calibrate_board_exitcali;
		}
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_sbs_word(sm, CMD_OEMFLAG, &oemflag);
		if (ret < 0) {
			pr_err("Comm error! cannot read oem flag!"); //20220104, Ethan
			ret = CALI_ERR_COMM; //20220104, Ethan
			goto fg_gauge_calibrate_board_exitcali;
		}
		oemflag_cali = !!((oemflag & CMD_MASK_OEM_CALI) == CMD_MASK_OEM_CALI);
		if (!oemflag_cali) //enter cali fail
			continue;

		ret = fg_write_block(sm, CMD_CALIBOARD, LEN_CALIBOARD, BUF_CALIBOARD);
		if (ret < 0) {
			pr_err("Comm error! cannot cali board!"); //20220104, Ethan
			ret = CALI_ERR_COMM;
			goto fg_gauge_calibrate_board_exitcali;
		}
		HOST_DELAY(WAIT_CALI_BOARD);

		ret = fg_read_current(sm);
		if (ret < 0) {
			pr_err("Comm error! cannot read 2nd current!"); //20220104, Ethan
			ret = CALI_ERR_COMM;
			goto fg_gauge_calibrate_board_exitcali;
		}
		if (sm->batt_curr != 0) {
			pr_err("User Corrupt! calied current too large! =%d", sm->batt_curr);
			ret = CAIL_ERR_USER_CORRUPT;
			continue;
		}

		ret = fg_read_block(sm, CMD_BOARDOFFSET, 32, BUF_CALIED_FLAG);
		if (ret < 0) {
			pr_err("Comm error! cannot read board2!"); //20220104, Ethan
			ret = CALI_ERR_COMM;
			goto fg_gauge_calibrate_board_exitcali; /* retry may result ic in cali */
		}
		ret = (s32)((s16)BUF2U16_BG(BUF_CALIED_FLAG));
		if (abs(ret) < VAL_MAX_BOARDOFFSET) {
			ret = CALI_ERR_NONE;
			break;
		}
		pr_err("User Corrupt! board2 too large! =%d", ret);
		ret = CAIL_ERR_USER_CORRUPT;
	}
	if (!oemflag_cali) {
		ret = CALI_ERR_CALIMODE;
		goto fg_gauge_calibrate_board_end;
	}

fg_gauge_calibrate_board_exitcali:
	if (ret == CALI_ERR_NONE) { //cali ok
		BUF_CALIED_FLAG[0] = (u8)(FLAG_CALIED_FLAG >> 8);
		BUF_CALIED_FLAG[1] = (u8)(FLAG_CALIED_FLAG);
		fg_write_block(sm, CMD_CALIED_FLAG, LEN_CALIED_FLAG, BUF_CALIED_FLAG);
		HOST_DELAY(CMD_SBS_DELAY);
	} else { //cali fail. restore boardoffset
		BUF_CALIED_FLAG[0] = (u8)(boardoffset >> 8);
		BUF_CALIED_FLAG[1] = (u8)(boardoffset);
		fg_write_block(sm, CMD_BOARDOFFSET, LEN_CALIED_FLAG, BUF_CALIED_FLAG);
		HOST_DELAY(CMD_SBS_DELAY);
	}
	/* exit cali and force update e2 */
	ret |= fg_write_block(sm, CMD_EXITCALI, LEN_EXITCALI, BUF_EXITCALI);
	if (ret < 0) {
		pr_err("Comm error! cannot exit cali!"); //20220104, Ethan
		ret = CALI_ERR_COMM;
		goto fg_gauge_calibrate_board_end;
	}
	HOST_DELAY(DELAY_WRITE_E2ROM);

	//ret = CALI_ERR_NONE;

fg_gauge_calibrate_board_end:
	fg_gauge_seal(sm);

	pr_info("cali done: ret=%d, curr=%d, OEMFlag=0x%04X, OEMFlag_Cali=%u, boardflag=%04X",
		ret, sm->batt_curr, oemflag, oemflag_cali, boardflag);

	return ret;
}
static s32 fg_gauge_check_sfr(struct sh_fg_chip* sm) /* 20230605, Ethan */
{
	s16 sfr_rc;
	int ret;
	u8 buf[GAUGEINFO_LEN];

	ret = fg_read_block(sm, CMD_GAUGEBLOCK3, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("fg_gauge_check_sfr: could not read CMD_GAUGEBLOCK3, ret = %d\n", ret);
		goto fg_gauge_check_sfr_end;
	}

	sfr_rc = (s16)BUF2U16_BG(&buf[0]);
	if ((sfr_rc <= -32768) || (sfr_rc >= 32767)) { //in case conflict with V01.04
		ret = fg_gauge_unseal(sm);
		if (ret < 0) {
			pr_err("fg_gauge_check_sfr: unseal ic fail! ret = %d\n", ret);
			goto fg_gauge_check_sfr_end;
		}

		ret = fg_write_sbs_word(sm, CMD_ALTMAC, (u16)CMD_ENABLE_GAUGE);
		if (ret < 0) {
			pr_err("fg_gauge_check_sfr: reset gauge fail! ret = %d\n", ret);
			goto fg_gauge_check_sfr_end;
		}
		HOST_DELAY(DELAY_ENABLE_GAUGE);

		pr_err("fg_gauge_check_sfr end. sfr_rc=%d, need reset!", sfr_rc);
	} else {
		pr_err("fg_gauge_check_sfr end. sfr_rc=%d, donot need reset!", sfr_rc);
	}

fg_gauge_check_sfr_end:
	fg_write_sbs_word(sm, CMD_ALTMAC, (u16)CMD_SEAL);
	pr_err("fg_gauge_check_sfr end. ret=%d\n", ret);
	return ret;
}

static s32 fg_gauge_check_sfr_entry(struct sh_fg_chip* sm) /* 20230605, Ethan */
{
	int i;
	int ret;

	for (i = 0; i < 5; i++) {
		ret = -1;
		if (mutex_trylock(&sm->cali_lock)) {
			ret = fg_gauge_check_sfr(sm);
			mutex_unlock(&sm->cali_lock);
		} else {
			pr_err("fg_gauge_check_sfr: could not get mutex!, retry=%d\n", i);
		}

		if (ret >= 0)
			break;
		HOST_DELAY(200);
	}
        return ret;
}
static void fg_monitor_workfunc(struct work_struct *work)
{
	struct sh_fg_chip *sm = container_of(work, struct sh_fg_chip, monitor_work.work);
	s32 interval = GAUGE_REFRESH_SPAN;
	s32 ret = 0;

	//mutex_lock(&sm->data_lock);
	//ret = fg_init(sm->client);
	//mutex_unlock(&sm->data_lock);

	//if (ret) //read error, retry after 1 sec
	//	interval = 1;
	//else
#ifdef FG_INFO_DEBUG
	if (sm->is_calied == false)
		pr_err("sh fg does not conduct 0 current calibration, please calibrate in time\n");
#endif
	ret = fg_refresh_status(sm);

	if (ret) { //read error, retry after 1 sec
		fg_check_i2cbus(sm);
		interval = 1;
	} else if (sm->start_cali == true) {
		ret = fg_gauge_calibrate_board(sm);
		if (ret == 0)
			sm->is_calied = true;
		sm->start_cali = false;
		pm_relax(sm->dev);
	}

	if (sm->is_suspend == false)
		schedule_delayed_work(&sm->monitor_work, interval * HZ);
}

static s32 fg_get_device_id(struct i2c_client *client)
{
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	s32 ret;
	u16 data;

	ret = fg_read_sbs_word(sm, sm->regs[SH_FG_REG_DEVICE_ID], &data);
	if (ret < 0) {
		pr_err("Failed to read DEVICE_ID, ret = %d\n", ret);
		return ret;
	}

	pr_info("device_id = 0x%04X\n", data);
	return ret;
}

static s32 fg_init(struct i2c_client *client)
{
	s32 ret;

	/*sh366101 i2c read check*/
	ret = fg_get_device_id(client);
	if (ret < 0)
		pr_err("%s: fail read device ID(%d)\n", __func__, ret);

	return ret;
}

#define PROPERTY_NAME_SIZE 128
static s32 fg_common_parse_dt(struct sh_fg_chip *sm)
{
	struct device *dev = &sm->client->dev;
	struct device_node *np = dev->of_node;

	WARN_ON(dev == 0);
	WARN_ON(np == 0);

	sm->gpio_int = of_get_named_gpio(np, "fg-irq-gpio", 0);
#ifdef FG_INFO_DEBUG
	pr_info("gpio_int=%d\n", sm->gpio_int);
#endif
	if (!gpio_is_valid(sm->gpio_int)) {
		pr_err("gpio_int is not valid\n");
		sm->gpio_int = -EINVAL;
	}

	/* EN TEMP EX/IN */
	if (of_property_read_bool(np, "sm,en_temp_ex"))
		sm->en_temp_ex = true;
	else
		sm->en_temp_ex = 0;
#ifdef FG_INFO_DEBUG
	pr_info("Temperature EX enabled = %d\n", sm->en_temp_ex);
#endif
	if (of_property_read_bool(np, "sm,en_temp_in"))
		sm->en_temp_in = true;
	else
		sm->en_temp_in = 0;
#ifdef FG_INFO_DEBUG
	pr_info("Temperature IN enabled = %d\n", sm->en_temp_in);
#endif
	/* EN BATT DET  */
	if (of_property_read_bool(np, "sm,en_batt_det"))
		sm->en_batt_det = true;
	else
		sm->en_batt_det = 0;
#ifdef FG_INFO_DEBUG
	pr_info("Batt Det enabled = %d\n", sm->en_batt_det);
#endif
	of_property_read_string(np, "battery-profile-psy", &sm->battery_profile_psy);
	return 0;
}

static s32 fg_gauge_unseal(struct sh_fg_chip *sm) /* 20211122, Ethan. Gauge Enable */
{
	s32 ret;

	ret = fg_write_sbs_word(sm, CMD_ALTMAC, (u16)CMD_UNSEALKEY);
	if (ret < 0)
		goto fg_gauge_unseal_End;
	HOST_DELAY(CMD_SBS_DELAY);

	ret = fg_write_sbs_word(sm, CMD_ALTMAC, (u16)(CMD_UNSEALKEY >> 16));
	if (ret < 0)
		goto fg_gauge_unseal_End;
	HOST_DELAY(CMD_SBS_DELAY);

	ret = 0;
fg_gauge_unseal_End:
	return ret;
}

static s32 fg_gauge_seal(struct sh_fg_chip *sm) /* 20211122, Ethan. Gauge Enable */
{
	return fg_write_sbs_word(sm, CMD_ALTMAC, CMD_SEAL);
}

static s32 fg_gauge_runstate_check(struct sh_fg_chip *sm) /* 20211126, Ethan */
{
	s32 ret;
	u16 oemflag;
	s32 retry_cnt;
	u32 socFlag = 0, qenFlag = 0, ltFlag = 0;

	/* 20211208, Ethan. In case por with poor connection */
	for (retry_cnt = 0; retry_cnt < 5; retry_cnt++) {
		ret = fg_read_soc(sm);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_volt(sm);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_fcc(sm);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_ocv(sm);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_current(sm);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;
		HOST_DELAY(CMD_SBS_DELAY);

		//20220106, Ethan
		ret = fg_read_temperature(sm, TEMPERATURE_EX);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_terminate_voltage(sm);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;
		HOST_DELAY(CMD_SBS_DELAY);

		ret = fg_read_sbs_word(sm, CMD_OEMFLAG, &oemflag);
		if (ret < 0)
			goto fg_gauge_runstate_check_End;

		//20220106, Ethan
		socFlag = !!(sm->batt_temp < TEMPER_MIN_RESET);

		if (sm->batt_ocv == 0 || sm->batt_fcc == 0)
			socFlag |= 0x1;

		if (sm->batt_curr <= 0) {
			socFlag |= !!((sm->batt_volt > VOLT_MIN_RESET)
				&& (sm->batt_soc < SOC_MIN_RESET));
			socFlag |= !!((sm->batt_soc == 0)
				&& ((sm->batt_volt - sm->terminate_voltage) > DELTA_VOLT));
		}
		if (socFlag) {
			ret = fg_gauge_unseal(sm);
			if (ret < 0)
				goto fg_gauge_runstate_check_End;

			ret = fg_write_sbs_word(sm, CMD_ALTMAC, (u16)CMD_RESET);
			if (ret < 0)
				goto fg_gauge_runstate_check_End;
			HOST_DELAY(DELAY_RESET);
		}

		/* por with poor connection */
		/* Gauge Un-enable */
		qenFlag = !!((oemflag & CMD_MASK_OEM_GAUGEEN) != CMD_MASK_OEM_GAUGEEN);
		if (qenFlag) {  //Gauge Disable. Re-enable gauge
			ret = fg_gauge_unseal(sm);
			if (ret < 0)
				goto fg_gauge_runstate_check_End;

			ret = fg_write_sbs_word(sm, CMD_ALTMAC, (u16)CMD_ENABLE_GAUGE);
			if (ret < 0)
				goto fg_gauge_runstate_check_End;
			HOST_DELAY(DELAY_ENABLE_GAUGE);
		}

		ltFlag = !!((oemflag & CMD_MASK_OEM_LIFETIMEEN) != CMD_MASK_OEM_LIFETIMEEN);
		if (ltFlag) { //Lifetime Disable. Re-enable lifetime
			ret = fg_gauge_unseal(sm);
			if (ret < 0)
				goto fg_gauge_runstate_check_End;

			ret = fg_write_sbs_word(sm, CMD_ALTMAC, (u16)CMD_ENABLE_LIFETIME);
			if (ret < 0)
				goto fg_gauge_runstate_check_End;
			HOST_DELAY(DELAY_ENABLE_GAUGE);
		}

		if (ltFlag || socFlag || qenFlag)
			ret = 1;
		else
			ret = 0;
		break;

fg_gauge_runstate_check_End:
		HOST_DELAY(CMD_SBS_DELAY << 1);
	}

	pr_err("soc=%d, volt=%d, termVolt=%d, ocv=%d, fcc=%d\n",
		sm->batt_soc, sm->batt_volt, sm->terminate_voltage, sm->batt_ocv, sm->batt_fcc);

	pr_err("OEMFlag=0x%04X, QEN_FLAG=%u, SOC_FLAG=%u, LifeTime_Flag=%u\n",
		oemflag, qenFlag, socFlag, ltFlag);
	fg_gauge_seal(sm);
	return ret;
}

static s32 Check_Chip_Version(struct sh_fg_chip *sm)
{
	struct device *dev = &sm->client->dev;
	struct device_node *np = dev->of_node;
	u32 version_main, version_date, version_afi, version_ts;
	u16 sector_flag = 0; //20220315
	s32 ret = CHECK_VERSION_ERR;
	u16 temp;
	s32 date;
	/* 20211025, Ethan. IAP Fail Check */
	struct sh_decoder decoder;
	u8 iap_read[IAP_READ_LEN];

	WARN_ON(dev == 0);
	WARN_ON(np == 0);

	/* battery_params node*/
	np = of_find_node_by_name(of_node_get(np), "fg_fw_params");
	if (np == NULL) {
		pr_err("Cannot find child node \"fg_fw_params\"\n");
		return CHECK_VERSION_ERR;
	}

	of_property_read_u32(np, "version_main", &version_main);
	of_property_read_u32(np, "version_date", &version_date);
	of_property_read_u8(np, "iap_twiadr", &decoder.addr); /* 20211025, Ethan */

	np = of_find_node_by_name(of_node_get(sm->batt_np), "fg_battery_params");
	if (np == NULL) {
		pr_err("Cannot find child node \"fg_battery_params\"\n");
		return CHECK_VERSION_ERR;
	}
	of_property_read_u32(np, "version_afi", &version_afi);
	of_property_read_u32(np, "version_ts", &version_ts);

	pr_err("main=0x%04X, date=0x%08X, afi=0x%04X, ts=0x%04X",
		version_main, version_date, version_afi, version_ts);

	/* 20211025, Ethan. IAP Fail Check. iap addr may differ from normal addr */
	decoder.reg = (u8)CMD_IAPSTATE_CHECK;
	decoder.length = IAP_READ_LEN;
	if ((fg_decode_iic_read(sm, &decoder, iap_read) >= 0) && (iap_read[0] != 0)
		&& (iap_read[1] != 0)) {
		pr_err("ic is in iap mode, force update all");
		ret = CHECK_VERSION_WHOLE_CHIP;
		goto Check_Chip_Version_End;
	}
	HOST_DELAY(CMD_SBS_DELAY); /* 20211029, Ethan */

	if (fg_gauge_unseal(sm) < 0) { /* 20211122, Ethan. Gauge Enable */
		ret = CHECK_VERSION_ERR;
		goto Check_Chip_Version_End;
	}
	//HOST_DELAY(CMD_SBS_DELAY);

	/* check fw version. FW update must update afi(for iap check flag) */
	if (fg_read_sbs_word(sm, CMD_FWVERSION_MAIN, &temp) < 0) {
		ret = CHECK_VERSION_ERR;
		goto Check_Chip_Version_End;
	}
#ifdef FG_INFO_DEBUG
	pr_err(" Chip Version: ic main=0x%04X ", temp);
#endif
	if (temp < version_main) {
		ret = CHECK_VERSION_WHOLE_CHIP;
		goto Check_Chip_Version_End;
	} else if (temp > version_main)
		ret = CHECK_VERSION_OK;
	else { //version equal, check date
		if (fg_read_sbs_word(sm, CMD_FWDATE1, &temp) < 0) {
			ret = CHECK_VERSION_ERR;
			goto Check_Chip_Version_End;
		}
		HOST_DELAY(CMD_SBS_DELAY);
		date = (u32)temp << 16;

		if (fg_read_sbs_word(sm, CMD_FWDATE2, &temp) < 0) {
			ret = CHECK_VERSION_ERR;
			goto Check_Chip_Version_End;
		}
		date |= (temp & FW_DATE_MASK);
		pr_err(" Chip Version: ic date=0x%08X ", date);
		if (date < version_date) {
			ret = CHECK_VERSION_WHOLE_CHIP;
			goto Check_Chip_Version_End;
		} else
			ret = CHECK_VERSION_OK;
	}

	/* check afi */
	if (fg_read_sbs_word(sm, CMD_AFI_STATIC_SUM, &temp) < 0) {
		ret = CHECK_VERSION_ERR;
		goto Check_Chip_Version_End;
	}

	//20220315, Ethan
	if (fg_read_sbs_word(sm, CMD_SECTOR_FLAG, &sector_flag) < 0) {
		ret = CHECK_VERSION_ERR;
		goto Check_Chip_Version_End;
	}
#ifdef FG_INFO_DEBUG
	pr_err(" Chip_Version: ic afi=0x%04X, sector_flag=0x%04X ", temp, sector_flag);
#endif
	if ((temp != version_afi) || ((sector_flag & MASK_SECTOR_FLAG) != 0))
		ret |= CHECK_VERSION_AFI;

#ifdef SH_FW_DEBUG
	int rc = 0;
	u16 afi_ver = 0;

	rc = request_firmware(&sh_afi_firmware, SH_AFI_FILE, dev);
	if (rc || !sh_afi_firmware) {
		pr_err("sh afi request_firmware failed for %s (ret = %d)\n",
			SH_AFI_FILE, rc);
	} else {
		ret &= 0xff - CHECK_VERSION_AFI;
		memcpy(&afi_ver, sh_afi_firmware->data, sizeof(u16));
		if (afi_ver != temp)
			ret |= CHECK_VERSION_AFI_DEBUG;

		pr_err("Probe: sh firmware debug afi version = 0x%x", afi_ver);
	}
#endif
	/* check TS */
	if (fg_read_sbs_word(sm, CMD_TS_VER, &temp) < 0) {
		ret = CHECK_VERSION_ERR;
		goto Check_Chip_Version_End;
	}
#ifdef FG_INFO_DEBUG
	pr_err(" Chip Version: ic ts=0x%04X ", temp);
#endif
	if (temp != version_ts)
		ret |= CHECK_VERSION_TS;

Check_Chip_Version_End:
	fg_gauge_seal(sm); /* 20211122, Ethan. Gauge Enable */
	return ret;
}

int file_decode_process(struct sh_fg_chip *sm, u8 id, char *profile_name)
{
	struct device *dev = &sm->client->dev;
	struct device_node *np = dev->of_node;
	u8 *pBuf = NULL;
	u8 *pBuf_Read = NULL;
	char strDebug[FILEDECODE_STRLEN];
	int buflen;
	int wait_ms;
	int i = 0, j = 0;
	int line_length;
	int result = -1;
	int retry;

	pr_info("start id = %d\n", id);

	/* battery_params node*/
	if (id > 0) {
		np = sm->batt_np;
		np = of_find_node_by_name(of_node_get(np), "fg_battery_params");
	} else {
		np = of_find_node_by_name(of_node_get(np), "fg_fw_params");
	}

	if (np == NULL) {
		pr_err("Cannot find child node id = %d", id);
		return -EINVAL;
	}

	buflen = of_property_count_u8_elems(np, profile_name);
	pr_info("buf_len=%d, key=%s", buflen, profile_name);

#ifdef SH_FW_DEBUG
	if (id == 2) {
		if (sh_afi_firmware)
			buflen = sh_afi_firmware->size - 4;
		else
			goto main_process_error;
	}
#endif

	pBuf = (u8 *)devm_kzalloc(dev, buflen, 0);
	pBuf_Read = (u8 *)devm_kzalloc(dev, BUF_MAX_LENGTH, 0);

	if ((pBuf == NULL) || (pBuf_Read == NULL)) {
		result = ERRORTYPE_ALLOC;
		pr_err("kzalloc error");
		goto main_process_error;
	}
#ifdef SH_FW_DEBUG
	if (id == 2) {
		pr_info("sh afi fw debug buf_len=%d, key=%s", buflen, profile_name);
		memcpy(pBuf, sh_afi_firmware->data + 4, sh_afi_firmware->size - 4);
		release_firmware(sh_afi_firmware);
	} else {
		result = of_property_read_u8_array(np, profile_name, pBuf, buflen);
		if (result) {
			pr_err("read dts fail %s\n", profile_name);
			goto main_process_error;
		}
	}
#else
	result = of_property_read_u8_array(np, profile_name, pBuf, buflen);
	if (result) {
		pr_err("read dts fail %s\n", profile_name);
		goto main_process_error;
	}
#endif
	print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, pBuf, 32);
	pr_err("first data=%s", strDebug);

	while (i < buflen) {
		/* delay: b0: operate, b1: 2, b2-b3: time, big-endian */
		/* other: b0: operate, b1: TWIADR, b2: reg, b3: data_length, b4...end: item */
		if (pBuf[i + INDEX_TYPE] == OPERATE_WAIT) {
			wait_ms = ((int)pBuf[i + INDEX_WAIT_HIGH] * 256)
				+ pBuf[i + INDEX_WAIT_LOW];
			pr_debug("loop wait: time=%d", wait_ms);

			if (pBuf[i + INDEX_WAIT_LENGTH] == 2) {
				HOST_DELAY(wait_ms);
				i += LINELEN_WAIT;
			} else {
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN,
					&pBuf[i + INDEX_TYPE], 32);
				pr_err("wait error! index=%d, str=%s", i, strDebug);
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}
		} else if (pBuf[i + INDEX_TYPE] == OPERATE_READ) {
			line_length = pBuf[i + INDEX_LENGTH];
			if (line_length <= 0) {
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}

			/* 20211026, Ethan. IAP addr may differ from default addr */
			if (fg_decode_iic_read(sm,
				(struct sh_decoder *)&pBuf[i + INDEX_ADDR], pBuf_Read) < 0) {
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN,
					&pBuf[i + INDEX_TYPE], 32);
				pr_err("read error! index=%d, str=%s", i, strDebug);
				result = ERRORTYPE_COMM;
				goto main_process_error;
			}

			i += LINELEN_READ;
		} else if (pBuf[i + INDEX_TYPE] == OPERATE_COMPARE) {
			line_length = pBuf[i + INDEX_LENGTH];
			if (line_length <= 0) {
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}

			for (retry = 0; retry < COMPARE_RETRY_CNT; retry++) {
				/* 20211026, Ethan. IAP addr may differ from default addr */
				if (fg_decode_iic_read(sm,
				(struct sh_decoder *)&pBuf[i + INDEX_ADDR], pBuf_Read) < 0) {
					print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN,
						&pBuf[i + INDEX_TYPE], 32);
					pr_err("compare read error! index=%d, str=%s", i, strDebug);
					result = ERRORTYPE_COMM;
					//goto file_decode_process_compare_loop_end;
					HOST_DELAY(COMPARE_RETRY_WAIT);
					continue;
				}

				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN,
					pBuf_Read, line_length);
				pr_debug("loop compare: IC read=%s", strDebug);

				result = 0;
				for (j = 0; j < line_length; j++) {
					if (pBuf[INDEX_DATA + i + j] != pBuf_Read[j]) {
						result = ERRORTYPE_COMPARE;
						break;
					}
				}

				if (result == 0)
					break;

				/* compare fail */
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN,
					&pBuf[i + INDEX_TYPE], 32);
				pr_err("compare error! index=%d, retry=%d, host=%s",
					i, retry, strDebug);
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN,
					pBuf_Read, 32);
				pr_err("ic=%s", strDebug);

			}

			if (retry >= COMPARE_RETRY_CNT) {
				result = ERRORTYPE_COMPARE; /* 20211125, Ethan */
				goto main_process_error;
			}

			i += LINELEN_COMPARE + line_length;
		} else if (pBuf[i + INDEX_TYPE] == OPERATE_WRITE) {
			line_length = pBuf[i + INDEX_LENGTH];
			if (line_length <= 0) {
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}

			/* 20211026, Ethan. IAP addr may differ from default addr */
			if (fg_decode_iic_write(sm,
				(struct sh_decoder *)&pBuf[i + INDEX_ADDR]) != 0) {
				print_buffer(strDebug,
					sizeof(char) * FILEDECODE_STRLEN,
					&pBuf[i + INDEX_TYPE], 32);
				pr_err("write error! index=%d, str=%s", i, strDebug);
				result = ERRORTYPE_COMM;
				goto main_process_error;
			}

			i += LINELEN_WRITE + line_length;
		} else {
			result = ERRORTYPE_LINE;
			goto main_process_error;
		}
	}
	result = ERRORTYPE_NONE;

main_process_error:
	if (pBuf)
		devm_kfree(dev, pBuf);
	if (pBuf_Read)
		devm_kfree(dev, pBuf_Read);
	pr_info("end: result=%d", result);
	return result;
}

static s32 fg_gauge_check_autocali(struct sh_fg_chip *sm) /* 20220117, Ethan */
{
	s32 ret;
	u8 bufComm[32];
	u16 temp16;

	ret = fg_gauge_unseal(sm);
	if (ret < 0) {
		pr_err("Comm error! cannot unseal!");
		ret = CALI_ERR_COMM;
		goto fg_gauge_check_autocali_end;
	}
	HOST_DELAY(CMD_SBS_DELAY);

	ret = fg_read_block(sm, CMD_CALIED_FLAG, 32, bufComm);
	if (ret < 0) {
		pr_err("Comm error! cannot read cali flag!");
		ret = CALI_ERR_COMM;
		goto fg_gauge_check_autocali_end;
	}
	HOST_DELAY(CMD_SBS_DELAY);
	temp16 = BUF2U16_BG(&bufComm[INDEX_CALIED_FLAG]);
	if (temp16 == FLAG_CALIED_FLAG) {
		pr_err("End. FG is calied! cali-flag=0x%04X", temp16);
		ret = 0;
		sm->is_calied = true;
		goto fg_gauge_check_autocali_end;
	} else {
		bufComm[INDEX_CALIED_FLAG] = 0;
		bufComm[INDEX_CALIED_FLAG + 1] = 0;
		fg_write_block(sm, CMD_CALIED_FLAG, LEN_CALIED_FLAG, bufComm);

		pr_err("End. FG is un-calied! cali-flag=0x%04X", temp16);
		sm->is_calied = false;
		ret = 0;
		goto fg_gauge_check_autocali_end;
	}

fg_gauge_check_autocali_end:
	HOST_DELAY(CMD_SBS_DELAY);
	fg_gauge_seal(sm);
	//pr_info(": ret=%d", ret);

	return ret;
}

static s32 fg_gauge_enable_autocali(struct sh_fg_chip *sm, bool force_enable) /* 20220117, Ethan */
{
	s32 ret;
	u8 bufComm[32];
	u16 temp16;

	ret = fg_gauge_unseal(sm);
	if (ret < 0) {
		pr_err("Comm error! cannot unseal!");
		ret = CALI_ERR_COMM;
		goto fg_gauge_eable_autocali_end;
	}
	HOST_DELAY(CMD_SBS_DELAY);

	ret = fg_read_block(sm, CMD_OEMINFO, 32, bufComm);
	if (ret < 0) {
		pr_err("Comm error! cannot read CMD_OEMINFO!");
		ret = CALI_ERR_COMM;
		goto fg_gauge_eable_autocali_end;
	}
	HOST_DELAY(CMD_SBS_DELAY);
	temp16 = BUF2U16_LT(&bufComm[INDEX_OEMINFO_INTFLAG]);
	if (!(temp16 & MASK_OEMINFO_AUTOCALI)) {
		pr_err("error! FW donot support auto-cali!, int-flag=0x%04X", temp16);
		ret = CALI_ERR_UNSUPPORT;
		goto fg_gauge_eable_autocali_end;
	}

	ret = fg_read_block(sm, CMD_CALIED_FLAG, 32, bufComm);
	if (ret < 0) {
		pr_err("Comm error! cannot read cali flag!");
		ret = CALI_ERR_COMM;
		goto fg_gauge_eable_autocali_end;
	}
	HOST_DELAY(CMD_SBS_DELAY);
	temp16 = BUF2U16_BG(&bufComm[INDEX_CALIED_FLAG]);
	if (temp16 == FLAG_CALIED_FLAG) {
		pr_err("OK! FG has already calied! cali-flag=0x%04X", temp16);
		ret = CALI_ERR_NONE;
		if (force_enable)
			pr_err("force re-cali FG\n");
		else
			goto fg_gauge_eable_autocali_end;
	}

	bufComm[INDEX_CALIED_FLAG] = (FLAG_CALIED_ENABLE >> 8) & 0xFF;
	bufComm[INDEX_CALIED_FLAG + 1] = FLAG_CALIED_ENABLE & 0xFF;

	ret = fg_write_block(sm, CMD_CALIED_FLAG, LEN_CALIED_FLAG, bufComm);
	if (ret < 0) {
		pr_err("Comm error! cannot write CMD_CALIED_FLAG!");
		ret = CALI_ERR_COMM;
		goto fg_gauge_eable_autocali_end;
	}
	ret = CALI_ERR_NONE;

fg_gauge_eable_autocali_end:
	HOST_DELAY(CMD_SBS_DELAY);
	fg_gauge_seal(sm);

	pr_err(": ret=%d", ret);

	return ret;
}

static s32 fg_battery_parse_dt(struct sh_fg_chip *sm)
{
	struct device *dev = &sm->client->dev;
	struct device_node *np = dev->of_node;
	//s32 battery_id = -1;
	const char *battery_name = NULL;

	WARN_ON(dev == 0);
	WARN_ON(np == 0);

	//if (of_property_read_u32(np, "battery,id", &battery_id) < 0)
	//	pr_err("not battery,id property\n");
	//if (battery_id == -1)
	//	battery_id = get_battery_id(sm);

	//pr_info("battery id = %d\n", battery_id);

	of_property_read_string(of_node_get(sm->batt_np), "battery-name",
				&battery_name);
	pr_info("battery name = %s\n", battery_name);

	if (of_property_read_u32(of_node_get(sm->batt_np), "fullbatt-voltage", &sm->fullbatt_vol))
		sm->fullbatt_vol = 0;
#ifdef FG_INFO_DEBUG
	pr_info("battery full vol = %d\n", sm->fullbatt_vol);
#endif
	np = of_find_node_by_name(of_node_get(sm->batt_np), "fg_battery_params");
	if (np == NULL) {
		pr_err("Cannot find child node \"fg_battery_params\"\n");
		return CHECK_VERSION_ERR;
	}

	if (of_property_read_u32(np, "nominal_voltage", &sm->nominal_voltage))
		sm->nominal_voltage = DEF_NOMINAL_VOLTAHE;
#ifdef FG_INFO_DEBUG
	pr_info("nominal voltage = %d\n", sm->nominal_voltage);
#endif
	return 0;
}

bool hal_fg_init(struct i2c_client *client)
{
	struct sh_fg_chip *sm = i2c_get_clientdata(client);
	s32 ret;

	dev_dbg(sm->dev, "sh366101 hal_fg_init...\n");
	mutex_lock(&sm->data_lock);
	if (client->dev.of_node) {
		///* Load common data from DTS*/
		//fg_common_parse_dt(sm);
		/* Load battery data from DTS*/
		fg_battery_parse_dt(sm);
	}

	if (sm->gpio_int != -EINVAL) {
		sm->irq = gpio_to_irq(sm->gpio_int);

		ret = gpio_direction_input(sm->gpio_int);
		if (ret < 0) {
			dev_err(sm->dev, "error set gpio input\n");
			return false;
		}

		if (sm->irq) {
			ret = devm_request_irq(sm->dev, sm->irq,
						fg_irq_handler,
						IRQ_TYPE_EDGE_FALLING,
						"sh fuel gauge irq", sm);
			if (ret < 0) {
				pr_err("request irq for irq=%d failed, ret = %d\n",
						sm->irq, ret);
				return false;
			}
		}

		pr_info("request irq id = %d\n", sm->irq);
	}

	ret = fg_init(client);
	if (ret)
		return false;

	mutex_unlock(&sm->data_lock);
	pr_info("hal fg init OK\n");
	return true;
}

#ifdef TEST
static s32 sh366101_get_psy(struct sh_fg_chip *sm)
{
#if !(IS_PACK_ONLY)
	if (!sm->usb_psy || !sm->batt_psy)
		return -EINVAL;

	sm->usb_psy = power_supply_get_by_name("usb");
	if (!sm->usb_psy) {
		pr_err("USB supply not found, defer probe\n");
		return -EINVAL;
	}

	sm->batt_psy = power_supply_get_by_name("battery");
	if (!sm->batt_psy) {
		pr_err("bms supply not found, defer probe\n");
		return -EINVAL;
	}
#endif
	return 0;
}

static s32 sh366101_notifier_call(struct notifier_block *nb, unsigned long ev, void *v)
{
	struct power_supply *psy = v;
	struct sh_fg_chip *sm = container_of(nb, struct sh_fg_chip, nb);
	union power_supply_propval pval = {0, };
	s32 rc;

	if (ev != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	rc = sh366101_get_psy(sm);
	if (rc < 0)
		return NOTIFY_OK;

	if (strcmp(psy->desc->name, "usb") != 0)
		return NOTIFY_OK;

	if (sm->usb_psy) {
		rc = power_supply_get_property(sm->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);

		if (rc < 0) {
			pr_err("failed get usb present\n");
			return -EINVAL;
		}
		if (pval.intval) {
			sm->usb_present = true;
			pm_stay_awake(sm->dev);
		} else {
			sm->batt_sw_fc = false;
			sm->usb_present = false;
			pm_relax(sm->dev);
		}
	}
	return NOTIFY_OK;
}
#endif

static s32 fg_fw_updata(struct sh_fg_chip *sm)
{
	s32 version_ret;
	s32 retry;
	s32 ret;

	/* 20211013, Ethan. Firmware Update */
	version_ret = Check_Chip_Version(sm);
	//version_ret = CHECK_VERSION_FW;
	ret = ERRORTYPE_NONE; //20220218, Ethan
	if (version_ret == CHECK_VERSION_ERR) {
		pr_err("Probe: Check version error!");
		ret = -1;
	} else if (version_ret == CHECK_VERSION_OK) {
		pr_err("Probe: Check version ok!");
		ret = 0;
	} else {
		pr_err("Probe: Check version update: %X", version_ret);

		if (version_ret & CHECK_VERSION_FW) {
			pr_err("Probe: Firmware Update start");
			for (retry = 0; retry < FILE_DECODE_RETRY; retry++) {
				ret = file_decode_process(sm, 0, "sinofs_image_data");
				if (ret == ERRORTYPE_NONE)
					break;
				HOST_DELAY(FILE_DECODE_DELAY); /* 20211029, Ethan */
			}
			pr_err("Probe: Firmware Update end, ret=%d", ret);
		}

		if (ret != ERRORTYPE_NONE) //20220218, Ethan
			goto sh_fg_probe_fwupdate_end;

		if (version_ret & CHECK_VERSION_TS) {
			pr_err("Probe: TS Update start");
			for (retry = 0; retry < FILE_DECODE_RETRY; retry++) {
				ret = file_decode_process(sm, 1, "sinofs_ts_data");
				if (ret == ERRORTYPE_NONE)
					break;
				HOST_DELAY(FILE_DECODE_DELAY); /* 20211029, Ethan */
			}
			pr_err("Probe: TS Update end, ret=%d", ret);
		}

		if (ret != ERRORTYPE_NONE) //20220218, Ethan
			goto sh_fg_probe_fwupdate_end;

#ifdef SH_FW_DEBUG
		if (version_ret & CHECK_VERSION_AFI_DEBUG) {
			pr_err("Probe: AFI debug Update start");
			for (retry = 0; retry < FILE_DECODE_RETRY; retry++) {
				ret = file_decode_process(sm, 2, "sinofs_afi_data");
				if (ret == ERRORTYPE_NONE)
					break;
				HOST_DELAY(FILE_DECODE_DELAY); /* 20211220, taoyang */
			}
			pr_err("Probe: AFI debug Update end, ret=%d", ret);
		} else if (version_ret & CHECK_VERSION_AFI) {
#else
		if (version_ret & CHECK_VERSION_AFI) {
#endif
			pr_err("Probe: AFI Update start");
			for (retry = 0; retry < FILE_DECODE_RETRY; retry++) {
				ret = file_decode_process(sm, 1, "sinofs_afi_data");
				if (ret == ERRORTYPE_NONE)
					break;
				HOST_DELAY(FILE_DECODE_DELAY); /* 20211029, Ethan */
			}
			pr_err("Probe: AFI Update end, ret=%d", ret);
		}

		if (ret != ERRORTYPE_NONE) //20220218, Ethan
			goto sh_fg_probe_fwupdate_end;
	}

sh_fg_probe_fwupdate_end:
	if (ret == 0)
		ret = version_ret;
	return ret;
}

static void fg_init_workfunc(struct work_struct *work)
{
	struct sh_fg_chip *sm = container_of(work, struct sh_fg_chip, fg_init_work.work);
	//struct device_node *np;
	struct power_supply *main_psy;
	s32 ret;
	s32 need_init = 0;

	main_psy = power_supply_get_by_name(sm->battery_profile_psy);
	if (!main_psy) {
		pr_err("Not Find battery psy-node(jcmbattery),retry!.");
		goto err_ret;
	}

	sm->batt_np = main_psy->of_node;
	power_supply_put(main_psy);
	WARN_ON(sm->batt_np == 0);
#ifdef FG_INFO_DEBUG
	fg_read_gaugeinfo_block(sm, 1); // print debug info before fw updata
#endif
	ret = fg_fw_updata(sm);
	if (ret < 0) {
		pr_err("sh fg firmware updata failed!\n");
		goto err_ret;
	} else if (ret > 0) {
		need_init = 1;
	}

	ret = fg_gauge_check_autocali(sm);
	if (ret < 0) {
		pr_err("sh fg check cali status failed!\n");
		goto err_ret;
	}

	ret = fg_gauge_runstate_check(sm);
	if (ret < 0) { /* 20211122, Ethan. Gauge Enable */
		pr_err("Failed to Enable Gauge\n");
		goto err_ret;
	} else if (ret > 0) {
		need_init = 1;
	}

	if (!hal_fg_init(sm->client)) {
		pr_err("Failed to Initialize Fuelgauge\n");
		goto err_ret;
	}

	if (need_init)
		msleep(1100); //wait sh366101 init done

	sm->fg_psy = NULL;
	ret = fg_refresh_status(sm);
	if (ret)
		goto err_ret;
	//msleep(20);
	fg_psy_register(sm);

	schedule_delayed_work(&sm->monitor_work, 2 * HZ);
	return;

err_ret:
	fg_check_i2cbus(sm);
	schedule_delayed_work(&sm->fg_init_work, 1 * HZ);
}

static s32 sh_fg_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	s32 ret;
	struct sh_fg_chip *sm;
	u32 *regs;

	sm = devm_kzalloc(&client->dev, sizeof(struct sh_fg_chip), GFP_KERNEL);

	dev_info(sm->dev, "sh fuel gauge probe in\n");
	if (!sm)
		return -ENOMEM;

	sm->dev = &client->dev;
	sm->client = client;
	sm->chip = id->driver_data;

	sm->batt_soc = -ENODATA;
	sm->batt_fcc = -ENODATA;
	sm->batt_volt = -ENODATA;
	sm->batt_temp = -ENODATA;
	sm->sh_temp = -ENODATA;
	sm->batt_curr = -ENODATA;
	sm->batt_soc_cycle = -ENODATA;
	sm->batt_ocv = -ENODATA;
	sm->batt_rmc = -ENODATA;
	sm->batt_designcap = -ENODATA;
	sm->aver_batt_volt = -ENODATA;
	sm->is_charging = -ENODATA;
	sm->is_calied = false;
	sm->start_cali = false;

#ifdef FG_INFO_DEBUG
	sm->is_debug_open = true;
#endif
	sm->is_suspend = false;

	if (sm->chip == SH366101) {
		regs = sh366101_regs;
	} else {
		pr_err("unexpected fuel gauge: %d\n", sm->chip);
		regs = sh366101_regs;
	}

	memcpy(sm->regs, regs, NUM_REGS * sizeof(u32));

	sm->i2c_bus_base = ioremap_nocache(I2C_BASE, 0x100);
	sm->i2c_pinctl_base = ioremap_nocache(PINCTL_BASE, 0x100);
	i2c_set_clientdata(client, sm);

	//set i2c timeout 2s, default 250ms;
	client->adapter->timeout = 2000;

	mutex_init(&sm->i2c_rw_lock);
	mutex_init(&sm->data_lock);
	mutex_init(&sm->cali_lock);

	/* Load common data from DTS*/
	fg_common_parse_dt(sm);
	///* Load battery data from DTS*/
	//fg_battery_parse_dt(sm);

	INIT_DELAYED_WORK(&sm->monitor_work, fg_monitor_workfunc);
	INIT_DELAYED_WORK(&sm->fg_init_work, fg_init_workfunc);

	//sm->nb.notifier_call = &sh366101_notifier_call;
	//ret = power_supply_reg_notifier(&sm->nb);
	//if (ret < 0)
	//	return ret;

	ret = sysfs_create_group(&sm->dev->kobj, &fg_attr_group);
	if (ret)
		pr_err("Failed to register sysfs:%d\n", ret);

	schedule_delayed_work(&sm->fg_init_work, msecs_to_jiffies(100));
	ret = fg_gauge_check_sfr_entry(sm);
	if (ret < 0)
		pr_err("Failed to check sfy:%d\n", ret);
	pr_info("sh fuel gauge probe successfully, %s\n", device2str[sm->chip]);

	return 0;
}

static int sh_fg_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	pr_info("in!\n");
	sm->is_suspend = false;
	schedule_delayed_work(&sm->monitor_work, msecs_to_jiffies(100));
	pr_info("exit!\n");
	return 0;
}

static int sh_fg_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	pr_info("in!\n");
	sm->is_suspend = true;
	cancel_delayed_work_sync(&sm->monitor_work);
	pr_info("exit!\n");
	return 0;
}

static const struct dev_pm_ops sh_fg_pm_ops = {
	.suspend = sh_fg_suspend,
	.resume = sh_fg_resume,
};

static s32 sh_fg_remove(struct i2c_client *client)
{
	struct sh_fg_chip *sm = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&sm->monitor_work);
	cancel_delayed_work_sync(&sm->fg_init_work);

	fg_psy_unregister(sm);

	mutex_destroy(&sm->data_lock);
	mutex_destroy(&sm->i2c_rw_lock);

	debugfs_remove_recursive(sm->debug_root);

	sysfs_remove_group(&sm->dev->kobj, &fg_attr_group);

	return 0;
}

static void sh_fg_shutdown(struct i2c_client *client)
{
	pr_info("sm fuel gauge driver shutdown!\n");
}

static const struct of_device_id sh_fg_match_table[] = {
	{.compatible = "sh,sh366101",},
	{},
};
MODULE_DEVICE_TABLE(of, sh_fg_match_table);

static const struct i2c_device_id sh_fg_id[] = {
	{ "sh366101", SH366101 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sh_fg_id);

static struct i2c_driver sh_fg_driver = {
	.driver = {
		.name = "sh366101",
		.owner = THIS_MODULE,
		.of_match_table	= sh_fg_match_table,
		.pm = &sh_fg_pm_ops,
	},
	.id_table = sh_fg_id,
	.probe = sh_fg_probe,
	.remove	= sh_fg_remove,
	.shutdown = sh_fg_shutdown,
};

module_i2c_driver(sh_fg_driver);

MODULE_DESCRIPTION("SH SH366101 Gauge Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sinowealth");

