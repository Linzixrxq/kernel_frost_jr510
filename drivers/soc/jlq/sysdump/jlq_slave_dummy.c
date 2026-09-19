// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
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
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/freezer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <asm/irq_regs.h>
#include <asm/cacheflush.h>
#include <asm-generic/cacheflush.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>


#define PD_ARADDR_RPT_OFF(n)             (16 * n + 0x0)
#define PD_ARID_RPT_OFF(n)               (16 * n + 0x4)
#define PD_AWADDR_RPT_OFF(n)             (16 * n + 0x8)
#define PD_AWID_RPT_OFF(n)               (16 * n + 0xc)
#define PD_EN_CFG(n)                     (0x7c + n * 4)
#define PD_INT_CLR_CFG                   (0xac)
#define PD_RD_WR_INT_RPT                 (PD_INT_CLR_CFG + 4)
#define PD_RD_WR_INT_EN_CFG              (PD_INT_CLR_CFG + 8)

// bit definition of PD_EN_CFG
#define PD_RD_ERROR_EN_BIT		BIT(1)
#define PD_WR_ERROR_EN_BIT		BIT(0)

// bit definition of PD_RD_WR_INT_RPT
#define PD_RPT_RD_BIT			BIT(0)
#define PD_RPT_WR_BIT			BIT(1)
#define DUMMY_SLAVE_NAME_LEN	16

struct jlq_dummy_data {
	void __iomem        *base;
	struct device       *dev;
	int                  irq;
	int                  supported_slaves;
	int                  *slave_rpt_id;
	int                  reported;
	char                 *slave_names[0];
};

struct jlq_dummy_data *g_data;

struct dummy_report {
	uint32_t	pd_araddr;
	uint32_t	pd_arid;
	uint32_t	pd_awaddr;
	uint32_t	pd_awid;
};

static void dummy_write(struct jlq_dummy_data *data, uint32_t val, uint32_t reg)
{
	writel(val, data->base + reg);
}

static uint32_t dummy_read(struct jlq_dummy_data *data, uint32_t reg)
{
	return readl(data->base + reg);
}

static void dummy_get_report(struct jlq_dummy_data *data, int id,
		struct dummy_report *report)
{
	dev_info(data->dev, "report id %d\n", id);
	report->pd_araddr = dummy_read(data,
			PD_ARADDR_RPT_OFF(id));
	report->pd_arid = dummy_read(data,
			PD_ARID_RPT_OFF(id));
	report->pd_awaddr = dummy_read(data,
			PD_AWADDR_RPT_OFF(id));
	report->pd_awid = dummy_read(data,
			PD_AWID_RPT_OFF(id));
}

static void dummy_check(struct jlq_dummy_data *data)
{
	uint32_t reg, i, status;
	struct dummy_report report;

	reg = dummy_read(data, PD_RD_WR_INT_RPT);
	if (!reg)
		return;

	dummy_write(data, 0, PD_RD_WR_INT_EN_CFG);

	for (i = 0; i < data->supported_slaves; i++) {
		status = (reg >> (2 * i)) & 0x3;
		if (status)
			break;
	}

	dummy_write(data, 0x3 << i, PD_INT_CLR_CFG);
	dev_err(data->dev, "dummy PD_RD_WR_INT_RPT = 0x%x  raw id %d\n", reg, i);
	dummy_get_report(data, data->slave_rpt_id[i], &report);

	if (status & PD_RPT_RD_BIT)
		dev_info(data->dev, "dummy slave %s araddr 0x%x arid %d\n",
			data->slave_names[i], report.pd_araddr, report.pd_arid);
	else if (status & PD_RPT_WR_BIT)
		dev_info(data->dev, "dummy slave %s awaddr 0x%x awid %d\n",
			data->slave_names[i], report.pd_awaddr, report.pd_awid);

}

static irqreturn_t dummy_irq_handler(int irq, void *dev_id)
{
	struct jlq_dummy_data *data = dev_id;

	dummy_check(data);
	dev_err(data->dev, "irq reported!\n");
	return IRQ_HANDLED;
}

static int dummy_panic_notifier(struct notifier_block *nb, unsigned long action, void *p)
{
	dummy_check(g_data);
	return NOTIFY_DONE;
}
static struct notifier_block dummy_nb = {
	.notifier_call = dummy_panic_notifier,
};

static void jlq_dummy_cfg(struct jlq_dummy_data *data)
{
	uint32_t val = 0;
	int i;

	for (i = 0; i < data->supported_slaves; i++) {
		val |= 0x3 << (i<<1);
		dummy_write(data,
			PD_RD_ERROR_EN_BIT | PD_WR_ERROR_EN_BIT,
			PD_EN_CFG(i));
	}

	dummy_write(data, val, PD_INT_CLR_CFG);
	dummy_write(data, val, PD_RD_WR_INT_EN_CFG);
}

static void  __maybe_unused jlq_dummy_test(struct jlq_dummy_data *data)
{
	void __iomem *base;

	// ceti base, trigger pd_read action which will cause buserr on A55.
	//base = ioremap(0x30600000, 0x1000);
	 base = ioremap(0x30400000, 0x1000); //gpu
	//base = ioremap(0x30500000, 0x1000);//vpu
	//base = ioremap(0x30300000, 0x1000);//ai
	if (!base)
		dev_err(data->dev, "ioremap ceti base failed\n");
	// display pd read
	//dev_info(data->dev, "read return 0x%x\n", readl(base));
	writel(0, base);
}

static int jlq_dummy_parse_dt(struct platform_device *pdev,
	struct jlq_dummy_data *data)
{
	int ret = 0;
	int len;
	char *key;
	struct resource *res;
	char **str_arr;
	char **slave_name;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	key = "dummy-slaves";
	data->supported_slaves =
		of_property_count_strings(pdev->dev.of_node, key);

	if (data->supported_slaves < 0 || data->supported_slaves > 20) {
		dev_err(&pdev->dev, "invalid dummy-slaves property");
		return -EINVAL;
	}

	len = sizeof(struct jlq_dummy_data);
	len += data->supported_slaves * sizeof(char *);
	data = krealloc(data, len, GFP_KERNEL);
	if (IS_ERR(data)) {
		dev_err(&pdev->dev, "krealloc failed\n");
		return PTR_ERR(data);
	}

	str_arr = kmalloc(data->supported_slaves * sizeof(char *), GFP_KERNEL);
	ret = of_property_read_string_array(pdev->dev.of_node,
		key, (const char **)str_arr, data->supported_slaves);
	if (ret <= 0) {
		dev_err(&pdev->dev, "get dummy-slaves failed\n");
		goto error;
	}

	slave_name = data->slave_names;
	for (len = 0; len < data->supported_slaves; len++)
		slave_name[len] = kstrdup(str_arr[len], GFP_KERNEL);

	key = "dummy-rpt-id";
	data->slave_rpt_id = devm_kmalloc(data->dev,
		sizeof(int) * data->supported_slaves, GFP_KERNEL);
	of_property_read_u32_array(pdev->dev.of_node,
		key, data->slave_rpt_id,  data->supported_slaves);

	kfree(str_arr);
	return 0;
error:
	kfree(str_arr);
	return ret;
}

static int jlq_dummy_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct jlq_dummy_data *data;

	data = devm_kzalloc(&pdev->dev,
			sizeof(struct jlq_dummy_data), GFP_KERNEL);
	if (IS_ERR_OR_NULL(data)) {
		dev_err(&pdev->dev, "alloc mem failed\n");
		return -ENOMEM;
	}

	data->dev = &pdev->dev;
	ret = jlq_dummy_parse_dt(pdev, data);
	if (ret) {
		dev_err(&pdev->dev, "parse dt failed\n");
		return -ENODEV;
	}

	data->irq = platform_get_irq_byname(pdev, "top_sysctl_irq");
	if (data->irq < 0) {
		dev_err(&pdev->dev, "get bus error rst failed\n");
		ret =  -ENODEV;
		goto error;
	}

	ret = devm_request_irq(&pdev->dev, data->irq,
		dummy_irq_handler, IRQF_SHARED,
		"dummy_salve_err", (void *)data);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed err %d\n", ret);
		goto error;
	}

	jlq_dummy_cfg(data);
	g_data = data;
	atomic_notifier_chain_register(&panic_notifier_list, &dummy_nb);
	//jlq_dummy_test(data);
	pr_info("Success to register dummy driver\n");

	return 0;
error:
	return ret;
}

static int jlq_dummy_remove(struct platform_device *pdev)
{
	int i;
	struct jlq_dummy_data *data = dev_get_drvdata(&pdev->dev);
	char **slave_name = data->slave_names;

	for (i = 0; i < data->supported_slaves; i++)
		kfree(slave_name[i]);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id jlq_dummy_of_match[] = {
	{ .compatible  = "jlq,dummy", },
	{ /*  sentinel */ }
};
MODULE_DEVICE_TABLE(of, jlq_dummy_of_match);
#endif

static struct platform_driver jlq_dummy_driver = {
	.probe = jlq_dummy_probe,
	.remove = jlq_dummy_remove,
	.driver = {
		.name = "jlq,dummy",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(jlq_dummy_of_match),
	},
};

static int __init jlq_dummy_init(void)
{
       int r = platform_driver_register(&jlq_dummy_driver);

       if (r < 0)
               pr_err("register failed %d", r);

       return r;
}

static void __exit jlq_dummy_exit(void)
{
       platform_driver_unregister(&jlq_dummy_driver);
}

module_init(jlq_dummy_init);
module_exit(jlq_dummy_exit);

MODULE_DESCRIPTION("JLQ bus dummy driver");
MODULE_LICENSE("GPL");
