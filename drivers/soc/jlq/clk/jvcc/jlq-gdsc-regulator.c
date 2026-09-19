// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>

#define REG_OFFSET 0x0
#define PWR_ON_MASK		BIT(1)
#define PWR_OFF_MASK	BIT(0)
#define QCHANNEL_MASK	(7 << 13)

struct gdsc {
	struct regulator_dev	*rdev;
	struct regulator_desc	rdesc;
	void __iomem		*gdscr;
	struct regmap           *regmap;
	struct regulator	*parent_regulator;
	bool				use_qchannel;
	bool				is_enabled;
	u32					enable_val;
	u32					disable_val;
	u32					retry_count;
};

static int poll_gdsc_status(struct gdsc *sc, bool enable)
{
	struct regmap *regmap = sc->regmap;
	int count = sc->retry_count;
	u32 val = 0;

	for (; count > 0; count--) {
		regmap_read(regmap, REG_OFFSET, &val);
		if (enable) {
			if ((val & PWR_ON_MASK) == 0x0) {
				if (sc->use_qchannel) {
					if ((val & QCHANNEL_MASK) == 0x0)
						return 0;
				} else {
					return 0;
				}
			}
		} else {
			if (sc->use_qchannel) {
				if ((val & QCHANNEL_MASK) == QCHANNEL_MASK) {
					if ((val & PWR_OFF_MASK) == 0x0)
						return 0;
				}
			} else {
				if ((val & PWR_OFF_MASK) == 0x0)
					return 0;
			}
		}
		udelay(5);
	}

	return -ETIMEDOUT;
}

static int gdsc_is_enabled(struct regulator_dev *rdev)
{
	struct gdsc *sc = rdev_get_drvdata(rdev);
	bool is_enabled = sc->is_enabled;

	if (sc->parent_regulator) {
		/*
		 * The parent regulator for the GDSC is required to be on to
		 * make any register accesses to the GDSC base. Return false
		 * if the parent supply is disabled.
		 */
		if (regulator_is_enabled(sc->parent_regulator) <= 0)
			return false;
	}

	return is_enabled;
}

static int gdsc_enable(struct regulator_dev *rdev)
{
	struct gdsc *sc = rdev_get_drvdata(rdev);
	int ret = 0;

	if (sc->parent_regulator)
		;
	regmap_write(sc->regmap, REG_OFFSET, sc->enable_val);

	ret = poll_gdsc_status(sc, 1);
	if (ret)
		dev_err(&rdev->dev, "[%s] poll fail!!\n", __func__);

	sc->is_enabled = 1;

	return ret;
}

static int gdsc_disable(struct regulator_dev *rdev)
{
	struct gdsc *sc = rdev_get_drvdata(rdev);
	int ret = 0;

	if (sc->parent_regulator)
		;
	regmap_write(sc->regmap, REG_OFFSET, sc->disable_val);

	ret = poll_gdsc_status(sc, 0);
	if (ret)
		dev_err(&rdev->dev, "[%s] poll fail!!\n", __func__);

	sc->is_enabled = 0;

	return ret;
}

static struct regulator_ops gdsc_ops = {
	.is_enabled = gdsc_is_enabled,
	.enable = gdsc_enable,
	.disable = gdsc_disable,
};

static const struct regmap_config gdsc_regmap_config = {
	.reg_bits   = 32,
	.reg_stride = 4,
	.val_bits   = 32,
	.fast_io    = true,
};

static int gdsc_parse_dt_data(struct gdsc *sc, struct device *dev,
				struct regulator_init_data **init_data)
{
	int ret;

	*init_data = of_get_regulator_init_data(dev, dev->of_node, &sc->rdesc);
	if (*init_data == NULL)
		return -ENOMEM;

	if (of_get_property(dev->of_node, "parent-supply", NULL))
		(*init_data)->supply_regulator = "parent";

	ret = of_property_read_string(dev->of_node, "regulator-name",
					&sc->rdesc.name);
	if (ret)
		return ret;

	sc->use_qchannel = of_property_read_bool(dev->of_node, "jlq,use-qchannel");

	of_property_read_u32(dev->of_node, "jlq,enable-value", &sc->enable_val);
	of_property_read_u32(dev->of_node, "jlq,disable-value", &sc->disable_val);
	of_property_read_u32(dev->of_node, "jlq,retry-count", &sc->retry_count);

	return 0;
}

static int gdsc_get_resources(struct gdsc *sc, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "Failed to get address resource\n");
		return -EINVAL;
	}

	sc->gdscr = devm_ioremap(dev, res->start, resource_size(res));
	if (sc->gdscr == NULL)
		return -ENOMEM;

	sc->regmap = devm_regmap_init_mmio(dev, sc->gdscr, &gdsc_regmap_config);
	if (!sc->regmap) {
		dev_err(dev, "Couldn't get regmap\n");
		return -EINVAL;
	}

	if (of_find_property(dev->of_node, "vdd_parent-supply", NULL)) {
		sc->parent_regulator = devm_regulator_get(dev, "vdd_parent");
		if (IS_ERR(sc->parent_regulator)) {
			ret = PTR_ERR(sc->parent_regulator);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Unable to get vdd_parent regulator, ret=%d\n",
					ret);
			return ret;
		}
	}

	return 0;
}

static int gdsc_probe(struct platform_device *pdev)
{
	static atomic_t gdsc_count = ATOMIC_INIT(-1);
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data = NULL;
	struct gdsc *sc;
	int ret;

	sc = devm_kzalloc(&pdev->dev, sizeof(*sc), GFP_KERNEL);
	if (sc == NULL)
		return -ENOMEM;

	ret = gdsc_parse_dt_data(sc, &pdev->dev, &init_data);
	if (ret)
		return ret;

	ret = gdsc_get_resources(sc, pdev);
	if (ret)
		return ret;

	sc->rdesc.id = atomic_inc_return(&gdsc_count);
	sc->rdesc.ops = &gdsc_ops;
	sc->rdesc.type = REGULATOR_VOLTAGE;
	sc->rdesc.owner = THIS_MODULE;

	reg_config.dev = &pdev->dev;
	reg_config.init_data = init_data;
	reg_config.driver_data = sc;
	reg_config.of_node = pdev->dev.of_node;
	reg_config.regmap = sc->regmap;

	sc->rdev = devm_regulator_register(&pdev->dev, &sc->rdesc, &reg_config);
	if (IS_ERR(sc->rdev)) {
		ret = PTR_ERR(sc->rdev);
		dev_err(&pdev->dev, "regulator_register(\"%s\") failed, ret=%d\n",
			sc->rdesc.name, ret);
		return ret;
	}

	platform_set_drvdata(pdev, sc);
	return 0;
}

static int gdsc_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id gdsc_match_table[] = {
	{ .compatible = "jlq,gdsc" },
	{}
};

static struct platform_driver gdsc_driver = {
	.probe  = gdsc_probe,
	.remove = gdsc_remove,
	.driver = {
		.name = "jlq-gdsc",
		.of_match_table = gdsc_match_table,
	},
};

static int __init gdsc_init(void)
{
	return platform_driver_register(&gdsc_driver);
}
subsys_initcall(gdsc_init);

static void __exit gdsc_exit(void)
{
	platform_driver_unregister(&gdsc_driver);
}
module_exit(gdsc_exit);

MODULE_DESCRIPTION("JLQ GDSC Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:jlq-gdsc-regulator");
