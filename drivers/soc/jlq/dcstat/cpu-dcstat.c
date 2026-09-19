// SPDX-License-Identifier: GPL-2.0
/*
 * JLQ cpu dutycycle statistic
 *
 * Copyright (c) 2020-2021   JLQ Technology Co.,Ltd
 * Ltd. or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/debugfs.h>
#include <soc/jlq/jr510/dcstat.h>
#include "../../../../kernel/sched/sched.h"
#include "cpu-dcstat.h"

#define KHZ_PER_MHZ	1000L
#define NSEC_PER_MSEC	1000000L
#define NSEC_PER_USEC	1000L
#define NSEC_PER_SEC	1000000000L

#define CPU_DCSTAT_IDLE_PERIOD   11
#define MAX_TIME 1000000000000000000
static unsigned long cpu_dcstat_idle_period[CPU_DCSTAT_IDLE_PERIOD] = {
10000, 50000, 100000, 250000, 500000, 750000, 1000000, 5000000,
25000000, 100000000, MAX_TIME};

static struct cpu_info cpu_info;
static DEFINE_PER_CPU(struct cpu_stat, cpu_stat);
static int cpu_dcstat_start_event(unsigned int msg);

static inline unsigned long div(unsigned long dividend,
				unsigned long divisor)
{
	return dividend / divisor;
}

static inline unsigned long div_rem(unsigned long dividend,
		unsigned long divisor, unsigned long *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

static inline unsigned long div_per(unsigned long dividend,
		unsigned long divisor, unsigned long *decimal)
{
	unsigned long remainder = 0;
	unsigned long result = 0;

	result = div_rem(dividend * 100, divisor, &remainder);
	*decimal = div(remainder * 100, divisor);
	return result;
}

static int cpu_dcstat_show(struct seq_file *seq, void *data)
{
	struct cpu_info *cpuinfo = &cpu_info;
	struct cpu_stat *cpustat;
	struct period_info *period;
	unsigned long *idlestat;
	unsigned long run_time = 0;
	unsigned long run_total = 0;
	unsigned long idle_time = 0;
	unsigned long idle_total = 0;
	unsigned long off_time = 0;
	unsigned long off_total = 0;
	unsigned long av_mips = 0;
	unsigned long mips_total = 0;
	unsigned long total_time = 0;
	unsigned long run_ratio_h = 0;
	unsigned long run_ratio_l = 0;
	unsigned long idle_ratio_h = 0;
	unsigned long idle_ratio_l = 0;
	unsigned long off_ratio_h = 0;
	unsigned long off_ratio_l = 0;
	unsigned long av_mips_h = 0;
	unsigned long av_mips_l = 0;
	unsigned int count = 0;
	unsigned int rate = 0;
	unsigned int cpu = 0;
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned long total_period = 0;
	unsigned long total_count = 0;
	char *time[12] = { "<10 us", "<50 us", "<100 us", "<250 us",
		"<500 us", "<750 us", "<1 ms", "<5 ms",
		"<25 ms", "<100 ms", ">100 ms"};

	if (cpuinfo->start) {
		seq_puts(seq, "Please stop cpu dcstat at first!\n");
		return 0;
	}

	count = CPU_DCSTAT_IDLE_PERIOD;
	total_time = cpuinfo->duration / NSEC_PER_MSEC;

	seq_printf(seq, "\nTotal duration is %lums.\n", total_time);

	seq_printf(seq, "\n| %4s | %10s | %10s | %10s | %10s ", "CPU#",
			"run (ms)", "idle (ms)", "rt ratio", "idle ratio");
	seq_printf(seq, "| %10s | %10s |\n",
			"off ratio", "MIPS (MHz)");
	for_each_possible_cpu(cpu) {
		av_mips = run_time = idle_time = off_time = 0;
		cpustat = &per_cpu(cpu_stat, cpu);
		for (i = 0; i < cpustat->opp_size; i++) {
			av_mips += (cpustat->opp[i].rate / KHZ_PER_MHZ) *
				(cpustat->run_time[i] / NSEC_PER_MSEC);
			run_time += (cpustat->run_time[i] / NSEC_PER_MSEC);
			idle_time += (cpustat->idle_time[i] / NSEC_PER_MSEC);
			off_time += (cpustat->off_time[i] / NSEC_PER_MSEC);
		}
		run_total += run_time;
		idle_total += idle_time;
		off_total += off_time;
		mips_total += av_mips;

		run_ratio_h = div_per(run_time, total_time, &run_ratio_l);
		idle_ratio_h = div_per(idle_time, total_time, &idle_ratio_l);
		off_ratio_h = div_per(off_time, total_time, &off_ratio_l);
		av_mips_h = div_rem(av_mips, total_time, &av_mips_l);
		av_mips_l = div(av_mips_l * 100, total_time);

		seq_printf(seq, "| %-4u | %10lu | %10lu ", cpu,
						run_time, idle_time);
		seq_printf(seq, "| %6lu.%02lu%% | %6lu.%02lu%% ",
						run_ratio_h, run_ratio_l,
						idle_ratio_h, idle_ratio_l);
		seq_printf(seq, "| %6lu.%02lu%% ",
						off_ratio_h, off_ratio_l);
		seq_printf(seq, "| %7lu.%02lu |\n",
						av_mips_h, av_mips_l);
	}

	run_ratio_h = div_per(run_total, total_time, &run_ratio_l);
	idle_ratio_h = div_per(idle_total, total_time, &idle_ratio_l);
	off_ratio_h = div_per(off_total, total_time, &off_ratio_l);
	av_mips_h = div_rem(mips_total, total_time, &av_mips_l);
	av_mips_l = div(av_mips_l * 100, total_time);

	seq_printf(seq, "\nTotal rt ratio:%lu.%02lu%%, idle ratio:%lu.%02lu%%,",
			run_ratio_h, run_ratio_l, idle_ratio_h, idle_ratio_l);
	seq_printf(seq, "off ratio:%lu.%02lu%%.",
			off_ratio_h, off_ratio_l);
	seq_printf(seq, "Total MIPS:%lu.%02luMHz.\n",
			av_mips_h, av_mips_l);

	seq_printf(seq, "\n| %4s | %3s | %10s | %10s ",
			"CPU#", "OP#", "rate(MHz)", "run(ms)");
	seq_printf(seq, "| %10s | %10s ", "idle(ms)", "rt ratio");

	cpustat = &per_cpu(cpu_stat, 0);
	for (i = 0; i < cpustat->idle_size; i++)
		seq_printf(seq, "| %5s%d%4s,%7s ", "s", i, "(ms)", "rt");

	seq_printf(seq, "| %10s,%6s |\n", "offline(ms)", "rt");

	for_each_possible_cpu(cpu) {
		cpustat = &per_cpu(cpu_stat, cpu);
		for (i = 0; i < cpustat->opp_size; i++) {
			if (i == 0)
				seq_printf(seq, "| %-4d ", cpu);
			else
				seq_puts(seq, "|      ");
			rate = (cpustat->opp[i].rate / KHZ_PER_MHZ);
			run_time = (cpustat->run_time[i] / NSEC_PER_MSEC);
			run_ratio_h = div_per(run_time,
					total_time, &run_ratio_l);
			idle_time = (cpustat->idle_time[i] / NSEC_PER_MSEC);
			seq_printf(seq, "| %-3u | %10u | %10lu | %10lu ",
					i, rate, run_time, idle_time);
			seq_printf(seq, "| %6lu.%02lu%% ",
					run_ratio_h, run_ratio_l);
			for (j = 0; j < cpustat->idle_size; j++) {
				idlestat = cpustat->idle_stat[j];
				idle_time = (idlestat[i] / NSEC_PER_MSEC);
				idle_ratio_h = div_per(idle_time,
						total_time, &idle_ratio_l);
				seq_printf(seq, "| %-10lu,%3lu.%02lu%% ",
					idle_time, idle_ratio_h, idle_ratio_l);

			}
			off_time = (cpustat->off_time[i] / NSEC_PER_MSEC);
			off_ratio_h = div_per(off_time,
					total_time, &off_ratio_l);
			seq_printf(seq, "| %-10lu,%3lu.%02lu%% |\n",
				off_time, off_ratio_h, off_ratio_l);
		}
	}

	for_each_possible_cpu(cpu) {
		seq_printf(seq, "\n| CPU%d idle | %15s | %15s |\n",
				cpu, "time (us)", "count");
		total_period = total_count = 0;
		cpustat = &per_cpu(cpu_stat, cpu);
		for (i = 0; i < count; i++) {
			period = &cpustat->period[i];
			if ((period->time) || (period->count)) {
				seq_printf(seq, "| %9s | %15lu | %15lu |\n",
					time[i], (period->time / NSEC_PER_USEC),
					period->count);
				total_period += (period->time / NSEC_PER_USEC);
				total_count += period->count;
			}
		}
		seq_printf(seq, "| %9s | %15lu | %15lu |\n", "SUM",
				total_period, total_count);
	}
	seq_puts(seq, "|\n");

	return 0;
}

static ssize_t cpu_dcstat_write(struct file *filp,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	struct cpu_info *cpuinfo = &cpu_info;
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

	if (start == cpuinfo->start) {
		pr_info("Cpufreq dcstat is already %s\n",
			start ? "started" : "stopped");
		return count;
	}

	pr_info("Cpufreq dcstat %s\n", start ? "started" : "stopped");

	if (start) {
		cpu_dcstat_start_event(CPU_DCSTAT_START);
		cpuinfo->start = start;
	} else {
		cpuinfo->start = start;
		cpu_dcstat_start_event(CPU_DCSTAT_STOP);
	}

	return count;
}

static int cpu_dcstat_open(struct inode *inode, struct file *file)
{
	return single_open(file, cpu_dcstat_show, NULL);
}

static const struct file_operations cpu_dcstat_fops = {
	.open           = cpu_dcstat_open,
	.read           = seq_read,
	.write          = cpu_dcstat_write,
	.llseek         = seq_lseek,
};

static int cpu_dcstat_start_event(unsigned int msg)
{
	struct cpu_info *cpuinfo = &cpu_info;
	struct cpu_stat *cpustat;
	unsigned long *idlestat;
	unsigned long *array;
	unsigned long period = 0;
	unsigned long total = 0;
	unsigned int count = 0;
	unsigned long ktime = 0;
	unsigned int cpu = 0;
	unsigned int freq = 0;
	unsigned int cur = 0;
	unsigned int i = 0;
	struct rq *rq;

	array = cpu_dcstat_idle_period;
	count = CPU_DCSTAT_IDLE_PERIOD;
	switch (msg) {
	case CPU_DCSTAT_START:
		ktime = ktime_to_ns(ktime_get());
		cpuinfo->duration = ktime;
		for_each_possible_cpu(cpu) {
			cpustat = &per_cpu(cpu_stat, cpu);

			freq = cpufreq_generic_get(cpu);
			while ((i < cpustat->opp_size)
				&& (cpustat->opp[i].rate != freq))
				i++;
			if (i < cpustat->opp_size)
				cur = i;

			memset(cpustat->run_time, 0,
				sizeof(unsigned long) * cpustat->opp_size);
			memset(cpustat->idle_time, 0,
				sizeof(unsigned long) * cpustat->opp_size);
			for (i = 0; i < cpustat->idle_size; i++)
				memset(cpustat->idle_stat[i], 0,
				sizeof(unsigned long) * cpustat->opp_size);
			memset(cpustat->stat_time, 0,
				sizeof(unsigned long) * cpustat->idle_size);
			memset(cpustat->off_time, 0,
				sizeof(unsigned long) * cpustat->opp_size);
			memset(cpustat->period, 0,
				sizeof(struct period_info) * count);
			cpustat->cur_opp = cur;

			rq = cpu_rq(cpu);
			spin_lock(&cpustat->lock);
			if (cpu_online(cpu) && (rq->curr != rq->idle))
				cpustat->run_start = ktime;
			else if (cpu_online(cpu) && (rq->curr == rq->idle))
				cpustat->idle_start = ktime;
			else if (!cpu_online(cpu))
				cpustat->off_start = ktime;
			spin_unlock(&cpustat->lock);
		}
		break;
	case CPU_DCSTAT_STOP:
		ktime = ktime_to_ns(ktime_get());
		cpuinfo->duration = ktime - cpuinfo->duration;
		for_each_possible_cpu(cpu) {
			cpustat = &per_cpu(cpu_stat, cpu);
			spin_lock(&cpustat->lock);
			if (cpustat->run_start) {
				cpustat->run_time[cpustat->cur_opp] +=
						ktime - cpustat->run_start;
				cpustat->run_start = 0;
			} else if (cpustat->idle_start) {
				period = ktime - cpustat->idle_start;
				cpustat->idle_time[cpustat->cur_opp] += period;
				idlestat =
					cpustat->idle_stat[cpustat->cur_idle];
				idlestat[cpustat->cur_opp] += period;
				for (i = 0; i < cpustat->opp_size; i++)
					total += idlestat[i];
				period = total -
					cpustat->stat_time[cpustat->cur_idle];
				cpustat->stat_time[cpustat->cur_idle] = total;
				for (i = 0; i < count; i++) {
					if (period && (period < array[i])) {
						cpustat->period[i].time +=
									period;
						cpustat->period[i].count++;
						break;
					}
				}
				cpustat->idle_start = 0;
			} else if (cpustat->off_start) {
				cpustat->off_time[cpustat->cur_opp] +=
						ktime - cpustat->off_start;
				cpustat->off_start = 0;
			}
			spin_unlock(&cpustat->lock);
		}
		break;
	default:
		break;
	}

	return 0;
}

int cpu_dcstat_idle_event(unsigned int cpu,
			unsigned int msg, unsigned int cur)
{
	struct cpu_info *cpuinfo = &cpu_info;
	struct cpu_stat *cpustat;
	unsigned long *idlestat;
	unsigned long *array;
	unsigned long period = 0;
	unsigned long total = 0;
	unsigned long ktime = 0;
	unsigned int count = 0;
	unsigned int i = 0;

	cpustat = &per_cpu(cpu_stat, cpu);

	if (cpuinfo->start == 0) {
		cpustat->cur_idle = cur;
		pr_debug("[%s]:cpu%d dcstat not start!\n", __func__, cpu);
		return 0;
	}

	spin_lock(&cpustat->lock);

	array = cpu_dcstat_idle_period;
	count = CPU_DCSTAT_IDLE_PERIOD;
	ktime = ktime_to_ns(ktime_get());

	switch (msg) {
	case CPU_DCSTAT_IDLE_ENTER:
		if (cpustat->run_start) {
			cpustat->run_time[cpustat->cur_opp] +=
					ktime - cpustat->run_start;
			cpustat->run_start = 0;
		}
		if (cpustat->idle_start) {
			period = ktime - cpustat->idle_start;
			cpustat->idle_time[cpustat->cur_opp] += period;
			idlestat = cpustat->idle_stat[cpustat->cur_idle];
			idlestat[cpustat->cur_opp] += period;
			for (i = 0; i < cpustat->opp_size; i++)
				total += idlestat[i];
			period = total - cpustat->stat_time[cpustat->cur_idle];
			cpustat->stat_time[cpustat->cur_idle] = total;
			for (i = 0; i < count; i++) {
				if (period && (period < array[i])) {
					cpustat->period[i].time += period;
					cpustat->period[i].count++;
					break;
				}
			}
			cpustat->idle_start = 0;
		}
		cpustat->cur_idle = cur;
		cpustat->idle_start = ktime;
		break;
	case CPU_DCSTAT_IDLE_EXIT:
		if (cpustat->run_start) {
			cpustat->run_time[cpustat->cur_opp] +=
					ktime - cpustat->run_start;
			cpustat->run_start = 0;
		}
		if (cpustat->idle_start) {
			period = ktime - cpustat->idle_start;
			cpustat->idle_time[cpustat->cur_opp] += period;
			idlestat = cpustat->idle_stat[cpustat->cur_idle];
			idlestat[cpustat->cur_opp] += period;
			for (i = 0; i < cpustat->opp_size; i++)
				total += idlestat[i];
			period = total - cpustat->stat_time[cpustat->cur_idle];
			cpustat->stat_time[cpustat->cur_idle] = total;
			for (i = 0; i < count; i++) {
				if (period && (period < array[i])) {
					cpustat->period[i].time += period;
					cpustat->period[i].count++;
					break;
				}
			}
			cpustat->idle_start = 0;
		}
		cpustat->cur_idle = cur;
		cpustat->run_start = ktime;
		break;
	default:
		break;
	}

	spin_unlock(&cpustat->lock);

	return 0;
}

int cpu_dcstat_hotplug_event(unsigned int cpu, unsigned int msg)
{
	struct cpu_info *cpuinfo = &cpu_info;
	struct cpu_stat *cpustat;
	unsigned long *idlestat;
	unsigned long *array;
	unsigned long period = 0;
	unsigned long total = 0;
	unsigned long ktime = 0;
	unsigned int count = 0;
	unsigned int i = 0;

	cpustat = &per_cpu(cpu_stat, cpu);
	spin_lock(&cpustat->lock);
	if (msg == CPU_DCSTAT_UP)
		cpustat->online = true;
	else
		cpustat->online = false;
	spin_unlock(&cpustat->lock);

	if (cpuinfo->start == 0) {
		pr_debug("[%s]:cpu%d dcstat not start!\n", __func__, cpu);
		return 0;
	}

	array = cpu_dcstat_idle_period;
	count = CPU_DCSTAT_IDLE_PERIOD;
	ktime = ktime_to_ns(ktime_get());

	switch (msg) {
	case CPU_DCSTAT_UP:
		spin_lock(&cpustat->lock);
		if (cpustat->off_start)
			cpustat->off_time[cpustat->cur_opp] +=
					ktime - cpustat->off_start;
		cpustat->off_start = 0;
		cpustat->idle_start = 0;
		cpustat->run_start = ktime;
		spin_unlock(&cpustat->lock);
		break;
	case CPU_DCSTAT_DOWN:
		spin_lock(&cpustat->lock);
		if (cpustat->run_start)
			cpustat->run_time[cpustat->cur_opp] +=
					ktime - cpustat->run_start;
		else if (cpustat->idle_start) {
			period = ktime - cpustat->idle_start;
			cpustat->idle_time[cpustat->cur_opp] += period;
			idlestat = cpustat->idle_stat[cpustat->cur_idle];
			idlestat[cpustat->cur_opp] += period;
			for (i = 0; i < cpustat->opp_size; i++)
				total += idlestat[i];
			period = total - cpustat->stat_time[cpustat->cur_idle];
			cpustat->stat_time[cpustat->cur_idle] = total;
			for (i = 0; i < count; i++) {
				if (period && (period < array[i])) {
					cpustat->period[i].time += period;
					cpustat->period[i].count++;
					break;
				}
			}

		} else if (cpustat->off_start)
			cpustat->off_time[cpustat->cur_opp] +=
					ktime - cpustat->off_start;
		cpustat->run_start = 0;
		cpustat->idle_start = 0;
		cpustat->off_start = 0;
		spin_unlock(&cpustat->lock);
		break;
	default:
		break;
	}

	return 0;
}

static int cpu_dcstat_freq_event(unsigned int cpu,  unsigned int cur)
{
	struct cpu_info *cpuinfo = &cpu_info;
	struct cpu_stat *cpustat;
	unsigned long *idlestat;
	unsigned long ktime = 0;
	unsigned long period = 0;

	if (cpuinfo->start == 0) {
		pr_debug("[%s]:cpu%d dcstat not start!\n", __func__, cpu);
		return 0;
	}

	cpustat = &per_cpu(cpu_stat, cpu);

	spin_lock(&cpustat->lock);
	ktime = ktime_to_ns(ktime_get());

	if (cpustat->run_start) {
		cpustat->run_time[cpustat->cur_opp] +=
				ktime - cpustat->run_start;
		cpustat->run_start = ktime;
	} else if (cpustat->idle_start) {
		period = ktime - cpustat->idle_start;
		cpustat->idle_time[cpustat->cur_opp] += period;
		idlestat = cpustat->idle_stat[cpustat->cur_idle];
		idlestat[cpustat->cur_opp] += period;
		cpustat->idle_start = ktime;
	} else if (cpustat->off_start) {
		cpustat->off_time[cpustat->cur_opp] +=
				ktime - cpustat->off_start;
		cpustat->off_start = ktime;
	}
	cpustat->cur_opp = cur;

	spin_unlock(&cpustat->lock);

	return 0;
}

static int cpu_dcstat_freq_notify(struct notifier_block *b,
				unsigned long event, void *data)
{
	struct cpufreq_freqs *freqs = data;
	unsigned int cpu = freqs->policy->cpu;
	//unsigned int cpu = freqs->cpu;
	unsigned int freq = freqs->new;
	struct cpu_stat *cpustat;
	unsigned int i = 0;

	cpustat = &per_cpu(cpu_stat, cpu);
	switch (event) {
	case CPUFREQ_POSTCHANGE:
		pr_debug("[%s]:cpu(%d), new(%d), old(%d)\n",
				__func__, cpu, freqs->new, freqs->old);

		while ((i < cpustat->opp_size)
			&& (cpustat->opp[i].rate != freq))
			i++;
		if (i < cpustat->opp_size)
			for_each_cpu(cpu, freqs->policy->related_cpus)
				cpu_dcstat_freq_event(cpu, i);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block cpu_dcstat_freq_notifier = {
	.notifier_call = cpu_dcstat_freq_notify,
};

static int cpu_dcstat_freq_alloc(unsigned int cpu,
			struct cpufreq_frequency_table  *table)
{
	struct cpu_stat *cpustat;
	unsigned int i = 0;

	while (table[i].frequency != CPUFREQ_TABLE_END)
		i++;

	cpustat = &per_cpu(cpu_stat, cpu);
	memset(cpustat, 0, sizeof(struct cpu_stat));

	cpustat->cpu_id = cpu;

	cpustat->opp_size = i;
	cpustat->opp = kcalloc(i, sizeof(struct opp_info), GFP_KERNEL);
	if (!cpustat->opp)
		goto fail;

	for (i = 0; i < cpustat->opp_size; i++) {
		cpustat->opp[i].index = i;
		cpustat->opp[i].rate = table[i].frequency;
	}

	cpustat->run_time = kcalloc(i, sizeof(unsigned long), GFP_KERNEL);
	if (!cpustat->run_time)
		goto fail;

	cpustat->idle_time = kcalloc(i, sizeof(unsigned long), GFP_KERNEL);
	if (!cpustat->idle_time)
		goto fail;

	cpustat->off_time = kcalloc(i, sizeof(unsigned long), GFP_KERNEL);
	if (!cpustat->off_time)
		goto fail;

	spin_lock_init(&cpustat->lock);

	return 0;
fail:
	kfree(cpustat->off_time);
	kfree(cpustat->idle_time);
	kfree(cpustat->run_time);
	kfree(cpustat->opp);
	memset(cpustat, 0, sizeof(struct cpu_stat));

	return -ENOMEM;
}

static int cpu_dcstat_freq_free(unsigned int cpu)
{
	struct cpu_stat *cpustat;

	cpustat = &per_cpu(cpu_stat, cpu);
	kfree(cpustat->opp);
	kfree(cpustat->idle_time);
	kfree(cpustat->run_time);
	kfree(cpustat->off_time);

	memset(cpustat, 0, sizeof(struct cpu_stat));

	return 0;
}

static int cpu_dcstat_freq_init(struct device *dev)
{
	struct cpufreq_policy *policy;
	unsigned int cpu = 0;
	int ret = 0;

	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get_raw(cpu);
		if (!policy) {
			//dev_err_ratelimited(dev,
				//"Cpu%d Cpufreq policy is not ready!\n", cpu);
			ret = -EPROBE_DEFER;
			goto fail;
		}

		if (cpu_dcstat_freq_alloc(cpu, policy->freq_table)) {
			dev_err(dev, "Cpu%d Alloc freqinfo failed!\n", cpu);
			ret = -ENOMEM;
			goto fail;
		}
	}

	ret = cpufreq_register_notifier(&cpu_dcstat_freq_notifier,
				CPUFREQ_TRANSITION_NOTIFIER);
	if (ret) {
		dev_err(dev, "Register cpufreq notifier fail!\n");
		ret = -EINVAL;
		goto fail;
	}

	dev_info(dev, "cpufreq_dcstat freq_init success!\n");
	return 0;
fail:
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy)
			cpu_dcstat_freq_free(cpu);
	}

	return ret;
}

static int cpu_dcstat_freq_exit(struct device *dev)
{
	struct cpufreq_policy *policy;
	unsigned int cpu = 0;

	policy = cpufreq_cpu_get(cpu);
	for_each_cpu(cpu, policy->related_cpus)
		cpu_dcstat_freq_free(cpu);

	return 0;
}

static int cpu_dcstat_idle_alloc(unsigned int cpu, unsigned int state_count)
{
	struct cpu_stat *cpustat;
	unsigned int cnt = CPU_DCSTAT_IDLE_PERIOD;
	unsigned int i = 0;

	cpustat = &per_cpu(cpu_stat, cpu);

	cpustat->idle_size = state_count;
	cpustat->idle_stat =
		kcalloc(state_count, sizeof(unsigned long *), GFP_KERNEL);
	if (!cpustat->idle_stat)
		goto fail;
	for (i = 0; i < state_count; i++) {
		cpustat->idle_stat[i] =
		kcalloc(cpustat->opp_size, sizeof(unsigned long), GFP_KERNEL);
		if (!cpustat->idle_stat[i])
			goto fail;
	}

	cpustat->stat_time =
		kcalloc(state_count, sizeof(unsigned long), GFP_KERNEL);
	if (!cpustat->stat_time)
		goto fail;

	cpustat->period =
		kcalloc(cnt, sizeof(struct period_info), GFP_KERNEL);
	if (!cpustat->period)
		goto fail;

	return 0;
fail:
	kfree(cpustat->period);
	cpustat->period = 0;
	kfree(cpustat->stat_time);
	cpustat->stat_time = 0;
	for (i = 0; (i < state_count) &&
		(cpustat->idle_stat != 0); i++) {
		kfree(cpustat->idle_stat[i]);
		cpustat->idle_stat[i] = 0;
	}
	kfree(cpustat->idle_stat);
	cpustat->idle_stat = 0;

	return -ENOMEM;
}

static int cpu_dcstat_idle_free(unsigned int cpu)
{
	struct cpu_stat *cpustat;
	unsigned int i = 0;

	cpustat = &per_cpu(cpu_stat, cpu);

	kfree(cpustat->period);
	cpustat->period = 0;
	kfree(cpustat->stat_time);
	cpustat->stat_time = 0;
	for (i = 0; (i < cpustat->idle_size) &&
		(cpustat->idle_stat != 0); i++) {
		kfree(cpustat->idle_stat[i]);
		cpustat->idle_stat[i] = 0;
	}
	kfree(cpustat->idle_stat);
	cpustat->idle_stat = 0;

	return 0;
}

static int cpu_dcstat_idle_init(struct device *dev)
{
	struct device_node *cpu_node, *state_node;
	unsigned int cpu = 0;
	unsigned int i = 0;

	for_each_possible_cpu(cpu) {
		cpu_node = of_cpu_device_node_get(cpu);
		for (i = 0; ; i++) {
			state_node = of_parse_phandle(cpu_node,
					"cpu-idle-states", i);
			if (!state_node)
				break;
			of_node_put(state_node);
		}
		i++;
		if (cpu_dcstat_idle_alloc(cpu, i)) {
			dev_err(dev, "Cpufreq-dcstat idle alloc fail!\n");
			goto fail;
		}
		of_node_put(cpu_node);
	}

	dev_info(dev, "cpufreq_dcstat idle_init success!\n");
	return 0;
fail:
	for_each_possible_cpu(cpu)
		cpu_dcstat_idle_free(cpu);

	return -ENOMEM;
}

static int cpu_dcstat_idle_exit(struct device *dev)
{
	unsigned int cpu = 0;

	for_each_possible_cpu(cpu)
		cpu_dcstat_idle_free(cpu);

	return 0;
}

static int cpu_dcstat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpu_info *cpuinfo = &cpu_info;
	struct dentry *dir = NULL;
	int ret = 0;

	ret = cpu_dcstat_freq_init(dev);
	if (ret)
		return ret;

	if (cpu_dcstat_idle_init(dev))
		goto fail;

	dir = debugfs_create_dir("cpufreq-dcstat", NULL);
	if (!dir)
		goto fail;
	cpuinfo->dir = dir;
	if (!debugfs_create_file("dcstat",
			0644, dir, NULL, &cpu_dcstat_fops))
		goto fail;

	dev_info(&pdev->dev, "Cpufreq dcstat register success!\n");
	return 0;
fail:
	debugfs_remove_recursive(dir);
	cpu_dcstat_freq_exit(dev);
	return -ENOMEM;
}

static int cpu_dcstat_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpu_info *cpuinfo = &cpu_info;

	debugfs_remove_recursive(cpuinfo->dir);
	cpu_dcstat_idle_exit(dev);
	cpu_dcstat_freq_exit(dev);

	return 0;
}

static const struct of_device_id cpu_dcstat_match[] = {
	{.compatible = "jlq,cpufreq-dcstat"},
	{},
};
static struct platform_driver cpu_dcstat_driver = {
	.driver = {
		.name	= "jlq-cpufreq-dcstat",
		.owner = THIS_MODULE,
		.of_match_table = cpu_dcstat_match,
	},
	.probe		= cpu_dcstat_probe,
	.remove		= cpu_dcstat_remove,
};
module_platform_driver(cpu_dcstat_driver);

MODULE_ALIAS("platform:jlq-cpufreq-dcstat");
MODULE_DESCRIPTION("JLQ cpufreq dcstat driver");
MODULE_LICENSE("GPL");
