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

#ifndef _VDSP_MEM_H_
#define _VDSP_MEM_H_

enum vdsp_mem_type_t {
	VDSP_CODE_MEM = 0,
	VDSP_CODE_BACKUP_MEM,
	VDSP_RESERVED_MEM,
	VDSP_PARAM_MEM,
	VDSP_SYSDUMP_MEM,
	VDSP_LOG_SYS_MEM,
	VDSP_SMD_SYS_MEM,
	VDSP_DYNAMIC_ALLOC_MEM,
	VDSP_MEM_TYPE_MAX
};

struct vdsp_mem_t {
	void __iomem *baseaddr;
	phys_addr_t phys_addr;
	u64 offset;
	u64 size;
};

struct vdsp_mem_region {
	u64 vdsp_start_addr;//vdsp start virtual address
	struct vdsp_mem_t vdsp_mem_info[VDSP_MEM_TYPE_MAX];
};

#endif /* _VDSP_MEM_H_ */
