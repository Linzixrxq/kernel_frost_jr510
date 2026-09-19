// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015, 2017-2018, 2019, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "devfreq-simple-dev: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/devfreq.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <trace/events/power.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/ipc_logging.h>
#include <opp.h>

#define VDD_NUM			3
#define DEVFREQ_IPC_LOG_PAGES	50
static int devfreq_set_opp(struct dev_pm_set_opp_data *data);

struct dev_data {
	struct clk			*clk;
	struct devfreq			*df;
	struct opp_table		*opp_table;
	struct dev_pm_set_opp_data	data;
	struct devfreq_dev_profile	profile;
	bool				freq_in_khz;
	int				regulator_count;
};

struct devfreq_info {
	struct dentry *dir;
	u32 start;
	struct mutex mutex;
	void *logbuf;
};

static struct devfreq_info devfreq_info;
static void devfreq_dump(struct clk *clk, unsigned long rate,
		struct dev_pm_opp_info *opp, unsigned int count)
{
	struct devfreq_info *dfinfo = &devfreq_info;
	unsigned int i;
	char buf[200];
	char *p = buf;

	for (i = 0; i < count; i++) {
		sprintf(p, "%8lu mv,", (opp->supplies[i].u_volt / 1000));
		p = buf + strlen(buf);
	}
	sprintf(p, "\n");

	mutex_lock(&dfinfo->mutex);
	if (dfinfo->logbuf)
		ipc_log_string(dfinfo->logbuf,
				"%20s: %8lu khz, %s",
				__clk_get_name(clk), (rate/1000), buf);

	mutex_unlock(&dfinfo->mutex);
}

static void devfreq_log_start(unsigned int start)
{
	struct devfreq_info *dfinfo = &devfreq_info;

	mutex_lock(&dfinfo->mutex);
	if (start) {
		dfinfo->logbuf = ipc_log_context_create(DEVFREQ_IPC_LOG_PAGES, "devfreq", 0);
		if (!dfinfo->logbuf)
			pr_err("create devfreq log buf failed\n");
	} else {
		ipc_log_context_destroy(dfinfo->logbuf);
		dfinfo->logbuf = NULL;
	}
	mutex_unlock(&dfinfo->mutex);

	dfinfo->start = start;
}

static ssize_t devfreq_debug_write(struct file *filp,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	struct devfreq_info *dfinfo = &devfreq_info;
	unsigned int start = 0;
	unsigned int cnt = 0;
	char buf[10];

	cnt = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, cnt))
		return -EFAULT;
	buf[cnt] = '\0';
	if (kstrtoint(buf, 0, &start)) {
		pr_info("Please input 1 for start or 0 for stop!\n");
		return count;
	}

	start = !!start;

	if (start == dfinfo->start) {
		pr_info("devfreq log is already %s\n",
			start ? "started" : "stopped");
		return count;
	}

	pr_info("devfreq log %s\n", start ? "started" : "stopped");

	devfreq_log_start(start);

	return count;
}

static const struct file_operations devfreq_debug_fops = {
	.write          = devfreq_debug_write,
};

static void devfreq_log_init(void)
{
	struct devfreq_info *dfinfo = &devfreq_info;

	dfinfo->dir = debugfs_create_dir("devfreq_debug", NULL);
	if (!dfinfo->dir)
		return;

	if (!debugfs_create_file("log_enable", 0644,
			dfinfo->dir, NULL, &devfreq_debug_fops)) {
		debugfs_remove_recursive(dfinfo->dir);
		dfinfo->dir = NULL;
		return;
	}

	devfreq_log_start(true);
}

static void find_freq(struct devfreq_dev_profile *p, unsigned long *freq,
			u32 flags)
{
	int i;
	unsigned long atmost, atleast, f;

	atleast = p->freq_table[0];
	atmost = p->freq_table[p->max_state-1];
	for (i = 0; i < p->max_state; i++) {
		f = p->freq_table[i];
		if (f <= *freq)
			atmost = max(f, atmost);
		if (f >= *freq)
			atleast = min(f, atleast);
	}
	if (flags & DEVFREQ_FLAG_LEAST_UPPER_BOUND)
		*freq = atmost;
	else
		*freq = atleast;
}

static int dev_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct dev_data *d = dev_get_drvdata(dev);
	unsigned long new_freq, old_freq;
	struct dev_pm_opp *new_opp = NULL, *old_opp = NULL;
	struct dev_pm_set_opp_data *data;
	unsigned int size;
	int ret = 0;

	find_freq(&d->profile, freq, flags);
	new_freq = clk_round_rate(d->clk, d->freq_in_khz ? *freq * 1000 : *freq);
	if (IS_ERR_VALUE(new_freq)) {
		dev_err(dev, "devfreq: Cannot find matching frequency for %lu\n",
			*freq);
		return new_freq;
	}

	new_opp = dev_pm_opp_find_freq_ceil(dev, &new_freq);
	if (IS_ERR(new_opp)) {
		ret = PTR_ERR(new_opp);
		goto out;
	}

	old_freq = clk_get_rate(d->clk);

	old_opp = dev_pm_opp_find_freq_ceil(dev, &old_freq);
	if (IS_ERR(old_opp)) {
		ret = PTR_ERR(old_opp);
		goto out;
	}

	data = &d->data;
	data->regulators = d->opp_table->regulators;
	data->regulator_count = d->regulator_count;
	data->clk = d->clk;
	data->dev = dev;

	data->old_opp.rate = old_freq;
	data->old_opp.supplies = old_opp->supplies;

	data->new_opp.rate = new_freq;
	data->new_opp.supplies = new_opp->supplies;

	ret = devfreq_set_opp(data);
	if (ret)
		dev_err(dev, "devfreq: cannot target frequency for %lu\n", *freq);
out:
	if (old_opp)
		dev_pm_opp_put(old_opp);
	if (new_opp)
		dev_pm_opp_put(new_opp);

	return ret;
}

static int dev_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct dev_data *d = dev_get_drvdata(dev);
	unsigned long f;

	f = clk_get_rate(d->clk);
	if (IS_ERR_VALUE(f))
		return f;
	*freq = d->freq_in_khz ? f / 1000 : f;
	return 0;
}

static int devfreq_set_opp(struct dev_pm_set_opp_data *data)
{
	struct dev_pm_opp_info *old_opp = &data->old_opp;
	struct dev_pm_opp_info *new_opp = &data->new_opp;
	unsigned long old_freq = old_opp->rate;
	unsigned long new_freq = new_opp->rate;
	unsigned int regulator_count = data->regulator_count;
	struct device *dev = data->dev;
	struct clk *clk = data->clk;
	unsigned int i;
	int ret = 0;

	if (new_freq >= old_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						new_opp->supplies[i].u_volt_min,
						new_opp->supplies[i].u_volt,
						new_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to set regulator[%d]: %d\n",
						__func__, i, ret);
				goto restore;
			}
		}
	}

	ret = clk_set_rate(clk, new_freq);
	if (ret) {
		dev_err(dev, "%s: failed to set clk[%s][%d]: %d\n",
			__func__, __clk_get_name(clk), new_freq, ret);
		goto restore;
	}

	if (new_freq < old_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						new_opp->supplies[i].u_volt_min,
						new_opp->supplies[i].u_volt,
						new_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to set regulator[%d]: %d\n",
						__func__, i, ret);
				goto restore;
			}
		}
	}

	devfreq_dump(clk, new_freq, new_opp, regulator_count);

	return 0;

restore:

	if (old_freq >= new_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						old_opp->supplies[i].u_volt_min,
						old_opp->supplies[i].u_volt,
						old_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to restore regulator[%d]: %d\n",
						__func__, i, ret);
				return -EINVAL;
			}
		}
	}

	ret = clk_set_rate(clk, old_freq);
	if (ret) {
		dev_err(dev, "%s: failed to restore clk[%s][%d]: %d\n",
			__func__, __clk_get_name(clk), old_freq, ret);
		return -EINVAL;
	}

	if (old_freq < new_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						old_opp->supplies[i].u_volt_min,
						old_opp->supplies[i].u_volt,
						old_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to restore regulator[%d]: %d\n",
						__func__, i, ret);
				return -EINVAL;
			}
		}
	}

	return 0;
}

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

static const char *names[VDD_NUM] = {"vdd_cpu", "vdd_core", "vdd_ddr"};
static int parse_freq_table(struct device *dev, struct dev_data *d)
{
	struct devfreq_dev_profile *p = &d->profile;
	unsigned long freq;
	struct dev_pm_opp *opp;
	int len, i;
	int ret = -EINVAL;

	d->regulator_count = 0;
	d->opp_table = NULL;
	for (i = 0; i < VDD_NUM; i++) {
		if (find_supply_name(dev, names[i]))
			d->regulator_count++;
	}

	if (d->regulator_count) {
		d->opp_table = dev_pm_opp_set_regulators(dev,
				names, d->regulator_count);
		if (IS_ERR(d->opp_table)) {
			ret = PTR_ERR(d->opp_table);
			dev_err(dev, "Failed to set regulator: %d\n", ret);
			return ret;
		}
	}

	if (dev_pm_opp_of_add_table(dev)) {
		dev_err(dev, "failed to init OPP table\n");
		ret = -EINVAL;
		goto put_regulator;
	}

	len = dev_pm_opp_get_opp_count(dev);
	if (len <= 0) {
		ret = -EPROBE_DEFER;
		goto put_opp_table;
	}

	d->freq_in_khz = false;
	p->freq_table = devm_kcalloc(dev, len, sizeof(*p->freq_table),
			     GFP_KERNEL);
	if (!p->freq_table) {
		ret = -ENOMEM;
		goto put_opp_table;
	}

	for (i = 0, freq = ULONG_MAX; i < len; i++, freq--) {
		opp = dev_pm_opp_find_freq_floor(dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto free_tables;
		}
		p->freq_table[i] = freq;
		dev_pm_opp_put(opp);
	}

	p->max_state = i;
	if (p->max_state == 0) {
		ret = -EINVAL;
		goto free_tables;
	}
	return 0;

free_tables:
	devm_kfree(dev, p->freq_table);
put_opp_table:
	dev_pm_opp_of_remove_table(dev);
put_regulator:
	if (d->regulator_count)
		dev_pm_opp_put_regulators(d->opp_table);

	return ret;
}

static int devfreq_clock_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dev_data *d;
	struct devfreq_dev_profile *p;
	u32 poll;
	const char *gov_name;
	int ret;

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	platform_set_drvdata(pdev, d);

	d->clk = devm_clk_get(dev, "devfreq_clk");
	if (IS_ERR(d->clk)) {
		ret = PTR_ERR(d->clk);
		goto free;
	}

	ret = parse_freq_table(dev, d);
	if (ret < 0)
		goto free;

	p = &d->profile;
	p->target = dev_target;
	p->get_cur_freq = dev_get_cur_freq;
	ret = dev_get_cur_freq(dev, &p->initial_freq);
	if (ret < 0)
		goto put_opp;

	p->polling_ms = 50;
	if (!of_property_read_u32(dev->of_node, "polling-ms", &poll))
		p->polling_ms = poll;

	if (of_property_read_string(dev->of_node, "governor", &gov_name))
		gov_name = "performance";

	if (of_property_read_bool(dev->of_node, "jlq,prepare-clk")) {
		ret = clk_prepare(d->clk);
		if (ret < 0)
			goto put_opp;
	}

	mutex_init(&devfreq_info.mutex);
	d->df = devfreq_add_device(dev, p, gov_name, NULL);
	if (IS_ERR(d->df)) {
		ret = -EPROBE_DEFER;
		goto add_err;
	}

	devfreq_log_init();

	dev_info(dev, "devfreq registered.\n");
	return 0;
add_err:
	if (of_property_read_bool(dev->of_node, "jlq,prepare-clk"))
		clk_unprepare(d->clk);
put_opp:
	devm_kfree(dev, p->freq_table);
	dev_pm_opp_of_remove_table(dev);
	if (d->regulator_count)
		dev_pm_opp_put_regulators(d->opp_table);
free:
	return ret;
}

static int devfreq_clock_remove(struct platform_device *pdev)
{
	struct dev_data *d = platform_get_drvdata(pdev);

	devfreq_remove_device(d->df);

	return 0;
}

static const struct of_device_id devfreq_simple_match_table[] = {
	{ .compatible = "devfreq-simple-dev" },
	{}
};

static struct platform_driver devfreq_clock_driver = {
	.probe = devfreq_clock_probe,
	.remove = devfreq_clock_remove,
	.driver = {
		.name = "devfreq-simple-dev",
		.of_match_table = devfreq_simple_match_table,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(devfreq_clock_driver);
MODULE_DESCRIPTION("Devfreq driver for setting generic device clock frequency");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: governor_cpufreq");
