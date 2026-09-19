// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-18 Linaro Limited

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/regmap.h>
#include <linux/input/qpnp-power-on.h>
#include <asm/system_misc.h>
#include <linux/pm.h>
#include <linux/input/qpnp-power-on.h>

#define RTC_ALARM_DATA1		0x40

enum {
	REBOOT_NONE,
	REBOOT_NORMAL,
	REBOOT_RECOVERY,
	REBOOT_BOOTLOADER,
	REBOOT_PANIC = 0xa,
	REBOOT_MAX
};
struct jr510_pon {
	struct device *dev;
	struct regmap *regmap;
	u32		base;
	struct reboot_mode_driver reboot_mode;
};

struct jr510_pon *g_pon;

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

	pr_debug("Writing 0x%02x to 0x%04x\n", val, reg);
	rc = regmap_bulk_write(regmap, reg, val, count);
	if (rc < 0)
		pr_err("failed to write 0x%04x\n", reg);

	return rc;
}

static int jlq_reboot_notifier(struct notifier_block *nb,
				unsigned long action, void *p)
{
	if (action == SYS_RESTART) {
		u8 reboot_reason = REBOOT_NORMAL;
		char *cmd = (char *)p;
		u8 val = 0;

		if (cmd) {
			if (!strcmp(cmd, "recovery"))
				reboot_reason = REBOOT_RECOVERY;
			else if (!strcmp(cmd, "bootloader"))
				reboot_reason = REBOOT_BOOTLOADER;
			else {
				reboot_reason = REBOOT_NORMAL;
				qpnp_pon_system_pwr_off(PON_POWER_OFF_HARD_RESET);
			}
		}

		pm6125_write(g_pon->regmap, g_pon->base + RTC_ALARM_DATA1, &reboot_reason, 1);
	} else if (action == SYS_POWER_OFF) {
		qpnp_pon_system_pwr_off(PON_POWER_OFF_SHUTDOWN);
	}

	return NOTIFY_DONE;
}

static struct notifier_block jlq_reboot_nb = {
	.notifier_call = jlq_reboot_notifier,
};
static int jlq_panic_notifier(struct notifier_block *nb,
	unsigned long action, void *p)
{
	u8 reboot_reason = REBOOT_PANIC;

	pm6125_write(g_pon->regmap, g_pon->base + RTC_ALARM_DATA1, &reboot_reason, 1);

	return NOTIFY_DONE;
}

static struct notifier_block jlq_panic_nb = {
	.notifier_call = jlq_panic_notifier,
};


static int jr510_pon_probe(struct platform_device *pdev)
{
	struct jr510_pon *pon;
	int error;

	pon = devm_kzalloc(&pdev->dev, sizeof(*pon), GFP_KERNEL);
	if (!pon)
		return -ENOMEM;

	pon->dev = &pdev->dev;

	pon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pon->regmap) {
		dev_err(&pdev->dev, "failed to locate regmap\n");
		return -ENODEV;
	}

	error = of_property_read_u32(pdev->dev.of_node, "reg",
				     &pon->base);
	if (error)
		return error;

	register_reboot_notifier(&jlq_reboot_nb);
	atomic_notifier_chain_register(&panic_notifier_list, &jlq_panic_nb);

	g_pon = pon;

	platform_set_drvdata(pdev, pon);

	return 0;
}

static const struct of_device_id jr510_pon_id_table[] = {
	{ .compatible = "jlq,jr510-restart" },
	{ }
};
MODULE_DEVICE_TABLE(of, jr510_pon_id_table);

static struct platform_driver jr510_pon_driver = {
	.probe = jr510_pon_probe,
	.driver = {
		.name = "jr510-restart",
		.of_match_table = of_match_ptr(jr510_pon_id_table),
	},
};
module_platform_driver(jr510_pon_driver);

MODULE_DESCRIPTION("jr510 Power On driver");
MODULE_LICENSE("GPL v2");
