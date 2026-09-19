// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/platform/jlq/irq/mailbox-irq-JA310.c
 *
 * Copyright (c) 2019-2021   JLQ Co.,Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

/*
 * AP-CP interrupt manage
 */
struct top_mailbox_chip_data {
	struct device_node *np;
	struct irq_domain *domain;
	void __iomem *irq_rbase;
	int parent_irq;
	int nr_irqs;
	raw_spinlock_t irq_mailbox_lock;
};

#define	AP_INTR_STA_NUM		1
#define	AP_INTR_SRC_BITS		32
#define TOP_MAILBOX_AP_SINTR_SET            (0x00)
#define TOP_MAILBOX_CM42AP_NINTR_SET        (0x30)
#define TOP_MAILBOX_ADSP2AP_NINTR_SET       (0x360)
#define TOP_MAILBOX_VDSP2AP_NINTR_SET       (0x380)
enum {
	IRQ_SET = 0,
	IRQ_EN = 0x04,
	IRQ_SRC_EN = 0x08,
	IRQ_STA = 0x0C,
	IRQ_STA_RAW0 = 0x10,
	IRQ_STA_RAW1 = 0x14,
};

static inline int irq_set_wake_common(int irq_start, int irq_end,
	int irq_src, int trigger, int enable)
{
	struct irq_desc *desc;
	int irq;
	int ret = 0;

	for (irq  = irq_start; irq <= irq_end; ++irq) {
		desc = irq_to_desc(irq);
		if (!desc || irq == irq_src)
			continue;

		if (irqd_is_wakeup_set(&desc->irq_data)) {
			ret = 1;
			break;
		}
	}

	if (ret)
		return 0;

	desc = irq_to_desc(trigger);
	if (desc && desc->irq_data.chip && desc->irq_data.chip->irq_set_wake)
		ret = desc->irq_data.chip->irq_set_wake(&desc->irq_data,
							enable);

	return ret;

}

static int top_mailbox_irq_set_wake(struct irq_data *d, unsigned int enable)
{
	struct top_mailbox_chip_data *data;

	if (!d)
		return -EINVAL;

	data = d->chip_data;

	return irq_set_wake_common(irq_find_mapping(data->domain, 0),
				irq_find_mapping(d->domain, data->nr_irqs - 1),
				d->irq, data->parent_irq, enable);
}

static void top_mailbox_irq_ack(struct irq_data *d)
{

}

static void top_mailbox_irq_mask(struct irq_data *d)
{
	struct top_mailbox_chip_data *data = d->chip_data;
	int mb_irq = d->hwirq;
	unsigned long val;

	raw_spin_lock(&data->irq_mailbox_lock);
	val = readl(data->irq_rbase + IRQ_SRC_EN);
	val &= ~(1 << mb_irq);
	writel(val, data->irq_rbase + IRQ_SRC_EN);
	raw_spin_unlock(&data->irq_mailbox_lock);
}

static void top_mailbox_irq_unmask(struct irq_data *d)
{
	struct top_mailbox_chip_data *data = d->chip_data;
	int mb_irq = d->hwirq;
	unsigned long val;

	raw_spin_lock(&data->irq_mailbox_lock);
	val = readl(data->irq_rbase + IRQ_SRC_EN);
	val &= ~(1 << mb_irq);
	writel(val, data->irq_rbase + IRQ_SRC_EN);
	val |= (1 << mb_irq);
	writel(val, data->irq_rbase + IRQ_SRC_EN);
	raw_spin_unlock(&data->irq_mailbox_lock);

}

static struct irq_chip top_mailbox_irq_chip = {
	.name		= "TOP MAILBOX",
	.irq_ack	= top_mailbox_irq_ack,
	.irq_mask	= top_mailbox_irq_mask,
	.irq_unmask	= top_mailbox_irq_unmask,
	.irq_set_wake	= top_mailbox_irq_set_wake,
	.irq_enable	= top_mailbox_irq_unmask,
	.irq_disable	= top_mailbox_irq_mask,
};

static void top_mailbox_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_data *mbox_irq_data = irq_desc_get_irq_data(desc);
	struct top_mailbox_chip_data *data =
		(struct top_mailbox_chip_data *)mbox_irq_data->chip_data;
	unsigned long status;
	int bit;
	int i;
	unsigned int irq_lxy;

	chained_irq_enter(chip, desc);

	for (i = 0; i < AP_INTR_STA_NUM; i++) {
		status = readl(data->irq_rbase + IRQ_STA);
		for_each_set_bit(bit, &status, AP_INTR_SRC_BITS) {
			/* Clear interrupt. */
			writel(1 << bit, data->irq_rbase + IRQ_STA);

			/* Check interrupt status. */
			while (readl(data->irq_rbase + IRQ_STA)
				& (1 << bit)) {
				writel(1 << bit, data->irq_rbase + IRQ_STA);
			}

			irq_lxy = irq_find_mapping(data->domain,
						bit + i * AP_INTR_SRC_BITS);
			generic_handle_irq(irq_lxy);
		}
	}
	chained_irq_exit(chip, desc);
}

static int top_mailbox_irqdomain_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	struct top_mailbox_chip_data *data = d->host_data;

	irq_set_chip_and_handler(irq, &top_mailbox_irq_chip,
				 handle_simple_irq);
	irq_set_chip_data(irq, data);
	//set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static const struct irq_domain_ops top_mailbox_irqdomain_ops = {
	.map = top_mailbox_irqdomain_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static int jlq_mailbox_probe(struct platform_device *pdev)
{
	struct top_mailbox_chip_data *data;
	struct resource *resource;
	struct irq_desc *desc;
	struct device_node *node = pdev->dev.of_node;
	int parent_irq;
	int irq;

	data = kzalloc(sizeof(struct top_mailbox_chip_data), GFP_ATOMIC);

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->irq_rbase = devm_ioremap_resource(&pdev->dev, resource);
	if (!data->irq_rbase) {
		pr_err("%s: get irq reg base addr failed!\n", __func__);
		goto err;
	}

	writel(1, data->irq_rbase + IRQ_EN);

	raw_spin_lock_init(&data->irq_mailbox_lock);

	of_property_read_u32(node, "irq_num", &data->nr_irqs);
	data->domain = irq_domain_add_linear(node, data->nr_irqs,
					&top_mailbox_irqdomain_ops, data);
	if (!data->domain) {
		pr_err("mailbox irq couldn't register IRQ domain\n");
		goto err;
	}

	/* create an IRQ mapping for each valid IRQ */
	for (irq = 0; irq < data->nr_irqs; irq++)
		irq_create_mapping(data->domain, irq);

	parent_irq = irq_of_parse_and_map(node, 0);
	data->parent_irq = parent_irq;
	irq_set_chained_handler(parent_irq, top_mailbox_irq_handler);
	desc = irq_to_desc(parent_irq);
	desc->irq_data.chip_data = data;

	if (irq_set_irq_wake(parent_irq, 1))
		pr_err("set irq %d wake up error\n", parent_irq);

	dev_info(&pdev->dev, "mailbox irq initialized\n");
	return 0;
err:
	kfree(data);
	return -ENODEV;
}

const static struct of_device_id jlq_mailbox_of_match_table[] = {
	{ .compatible = "jlq,top-mbox-irq-sec", },
	{ .compatible = "jlq,top-mbox-irq-cm4nosec", },
	{ .compatible = "jlq,top-mbox-irq-vdspnosec", },
	{ .compatible = "jlq,top-mbox-irq-adspnosec", },
	{}
};
static struct platform_driver jlq_mailbox_drv = {
	.probe = jlq_mailbox_probe,
	.driver = {
		.name = "jlq_mailbox",
		.of_match_table = jlq_mailbox_of_match_table,
		.owner = THIS_MODULE,
	},
};

static int __init jlq_mailbox_init(void)
{
       return platform_driver_register(&jlq_mailbox_drv);
}

static void __exit jlq_mailbox_exit(void)
{
       platform_driver_unregister(&jlq_mailbox_drv);
}

#ifdef MODULE
module_init(jlq_mailbox_init);
#else
core_initcall(jlq_mailbox_init);
#endif
module_exit(jlq_mailbox_exit);
MODULE_LICENSE("GPL");
