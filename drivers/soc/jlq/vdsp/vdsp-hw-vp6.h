/* SPDX-License-Identifier: GPL-2.0 */
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
 * See the GNU General Public License for more details.
 *
 */

#ifndef _VDSP_HW_VP6_H_
#define _VDSP_HW_VP6_H_

#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/interconnect.h>

#include "vdsp-hw-api.h"
#include "vdsp-mem.h"

enum vdsp_irq_mode {
	VDSP_IRQ_NONE,
	VDSP_IRQ_LEVEL,
	VDSP_IRQ_EDGE,
	VDSP_IRQ_EDGE_SW,
	VDSP_IRQ_MAX,
};

enum {
	VDSP_SYNC_IRQ_MODE_NONE = 0x0,
	VDSP_SYNC_IRQ_MODE_LEVEL = 0x1,
	VDSP_SYNC_IRQ_MODE_EDGE = 0x2,
};

struct vdsp_clk_vol {
	unsigned long clk;
	int vol;
};

struct vdsp_hw_sync_data {
	__u32 device_sysctl_base;
	__u32 device_tcm_base;
	__u32 host_irq_mode;
	__u32 host_irq_type;
	__u32 host_irq_id;
	__u32 device_irq_mode;
	__u32 device_irq_type;
	__u32 device_irq_id;
	__u32 reserved;
	__u32 log_enable;
	__u32 log_output_mode;
	__u32 log_output_modules;
	__u32 log_level;
	__u32 log_fifo_depth;
	__u32 log_fifo_width;
	__u32 log_fifo_watermark;
};

struct vdsp_config_param {
	unsigned int clk;
	unsigned int reserved;
	unsigned long sec_offset;
	unsigned long usec_offset;
};

struct vdsp_hw {
	struct vdsp *vdsp;
	phys_addr_t tcm_phys;
	phys_addr_t sysctl_phys;
	phys_addr_t irqtrig_phys;
	phys_addr_t lpm_phys;
	phys_addr_t mem_phys;
	void __iomem *tcm_regs;
	void __iomem *sysctl_regs;
	void __iomem *irqtrig_regs;
	void __iomem *lpm_regs;
	void __iomem *top_crg_regs;
	void __iomem *mem_baseaddr;
	void __iomem *firmware_addr;
	void __iomem *firmware_backup_addr;
	void __iomem *param_addr;
	u64 mem_size;
	struct vdsp_mem_region vdsp_mem_region;
	struct clk *coreclk;
	struct clk *bus_clk;
	struct clk *coresight_clk;
	unsigned int is_debug_enable;
	unsigned int is_support_smmu;
	struct regulator *tbu_regulator;
	struct regulator *regulator;
	/* how IRQ is used to notify the device of incoming data */
	enum vdsp_irq_mode device_irq_mode;
	/*
	 * IRQ number (device side)
	 * IRQ type (SECURE(0)/NON_SECURE(1))
	 * reserved
	 */
	u32 device_irq[3];
	/* how IRQ is used to notify the host of incoming data */
	enum vdsp_irq_mode host_irq_mode;
	/*
	 * IRQ number (device side)
	 * IRQ type (SECURE(0)/NON_SECURE(1))
	 */
	u32 host_irq[2];
	struct icc_path *vdsp_ddr;
	/* log_enable:0 or 1; output_mode:enum log_output_mode; */
	/* output_modules:enum log_output_modules; log_level:enum log_output_levels; */
	/* fifo_depth; fifo_width; fifo_watermark */
	u32 log[7];
	struct log_init_params run_params;
	unsigned long *freq_table;
	unsigned int max_state;
	struct opp_table *opp_table;
};

#endif /* _VDSP_HW_VP6_H_ */
