/* SPDX-License-Identifier: GPL-2.0
 * drivers/platform/jlq/dcstat/cpu-dcstat.h
 *
 * Copyright (c) 2020-2021   JLQ Technology Co.,Ltd
 *
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
 * along with this program;
 */

#ifndef __CPU_DCSTAT_H
#define __CPU_DCSTAT_H

struct opp_info {
	unsigned int index;
	unsigned int rate;
};

struct period_info {
	unsigned long time;
	unsigned long count;
};

struct cpu_stat {
	unsigned int cpu_id;
	struct opp_info *opp;
	unsigned int opp_size;
	unsigned int cur_opp;
	unsigned long run_start;
	unsigned long *run_time;
	unsigned long idle_start;
	unsigned long *idle_time;
	unsigned long **idle_stat;
	unsigned int idle_size;
	unsigned long *stat_time;
	unsigned int in_idle;
	unsigned int cur_idle;
	unsigned long off_start;
	unsigned long *off_time;
	unsigned int online;
	struct period_info *period;
	spinlock_t lock;
};

struct cpu_info {
	unsigned int start;
	unsigned long duration;
	struct dentry *dir;
};

#endif
