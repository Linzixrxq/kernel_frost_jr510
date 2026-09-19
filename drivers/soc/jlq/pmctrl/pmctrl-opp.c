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
#include <linux/pm_opp.h>
#include <opp.h>
#include "pmctrl.h"

#define VDD_NUM		2
struct opp_data {
	unsigned long freq;
	struct dev_pm_opp_supply supplies[2];
};

struct pmctrl_opps {
	unsigned int id;
	unsigned int reg_cnt;
	struct opp_table *opp_table;
	struct opp_data *opp;
	unsigned int opp_cnt;
};

static const char * const names[] = {"vdd_core", "vdd_ddr"};

static int find_supply_name(struct device *dev, const char *name)
{
	struct device_node *np;
	struct property *pp;
	char names[50];
	unsigned int found = 0;

	np = of_node_get(dev->of_node);

	/* This must be valid for sure */
	if (WARN_ON(!np))
		return 0;

	strcpy(names, name);
	strcat(names, "-supply");
	pp = of_find_property(np, names, NULL);
	if (pp)
		found = 1;

	of_node_put(np);
	return found;
}

static int parse_of_add_opp(struct device *dev)
{
	struct pmctrl_opps *data = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	struct dev_pm_opp *opp;
	unsigned long freq;
	unsigned int i = 0, j = 0;
	int ret = 0;

	for (i = 0; i < VDD_NUM; i++) {
		if (find_supply_name(dev, names[i]))
			data->reg_cnt++;
	}

	if (data->reg_cnt) {
		data->opp_table = dev_pm_opp_set_regulators(dev,
				names, data->reg_cnt);
		if (IS_ERR(data->opp_table)) {
			ret = PTR_ERR(data->opp_table);
			dev_err(dev, "%s: failed to set regulator, %d\n",
					__func__, ret);
			goto out;
		}
	}

	ret = of_property_read_u32(np, "opp-supplies", &data->id);
	if (ret) {
		dev_err(dev, "%s: couldn't find opp-supplies, %d\n", __func__, ret);
		goto put_reg;
	}

	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "%s: couldn't find opp table, %d\n",
				__func__, ret);
		goto put_reg;
	}

	ret = dev_pm_opp_get_opp_count(dev);
	if (ret <= 0) {
		dev_err(dev, "%s: couldn't get opp count, %d\n",
				__func__, ret);
		goto put_opp;
	}

	data->opp_cnt = ret;

	data->opp = devm_kcalloc(dev, data->opp_cnt, sizeof(*data->opp),
			GFP_KERNEL);
	if (!data->opp) {
		ret = -ENOMEM;
		goto put_opp;
	}

	for (i = 0, freq = 0; i < data->opp_cnt; i++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto free;
		}
		data->opp[i].freq = freq;

		for (j = 0; j < data->reg_cnt; j++)
			data->opp[i].supplies[j].u_volt = opp->supplies[j].u_volt;

		dev_pm_opp_put(opp);
	}

	return 0;
free:
	devm_kfree(dev, data->opp);
put_opp:
	dev_pm_opp_of_remove_table(dev);
put_reg:
	if (data->opp_table)
		dev_pm_opp_put_regulators(data->opp_table);
out:
	return ret;
}

static void parse_of_remove_opp(struct device *dev)
{
	struct pmctrl_opps *data = dev_get_drvdata(dev);

	devm_kfree(dev, data->opp);
	dev_pm_opp_of_remove_table(dev);

	if (data->opp_table)
		dev_pm_opp_put_regulators(data->opp_table);
}

static int pmctrl_opp_send_msg(struct device *dev)
{
	struct pmctrl_opps *data = dev_get_drvdata(dev);
	unsigned int cmd = PMCTL_MSG_SET_OPP;
	unsigned int id = data->id;
	unsigned int msg[4] = {0, 0, 0, 0};
	unsigned int i = 0, j = 0, k = 0;
	struct pmctl_msg *ack;
	int ret = 0;

	for (i = 0; i < data->opp_cnt; i++) {
		k = 0;
		memset(msg, 0, sizeof(msg));
		msg[k++] = data->reg_cnt;
		msg[k++] = data->opp[i].freq / 1000;
		for (j = 0; j < data->reg_cnt; j++)
			msg[k++] = data->opp[i].supplies[j].u_volt / 1000;

		dev_dbg(dev, "%s: id(0x%x), opp(%d), "
				"msg[0] = %lu, msg[1] = %lu, msg[2] = %lu, msg[3] = %lu\n",
				__func__, id, i, msg[0], msg[1], msg[2], msg[3]);

		ack = pmctrl_send_wait_ack_msg(cmd, id, msg);

		if (ack->status != SMD_EVENT_SUCCESS)
			dev_info(dev, "%s: no such freq %lu: id(0x%x), opp(%d), "
					"msg[0] = %lu, msg[1] = %lu, msg[2] = %lu, msg[3] = %lu\n",
					__func__, data->opp[i].freq, id, i, msg[0],
					msg[1], msg[2], msg[3]);
	}

	return ret;
}

static int pmctrl_opp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pmctrl_opps *data;
	int ret = 0;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		dev_err(dev, "%s: data malloc failed\n", __func__);
		goto out;
	}

	dev_set_drvdata(dev, data);

	ret = parse_of_add_opp(dev);
	if (ret) {
		dev_err(dev, "%s: parse opp failed, %d\n", __func__, ret);
		goto free;
	}

	ret = pmctrl_opp_send_msg(dev);
	if (ret) {
		dev_err(dev, "%s: opp send msg failed, %d\n", __func__, ret);
		goto remove;
	}

	dev_info(dev, "PMCTRL OPP Registered\n");

	return 0;
remove:
	parse_of_remove_opp(dev);
free:
	devm_kfree(dev, data);
out:
	return ret;
}

static const struct of_device_id pmctrl_opp_match_table[] = {
	{ .compatible = "jlq,pmctrl-opp", },
	{}
};

static struct platform_driver pmctrl_opp_driver = {
	.probe = pmctrl_opp_probe,
	.driver = {
		.name = "pmctrl-opp",
		.of_match_table = pmctrl_opp_match_table,
	},
};

static int __init pmctrl_opp_init(void)
{
	return platform_driver_register(&pmctrl_opp_driver);
}
arch_initcall(pmctrl_opp_init);

static void __exit pmctrl_opp_exit(void)
{
	platform_driver_unregister(&pmctrl_opp_driver);
}
module_exit(pmctrl_opp_exit);

MODULE_DESCRIPTION("JLQ PMCTRL OPP Driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: pm-ctrl");
