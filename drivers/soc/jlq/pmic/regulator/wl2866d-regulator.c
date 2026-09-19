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
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

#define VSET_EN		 0x0E

#define DIV_ROUND(x, len) (((x) + (len) - 1) / (len))
#define ROUND_UP(x, align) DIV_ROUND(x, align) * (align)

struct jlq_wl2866d_regulator {
	struct regulator_desc	rdesc;
	struct regmap		*regmap;
	struct device		*dev;
	int			voltage;
	bool			enabled;
	u16			reg;
	u16			step;
	u16			id;
};

/* common functions */
static int wl2866d_read(struct regmap *regmap,  u8 reg, u8 *val, int count)
{
	int rc;

	rc = regmap_bulk_read(regmap, reg, val, count);
	if (rc < 0)
		pr_err("Failed to read 0x%04x\n", reg);

	return rc;
}

static int wl2866d_write(struct regmap *regmap, u8 reg, u8 *val, int count)
{
	int rc;

	pr_debug("Writing 0x%02x to 0x%04x\n", *val, reg);
	rc = regmap_bulk_write(regmap, reg, val, count);
	if (rc < 0)
		pr_err("Failed to write 0x%04x\n", reg);

	return rc;
}

static int wl2866d_masked_write(struct regmap *regmap, u8 reg, u8 mask, u8 val)
{
	int rc;

	pr_debug("Writing 0x%02x to 0x%04x with mask 0x%02x\n", val, reg, mask);
	rc = regmap_update_bits(regmap, reg, mask, val);
	if (rc < 0)
		pr_err("Failed to write 0x%02x to 0x%04x with mask 0x%02x\n", val, reg, mask);

	return rc;
}

static int lc_wl2866d_init(struct regmap *regmap)
{
	int rc = 0, i =0;
	u8 reg[16] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
	u8 val[16] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	for(i = 0;i < 16;i++)
	{
		rc = wl2866d_write(regmap, reg[i], &val[i], 1);
		if(rc < 0)
		{
			pr_err("wl2866d:write reg[%d] fail\n",i);
		}
	}
	return rc;
}

static int jlq_wl2866d_regulator_set_voltage(struct regulator_dev *rdev, int min_uV,
				  int max_uV, unsigned int *selector)
{
	struct jlq_wl2866d_regulator *vreg = rdev_get_drvdata(rdev);
	u8 vset_raw = 0;
	int mv, rc;

	mv = ROUND_UP(min_uV, vreg->step);

	if (mv > rdev->constraints->max_uV)
		mv = rdev->constraints->max_uV;

	vset_raw = (mv - vreg->rdesc.min_uV) / vreg->step;

	rc = wl2866d_write(rdev->regmap, vreg->reg, &vset_raw, 1);
	if (rc < 0)
		return rc;

	*selector = DIV_ROUND_UP(min_uV - vreg->rdesc.min_uV, vreg->step);
	return 0;
}

static int jlq_wl2866d_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct jlq_wl2866d_regulator *vreg = rdev_get_drvdata(rdev);
	u8 vset_raw;
	int rc;

	rc = wl2866d_read(rdev->regmap, vreg->reg, &vset_raw, 1);
	if (rc < 0)
		return rc;
	return (vreg->rdesc.min_uV + vset_raw * vreg->step);
}

static int jlq_wl2866d_regulator_enable(struct regulator_dev *rdev)
{
	struct jlq_wl2866d_regulator *vreg = rdev_get_drvdata(rdev);
	u8 val = (1 << vreg->id);
	u8 mask = (1 << vreg->id);
	int rc;

	rc = wl2866d_masked_write(rdev->regmap, VSET_EN, mask, val);
	if (rc < 0)
		return rc;

	vreg->enabled = true;

	return 0;
}

static int jlq_wl2866d_regulator_disable(struct regulator_dev *rdev)
{
	struct jlq_wl2866d_regulator *vreg = rdev_get_drvdata(rdev);
	u8 val = ~(1 << vreg->id) & 0xf;
	u8 mask = (1 << vreg->id);
	int rc;

	rc = wl2866d_masked_write(rdev->regmap, VSET_EN, mask, val);
	if (rc < 0)
		return rc;

	vreg->enabled = false;

	return 0;
}

static int jlq_wl2866d_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct jlq_wl2866d_regulator *vreg = rdev_get_drvdata(rdev);

	return vreg->enabled;
}

static struct regulator_ops jlq_wl2866d_regulator_ops = {
	.enable			= jlq_wl2866d_regulator_enable,
	.disable		= jlq_wl2866d_regulator_disable,
	.is_enabled		= jlq_wl2866d_regulator_is_enabled,
	.set_voltage		= jlq_wl2866d_regulator_set_voltage,
	.get_voltage		= jlq_wl2866d_regulator_get_voltage,
	.list_voltage		= regulator_list_voltage_linear,
};

static int jlq_wl2866d_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;
	struct device *dev = &pdev->dev;
	struct jlq_wl2866d_regulator *vreg;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	int rc;
	u32 reg = 0;
	u32 step = 0;
	u32 id = 0;

	if (!dev->of_node) {
		dev_err(dev, "%s: device node missing\n", __func__);
		return -ENODEV;
	}

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		pr_err("Parent regmap is missing\n");
		return -EINVAL;
	}

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

    lc_wl2866d_init(regmap);

	rc = of_property_read_u32(dev->of_node, "vol-reg", &reg);
	if (rc < 0) {
		pr_err("failed to get regulator base rc=%d\n", rc);
		return rc;
	}
	vreg->reg = reg;

	rc = of_property_read_u32(dev->of_node, "step", &step);
	if (rc < 0) {
		pr_err("failed to get regulator step rc=%d\n", rc);
		return rc;
	}
	vreg->step = step;

	rc = of_property_read_u32(dev->of_node, "id", &id);
	if (rc < 0) {
		pr_err("failed to get regulator step rc=%d\n", rc);
		return rc;
	}
	vreg->id = id;

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
	vreg->rdesc.ops		= &jlq_wl2866d_regulator_ops;
	vreg->rdesc.owner	= THIS_MODULE;
	vreg->rdesc.type	= REGULATOR_VOLTAGE;
	vreg->dev = dev;
	vreg->regmap = regmap;
	vreg->rdesc.uV_step = vreg->step;
	vreg->rdesc.min_uV = init_data->constraints.min_uV;
	vreg->rdesc.n_voltages = ((init_data->constraints.max_uV -
					init_data->constraints.min_uV) / vreg->step) + 1;

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

	return 0;
}

static const struct of_device_id jlq_wl2866d_regulator_match_table[] = {
	{ .compatible = "wl2866d-regulator", },
	{}
};

static struct platform_driver jlq_wl2866d_regulator_driver = {
	.driver	= {
		.name = "wl2866d-regulator",
		.of_match_table = of_match_ptr(jlq_wl2866d_regulator_match_table),
	},
	.probe	= jlq_wl2866d_regulator_probe,
};

int __init jlq_wl2866d_regulator_init(void)
{
	return platform_driver_register(&jlq_wl2866d_regulator_driver);
}
postcore_initcall(jlq_wl2866d_regulator_init);

static void __exit jlq_wl2866d_regulator_exit(void)
{
	platform_driver_unregister(&jlq_wl2866d_regulator_driver);
}
module_exit(jlq_wl2866d_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("jlq wl2866d regulator driver");
MODULE_SOFTDEP("pre: wl2866d");
