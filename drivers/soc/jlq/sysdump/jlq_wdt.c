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

#include <linux/clk.h>
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
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched.h>
#include <asm/irq_regs.h>
#include <linux/reset.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/interrupt.h>
#include <jlq_wdt.h>

#define WDOG_CONTROL_REG			0x00
#define WDOG_TIMEOUT_RANGE_REG		0x04
#define WDOG_CURRENT_COUNT_REG		0x08
#define WDOG_COUNTER_RESTART_REG		0x0c
#define WDOG_INTR_STAT_REG		0x10
#define WDOG_INTR_CLR_REG		0x14

#define WDOG_CONTROL_REG_RMOD_ENABLE		BIT(1)
#define WDOG_CONTROL_REG_WDT_EN_MASK		BIT(0)
#define WDOG_COUNTER_RESTART_KICK_VALUE		0x76
#define WDT_MAX_TOP			0xF

struct jlq_wdog_data {
	void __iomem *base;
	struct clk *clk;
	unsigned int clk_rate;
	int bark_irq;
	unsigned int bark_time;
	ktime_t last_pet_time;
	long timer_counter_val;
	struct tm kick_wdog_tm;
	struct device *dev;
	struct reset_control *rstc;
};

static struct jlq_wdog_data *wdog_data;

static inline void jlq_wdt_int_clr(struct jlq_wdog_data *wdog_data)
{
	readl(wdog_data->base + WDOG_INTR_CLR_REG);
}

static inline void jlq_wdt_enable(struct jlq_wdog_data *wdog_data)
{
	unsigned int val;

	val = readl(wdog_data->base + WDOG_CONTROL_REG);
	val |= (1 << 0);
	writel(val, wdog_data->base + WDOG_CONTROL_REG);
}

static inline void jlq_wdt_disable(struct jlq_wdog_data *wdog_data)
{
	unsigned int val;

	val = readl(wdog_data->base + WDOG_CONTROL_REG);
	val &= ~(1 << 0);
	writel(val, wdog_data->base + WDOG_CONTROL_REG);
}

void kick_watchdog(struct jlq_wdog_data *wdt)
{
	writel(WDOG_COUNTER_RESTART_KICK_VALUE, wdt->base +
	       WDOG_COUNTER_RESTART_REG);

	dev_info(wdt->dev, "WDT:Kick watchdog\n");
}

void touch_watchdog(void)
{
	kick_watchdog(wdog_data);
}
EXPORT_SYMBOL(touch_watchdog);

static irqreturn_t jlq_wdt_interrupt(int irq, void *dev_id)
{
	struct jlq_wdog_data *wdt = wdog_data;

	wdog_data->last_pet_time = ktime_get();
	wdog_data->timer_counter_val = arch_timer_read_counter();
	time64_to_tm(ktime_get_real_seconds(), 0 , &wdt->kick_wdog_tm);

	jlq_wdt_int_clr(wdt);
	dev_dbg(wdt->dev, "Watchdog bark!\n");

	return IRQ_HANDLED;
}

static inline unsigned int wdt_top_in_count(unsigned long top)
{
	/*
	 * There are 16 possible timeout values in 0..15 where the number of
	 * cycles is 2 ^ (16 + i) and the watchdog counts down.
	 */

	return (1 << (16 + top)) - 1;
}

static void jlq_wdt_set_rmod(struct jlq_wdog_data *wdog_data)
{
	unsigned int rmod_val;

	rmod_val = readl(wdog_data->base + WDOG_CONTROL_REG);
	rmod_val |= WDOG_CONTROL_REG_RMOD_ENABLE;
	rmod_val |= (0x6 << 2);

	writel(rmod_val, wdog_data->base + WDOG_CONTROL_REG);
}

static void jlq_wdt_set_top(struct jlq_wdog_data *wdog_data)
{
	int i, top_val = WDT_MAX_TOP;
	unsigned int clk;

	if (!wdog_data->clk_rate)
		wdog_data->clk_rate = clk_get_rate(wdog_data->clk);

	clk = wdog_data->clk_rate;

	/*
	 * Iterate over the timeout values until we find the closest match. We
	 * always look for >=.
	 */
	for (i = 0; i <= WDT_MAX_TOP; ++i) {
		if ((unsigned long long)wdt_top_in_count(i) * 1000 / clk >=
		    wdog_data->bark_time) {
			top_val = i;
			break;
		}
	}
	/*
	 * Set the new value in the watchdog.  Some versions of wdog_data
	 * have TOPINIT in the TIMEOUT_RANGE register (as per
	 * CP_WDT_DUAL_TOP in WDT_COMP_PARAMS_1).  On those we
	 * effectively get a pat of the watchdog right here.
	 */
	writel(top_val, wdog_data->base + WDOG_TIMEOUT_RANGE_REG);

	/*
	 * Add an explicit pat to handle versions of the watchdog that
	 * don't have TOPINIT.  This won't hurt on versions that have
	 * it.
	 */
	kick_watchdog(wdog_data);
}

void jlq_wdt_hw_init(struct jlq_wdog_data *wdt)
{
	jlq_wdt_set_rmod(wdt);
	jlq_wdt_set_top(wdt);
	jlq_wdt_enable(wdt);
}

void jlq_trigger_wdog_bite(void)
{

	if (!wdog_data)
		return;

	/* reconfig timeout range register, wdt will bite to start
	 * ramdump flow
	 */
	wdog_data->bark_time = 1000;
	jlq_wdt_set_top(wdog_data);
	kick_watchdog(wdog_data);

	/* Mke sure bite time is written before we reset */
	mb();

	dev_info(wdog_data->dev, "CR: 0x%x, TORR: 0x%x, CCVR: 0x%x, STAT: 0x%x",
	         __raw_readl(wdog_data->base + WDOG_CONTROL_REG),
	         __raw_readl(wdog_data->base + WDOG_TIMEOUT_RANGE_REG),
	         __raw_readl(wdog_data->base + WDOG_COUNTER_RESTART_REG),
	         __raw_readl(wdog_data->base + WDOG_INTR_STAT_REG));

	/* for gki version, KENREL_PANIC_TIMEOUT will use default -1
	 * and kernel will reboot once panic which will corrupt ramdump flow,
	 * just wait here to wait wdt bite
	 */
#if (!IS_ENABLED(CONFIG_JGKI))
	while (1)
		udelay(1);
#endif
}
EXPORT_SYMBOL_GPL(jlq_trigger_wdog_bite);

static void jlq_wdt_hw_reset(struct jlq_wdog_data *wdt)
{
	if (IS_ERR_OR_NULL(wdt->rstc))
		return;
	reset_control_assert(wdt->rstc);
	udelay(2);
	reset_control_deassert(wdt->rstc);
}
void jlq_wdt_hw_deinit(struct jlq_wdog_data *wdt)
{
	jlq_wdt_int_clr(wdt);
	if (likely(!oops_in_progress))
		clk_disable_unprepare(wdt->clk);
	jlq_wdt_hw_reset(wdt);
}

static int jlq_wdt_probe(struct platform_device *pdev)
{
	struct jlq_wdog_data *wdt;
	struct resource *resource;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct jlq_wdog_data),
	                   GFP_KERNEL);
	if (IS_ERR_OR_NULL(wdt)) {
		ret = -ENOMEM;
		goto out;
	}

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(&pdev->dev, resource);
	if (IS_ERR(wdt->base)) {
		ret = PTR_ERR(wdt->base);
		dev_err(&pdev->dev, "Failed to ioremap, err %d\n", ret);
		goto out;
	}

	wdt->clk = devm_clk_get(&pdev->dev, "wdt0_clk");
	if (IS_ERR(wdt->clk)) {
		dev_err(&pdev->dev, "Failed to get clk\n");
		ret = PTR_ERR(wdt->clk);
		goto out;
	}

	if (!of_property_read_u32(pdev->dev.of_node, "clock-freq", &ret))
		clk_set_rate(wdt->clk, ret);

	ret = clk_prepare_enable(wdt->clk);
	if (ret) {
		dev_err(&pdev->dev, "wdt clock enable failed\n");
		return ret;
	}

	wdt->bark_irq = platform_get_irq(pdev, 0);
	if (wdt->bark_irq < 0) {
		dev_err(&pdev->dev, "wdt:get bark_irq fail\n");
		ret =  -ENODEV;
		goto error;
	}

	if (devm_request_irq(&pdev->dev, wdt->bark_irq, jlq_wdt_interrupt,
	                     IRQF_TRIGGER_HIGH, "jlq_wdt", NULL)) {
		ret = -EIO;
		goto error;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "jlq,bark-time",
	                           &wdt->bark_time);
	if (ret) {
		dev_err(&pdev->dev, "reading bark time failed\n");
		return -ENXIO;
	}

	wdt->dev = &pdev->dev;
	wdt->rstc = devm_reset_control_get_exclusive(&pdev->dev, "wdt0-reset");
	if (IS_ERR(wdt->rstc)) {
		dev_err(wdt->dev, "get reset failed\n");
		return PTR_ERR(wdt->rstc);
	}

	platform_set_drvdata(pdev, wdt);
	jlq_wdt_hw_init(wdt);
	wdog_data = wdt;

	return 0;

error:
	clk_disable_unprepare(wdt->clk);
out:
	return ret;
}

static int jlq_wdt_remove(struct platform_device *pdev)
{
	struct jlq_wdog_data *wdog_data = platform_get_drvdata(pdev);

	jlq_wdt_disable(wdog_data);
	clk_disable_unprepare(wdog_data->clk);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int jlq_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct jlq_wdog_data *wdog_data = platform_get_drvdata(pdev);

	kick_watchdog(wdog_data);
	//jlq_wdt_disable(wdog_data);

	return 0;
}

static int jlq_wdt_resume(struct platform_device *pdev)
{
	struct jlq_wdog_data *wdog_data = platform_get_drvdata(pdev);

	//jlq_wdt_hw_init(wdog_data);
	kick_watchdog(wdog_data);

	return 0;
}
#endif

#ifdef CONFIG_OF
static const struct of_device_id wdt_of_match[] = {
	{ .compatible  = "jlq,ap_wdt", },
	{ /*  sentinel */ }
};
MODULE_DEVICE_TABLE(of, wdt_of_match);
#endif

static struct platform_driver jlq_wdt_driver = {
	.probe	= jlq_wdt_probe,
	.remove	= jlq_wdt_remove,
#ifdef CONFIG_PM
	.suspend = jlq_wdt_suspend,
	.resume	= jlq_wdt_resume,
#endif
	.driver = {
		.name = "jlq_wdt",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(wdt_of_match),
#endif
	},
};

static int __init jlq_wdt_init(void)
{
       return platform_driver_register(&jlq_wdt_driver);
}

static void __exit jlq_wdt_exit(void)
{
       platform_driver_unregister(&jlq_wdt_driver);
}

module_init(jlq_wdt_init);
module_exit(jlq_wdt_exit);

MODULE_AUTHOR("JLQ");
MODULE_DESCRIPTION("JLQ watchdog driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: reset-jlq");
