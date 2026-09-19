// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Altera Corporation
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * Modified from mach-picoxcell/time.c
 */
#include <linux/delay.h>
#include <linux/dw_apb_timer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/sched_clock.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>

static void __init timer_get_base_and_rate(struct device_node *np,
				    void __iomem **base, u32 *rate)
{
	struct clk *timer_clk;
	struct clk *pclk;
	struct reset_control *rstc;

	*base = of_iomap(np, 0);

	if (!*base)
		panic("Unable to map regs for %pOFn", np);

	/*
	 * Reset the timer if the reset control is available, wiping
	 * out the state the firmware may have left it
	 */
	rstc = of_reset_control_get(np, NULL);
	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		reset_control_deassert(rstc);
	}

	/*
	 * Not all implementations use a periphal clock, so don't panic
	 * if it's not present
	 */
	pclk = of_clk_get_by_name(np, "pclk");
	if (!IS_ERR(pclk))
		if (clk_prepare_enable(pclk))
			pr_warn("pclk for %pOFn is present, but could not be activated\n",
				np);

	timer_clk = of_clk_get_by_name(np, "timer");
	if (IS_ERR(timer_clk))
		goto try_clock_freq;

	if (!clk_prepare_enable(timer_clk)) {
		/* set tclk once specified in dts */
		if (!of_property_read_u32(np, "clock-freq", rate))
			clk_set_rate(timer_clk, *rate);

		*rate = clk_get_rate(timer_clk);
		return;
	}

try_clock_freq:
	if (of_property_read_u32(np, "clock-freq", rate) &&
	    of_property_read_u32(np, "clock-frequency", rate))
		panic("No clock nor clock-frequency property for %pOFn", np);
}

static void __init add_clockevent(struct device_node *event_timer)
{
	void __iomem *iobase;
	struct dw_apb_clock_event_device *ced;
	u32 irq, rate;

	irq = irq_of_parse_and_map(event_timer, 0);
	if (irq == 0)
		panic("No IRQ for clock event timer");

	timer_get_base_and_rate(event_timer, &iobase, &rate);

	ced = dw_apb_clockevent_init(0, event_timer->name, 300, iobase, irq,
				     rate);
	if (!ced)
		panic("Unable to initialise clockevent device");

	dw_apb_clockevent_register(ced);
}

static int __init dw_apb_timer_init(struct device_node *timer)
{
	add_clockevent(timer);

	return 0;
}

#ifdef MODULE
static int dw_apb_timer_probe(struct platform_device *pdev)
{
	return dw_apb_timer_init(pdev->dev.of_node);
}

const static struct of_device_id dw_apb_timer_of_match_table[] = {
	{ .compatible = "jlq,dw-apb-timer", },
	{}
};

static struct platform_driver dw_apb_timer_driver = {
	.probe = dw_apb_timer_probe,
	.driver = {
		.name = "jlq,dw-apb-timer",
		.of_match_table = dw_apb_timer_of_match_table,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(dw_apb_timer_driver);

#else
TIMER_OF_DECLARE(apb_timer, "jlq,dw-apb-timer", dw_apb_timer_init);
#endif

MODULE_DESCRIPTION("JLQ timer driver");
MODULE_LICENSE("GPL");
