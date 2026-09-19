
/* Copyright 2019~2020 JLQ Technology Co., Ltd. or its affiliates.
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License version 2 as published
* by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
*/
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/reboot.h>
#include <linux/bitmap.h>
#include <soc/jlq/jr510/isr_monitor.h>
#include <linux/smp.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <trace/events/irq.h>
#include <internals.h>

struct jlq_irq_data *gidata;
void show_isr_monitor(void)
{
	int i = 0;
	unsigned long flags, delta;
	struct irq_desc *desc;
	struct irqaction *action;
	int cpu = 0;
	struct jlq_irq_stat *stat = gidata->stat[cpu];

	if (!gidata)
		return ;

	hrtimer_cancel(&gidata->isr_monitor_hrtimer);
	delta = (unsigned long)local_clock() - gidata->isr_monitor_clear_time;

	pr_info("boot cpu irq stastics in recent %ld ns", delta);

	for (i = 0; i < gidata->max_irqs; i++) {
		if (stat[i].cnt == 0)
			continue;
		desc = irq_to_desc(i);
		if (!desc)
			continue;

		if (!raw_spin_trylock_irqsave(&desc->lock, flags))
			return;
		pr_cont("\n");
		action = desc->action;
		if (action) {
			pr_cont("%-12s", action->name);
			while ((action = action->next) != NULL)
				pr_cont(",%s", action->name);
		}
		pr_cont(
			" %3d: cnt:%-8d l_s:%-15ld l_e:%-15ld s:%-15ld e:%-15ld",
			desc->irq_data.irq,
			stat[i].cnt,
			stat[i].last_enter,
			stat[i].last_exit,
			stat[i].enter,
			stat[i].exit);

		if (desc->irq_data.chip) {
			if (desc->irq_data.chip->name)
				pr_cont( " %8s", desc->irq_data.chip->name);
			else
				pr_cont( " %8s", "-");
		} else
			pr_cont( " %8s", "None");

#ifdef CONFIG_GENERIC_IRQ_SHOW_LEVEL
		pr_cont( "%-8s", irqd_is_level_type(&desc->irq_data) ?
			"Level" : "Edge");
#endif
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
	pr_cont("\n");
}
EXPORT_SYMBOL(show_isr_monitor);

ssize_t isr_info_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	show_isr_monitor();
	return 0;
}

static DEVICE_ATTR_RO(isr_info);
static const struct attribute *isr_attrs[] = {
	&dev_attr_isr_info.attr,
	NULL
};

void jlq_trace_isr_enter(void *ig, int irq, struct irqaction *action)
{
	int cpu = smp_processor_id();
	struct jlq_irq_stat *stat;

	if (!gidata)
		return;

	stat = gidata->stat[cpu];

	if (likely(irq < gidata->max_irqs)) {
		stat[irq].cnt++;
		stat[irq].last_enter = stat[irq].enter;
		stat[irq].last_exit = stat[irq].exit;
		stat[irq].enter = local_clock();
		stat[irq].exit = 0;
	} else
		return;

	if ((stat[irq].cnt / gidata->irq_interval) >> 12)
		panic("detect irq storm(> 4096Hz) for irq(%d)", irq);
}
EXPORT_SYMBOL(jlq_trace_isr_enter);

void jlq_trace_isr_exit(void *ig, int irq, struct irqaction *action,
	int ret)
{
	int cpu = smp_processor_id();

	if (!gidata)
		return;

	if (likely(irq < gidata->max_irqs))
		gidata->stat[cpu][irq].exit = local_clock();
	else
		return;
}
EXPORT_SYMBOL(jlq_trace_isr_exit);

void isr_monitor_clear(struct jlq_irq_data *data)
{
	int i;
	int size = data->max_irqs * sizeof(struct jlq_irq_stat);

	/*
	 * backup monitor irq count
	 */
	memcpy((void *)data->boot_cpu_stat,
		(const void *)data->stat[__boot_cpu_id], size);

	for_each_possible_cpu(i) {
		memset((void *)data->stat[i], 0, size);
	}

	data->isr_monitor_clear_time = local_clock();

}
EXPORT_SYMBOL(isr_monitor_clear);

static enum hrtimer_restart isr_monitor_timer_fn(struct hrtimer *hrtimer)
{
	hrtimer_forward_now(hrtimer, ns_to_ktime(gidata->irq_interval));
	isr_monitor_clear(gidata);
	return HRTIMER_RESTART;
}

static int isr_monitor_parse_dt(struct jlq_irq_data *data,
	struct platform_device *pdev)
{
	int ret;
	u32 val;

	ret = of_property_read_u32(pdev->dev.of_node,
		"irq-interval", &val);
	if (ret) {
		dev_info(&pdev->dev, "no irq-interval found ,use default\n");
		data->irq_interval = 20;
	} else
		data->irq_interval = val;

	data->irq_interval *= NSEC_PER_SEC;

	ret = of_property_read_u32(pdev->dev.of_node,
		"max-irqs", &data->max_irqs);
	if (ret) {
		dev_info(&pdev->dev, "no max-irqs found ,use default\n");
		data->max_irqs = 256;
	}
	return 0;
}
static int isr_monitor_probe(struct platform_device *pdev)
{
	int ret;
	int i, len;

	struct jlq_irq_data *data = kmalloc(
			sizeof(struct jlq_irq_data), GFP_KERNEL);

	if (IS_ERR(data)) {
		dev_err(&pdev->dev, "failed to alloc mem\n");
		return -ENOMEM;
	}
	data->dev = &pdev->dev;
	if (isr_monitor_parse_dt(data, pdev))
		return -EINVAL;

	len = sizeof(struct jlq_irq_data) +
		num_present_cpus() * sizeof(struct jlq_irq_stat *);

	data = krealloc(data, len, GFP_KERNEL);

	for (i = 0; i < num_present_cpus(); i++) {
		data->stat[i] = devm_kmalloc(&pdev->dev,
			data->max_irqs * sizeof(struct jlq_irq_stat),
			GFP_KERNEL);
	}

	data->boot_cpu_stat = devm_kmalloc(&pdev->dev,
			sizeof(struct jlq_irq_stat) * data->max_irqs,
			GFP_KERNEL);

	gidata = data;
	isr_monitor_clear(data);

	hrtimer_init(&data->isr_monitor_hrtimer,
		CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->isr_monitor_hrtimer.function = isr_monitor_timer_fn;
	hrtimer_start(&data->isr_monitor_hrtimer,
		ns_to_ktime(data->irq_interval), HRTIMER_MODE_REL);

	ret = sysfs_create_files(&pdev->dev.kobj, isr_attrs);
	if (ret) {
		dev_err(&pdev->dev, "create sysfs failed\n");
		goto err;
	}
	platform_set_drvdata(pdev, data);

	register_trace_irq_handler_entry(jlq_trace_isr_enter, NULL);
	register_trace_irq_handler_exit(jlq_trace_isr_exit, NULL);
	return 0;
err:
	hrtimer_cancel(&data->isr_monitor_hrtimer);
	return ret;
}

static int isr_monitor_remove(struct platform_device *pdev)
{
	struct jlq_irq_data *data = platform_get_drvdata(pdev);
	hrtimer_cancel(&data->isr_monitor_hrtimer);
	kfree(data);
	return 0;
}

const static struct of_device_id isr_monitor_of_match_table[] = {
	{ .compatible = "jlq,isr-monitor", },
	{}
};

static struct platform_driver isr_monitor_drv = {
	.probe = isr_monitor_probe,
	.remove = isr_monitor_remove,
	.driver = {
		.name = "jlq,isr-monitor",
		.of_match_table = isr_monitor_of_match_table,
		.owner = THIS_MODULE,
	},
};

static int __init jlq_isr_monitor_init(void)
{
       int r = platform_driver_register(&isr_monitor_drv);

       if (r < 0)
               pr_err("register failed %d", r);

       return r;
}

static void __exit jlq_isr_monitor_exit(void)
{
       platform_driver_unregister(&isr_monitor_drv);
}

module_init(jlq_isr_monitor_init);
module_exit(jlq_isr_monitor_exit);

MODULE_DESCRIPTION("JLQ ISR MONITOR");
MODULE_LICENSE("GPL");
