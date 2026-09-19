
/* Copyright 2019~2020 JLQ Technology Co., Ltd. or its affiliates.
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License version 2 as published
* by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
*/

#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/bitops.h>
#include <dt-bindings/jlq/jr510/reset.h>

struct jlq_reset_controller {
	struct reset_controller_dev rst;
	struct regmap *map;
};

#define to_jlq_reset_controller(_rst) \
	container_of(_rst, struct jlq_reset_controller, rst)

static int jlq_reset_program_hw(struct reset_controller_dev *rcdev,
				   unsigned long idx, bool assert)
{
	struct jlq_reset_controller *rc = to_jlq_reset_controller(rcdev);
	unsigned int offset = idx >> 10;
	int type = (idx >> 8) & 0x3;
	unsigned int mask = BIT(idx & 0x1f);

	if (type == RESET_BITWE_HIGH_ACTIVE)
		WARN((idx & 0x1f) > 15, "bits should < 15 in bitwe mode");

	if (assert) {
		switch (type) {
		case RESET_HIGH_ACTIVE:
			return regmap_update_bits(rc->map, offset, mask, mask);
		case RESET_BITWE_HIGH_ACTIVE:
			return regmap_write(rc->map, offset,
					mask | (mask << 16));
		case RESET_LOW_ACTIVE:
			return regmap_update_bits(rc->map, offset, mask, 0);
		default:
			pr_warn("%s invalide idx 0x%lx\n", __func__, idx);
			return 0;
		}
	} else {
		switch (type) {
		case RESET_HIGH_ACTIVE:
			return regmap_update_bits(rc->map, offset, mask, 0);
		case RESET_BITWE_HIGH_ACTIVE:
			return regmap_write(rc->map, offset,
				0 | (mask << 16));
		case RESET_LOW_ACTIVE:
			return regmap_update_bits(rc->map, offset, mask, mask);
		default:
			pr_warn("%s invalide idx 0x%lx\n", __func__, idx);
			return 0;
		}
	}
	return 0;
}

static int jlq_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long idx)
{
	return jlq_reset_program_hw(rcdev, idx, true);
}

static int jlq_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long idx)
{
	return jlq_reset_program_hw(rcdev, idx, false);
}

static int jlq_reset_dev(struct reset_controller_dev *rcdev,
			    unsigned long idx)
{
	int err;

	err = jlq_reset_assert(rcdev, idx);
	if (err)
		return err;

	return jlq_reset_deassert(rcdev, idx);
}

static struct reset_control_ops jlq_reset_ops = {
	.reset    = jlq_reset_dev,
	.assert   = jlq_reset_assert,
	.deassert = jlq_reset_deassert,
};

static int jlq_reset_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *reset_spec)
{
	unsigned int offset, bit, type;

	offset = reset_spec->args[0];
	bit = reset_spec->args[1];
	type = reset_spec->args[2];

	return (offset << 10) | (type << 8) | bit;
}

static int jlq_reset_probe(struct platform_device *pdev)
{
	struct jlq_reset_controller *rc;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;

	rc = devm_kzalloc(dev, sizeof(*rc), GFP_KERNEL);
	if (!rc)
		return -ENOMEM;

	rc->map = syscon_regmap_lookup_by_phandle(np, "jlq,rst-syscon");
	if (IS_ERR(rc->map)) {
		dev_err(dev, "failed to get jlq,rst-syscon\n");
		return PTR_ERR(rc->map);
	}

	rc->rst.ops = &jlq_reset_ops,
	rc->rst.of_node = np;
	rc->rst.of_reset_n_cells = 3;
	rc->rst.of_xlate = jlq_reset_xlate;

	return reset_controller_register(&rc->rst);
}

static const struct of_device_id jlq_reset_match[] = {
	{ .compatible = "jlq,ja310-reset", },
	{},
};
MODULE_DEVICE_TABLE(of, jlq_reset_match);

static struct platform_driver jlq_reset_driver = {
	.probe = jlq_reset_probe,
	.driver = {
		.name = "jlq-reset",
		.of_match_table = jlq_reset_match,
	},
};

static int __init jlq_reset_init(void)
{
	return platform_driver_register(&jlq_reset_driver);
}
arch_initcall(jlq_reset_init);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jlq-reset");
MODULE_DESCRIPTION("JLQ Ja310 Reset Driver");
MODULE_SOFTDEP("pre: jlq-gpio");
