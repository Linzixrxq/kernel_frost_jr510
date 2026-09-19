// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021   JLQ Co.,Ltd

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
 * along with this program; *
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/gcd.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include <linux/of_device.h>
#include <linux/of.h>


#include "clk-jr510.h"
#include "clk-smd.h"
#include "smd.h"

//#define CLK_DEBUG
#ifdef CLK_DEBUG
#define clk_debug(fmt, ...) pr_debug(fmt, ##__VA_ARGS__)
#else
#define clk_debug(fmt, ...)
#endif

typedef enum {
	CLK_GET_RATE    = 0,
	CLK_SET_RATE    = 1,
} CLK_SMD_EVENT;

static int jlq_clksmd_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct jlq_smdclk *sclk = container_of(hw, struct jlq_smdclk, hw);
	unsigned long data = rate;
	int ret = 0;

	pr_debug("%s: rate(%d)\n", __func__, rate);
	ret = clk_smd_send_meassage(CLK_SET_RATE, sclk->id, &data);
	if (ret)
		return ret;

	sclk->rate = data;

	pr_debug("%s: data(%d)\n", __func__, data);
	return ret;
}

static long jlq_clksmd_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	return rate;
}

static unsigned long jlq_clksmd_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct jlq_smdclk *sclk = container_of(hw, struct jlq_smdclk, hw);
	//unsigned long data = 0;
	//int ret = 0;

	pr_debug("%s: rate(%d)\n", __func__, sclk->rate);
	//TBD
	//ret = clk_smd_send_meassage(CLK_GET_RATE, sclk->id, &data);
	//sclk->rate = data;

	/*
	 * RPM handles rate rounding and we don't have a way to
	 * know what the rate will be, so just return whatever
	 * rate was set.
	 */
	return sclk->rate;
}

static const struct clk_ops jlq_clksmd_ops = {
	.recalc_rate = jlq_clksmd_recalc_rate,
	.round_rate = jlq_clksmd_round_rate,
	.set_rate = jlq_clksmd_set_rate,
	.debug_init = jlq_clk_debug_init,
};

static int jlq_smd_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct jlq_smdclk *sclk;
	struct clk_init_data *init;
	struct clk *clk;
	const char *clk_name, **parent_names;
	unsigned int data[2] = {0};
	int ret = 0;

	ret = clk_smd_probe(pdev);
	if (ret)
		return -EINVAL;

	if (of_property_read_string(np, "clock-output-names", &clk_name)) {
		dev_err(&pdev->dev,
			"[%s] %s node doesn't have clock-output-name!\n",
			__func__, np->name);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "jlq,clksmd", &data[0], 2)) {
		dev_err(&pdev->dev,
			"[%s] %s node doesn't have jlq,clksmd property!\n",
			__func__, np->name);
		return -EINVAL;
	}

	/* real parents from rpm */
	parent_names = kzalloc(sizeof(char *), GFP_KERNEL);
	if (!parent_names)
		goto err_out;

	parent_names[0] = of_clk_get_parent_name(np, 0);

	sclk = kzalloc(sizeof(struct jlq_smdclk), GFP_KERNEL);
	if (!sclk)
		goto err_sclk;

	init = kzalloc(sizeof(struct clk_init_data), GFP_KERNEL);
	if (!init)
		goto err_init;

	init->name = clk_name;
	init->ops = &jlq_clksmd_ops;
	init->flags = CLK_IGNORE_UNUSED;
	init->parent_names = parent_names;
	init->num_parents = 1;

	sclk->id = data[0];
	sclk->rate = data[1];
	sclk->hw.init = init;

	clk = clk_register(NULL, &(sclk->hw));
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev,
			"[%s] fail to reigister clk %s!\n",
			__func__, clk_name);
		goto err_reg;
	}

	clk_register_clkdev(clk, clk_name, NULL);
	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	kfree(init);
	kfree(parent_names);
	dev_info(&pdev->dev, "Registered %s clocks\n", clk_name);

	return 0;

err_reg:
	kfree(init);
err_init:
	kfree(sclk);
err_sclk:
	kfree(parent_names);
err_out:
	dev_err(&pdev->dev,
		"[%s] fail to reigister clk %s!\n",
		__func__, clk_name);
	return -EINVAL;
}

static const struct of_device_id jlq_clk_match_table[] = {
	{ .compatible = "jlq,clk-smd", },
	{}
};

static struct platform_driver jlq_smd_driver = {
	.probe = jlq_smd_probe,
	.driver = {
		.name = "jlq-clk-smd",
		.of_match_table = jlq_clk_match_table,
	},
};

static int __init jlq_smd_init(void)
{
	return platform_driver_register(&jlq_smd_driver);
}
core_initcall(jlq_smd_init);

static void __exit jlq_smd_exit(void)
{
	platform_driver_unregister(&jlq_smd_driver);
}
module_exit(jlq_smd_exit);

MODULE_DESCRIPTION("JLQ CLK SMD Driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("post: gdsc-regulator jlq-gdsc-regulator bus-jr510");
