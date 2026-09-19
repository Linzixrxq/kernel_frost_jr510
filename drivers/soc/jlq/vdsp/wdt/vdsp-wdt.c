// SPDX-License-Identifier: GPL-2.0
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched/debug.h>
#include <linux/interrupt.h>

#include "vdsp-wdt.h"

#define WDOG_CONTROL_REG_OFFSET             0x00
#define WDOG_TIMEOUT_RANGE_REG_OFFSET       0x04
#define WDOG_CURRENT_COUNT_REG_OFFSET       0x08
#define WDOG_COUNTER_RESTART_REG_OFFSET     0x0c
#define WDOG_INTR_STAT_REG_OFFSET           0x10
#define WDOG_INTR_CLR_REG_OFFSET            0x14

#define WDOG_CONTROL_REG_WDT_EN_MASK        0x00
#define WDOG_CONTROL_REG_RMOD_ENABLE        0x01
#define WDOG_COUNTER_RESTART_KICK_VALUE     0x76

#define WDOG_CRG_TOP_RST_CTL                0x24C

#define CRG_TOP_PRST_CTL_BIT                7
#define CRG_TOP_TRST_CTL_BIT                6
#define CRG_WRITE_ENABLE                    16

#define WDT_DEFAULT_TIMEOUT 10
#define WDT_MAX_TOP 0xF

#define REG_VALUE(reg)                      ioread32(reg)
#define REG_WRITE(reg, val)                 iowrite32(val, reg)
#define BIT_FIELD(len)                      (((unsigned int)(1 << (len))) - 1)
#define SETBITFIELD_WEN(reg, op, bit, len)  \
		(REG_WRITE(reg, ((REG_VALUE(reg)) \
		&(~(BIT_FIELD(len)<<(bit)))|((unsigned int)(op)<<(bit))\
		&(~(BIT_FIELD(len)<<(bit+16)))|(BIT_FIELD(len)<<(bit+16)))))

#define SETBIT_WEN(reg, bit)            SETBITFIELD_WEN(reg, 1, bit, 1)
#define CLRBIT_WEN(reg, bit)            SETBITFIELD_WEN(reg, 0, bit, 1)

struct vdsp_wdt_dev {
	char name[16];
	void __iomem *base;
	void __iomem *crg_base;
	struct clk *tclk;
	struct clk *pclk;
	int irq;
	struct device *dev;
};

static struct vdsp_wdt_dev *wdt_data;

static int __read_mostly watchdog_thresh = 5;

static atomic_t is_wdt_inited = ATOMIC_INIT(0);


static inline void vdsp_wdt_int_clr(struct vdsp_wdt_dev *wdt_dev)
{
	/* read clear int */
	readl(wdt_dev->base + WDOG_INTR_CLR_REG_OFFSET);
}

static inline void vdsp_wdt_enable(struct vdsp_wdt_dev *wdt_dev)
{
	unsigned int val;

	val = readl(wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
	val |= (1 << WDOG_CONTROL_REG_WDT_EN_MASK);
	writel(val, wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
}

/*Effective before calling vdsp_wdt_enable, Otherwise invalid.*/
static inline void vdsp_wdt_disable(struct vdsp_wdt_dev *wdt_dev)
{
	unsigned int val;

	val = readl(wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
	val &= ~(1 << WDOG_CONTROL_REG_WDT_EN_MASK);
	writel(val, wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
}

static inline unsigned int wdt_top_in_count(unsigned long top)
{
	/*
	 * There are 16 possible timeout values in 0..15 where the number of
	 * cycles is 2 ^ (16 + i) and the watchdog counts down.
	 */

	return (1 << (16 + top))-1;
}

static void dump_info(void)
{
	pr_err("%s Software VDSP WDT3 timeout happen!!!\n", __func__);

	pr_err("%s ### Show current process backtrace ###\n", __func__);
	dump_stack();
}

static irqreturn_t vdsp_wdt_interrupt(int irq, void *dev_id)
{
	struct vdsp_wdt_dev *wdt = (struct vdsp_wdt_dev *)dev_id;

	pr_err("%s: test ENTER Interrupt ================ !!!\n", __func__);
	vdsp_wdt_int_clr(wdt);
	dump_info();

	vdsp_wdt_hw_deinit();

	return IRQ_HANDLED;
}

int dump_vdsp_wdt_reg(void)
{
	unsigned int val;
	struct vdsp_wdt_dev *wdt_dev = wdt_data;

	if (!wdt_dev) {
		pr_err("%s wdt is NULL.\n", __func__);
		return -ENODEV;
	}

	pr_debug("%s WDT3:Start to dump WDT3 resgister\n", __func__);
	val = readl(wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
	pr_debug("%s AP_WDTx_CR = 0x%x\n", __func__, val);
	val = readl(wdt_dev->base + WDOG_TIMEOUT_RANGE_REG_OFFSET);
	pr_debug("%s AP_WDTx_TORR = 0x%x\n", __func__, val);
	val = readl(wdt_dev->base + WDOG_CURRENT_COUNT_REG_OFFSET);
	pr_debug("%s AP_WDTx_CCVR = 0x%x\n", __func__, val);
	val = readl(wdt_dev->base + WDOG_INTR_STAT_REG_OFFSET);
	pr_debug("%s AP_WDTx_STAT = 0x%x\n", __func__, val);
	pr_debug("%s WDT3:End to dump WDT3 resgister\n", __func__);

	return 0;
}

static void vdsp_wdt_restart_counter(struct vdsp_wdt_dev *wdt_dev)
{
	writel(WDOG_COUNTER_RESTART_KICK_VALUE,
		wdt_dev->base + WDOG_COUNTER_RESTART_REG_OFFSET);
}

static inline void vdsp_wdt_set_rst_time(struct vdsp_wdt_dev *wdt_dev)
{
	unsigned int val;

	val = readl(wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
	val |= (0x6 << 2); //2+128pclk
	writel(val, wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
}

static void vdsp_wdt_set_rmod(struct vdsp_wdt_dev *wdt_dev)
{
	unsigned int rmod_val;

	rmod_val = readl(wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
	rmod_val |= (1 << WDOG_CONTROL_REG_RMOD_ENABLE);

	writel(rmod_val, wdt_dev->base + WDOG_CONTROL_REG_OFFSET);
}

static void vdsp_wdt_set_top(struct vdsp_wdt_dev *wdt_dev)
{
	int i, top_val = WDT_MAX_TOP;
	unsigned long clk = clk_get_rate(wdt_dev->tclk);

	/*
	 * Iterate over the timeout values until we find the closest match. We
	 * always look for >=.
	 */

	for (i = 0; i <= WDT_MAX_TOP; ++i) {
		if ((unsigned long long)wdt_top_in_count(i)*1000/clk >=
				watchdog_thresh * 1000) {
			top_val = i;
			break;
		}
	}
	/*
	 * Set the new value in the watchdog.  Some versions of wdt_dev
	 * have TOPINIT in the TIMEOUT_RANGE register (as per
	 * CP_WDT_DUAL_TOP in WDT_COMP_PARAMS_1).  On those we
	 * effectively get a pat of the watchdog right here.
	 */
	writel(11, wdt_dev->base + WDOG_TIMEOUT_RANGE_REG_OFFSET);
}

static void crg_hw_reset(struct vdsp_wdt_dev *wdt_dev)
{
	/* assert preset and treset*/
	CLRBIT_WEN(wdt_dev->crg_base + WDOG_CRG_TOP_RST_CTL, CRG_TOP_TRST_CTL_BIT);
	CLRBIT_WEN(wdt_dev->crg_base + WDOG_CRG_TOP_RST_CTL, CRG_TOP_PRST_CTL_BIT);
	udelay(2);
	/* de-assert preset and treset*/
	SETBIT_WEN(wdt_dev->crg_base + WDOG_CRG_TOP_RST_CTL, CRG_TOP_PRST_CTL_BIT);
	SETBIT_WEN(wdt_dev->crg_base + WDOG_CRG_TOP_RST_CTL, CRG_TOP_TRST_CTL_BIT);
}

static void crg_hw_deinit(struct vdsp_wdt_dev *wdt_dev)
{
	if (wdt_dev->tclk)
		clk_disable_unprepare(wdt_dev->tclk);
	if (wdt_dev->pclk)
		clk_disable_unprepare(wdt_dev->pclk);

	crg_hw_reset(wdt_dev);
}

static int crg_hw_init(struct vdsp_wdt_dev *wdt_dev)
{
	int ret = 0;

	ret = clk_prepare_enable(wdt_dev->tclk);
	if (ret) {
		pr_err("%s:%d enable tclk failed.\n", __func__, __LINE__);
		goto out;
	}

	ret = clk_prepare_enable(wdt_dev->pclk);
	if (ret) {
		pr_err("%s:%d enable pclk failed.\n", __func__, __LINE__);
		goto pclk_err;
	}

	return ret;

pclk_err:
	clk_disable_unprepare(wdt_dev->tclk);
out:
	return ret;
}

int vdsp_wdt_hw_deinit(void)
{
	struct vdsp_wdt_dev *wdt_dev = wdt_data;

	if (!wdt_dev) {
		pr_err("%s wdt is NULL.\n", __func__);
		return -ENODEV;
	}

	if (atomic_read(&is_wdt_inited)) {
		dump_vdsp_wdt_reg();

		crg_hw_deinit(wdt_dev);
		atomic_set(&is_wdt_inited, 0);
	}

	return 0;
}
EXPORT_SYMBOL(vdsp_wdt_hw_deinit);

int vdsp_wdt_hw_init(void)
{
	struct vdsp_wdt_dev *wdt_dev = wdt_data;

	if (!wdt_dev) {
		pr_err("%s wdt is NULL.\n", __func__);
		return -ENODEV;
	}

	if (!atomic_read(&is_wdt_inited)) {
		/* init crg wdt3 clk and reset */
		if (crg_hw_init(wdt_dev)) {
			pr_err("%s:%d crg init failed.\n", __func__, __LINE__);
			return -1;
		}

		/*set TOP value*/
		vdsp_wdt_set_top(wdt_dev);

		/*set rmod:1*/
		vdsp_wdt_set_rmod(wdt_dev);

		/*set rest continue time*/
		vdsp_wdt_set_rst_time(wdt_dev);

		/*WDOG enable*/
		vdsp_wdt_enable(wdt_dev);

		/* restart counter, top effectively */
		vdsp_wdt_restart_counter(wdt_dev);

		atomic_set(&is_wdt_inited, 1);
	}

	return 0;
}
EXPORT_SYMBOL(vdsp_wdt_hw_init);

static int vdsp_wdt_parse_dt(struct platform_device *pdev,
		struct vdsp_wdt_dev *wdt)
{
	struct resource *resource;
	char *key;
	int ret = -ENODEV;
	struct device_node *dev_node;
	int tclk_freq;

	if (!wdt)
		return -EINVAL;

	snprintf(wdt->name, sizeof(wdt->name), "vdsp_wdt3");

	key = "wdt3_base";
	resource = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!resource) {
		dev_err(&pdev->dev, "Fail to get vdsp wdt3 resource\n");
		ret = -ENODEV;
		goto out;
	}
	wdt->base = devm_ioremap_resource(&pdev->dev, resource);
	if (IS_ERR(wdt->base)) {
		dev_err(&pdev->dev, "Failed to wdt ioremap\n");
		ret = PTR_ERR(wdt->base);
		goto out;
	}

	dev_node = of_find_compatible_node(NULL, NULL, "jlq,crg-base");
	if (!dev_node) {
		dev_err(&pdev->dev, "jlq,crg-base No compatible node found\n");
		return -ENODEV;
	}
	wdt->crg_base = of_iomap(dev_node, 0);
	if (IS_ERR(wdt->crg_base)) {
		dev_err(&pdev->dev, "crg_base reg ioremap failed\n");
		return -ENOMEM;
	}
	of_node_put(dev_node);

	wdt->tclk = devm_clk_get(&pdev->dev, "wdt3_tclk");
	if (IS_ERR(wdt->tclk)) {
		dev_err(&pdev->dev, "Failed to get wdt3 tclk\n");
		ret = PTR_ERR(wdt->tclk);
		goto out;
	}

	if (!of_property_read_u32(pdev->dev.of_node, "clock-freq", &tclk_freq))
		clk_set_rate(wdt->tclk, tclk_freq);

	wdt->pclk = devm_clk_get(&pdev->dev, "wdt3_pclk");
	if (IS_ERR(wdt->pclk)) {
		dev_err(&pdev->dev, "Failed to get wdt3 pclk\n");
		ret = PTR_ERR(wdt->pclk);
		goto unprepare_tclk;
	}

	key = "wdt3_bite";
	wdt->irq = platform_get_irq_byname(pdev, key);
	if (wdt->irq < 0) {
		dev_err(&pdev->dev, "wdt3 get irq fail\n");
		ret = -ENODEV;
		goto unprepare_clk;
	}

	if (devm_request_irq(&pdev->dev, wdt->irq, vdsp_wdt_interrupt,
			 IRQF_TRIGGER_HIGH, "jlq_vdsp_wdt", wdt)) {
		dev_err(&pdev->dev, "wdt3 request irq fail\n");
		ret = -EIO;
		goto unprepare_clk;
	}

	return 0;

unprepare_clk:
	devm_clk_put(&pdev->dev, wdt->pclk);
unprepare_tclk:
	devm_clk_put(&pdev->dev, wdt->tclk);
out:
	return ret;
}

static int vdsp_wdt_probe(struct platform_device *pdev)
{
	struct vdsp_wdt_dev *wdt;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct vdsp_wdt_dev),
			   GFP_KERNEL);
	if (wdt == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	ret = vdsp_wdt_parse_dt(pdev, wdt);
	if (ret) {
		dev_err(&pdev->dev, "parse dts failed.\n");
		goto out;
	}

	wdt_data = wdt;
	platform_set_drvdata(pdev, wdt);
	wdt->dev = &pdev->dev;

	crg_hw_init(wdt);
	crg_hw_deinit(wdt);
	pr_info("%s: vdsp wdt3 probe successfully.\n", __func__);

	return 0;

out:
	return ret;
}

static int vdsp_wdt_remove(struct platform_device *pdev)
{
	struct vdsp_wdt_dev *wdt = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	pr_info("%s:%d vdsp wdt3 remove.\n", __func__, __LINE__);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id vdsp_wdt_of_match[] = {
	{ .compatible  = "jlq,vdsp_wdt", },
	{ /*  sentinel */ }
};
MODULE_DEVICE_TABLE(of, vdsp_wdt_of_match);
#endif

static struct platform_driver vdsp_wdt_driver = {
	.probe = vdsp_wdt_probe,
	.remove = vdsp_wdt_remove,
	.driver = {
		.name = "jlq_vdsp_wdt",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(vdsp_wdt_of_match),
#endif
	},
};

static int __init vdsp_wdt_init(void)
{
	int ret;

	ret = platform_driver_register(&vdsp_wdt_driver);
	if (ret) {
		pr_err("Fail to register vdsp wdt driver\n");
		return -1;
	}

	pr_info("%s:Success to register vdsp wdt driver\n", __func__);
	return 0;
}

subsys_initcall(vdsp_wdt_init);

MODULE_DESCRIPTION("JLQ JR510 vdsp watchdog driver");
MODULE_LICENSE("GPL v2");
