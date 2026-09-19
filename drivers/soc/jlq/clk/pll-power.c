// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 */
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#define VDD_NUM		3

struct dev_data {
	struct regulator **supplies;
	unsigned int count;
};

static const char *names[VDD_NUM] = {"supply_0", "supply_1", "supply_2"};

static const struct of_device_id pll_power_match_table[] = {
	{ .compatible = "jlq,pll-power" },
	{ }
};
MODULE_DEVICE_TABLE(of, pll_power_match_table);

static int pll_power_get_reg(struct device *dev, unsigned int count)
{
	struct dev_data *data = dev_get_drvdata(dev);
	struct regulator *reg;
	unsigned int i = 0;
	int ret = 0;

	for (i = 0; i < count; i++) {
		reg = devm_regulator_get(dev, names[i]);
		if (IS_ERR(reg)) {
			ret = PTR_ERR(reg);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "%s: no regulator (%s) found: %d\n",
					__func__, names[i], ret);
			goto free_regulators;
		}

		data->supplies[i] = reg;
	}

	data->count = count;

	return 0;

free_regulators:
	while (i != 0)
		devm_regulator_put(data->supplies[--i]);

	data->supplies = NULL;
	data->count = 0;

	return ret;
}

static int pll_power_put_reg(struct device *dev)
{
	struct dev_data *data = dev_get_drvdata(dev);
	unsigned int i = data->count;

	while (i != 0)
		devm_regulator_put(data->supplies[--i]);

	data->supplies = NULL;
	data->count = 0;

	return 0;
}

static int pll_power_set_volt(struct device *dev)
{
	struct dev_data *data = dev_get_drvdata(dev);
	unsigned int count = data->count;
	unsigned int i = 0;
	unsigned int val[2] = {0};
	char name[20];
	int ret = 0;

	for (i = 0; i < count; i++) {
		sprintf(name, "jlq,%s", names[i]);

		if (of_property_read_u32_array(dev->of_node, name, &val[0], 2)) {
			dev_err(dev, "Unable to get %s\n", name);
			goto fail;
		}

		ret = regulator_set_voltage(data->supplies[i], val[0], val[1]);
		if (ret)
			goto fail;

		ret = regulator_enable(data->supplies[i]);
		if (ret)
			goto fail;
	}

	return 0;
fail:
	while (i != 0)
		regulator_disable(data->supplies[i]);

	return ret;
}

static int pll_power_put_volt(struct device *dev)
{
	struct dev_data *data = dev_get_drvdata(dev);
	unsigned int i = data->count;

	while (i != 0)
		regulator_disable(data->supplies[i]);

	return 0;
}

static int pll_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dev_data *data;
	unsigned int count = VDD_NUM;
	int ret = 0;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->supplies = devm_kcalloc(dev, count,
			sizeof(*data->supplies), GFP_KERNEL);
	if (!data->supplies) {
		ret = -ENOMEM;
		goto free;
	}

	dev_set_drvdata(dev, data);

	ret = pll_power_get_reg(dev, count);
	if (ret)
		goto free;

	ret = pll_power_set_volt(dev);
	if (ret)
		goto put;

	dev_info(dev, "%s registered\n", dev_name(dev));

	return 0;
put:
	pll_power_put_reg(dev);
free:
	if (data->supplies)
		devm_kfree(dev, data->supplies);
	if (data)
		devm_kfree(dev, data);

	return ret;
}

static int pll_power_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pll_power_put_volt(dev);
	pll_power_put_reg(dev);

	return 0;
}

static struct platform_driver pll_power_driver = {
	.probe = pll_power_probe,
	.remove = pll_power_remove,
	.driver = {
		.name = "pll-power",
		.owner = THIS_MODULE,
		.of_match_table = pll_power_match_table,
	},
};

static int __init pll_power_init(void)
{
	return platform_driver_register(&pll_power_driver);
}
subsys_initcall(pll_power_init);

static void __exit pll_power_exit(void)
{
	platform_driver_unregister(&pll_power_driver);
}
module_exit(pll_power_exit);

MODULE_DESCRIPTION("JLQ PLL POWER Driver");
MODULE_LICENSE("GPL v2");
