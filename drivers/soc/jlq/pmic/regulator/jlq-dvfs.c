// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <asm/div64.h>
#include <linux/tick.h>
#include <linux/cpu.h>
#include <linux/pm_opp.h>
#include <opp.h>
#include <internal.h>

enum {
	VL_0,
	VL_1,
	VL_2,
	VL_3,
	VL_4,
	VL_5,
	VL_6,
	VL_7,
	VL_MAX,
};

struct vol_stat_info {
	u64 time;
	u32 vol_val;
};

struct vol_hw_info {
	bool stat_start;
	struct vol_stat_info vlts[VL_MAX];
	u32 vol_change[VL_MAX][VL_MAX];
	u32 vc_total_count;
	u32 cur_lvl;
	ktime_t breakdown_start;
	ktime_t prev_ts;
	u64 total_time;
};

struct vol_hw_dev {
	struct regulator *reg;
	struct notifier_block nb;
	struct device *dev;
	const char *name;
};

static int vol_lvl_table[3][VL_MAX] = {
	{
		0,
	},
	{
		700000,
		800000,
	},
	{
		700000,
		800000,
	},
};

static DEFINE_SPINLOCK(vol_lock);
struct vol_hw_info g_dc_info[3];

static inline u32 calculate_dc(u32 busy, u32 total, u32 *fraction)
{
	u32 result, remainder;
	u64 busy64, remainder64;

	busy64 = (u64)busy;
	result = div_u64_rem(busy64 * 100, total, &remainder);
	remainder64 = (u64)remainder;
	*fraction = div_u64(remainder64 * 100, total);

	return result;
}

static ssize_t vol_dc_common_read(struct file *filp, char __user *buffer,
		size_t count, loff_t *ppos, int index)
{
	char *buf;
	ssize_t ret, size = 2 * PAGE_SIZE - 1;
	u32 i, j, dc_int = 0, dc_fra = 0, len = 0, vlnum = VL_MAX;

	buf = (char *)__get_free_pages(GFP_NOIO, get_order(size));
	if (!buf)
		return -ENOMEM;

	spin_lock(&vol_lock);
	if (!g_dc_info[index].total_time) {
		len += snprintf(buf + len, size - len,
				"No stat information! ");
		len += snprintf(buf + len, size - len,
				"Help information :\n");
		len += snprintf(buf + len, size - len,
				"1. echo 1 to start duty cycle stat:\n");
		len += snprintf(buf + len, size - len,
				"2. echo 0 to stop duty cycle stat:\n");
		len += snprintf(buf + len, size - len,
				"3. cat to check duty cycle info from start to stop:\n\n");
		goto out;
	}

	if (g_dc_info[index].stat_start) {
		len += snprintf(buf + len, size - len,
				"Please stop the vol duty cycle stats at first\n");
		goto out;
	}

	len += snprintf(buf + len, size - len,
			"Total time:%8llums (%6llus)\n",
			 div64_u64(g_dc_info[index].total_time, (u64)(1000)),
			 div64_u64(g_dc_info[index].total_time, (u64)(1000000)));
	len += snprintf(buf + len, size - len,
			"|Level|Vol(mV)|Time(ms)|      %%|\n");
	for (i = 0; i < vlnum; i++) {
		dc_int = calculate_dc(g_dc_info[index].vlts[i].time,
				g_dc_info[index].total_time, &dc_fra);
		len += snprintf(buf + len, size - len,
				"| VL_%1d|% 7u| %8llu|%3u.%02u%%|\n",
				i, vol_lvl_table[index][i],
				div64_u64(g_dc_info[index].vlts[i].time,
				(u64)(1000)),
				dc_int, dc_fra);
	}

	/* show voltage-change times */
	len += snprintf(buf + len, size - len,
			"\nTotal voltage-change times:%8u",
			g_dc_info[index].vc_total_count);
	len += snprintf(buf + len, size - len, "\n|from\\to|");
	for (j = 0; j < vlnum; j++)
		len += snprintf(buf + len, size - len, " Level%1d|", j);
	for (i = 0; i < vlnum; i++) {
		len += snprintf(buf + len, size - len, "\n| Level%1d|", i);
		for (j = 0; j < vlnum; j++)
			if (i == j)
				len +=  snprintf(buf + len, size - len,
						"   ---  |");
			else
				/* [from ][t o] */
				len += snprintf(buf + len, size - len, "%7u|",
						g_dc_info[index].vol_change[i][j]);
	}

	len += snprintf(buf + len, size - len, "\n");
out:
	spin_unlock(&vol_lock);
	ret = simple_read_from_buffer(buffer, count, ppos, buf, len);
	free_pages((unsigned long)buf, get_order(size));
	return ret;
}

static ssize_t vol_dc_cpu_read(struct file *filp, char __user *buffer,
		size_t count, loff_t *ppos)
{
	return vol_dc_common_read(filp, buffer, count, ppos, 0);
}

static ssize_t vol_dc_ddr_read(struct file *filp, char __user *buffer,
		size_t count, loff_t *ppos)
{
	return vol_dc_common_read(filp, buffer, count, ppos, 1);
}

static ssize_t vol_dc_core_read(struct file *filp, char __user *buffer,
		size_t count, loff_t *ppos)
{
	return vol_dc_common_read(filp, buffer, count, ppos, 2);
}

static ssize_t vol_dc_common_write(struct file *filp, const char __user *buffer,
					    size_t count, loff_t *ppos, int index)
{
	unsigned int start, idx;
	char buf[10] = { 0 };
	ktime_t cur_ts;
	u64 time_us;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	if (kstrtouint(buf, 10, &start))
		return -EFAULT;

	spin_lock(&vol_lock);
	start = !!start;

	if (g_dc_info[index].stat_start == start) {
		pr_err("[WARNING]Voltage duty-cycle statistics is already %s\n",
				g_dc_info[index].stat_start ? "started" : "stopped");
		spin_unlock(&vol_lock);
		return -EINVAL;
	}
	g_dc_info[index].stat_start = start;

	cur_ts = ktime_get();
	if (g_dc_info[index].stat_start) {
		for (idx = 0; idx < VL_MAX; idx++)
			g_dc_info[index].vlts[idx].time = 0;
		memset(g_dc_info[index].vol_change, 0, sizeof(u32) * VL_MAX * VL_MAX);
		g_dc_info[index].prev_ts = cur_ts;
		g_dc_info[index].breakdown_start = cur_ts;
		g_dc_info[index].total_time = -1UL;
		g_dc_info[index].vc_total_count = 0;
	} else {
		time_us = ktime_to_us(ktime_sub(cur_ts, g_dc_info[index].prev_ts));
		g_dc_info[index].vlts[g_dc_info[index].cur_lvl].time += time_us;
		g_dc_info[index].total_time = ktime_to_us(ktime_sub(cur_ts,
					g_dc_info[index].breakdown_start));
	}

	spin_unlock(&vol_lock);

	return count;
}

static ssize_t vol_dc_cpu_write(struct file *filp, const char __user *buffer,
					    size_t count, loff_t *ppos)
{
	return vol_dc_common_write(filp, buffer, count, ppos, 0);
}

static ssize_t vol_dc_ddr_write(struct file *filp, const char __user *buffer,
					    size_t count, loff_t *ppos)
{
	return vol_dc_common_write(filp, buffer, count, ppos, 1);
}

static ssize_t vol_dc_core_write(struct file *filp, const char __user *buffer,
					    size_t count, loff_t *ppos)
{
	return vol_dc_common_write(filp, buffer, count, ppos, 2);
}

static const struct file_operations vol_dc_cpu_ops = {
	.owner = THIS_MODULE,
	.read = vol_dc_cpu_read,
	.write = vol_dc_cpu_write,
};

static const struct file_operations vol_dc_ddr_ops = {
	.owner = THIS_MODULE,
	.read = vol_dc_ddr_read,
	.write = vol_dc_ddr_write,
};

static const struct file_operations vol_dc_core_ops = {
	.owner = THIS_MODULE,
	.read = vol_dc_core_read,
	.write = vol_dc_core_write,
};

static int jlq_dc_common_notify(struct notifier_block *nb,
				    unsigned long event,
				    void *data, int index)
{
	ktime_t cur_ts;
	u64 time_us;
	int new_lvl = VL_MAX;
	int i;

	if (event & REGULATOR_EVENT_VOLTAGE_CHANGE) {
		for (i = 0; i < VL_MAX; i++) {
			if ((unsigned long)data == vol_lvl_table[index][i]) {
				new_lvl = i;
				break;
			}
		}

		if (i == VL_MAX)
			pr_err("please update vol_lvl_table info\n");

		if (g_dc_info[index].stat_start == 0) {
			g_dc_info[index].cur_lvl = new_lvl;
			return NOTIFY_OK;
		}

		g_dc_info[index].vc_total_count++;
		g_dc_info[index].vol_change[g_dc_info[index].cur_lvl][new_lvl]++;
		g_dc_info[index].vlts[new_lvl].vol_val = (unsigned long)data;

		cur_ts = ktime_get();
		time_us = ktime_to_us(ktime_sub(cur_ts, g_dc_info[index].prev_ts));
		g_dc_info[index].vlts[g_dc_info[index].cur_lvl].time += time_us;
		g_dc_info[index].prev_ts = cur_ts;
		g_dc_info[index].cur_lvl = new_lvl;
	}

	return NOTIFY_OK;
}

static int jlq_dc_cpu_notify(struct notifier_block *nb,
				    unsigned long event,
				    void *data)
{
	return jlq_dc_common_notify(nb, event, data, 0);
}

static int jlq_dc_ddr_notify(struct notifier_block *nb,
				    unsigned long event,
				    void *data)
{
	return jlq_dc_common_notify(nb, event, data, 1);
}

static int jlq_dc_core_notify(struct notifier_block *nb,
				    unsigned long event,
				    void *data)
{
	return jlq_dc_common_notify(nb, event, data, 2);
}

static int jlq_dcdc2_round(struct regulator *reg, int volts)
{
	struct regulator_dev *rdev = reg->rdev;
	int count = 0;
	int i, selector_volts = 0;

	if (!rdev) {
		pr_err("failed: rdev is NULL\n");
		return 0;
	}

	if ((volts < rdev->constraints->min_uV)
		|| (volts > rdev->constraints->max_uV)) {
		pr_err("failed: volts --> selector\n");
		return 0;
	}
	count = regulator_count_voltages(reg);
	for (i = 0; i < count; i++) {
		selector_volts = regulator_list_voltage(reg, i);
		if (selector_volts >= volts)
			return selector_volts;
	}
	return 0;
}

static int jlq_check_selector_cpu_volts(int i, int selector_volts)
{
	if (i == 0)
		return 1;
	if (selector_volts == vol_lvl_table[0][i - 1])
		return 0;
	return jlq_check_selector_cpu_volts(i - 1, selector_volts);
}

static const struct of_device_id jlq_dc_match[] = {
	{ .compatible = "jlq,dc-stat", },
	{},
};

static int jlq_dc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct vol_hw_dev *dc_dev;
	struct device *dev;
	struct opp_table *opp_table;
	struct dev_pm_opp *opp = ERR_PTR(-ENODEV);
	int selector_volts = 0;
	int i = 0;
	int ret = 0;

	if (!np)
		return -ENODEV;

	dc_dev = devm_kzalloc(&pdev->dev, sizeof(struct vol_hw_dev),
						GFP_KERNEL);
	if (!dc_dev)
		return -ENOMEM;

	ret = of_property_read_string(np, "lable", &dc_dev->name);
	if (ret) {
		dev_err(dc_dev->dev, "get lable property fail!\n");
		kfree(dc_dev);
		return -ENODEV;
	}

	dc_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, dc_dev);

	if (!strcmp(dc_dev->name, "cpu")) {
		dc_dev->reg = devm_regulator_get_optional(dc_dev->dev, "cpu");
		if (IS_ERR(dc_dev->reg)) {
			dev_err(dc_dev->dev, "couldn't get cpu regulator\n");
			return -ENODEV;
		}

		dev = get_cpu_device(0);
		if (unlikely(!dev)) {
			dev_err(dc_dev->dev,
				 "No cpu device for cpu0\n");
		}

		opp_table = dev_pm_opp_get_opp_table(dev);
		if (IS_ERR(opp_table))
			return PTR_ERR(opp_table);

		list_for_each_entry(opp, &opp_table->opp_list, node) {
			if (!opp->available)
				continue;

			selector_volts = jlq_dcdc2_round(dc_dev->reg,
						opp->supplies[0].u_volt);
			if (jlq_check_selector_cpu_volts(i, selector_volts)) {
				vol_lvl_table[0][i] = selector_volts;
				i++;
			}
			if (i == VL_MAX)
				dev_err(dc_dev->dev, "get too many opp-microvolt property\n");
		}
		dc_dev->nb.notifier_call = jlq_dc_cpu_notify;
	} else if (!strcmp(dc_dev->name, "ddr")) {
		dc_dev->reg = devm_regulator_get_optional(dc_dev->dev, "ddr");
		if (IS_ERR(dc_dev->reg)) {
			dev_err(dc_dev->dev, "couldn't get ddr regulator\n");
			return -ENODEV;
		}
		dc_dev->nb.notifier_call = jlq_dc_ddr_notify;
	} else if (!strcmp(dc_dev->name, "core")) {
		dc_dev->reg = devm_regulator_get_optional(dc_dev->dev, "core");
		if (IS_ERR(dc_dev->reg)) {
			dev_err(dc_dev->dev, "couldn't get core regulator\n");
			return -ENODEV;
		}
		dc_dev->nb.notifier_call = jlq_dc_core_notify;
	} else {
		dev_err(dc_dev->dev, "couldn't get the lable property of regulator\n");
		return -ENODEV;
	}

	/* register regulator notifier */
	ret = regulator_register_notifier(dc_dev->reg, &dc_dev->nb);
	if (ret) {
		dev_err(&pdev->dev,
			"regulator notifier request failed\n");
	}

	return 0;
}

static int jlq_dc_remove(struct platform_device *pdev)
{
	struct vol_hw_dev *dc_dev = platform_get_drvdata(pdev);

	regulator_unregister_notifier(dc_dev->reg, &dc_dev->nb);
	kfree(dc_dev);
	return 0;
}

static struct platform_driver jlq_dc_driver = {
	.probe   = jlq_dc_probe,
	.remove  = jlq_dc_remove,
	.driver  = {
		.name  = "jlq-dc-stat",
		.of_match_table = jlq_dc_match,
	},
};

static int __init hwdc_stat_init(void)
{
	struct dentry *vlstat_node, *volt_dc_cpu_stat, *volt_dc_ddr_stat, *volt_dc_core_stat;
	int ret;

	ret = platform_driver_register(&jlq_dc_driver);
	if (ret != 0) {
		pr_err("Failed to register JLQ duty cycle driver: %d\n", ret);
		return -ENOENT;
	}

	vlstat_node = debugfs_create_dir("vlstat", NULL);
	if (!vlstat_node)
		return -ENOENT;

	volt_dc_cpu_stat = debugfs_create_file("vol_dc_cpu_stat", 0444,
					vlstat_node, NULL, &vol_dc_cpu_ops);
	if (!volt_dc_cpu_stat)
		goto err;

	volt_dc_ddr_stat = debugfs_create_file("vol_dc_ddr_stat", 0444,
					vlstat_node, NULL, &vol_dc_ddr_ops);
	if (!volt_dc_ddr_stat)
		goto err;

	volt_dc_core_stat = debugfs_create_file("vol_dc_core_stat", 0444,
					vlstat_node, NULL, &vol_dc_core_ops);
	if (!volt_dc_core_stat)
		goto err;

	return 0;

err:
	debugfs_remove(vlstat_node);
	return -ENOENT;
}

late_initcall_sync(hwdc_stat_init);
MODULE_LICENSE("GPL");
