// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, 2016, 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>
#include "soc/jlq/jr510/jlq-bridge.h"
#include <linux/ipc_logging.h>
#include <linux/debugfs.h>

#define VSET_STEP_UV 1000
#define VSET_LB		 0x40
#define VSET_EN		 0x46

struct jlq_regulator {
	struct regulator_desc	rdesc;
	struct regmap			*regmap;
	struct device			*dev;
	int			voltage;
	int			pre_voltage;
	unsigned int		mode;
	int			hpm_min_load_uA;
	int			system_load_uA;
	u16			base;
	bool		enabled;
	bool		enable_request;
};

#define REGULATOR_NAME_LEN 9
#define REQUEST_BUF_LEN 128
#define IPC_LOG_PAGES 30

struct bridge_channel *ch;
struct completion cmd_complete;
void __iomem *mailbox_arbiter_base;
void        *ipc_log_reg_s3s8;
void        *ipc_log_reg_s1;

spinlock_t mailbox_lock;
static int enable_dvfs_flag = 1;
static struct dentry *debugfs_dir;

/* common functions */
static int pm6125_read(struct regmap *regmap,  u16 reg, u8 *val, int count)
{
	int rc;

	rc = regmap_bulk_read(regmap, reg, val, count);
	if (rc < 0)
		pr_err("failed to read 0x%04x\n", reg);

	return rc;
}

static int pm6125_write(struct regmap *regmap, u16 reg, u8 *val, int count)
{
	int rc;

	pr_debug("Writing 0x%02x to 0x%04x\n", *val, reg);
	rc = regmap_bulk_write(regmap, reg, val, count);
	if (rc < 0)
		pr_err("failed to write 0x%04x\n", reg);

	return rc;
}

void regulator_notify(void *priv, unsigned int flags)
{
	complete(&cmd_complete);
}

static int send_request(struct regulator_dev *rdev, int min_uV, int max_uV)
{
	char *buf;
	int len;

	reinit_completion(&cmd_complete);
	buf = kzalloc(REQUEST_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	strncpy(buf, rdev->desc->name, REGULATOR_NAME_LEN);
	sprintf(buf + REGULATOR_NAME_LEN, "%c", '\0');
	len = sprintf(buf + REGULATOR_NAME_LEN + 1, "%d", min_uV);
	len += sprintf(buf + len + REGULATOR_NAME_LEN + 1, "%c", '\0');
	len += sprintf(buf + len + REGULATOR_NAME_LEN + 1, "%d", max_uV);
	len += sprintf(buf + len + REGULATOR_NAME_LEN + 1, "%c", '\0');

	if (bridge_write(ch, buf, len + REGULATOR_NAME_LEN) < 0) {
		dev_err_ratelimited(&rdev->dev, "send request fail!\n");
		goto fail;
	}

	if (!wait_for_completion_timeout(&cmd_complete, msecs_to_jiffies(3000))) {
		dev_err_ratelimited(&rdev->dev, "wait cm4 ack timeout!\n");
		goto fail;
	}

	if(bridge_read(ch, buf, sizeof("successful")) < 0) {
		dev_err_ratelimited(&rdev->dev, "read bridge data fail!\n");
		goto fail;
	}
	if (!strcmp(buf, "successful")) {
		kfree(buf);
		return 0;
	}

fail:
	kfree(buf);
	return -EINVAL;
}

#define REGULATOR_ARBITER_BITS	(12)
#define CPU_ARBITER_EN			(0x2C0)
#define CPU6_ARBITER_REQ		(0x28c)
#define CPU6_ARBITER_ACK		(0x2B8)

/*
 *APSS SIDE:
 *		BIT0  BIT1  BIT2  BIT3
 *VDDR: 800   700   RESV  RESV
 *		BIT4  BIT5  BIT6  BIT7
 *VDDC: 800   700   RESV  RESV
 *
 *CM4F  SIDE:
 *		BIT8   BIT9   BIT10  BIT11
 *VDDR: 800    700    RESV   RESV
 *		BIT12  BIT13  BIT14  BIT15
 *VDDC: 800    700    RESV   RESV
 */
#define REGULATOR_VDDC_VOLT_REGS		(0x164)
#define REGULATOR_VDDR_VOLT_REGS		(0x160)

#define CM4F_VOLTS_BITS_OFFSET		(8)
#define APPS_VOLTS_BITS_OFFSET		(0)
#define VOLTS_BITS_MASK			    (0xFF)
#define MAX_VOLTS_LEVEL             (800)
#define MIN_VOLTS_LEVEL             (400)
#define VOLTS_STEP                  (2)

#define WAIT_MAILBOX_TIMEOUT        (11)

enum VOLT_TYPE {
	VDD_CORE,
	VDD_DDR,
	VDD_INVALID,
};

static void print_ipc(void *ipc_ctx, char *prefix, char *buf, u64 addr, int size)
{
	if (!ipc_ctx)
		return;
	ipc_log_string(ipc_ctx, "%s[0x%x:%d] : %s", prefix,
		(unsigned int)addr, size, buf);
}

void  get_regulator_arbiter(void)
{
	int val;
	unsigned long timeout =
		jiffies + (msecs_to_jiffies(WAIT_MAILBOX_TIMEOUT));

	/*Aribiter 12 --->bit0*/
	val = readl(mailbox_arbiter_base + CPU6_ARBITER_REQ);
	val = val | (1 << 0);
	writel(val, mailbox_arbiter_base + CPU6_ARBITER_REQ);
	do {
		val = readl(mailbox_arbiter_base + CPU6_ARBITER_ACK);
		val = (val >> REGULATOR_ARBITER_BITS) & 0x01;
		if (time_after(jiffies, timeout))
			panic("wait mailbox timeout, please check CM4\n");
	} while (val != 1);
}

void  put_regulator_arbiter(void)
{
	int val = 0;

	/*Aribiter 12 --->bit0*/
	val = readl(mailbox_arbiter_base + CPU6_ARBITER_REQ);
	val = val & 0xfffffffe;
	writel(val, mailbox_arbiter_base + CPU6_ARBITER_REQ);
}

void init_regulator_arbiter(void)
{
	int val;

	val = readl(mailbox_arbiter_base + CPU_ARBITER_EN);
	val |= (1 << REGULATOR_ARBITER_BITS) | (1 << (REGULATOR_ARBITER_BITS + 16));
	writel(val, mailbox_arbiter_base + CPU_ARBITER_EN);
}

int get_last_hw_volts(int cpu_offset, enum VOLT_TYPE type)
{
	int16_t val;
	int64_t reg;

	switch (type) {
	case VDD_CORE:
		reg = mailbox_arbiter_base + REGULATOR_VDDC_VOLT_REGS;
		break;
	case VDD_DDR:
		reg = mailbox_arbiter_base + REGULATOR_VDDR_VOLT_REGS;
		break;
	default:
		val = MAX_VOLTS_LEVEL;
		return val;
	}

	val = readl(reg) >> cpu_offset;
	val = val & VOLTS_BITS_MASK;
	val = MIN_VOLTS_LEVEL + val * VOLTS_STEP;

	if (val > MAX_VOLTS_LEVEL)
		val = MAX_VOLTS_LEVEL;

	return val;
}

void set_last_hw_volts(enum VOLT_TYPE type, int volts)
{
	int16_t val, val1;
	int64_t reg;

	switch (type) {
	case VDD_CORE:
		reg = mailbox_arbiter_base + REGULATOR_VDDC_VOLT_REGS;
		break;
	case VDD_DDR:
		reg = mailbox_arbiter_base + REGULATOR_VDDR_VOLT_REGS;
		break;
	default:
		return;
	}

	if (volts < MIN_VOLTS_LEVEL || volts > MAX_VOLTS_LEVEL)
		volts = MAX_VOLTS_LEVEL;

	/* convert volts to register value */
	val = DIV_ROUND_UP(volts - MIN_VOLTS_LEVEL, VOLTS_STEP);
	val = val & VOLTS_BITS_MASK;

	/* update mailbox register */
	val1 = readl(reg) & 0xff00;
	val1 |= val;
	writel(val1, reg);
}

int mailbox_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV)
{
	int ret;
	int vote_cm4f, vote_ap, volt_cur, volt_t;
	int volt_new = min_uV;
	int mv;
	unsigned long flags;
	char buf[64];
	int size = 0;
	u8 vset_raw[2] = {0};
	u8 vget_raw[2];
	u8 vget_volt[2];
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);

	if ((volt_new == vreg->pre_voltage) || !enable_dvfs_flag)
		goto print_ipc;

	size = sprintf(buf, "%c", '[');
	size += sprintf(buf + size, "%#d", min_uV / 10000);
	spin_lock_irqsave(&mailbox_lock, flags);
	get_regulator_arbiter();

	if (!strcmp(rdev->constraints->name, "pm6125_s3")) {
		size += sprintf(buf + size, "%s", "s3");
		vote_cm4f = get_last_hw_volts(CM4F_VOLTS_BITS_OFFSET,
						VDD_CORE) * 1000;
		vote_ap = get_last_hw_volts(APPS_VOLTS_BITS_OFFSET,
						VDD_CORE) * 1000;
		size += sprintf(buf + size, "vm%d", vote_cm4f / 10000);
		size += sprintf(buf + size, "va%d", vote_ap / 10000);
	} else if (!strcmp(rdev->constraints->name, "pm6125_s8")) {
		size += sprintf(buf + size, "%s", "s8");
		vote_cm4f = get_last_hw_volts(CM4F_VOLTS_BITS_OFFSET,
						VDD_DDR) * 1000;
		vote_ap = get_last_hw_volts(APPS_VOLTS_BITS_OFFSET,
						VDD_DDR) * 1000;
		size += sprintf(buf + size, "vm%d", vote_cm4f / 10000);
		size += sprintf(buf + size, "va%d", vote_ap / 10000);
	} else {
		put_regulator_arbiter();
		spin_unlock_irqrestore(&mailbox_lock, flags);
		size += sprintf(buf + size, "%c", ']');
		goto print_ipc;
	}

	volt_cur = vote_ap > vote_cm4f ? vote_ap : vote_cm4f;
	volt_t = vote_cm4f > volt_new ? vote_cm4f : volt_new;

	size += sprintf(buf + size, "tc%d", volt_cur / 10000);
	size += sprintf(buf + size, "tt%d", volt_t / 10000);

	if (volt_new > volt_cur) {
		if (!strcmp(rdev->constraints->name, "pm6125_s3"))
			set_last_hw_volts(VDD_CORE, volt_new / 1000);
		else if (!strcmp(rdev->constraints->name, "pm6125_s8"))
			set_last_hw_volts(VDD_DDR, volt_new / 1000);

		size += sprintf(buf + size, "%c", 'u');
		vreg->pre_voltage = volt_new;
		mv = DIV_ROUND_UP(volt_new, 1000);

		vset_raw[0] = mv & 0xff;
		vset_raw[1] = (mv >> 8) & 0xf;

		ret = pm6125_write(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
		if (ret < 0) {
			do {
				ret = pm6125_write(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
				if (!ret)
					break;
			} while (ret < 0);
		}
		put_regulator_arbiter();
		spin_unlock_irqrestore(&mailbox_lock, flags);
		size += sprintf(buf + size, "%d", volt_new / 10000);
		size += sprintf(buf + size, "%c", ']');
	} else if (volt_t < volt_cur) {
		if (!strcmp(rdev->constraints->name, "pm6125_s3"))
			set_last_hw_volts(VDD_CORE, volt_new / 1000);
		else if (!strcmp(rdev->constraints->name, "pm6125_s8"))
			set_last_hw_volts(VDD_DDR, volt_new / 1000);

		size += sprintf(buf + size, "%c", 'u');
		vreg->pre_voltage = volt_new;
		mv = DIV_ROUND_UP(volt_t, 1000);

		vset_raw[0] = mv & 0xff;
		vset_raw[1] = (mv >> 8) & 0xf;

		ret = pm6125_write(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
		if (ret < 0) {
			do {
				ret = pm6125_write(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
				if (!ret)
					break;
			} while (ret < 0);
		}
		put_regulator_arbiter();
		spin_unlock_irqrestore(&mailbox_lock, flags);
		size += sprintf(buf + size, "%d", volt_t / 10000);
		size += sprintf(buf + size, "%c", ']');
	} else {
		if (!strcmp(rdev->constraints->name, "pm6125_s3"))
			set_last_hw_volts(VDD_CORE, volt_new / 1000);
		else if (!strcmp(rdev->constraints->name, "pm6125_s8"))
			set_last_hw_volts(VDD_DDR, volt_new / 1000);

		size += sprintf(buf + size, "%c", 'N');
		ret = pm6125_read(rdev->regmap, vreg->base + VSET_LB, vget_volt, 2);
		if (ret < 0) {
			do {
				ret = pm6125_read(rdev->regmap, vreg->base + VSET_LB, vget_volt, 2);
				if (!ret)
					break;
			} while (ret < 0);
		}

		size += sprintf(buf + size, "%c", 'E');
		if ((vget_volt[1] << 8 | vget_volt[0]) < DIV_ROUND_UP(volt_new, 1000)) {
			pr_info("[%s %d %d]", rdev->constraints->name,
					vget_volt[1] << 8 | vget_volt[0], volt_new);
			panic("set voltage fail!!!!");
			mv = DIV_ROUND_UP(volt_new, 1000);

			vset_raw[0] = mv & 0xff;
			vset_raw[1] = (mv >> 8) & 0xf;

			size += sprintf(buf + size, "%c", 'U');
			ret = pm6125_write(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
			if (ret < 0) {
				do {
					ret = pm6125_write(rdev->regmap, vreg->base + VSET_LB,
										vset_raw, 2);
					if (!ret)
						break;
				} while (ret < 0);
			}
		}

		vreg->pre_voltage = volt_new;
		put_regulator_arbiter();
		spin_unlock_irqrestore(&mailbox_lock, flags);
		size += sprintf(buf + size, "%c", ']');
	}

print_ipc:
	ret = pm6125_read(rdev->regmap, vreg->base + VSET_LB, vget_raw, 2);
	if (ret < 0) {
		do {
			ret = pm6125_read(rdev->regmap, vreg->base + VSET_LB, vget_raw, 2);
			if (!ret)
				break;
		} while (ret < 0);
	}
	size += sprintf(buf + size, "%c", 'R');
	if (!strcmp(rdev->constraints->name, "pm6125_s3"))
		size += sprintf(buf + size, "%s", "s3");
	else if (!strcmp(rdev->constraints->name, "pm6125_s8"))
		size += sprintf(buf + size, "%s", "s8");
	size += sprintf(buf + size, "%d", (vget_raw[1] << 8 | vget_raw[0]));
	print_ipc(ipc_log_reg_s3s8, "DVFS", buf, 0, size);
	return 0;
}

static int jlq_regulator_set_voltage(struct regulator_dev *rdev, int min_uV,
				  int max_uV, unsigned int *selector)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);
	u8 vset_raw[2] = {0};
	int mv, rc;
	char buf[32];
	int size = 0;

	BUG_ON(in_interrupt());

	if (vreg && vreg->enable_request) {
		if (0)
			send_request(rdev, min_uV, max_uV);
		mailbox_set_voltage(rdev, min_uV, max_uV);
		goto out;
	}

	if (!strcmp(rdev->constraints->name, "pm6125_s1"))
		size = sprintf(buf, "%c", '[');
	mv = DIV_ROUND_UP(min_uV, 1000);

	vset_raw[0] = mv & 0xff;
	vset_raw[1] = (mv >> 8) & 0xf;

	if (!strcmp(rdev->constraints->name, "pm6125_s1"))
		size += sprintf(buf + size, "%d", min_uV / 1000);
	rc = pm6125_write(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
	if (rc < 0) {
		do {
			rc = pm6125_write(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
			if (!rc)
				break;
		} while (rc < 0);
	}
	if (!strcmp(rdev->constraints->name, "pm6125_s1")) {
		size += sprintf(buf + size, "%c", ']');
		print_ipc(ipc_log_reg_s1, "S1", buf, 0, size);
	}
out:
	udelay(50);
	*selector = DIV_ROUND_UP(min_uV - vreg->rdesc.min_uV, VSET_STEP_UV);
	return 0;
}

static int jlq_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);
	u8 vset_raw[2];
	int rc;

	rc = pm6125_read(rdev->regmap, vreg->base + VSET_LB, vset_raw, 2);
	if (rc < 0)
		return rc;
	return (vset_raw[1] << 8 | vset_raw[0]) * 1000;
}

static unsigned int jlq_regulator_get_mode(struct regulator_dev *rdev)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);

	return vreg->mode;
}

static int jlq_regulator_set_mode(struct regulator_dev *rdev,
				   unsigned int mode)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);

	if (mode != REGULATOR_MODE_NORMAL && mode != REGULATOR_MODE_IDLE) {
		dev_err(&rdev->dev, "%s: invalid mode requested %u\n",
			__func__, mode);
		return -EINVAL;
	}
	vreg->mode = mode;

	return 0;
}

static unsigned int jlq_regulator_get_optimum_mode(struct regulator_dev *rdev,
		int input_uV, int output_uV, int load_uA)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);
	unsigned int mode;

	if (load_uA + vreg->system_load_uA >= vreg->hpm_min_load_uA)
		mode = REGULATOR_MODE_NORMAL;
	else
		mode = REGULATOR_MODE_IDLE;

	return mode;
}

static int jlq_regulator_enable(struct regulator_dev *rdev)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);
	u8 val = (1 << 7);
	int rc;

	rc = pm6125_write(rdev->regmap, vreg->base + VSET_EN, &val, 1);
	if (rc < 0)
		return rc;

	vreg->enabled = true;

	return 0;
}

static int jlq_regulator_disable(struct regulator_dev *rdev)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);
	u8 val = (0 << 7);
	int rc;

	rc = pm6125_write(rdev->regmap, vreg->base + VSET_EN, &val, 1);
	if (rc < 0)
		return rc;

	vreg->enabled = false;

	return 0;
}

static int jlq_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct jlq_regulator *vreg = rdev_get_drvdata(rdev);

	return vreg->enabled;
}

static struct regulator_ops jlq_regulator_ops = {
	.enable			= jlq_regulator_enable,
	.disable		= jlq_regulator_disable,
	.is_enabled		= jlq_regulator_is_enabled,
	.set_voltage		= jlq_regulator_set_voltage,
	.get_voltage		= jlq_regulator_get_voltage,
	.list_voltage		= regulator_list_voltage_linear,
	.set_mode		= jlq_regulator_set_mode,
	.get_mode		= jlq_regulator_get_mode,
	.get_optimum_mode	= jlq_regulator_get_optimum_mode,
};

static int jlq_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;
	struct device *dev = &pdev->dev;
	struct jlq_regulator *vreg;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	struct device_node *dev_node;
	int rc;
	u32 base = 0;

	if (!dev->of_node) {
		dev_err(dev, "%s: device node missing\n", __func__);
		return -ENODEV;
	}

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	rc = of_property_read_u32(dev->of_node, "reg", &base);
	if (rc < 0) {
		pr_err("failed to get regulator base rc=%d\n", rc);
		return rc;
	}
	vreg->base = base;

	if (of_property_read_bool(dev->of_node, "send-request"))
		vreg->enable_request = true;
	else
		vreg->enable_request = false;

	init_data = of_get_regulator_init_data(dev, dev->of_node, &vreg->rdesc);
	if (!init_data)
		return -ENOMEM;

	if (!init_data->constraints.name) {
		dev_err(dev, "%s: regulator name not specified\n", __func__);
		return -EINVAL;
	}

	init_data->constraints.input_uV = init_data->constraints.max_uV;
	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_MODE |
						 REGULATOR_CHANGE_DRMS;
	init_data->constraints.valid_modes_mask
			= REGULATOR_MODE_NORMAL | REGULATOR_MODE_IDLE;

	vreg->rdesc.name = devm_kstrdup(dev, init_data->constraints.name,
					GFP_KERNEL);
	if (!vreg->rdesc.name)
		return -ENOMEM;

	vreg->rdesc.supply_name	= "parent";
	vreg->rdesc.ops		= &jlq_regulator_ops;
	vreg->rdesc.owner	= THIS_MODULE;
	vreg->rdesc.type	= REGULATOR_VOLTAGE;
	vreg->dev = dev;
	vreg->regmap = regmap;
	vreg->rdesc.uV_step = VSET_STEP_UV;
	vreg->rdesc.min_uV = init_data->constraints.min_uV;
	vreg->rdesc.n_voltages = ((init_data->constraints.max_uV -
					init_data->constraints.min_uV) / VSET_STEP_UV) + 1;

	reg_config.dev		= dev;
	reg_config.init_data	= init_data;
	reg_config.driver_data	= vreg;
	reg_config.of_node	= dev->of_node;
	reg_config.regmap = regmap;

	rdev = devm_regulator_register(dev, &vreg->rdesc, &reg_config);
	if (IS_ERR(rdev)) {
		rc = PTR_ERR(rdev);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "%s: regulator_register failed\n",
				__func__);
		return rc;
	}

	if (!ch && vreg->enable_request) {
		rc = bridge_name_open("VREGULATOR", 4, &ch, vreg, regulator_notify);
		if (rc) {
			dev_err(dev, "open bridge failed; err=%d\n", rc);
			return rc;
		}
		dev_node = of_find_compatible_node(NULL,
								NULL, "jlq,top-mailbox-base");

		mailbox_arbiter_base = of_iomap(dev_node, 0);
		init_regulator_arbiter();
		init_completion(&cmd_complete);
		spin_lock_init(&mailbox_lock);
		ipc_log_reg_s3s8 = ipc_log_context_create(
				IPC_LOG_PAGES, "jlq-reg-s3s8", 0);
		debugfs_dir = debugfs_create_dir("test-dvfs", NULL);
		if (!debugfs_dir)
			pr_err("dvfs: error creating debugfs directory\n");
		debugfs_create_u32("enable", 0600, debugfs_dir, &enable_dvfs_flag);

	}
	if (!strcmp(rdev->constraints->name, "pm6125_s1"))
		ipc_log_reg_s1 = ipc_log_context_create(
				IPC_LOG_PAGES, "jlq-reg-s1", 0);

	return 0;
}

static const struct of_device_id jlq_regulator_match_table[] = {
	{ .compatible = "jlq-regulator", },
	{}
};

static struct platform_driver jlq_regulator_driver = {
	.driver	= {
		.name = "jlq-regulator",
		.of_match_table = of_match_ptr(jlq_regulator_match_table),
	},
	.probe	= jlq_regulator_probe,
};

int __init jlq_regulator_init(void)
{
	return platform_driver_register(&jlq_regulator_driver);
}

#ifdef MODULE
module_init(jlq_regulator_init);
#else
postcore_initcall(jlq_regulator_init);
#endif

static void __exit jlq_regulator_exit(void)
{
	platform_driver_unregister(&jlq_regulator_driver);
}
module_exit(jlq_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("jlq regulator driver");
MODULE_SOFTDEP("pre: spmi-pmic-arb leds-qpnp-flash-v2 pinctrl-spmi-gpio");
MODULE_SOFTDEP("pre: qcom-i2c-pmic jlq-poweroff qpnp-power-on pwm-qti-lpg");
MODULE_SOFTDEP("pre: qpnp-revid leds-qti-tri-led qcom_pm8008-regulator");
MODULE_SOFTDEP("pre: qpnp-lcdb-regulator rpm-smd-regulator gdsc-regulator");
