// SPDX-License-Identifier: GPL-2.0-only
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regmap.h>

struct jlq_pmic_debug {
	struct regmap *regmap;
	u16				reg;
	u16				count;
};

static ssize_t show_count(struct device *dev, struct device_attribute *attr,
						 char *buf)
{
	struct jlq_pmic_debug *pmic_debug = dev_get_drvdata(dev);
	unsigned int count = pmic_debug->count;

	return scnprintf(buf, PAGE_SIZE, "0x%02x\n", count);
}

static ssize_t store_count(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct jlq_pmic_debug *pmic_debug = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	pmic_debug->count = value;

	return count;
}

static DEVICE_ATTR(count, 0660, show_count, store_count);

static ssize_t pmic_show_reg(struct device *dev, struct device_attribute *attr,
						 char *buf)
{
	struct jlq_pmic_debug *pmic_debug = dev_get_drvdata(dev);
	unsigned int reg = pmic_debug->reg;

	return scnprintf(buf, PAGE_SIZE, "0x%02x\n", reg);
}

static ssize_t pmic_store_reg(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct jlq_pmic_debug *pmic_debug = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	pmic_debug->reg = value;

	return count;
}

static DEVICE_ATTR(pmic_reg, 0660, pmic_show_reg, pmic_store_reg);

static ssize_t pmic_show_reg_val(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 value;
	struct jlq_pmic_debug *pmic_debug = dev_get_drvdata(dev);
	struct regmap *regmap = pmic_debug->regmap;

	regmap_bulk_read(regmap, pmic_debug->reg, &value, 1);

	return scnprintf(buf, PAGE_SIZE, "0x%02x\n", value);
}

static ssize_t pmic_store_reg_val(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct jlq_pmic_debug *pmic_debug = dev_get_drvdata(dev);
	struct regmap *regmap = pmic_debug->regmap;
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	regmap_bulk_write(regmap, pmic_debug->reg, &value, 1);

	return count;
}

static DEVICE_ATTR(pmic_reg_val, 0660, pmic_show_reg_val,
		pmic_store_reg_val);

int pmic_create_sysfs(struct device *dev)
{
	int ret;

	ret = device_create_file(dev, &dev_attr_count);
	if (ret)
		return ret;

	ret = device_create_file(dev, &dev_attr_pmic_reg);
	if (ret)
		return ret;

	ret = device_create_file(dev, &dev_attr_pmic_reg_val);
	if (ret)
		return ret;

	return ret;
}

static int jlq_pmic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct jlq_pmic_debug *pmic_debug;

	if (!dev->of_node) {
		dev_err(dev, "%s: device node missing\n", __func__);
		return -ENODEV;
	}

	pmic_debug = devm_kzalloc(&pdev->dev, sizeof(*pmic_debug), GFP_KERNEL);
	if (!pmic_debug)
		return -ENOMEM;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}
	pmic_debug->regmap = regmap;

	dev_set_drvdata(&pdev->dev, pmic_debug);

	pmic_create_sysfs(&pdev->dev);

	return 0;
}

static const struct of_device_id jlq_pmic_match_table[] = {
	{ .compatible = "jlq-pmic-debug", },
	{}
};

static struct platform_driver jlq_pmic_driver = {
	.driver	= {
		.name = "jlq-pmic-debug",
		.of_match_table = of_match_ptr(jlq_pmic_match_table),
	},
	.probe	= jlq_pmic_probe,
};

int __init jlq_pmic_init(void)
{
	return platform_driver_register(&jlq_pmic_driver);
}
arch_initcall(jlq_pmic_init);

static void __exit jlq_pmic_exit(void)
{
	platform_driver_unregister(&jlq_pmic_driver);
}
module_exit(jlq_pmic_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("jlq pmic driver");
