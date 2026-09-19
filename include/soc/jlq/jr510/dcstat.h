/* SPDX-License-Identifier: GPL-2.0
 * include/soc/jlq/jr510/dcstat.h
 *
 * Copyright (c) 2020-2021   JLQTech Co.,Ltd
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

#ifndef __JR510_DCSTAT_H
#define __JR510_DCSTAT_H

enum cpu_msg {
	CPU_DCSTAT_START = 0,
	CPU_DCSTAT_STOP,
	CPU_DCSTAT_CHANGE_FREQ,
	CPU_DCSTAT_IDLE_ENTER,
	CPU_DCSTAT_IDLE_EXIT,
	CPU_DCSTAT_UP,
	CPU_DCSTAT_DOWN,
};

int cpu_dcstat_idle_event(unsigned int cpu,
			unsigned int msg, unsigned int cur);
int cpu_dcstat_hotplug_event(unsigned int cpu, unsigned int msg);

#endif
