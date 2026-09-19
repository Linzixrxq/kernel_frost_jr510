/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2020   JLQ Co.,Ltd
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
#ifndef __CLK_SMD_H
#define __CLK_SMD_H

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/clk.h>

struct jlq_smdclk {
	struct clk_hw   hw;
	void __iomem    *reg; /* enable register */
	spinlock_t  *lock;
	unsigned long rate;
	unsigned int id;
	unsigned int flags;
};

#endif
