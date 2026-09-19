
/*
 * Copyright 2021~2023 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/freezer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched/debug.h>
#include <asm/irq_regs.h>
#include <linux/reset.h>
#include <linux/gfp.h>
#include <soc/jlq/jr510/lpm_irq_ctrl.h>

typedef uint8_t irq_num_type;

struct lpm_irq_ctrl_device {
	void __iomem *base;
	int nr_mappings;
	struct device *dev;
	spinlock_t lock;
	irq_num_type *mapping; // max mapped spi_irq num: 255
};

#define LPM_IRQ_CTRL_REG_SIZE	32
#define SPI_IRQ(spi_irq_num)	(spi_irq_num + 32)

struct lpm_irq_ctrl_device *lpm_irq_ctrl;

static inline int jlq_lpm_find_mapping_irq(int hwirq)
{
	int bit_nr;
	irq_num_type *map = lpm_irq_ctrl->mapping;

	for (bit_nr = 0; bit_nr < lpm_irq_ctrl->nr_mappings; bit_nr++) {
		if (SPI_IRQ(map[bit_nr]) == hwirq)
			return bit_nr;
		else
			continue;
	}
	return -ENOENT;
}

int jlq_lpm_irq_enable(int virq)
{
	struct irq_desc *desc;
	int val, bitnr;

	if (unlikely(lpm_irq_ctrl == NULL)) {
		pr_info("lpm irq ctrl driver is not ready\n");
		return -EAGAIN;
	}

	desc = irq_to_desc(virq);
	if (!desc)
		return -EINVAL;

	bitnr = jlq_lpm_find_mapping_irq(desc->irq_data.hwirq);
	if (bitnr < 0) {
		dev_info(lpm_irq_ctrl->dev, "hwirq %ld is not mapped\n",
			desc->irq_data.hwirq);
		return -ENOENT;
	}

	dev_dbg(lpm_irq_ctrl->dev, "enable virq %d hwirq %ld bitnr %d",
		virq, desc->irq_data.hwirq, bitnr);

	spin_lock(&lpm_irq_ctrl->lock);

	/*
	 * enable lpm irq line
	 */
	val = readl(lpm_irq_ctrl->base + bitnr / 32);
	val &= ~BIT(bitnr % 32);
	writel(val, lpm_irq_ctrl->base + bitnr / 32);


	spin_unlock(&lpm_irq_ctrl->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(jlq_lpm_irq_enable);

int jlq_lpm_irq_clear(int virq)
{
	struct irq_desc *desc;
	int val, bitnr;

	if (unlikely(lpm_irq_ctrl == NULL)) {
		pr_info("lpm irq ctrl driver is not ready\n");
		return -EAGAIN;
	}

	desc = irq_to_desc(virq);
	if (!desc)
		return -EINVAL;

	bitnr = jlq_lpm_find_mapping_irq(desc->irq_data.hwirq);
	if (bitnr < 0) {
		dev_info(lpm_irq_ctrl->dev, "hwirq %ld is not mapped\n",
			desc->irq_data.hwirq);
		return -ENOENT;
	}

	dev_dbg(lpm_irq_ctrl->dev, "clear virq %d hwirq %ld bitnr %d",
		virq, desc->irq_data.hwirq, bitnr);

	spin_lock(&lpm_irq_ctrl->lock);

	/*
	 * clear and disable lpm irq line
	 */
	val = readl(lpm_irq_ctrl->base + bitnr / 32);
	writel(val | BIT(bitnr % 32), lpm_irq_ctrl->base + bitnr / 32);
	/*
	 * re-enable lpm irq line
	 */
	val &= ~BIT(bitnr);
	writel(val, lpm_irq_ctrl->base + bitnr / 32);

	spin_unlock(&lpm_irq_ctrl->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(jlq_lpm_irq_clear);

int jlq_lpm_irq_ctrl_dt_probe(struct platform_device *pdev,
			struct lpm_irq_ctrl_device *lpm_irq)
{
	int ret;
	char *key;
	int i;

	key = "irq-mappings";
	ret = of_property_count_u8_elems(pdev->dev.of_node, key);
	if (ret <= 0) {
		dev_err(&pdev->dev, "no irq-mappings specified, err %d\n", ret);
		return -ENOENT;
	}

	lpm_irq->nr_mappings = ret;
	dev_dbg(&pdev->dev, "get nr_mapping = %d", ret);

	lpm_irq->mapping = krealloc(lpm_irq->mapping,
		sizeof(struct lpm_irq_ctrl_device) +
		lpm_irq->nr_mappings * sizeof(irq_num_type), GFP_KERNEL);
	if (IS_ERR_OR_NULL(lpm_irq->mapping)) {
		dev_err(&pdev->dev, "realloc failed\n");
		return ret;
	}

	ret = of_property_read_u8_array(pdev->dev.of_node, (const char *)key,
		lpm_irq->mapping, lpm_irq->nr_mappings);
	if (ret < 0) {
		dev_err(&pdev->dev, "parse prop:%s failed\n", key);
		return ret;
	}

	for (i = 0; i < lpm_irq->nr_mappings; i++)
		dev_dbg(&pdev->dev, "map num %d -> hwirq = %d\n",
				i, lpm_irq->mapping[i]);

	return ret;
}
static int lpm_irq_ctrl_probe(struct platform_device *pdev)
{
	struct lpm_irq_ctrl_device *lpm_ctrl;
	struct resource *resource;
	int ret;

	lpm_ctrl = devm_kzalloc(&pdev->dev,
		sizeof(struct lpm_irq_ctrl_device), GFP_KERNEL);

	if (IS_ERR_OR_NULL(lpm_ctrl)) {
		ret = -ENOMEM;
		goto out;
	}

	ret = jlq_lpm_irq_ctrl_dt_probe(pdev, lpm_ctrl);
	if (ret) {
		dev_err(&pdev->dev, "parse lpm_irq_ctrl dt failed\n");
		return ret;
	}

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!resource) {
		dev_err(&pdev->dev, "Fail to get lpm-irq-ctrl resource\n");
		ret = -EINVAL;
		goto out;
	}

	if (resource_size(resource) <
	    (ALIGN(lpm_ctrl->nr_mappings, 32) / 8)) {
		dev_err(&pdev->dev, "mapping irqs larger than reg resource!");
		return -ENOENT;
	}

	lpm_ctrl->base = devm_ioremap_resource(&pdev->dev, resource);
	if (IS_ERR(lpm_ctrl->base)) {
		dev_err(&pdev->dev, "Failed to ioremap\n");
		ret = PTR_ERR(lpm_ctrl->base);
		goto out;
	}

	spin_lock_init(&lpm_ctrl->lock);
	platform_set_drvdata(pdev, lpm_ctrl);
	lpm_ctrl->dev = &pdev->dev;
	lpm_irq_ctrl = lpm_ctrl;

	return 0;

out:
	return ret;
}

static int lpm_irq_ctrl_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id lpm_irq_of_match[] = {
	{ .compatible  = "jlq,lpm-irq-ctrl", },
	{ /*  sentinel */ }
};
MODULE_DEVICE_TABLE(of, lpm_irq_of_match);
#endif

static struct platform_driver lpm_irq_ctrl_driver = {
	.probe = lpm_irq_ctrl_probe,
	.remove = lpm_irq_ctrl_remove,
	.driver = {
		.name = "lpm_irq_ctrl",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(lpm_irq_of_match),
#endif
	},
};
static int __init lpm_irq_ctl_init(void)
{
	int ret;

	ret = platform_driver_register(&lpm_irq_ctrl_driver);
	if (ret) {
		pr_err("Fail to lpm irq ctrl driver\n");
		return -1;
	}

	return 0;
}

postcore_initcall(lpm_irq_ctl_init)

MODULE_AUTHOR("JLQ");
MODULE_DESCRIPTION("JLQ LPM irq control driver");
MODULE_LICENSE("GPL");
