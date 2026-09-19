
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
#ifndef _ISR_MONITOR_H_
#define _ISR_MONITOR_H_

#include <linux/hrtimer.h>
#if 1

struct jlq_irq_stat {
	unsigned long    last_enter;
	unsigned long    last_exit;
	unsigned long    enter;
	unsigned long    exit;
	int              cnt;
};

struct jlq_irq_data {
	unsigned long         irq_interval;
	int                   max_irqs;
	struct device         *dev;
	struct hrtimer        isr_monitor_hrtimer;
	long                  isr_monitor_clear_time;
	struct jlq_irq_stat   *boot_cpu_stat;
	struct jlq_irq_stat   *stat[0];
};

extern void show_isr_monitor(void);
#else
#define jlq_trace_isr_enter(irq)
#define jlq_trace_isr_exit(irq)
#define show_isr_monitor() {void}
#endif

#endif
