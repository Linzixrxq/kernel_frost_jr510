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

#ifndef _VDSP_SMMU_API_H_
#define _VDSP_SMMU_API_H_
#include <linux/device.h>
#include "vdsp-mem.h"

int vdsp_smmu_mem_region_map_init(struct device *dev,
		struct vdsp_mem_region *mem_region);

int vdsp_smmu_mem_region_map_deinit(struct device *dev,
		struct vdsp_mem_region *mem_region);

#endif /* _VDSP_SMMU_API_H_ */
