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
#ifndef __CLK_JR510_H
#define __CLK_JR510_H

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/clk.h>


#define JLQ_ROUND_UP(r, g, d) ((r) / (g) * (d))

#define RET_FOUND 0
#define RET_MISS  1

#define FLAG_VAL_ODD      BIT(0)
#define FLAG_DIV_DESCEND  BIT(1)
#define FLAG_RATE_HIGHER  BIT(0)
#define FLAG_RATE_LOWER BIT(1)

#define FLAG_RATE_CLOSER BIT(2)
#define FLAG_MULTI_LEVEL  BIT(4)
#define FLAG_NEED_UPDATE  BIT(5)

struct jlq_clk {
	void __iomem    *top_crg;
	void __iomem    *ai_sysctrl;
	void __iomem    *cm4_sysctrl;
	void __iomem    *display_sysctrl;
	void __iomem    *hblk_sysctrl;
	void __iomem    *vdsp_sysctrl;
	void __iomem    *vpu_sysctrl;
	void __iomem    *audio_sysctrl;
	void __iomem    *testpwr;
	spinlock_t lock;
};

struct jlq_div_desc {
	char *name;
	/* when regval from min to max, it's default div order.*/
	int  def_div_ord;
};

enum ord {
	ORD_DES = 0, /*order descend.*/
	ORD_AS,      /*order ascend.*/
	ORD_NONE,
};

struct jlq_div_table {
	unsigned int val;
	unsigned int grp;
	unsigned int div;
};

struct table_param {
	unsigned int flags;
	int min_max[2];
	enum ord def_div_ord;
	void (*formula)(unsigned int val, unsigned int mask,
			struct jlq_div_table *ptbl);
};

struct jlq_divclk {
	struct clk_hw  hw;
	void __iomem   *reg; /* divider register */
	spinlock_t     *lock;
	struct jlq_div_table  *table;
	unsigned int mask;
	unsigned int shift;
	unsigned int weshift;
	unsigned int type;
	unsigned int flags;
};

struct jlq_gateclk {
	struct clk_hw   hw;
	void __iomem    *reg; /* enable register */
	spinlock_t  *lock;
	unsigned int shift;
	unsigned int weshift;
};

struct jlq_thruclk {
	struct clk_hw   hw;
};

struct jlq_fdiv_table {
	unsigned long baud;
	unsigned long rate;
	unsigned int mul;
	unsigned int div;
};

struct jlq_fdivclk {
	struct clk_hw hw;
	void __iomem *reg;	/* divider register */
	spinlock_t *lock;
	struct jlq_fdiv_table *table;
	unsigned int divmask;
	unsigned int mulmask;
	unsigned int divshift;
	unsigned int mulshift;
	unsigned int flags;
};

struct jlq_pllx_table {
	unsigned long rate;
	unsigned int p;
	unsigned int m;
	unsigned int s;
	unsigned int k;
};

struct jlq_pllxclk {
	struct clk_hw hw;
	void __iomem *reg0;
	void __iomem *reg1;
	void __iomem *reg2;
	spinlock_t *lock;
	struct jlq_pllx_table *table;
	unsigned int pmask;
	unsigned int mmask;
	unsigned int smask;
	unsigned int kmask;
	unsigned int pshift;
	unsigned int mshift;
	unsigned int sshift;
	unsigned int kshift;
	unsigned int pdshift;
	unsigned int pdweshift;
	unsigned int adjshift;
	unsigned int adjweshift;
	unsigned int flags;
};

struct jlq_mux_table {
	unsigned int idx;
	unsigned long rate;
	struct clk *parent_clk;
};

struct jlq_muxclk {
	struct clk_hw hw;
	void __iomem *reg; /* mux select register */
	spinlock_t *lock;
	struct jlq_mux_table *table;
	unsigned int mask;
	unsigned int shift;
	unsigned int weshift;
	unsigned int mask1;
	unsigned int shift1;
	unsigned int weshift1;
	unsigned int upshift;
	unsigned int upweshift;
	unsigned int flags;
};

struct jlq_clk_ops {
	//void (*setup)(struct device *dev);
	void (*setup)(struct device_node *np);
	unsigned int flags;
};

void jlq_clk_debug_init(struct clk_hw *hw, struct dentry *dentry);

#endif
