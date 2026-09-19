// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2019, Linaro Ltd.
 */

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define KHZ_PER_GHZ	1000000L

struct cpufreq_proc {
	struct proc_dir_entry *proc_dir;
	unsigned long  cpumaxfreq;
};

static int cpufreq_proc_show(struct seq_file *seq, void *v)
{
	struct cpufreq_proc *proc_info = seq->private;
	int ret = 0;

	ret = proc_info->cpumaxfreq/1000 + 5;
	seq_printf(seq, "%u.%02u\n", ret/1000, ret%1000/10);

	//seq_printf(seq, "%d\n", (proc_info->cpumaxfreq/KHZ_PER_GHZ));

	return 0;
}

static int cpufreq_proc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpufreq_proc *proc_info;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *table;
	unsigned int cpumaxfreq = 0;
	unsigned int i = 0;
	int ret = 0;

	proc_info = devm_kzalloc(dev, sizeof(*proc_info), GFP_KERNEL);
	if (!proc_info)
		return -ENOMEM;

	policy = cpufreq_cpu_get_raw(0);
	if (!policy) {
		ret = -EPROBE_DEFER;
		goto free;
	}

	table = policy->freq_table;

	while (table[i].frequency != CPUFREQ_TABLE_END) {
		cpumaxfreq = max(cpumaxfreq, table[i].frequency);
		i++;
	}

	proc_info->cpumaxfreq = cpumaxfreq;

	proc_info->proc_dir = proc_create_single_data("cpumaxfreq", 0, NULL,
			cpufreq_proc_show, proc_info);
	if (!proc_info->proc_dir) {
		dev_err(dev, "%s: failed to register with procfs.\n");
		ret = -ENOMEM;
		goto free;
	}

	dev_set_drvdata(dev, proc_info);

	dev_info(dev, "cpufreq procfs registered.\n");

	return 0;
free:
	if (proc_info->proc_dir)
		proc_remove(proc_info->proc_dir);

	if (proc_info)
		devm_kfree(dev, proc_info);

	return ret;
}

static int cpufreq_proc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpufreq_proc *proc_info = dev_get_drvdata(dev);

	if (proc_info->proc_dir)
		proc_remove(proc_info->proc_dir);

	if (proc_info)
		devm_kfree(dev, proc_info);

	return 0;
}

const static struct of_device_id cpufreq_proc_match_table[] = {
	{ .compatible = "jlq,cpufreq-proc", },
	{}
};
static struct platform_driver cpufreq_proc_driver = {
	.probe = cpufreq_proc_probe,
	.remove = cpufreq_proc_remove,
	.driver  = {
		.name = "jlq-cpufreq-proc",
		.of_match_table = cpufreq_proc_match_table,
	},
};

static int __init cpufreq_proc_init(void)
{
	return platform_driver_register(&cpufreq_proc_driver);
}

static void __exit cpufreq_proc_exit(void)
{
	platform_driver_unregister(&cpufreq_proc_driver);
}

module_init(cpufreq_proc_init);
module_exit(cpufreq_proc_exit);

MODULE_DESCRIPTION("JLQ cpufreq proc fs");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:jlq-cpufreq-proc");
MODULE_SOFTDEP("pre: cpufreq-dt-jlq");
MODULE_SOFTDEP("pre: governor_cpufreq");
