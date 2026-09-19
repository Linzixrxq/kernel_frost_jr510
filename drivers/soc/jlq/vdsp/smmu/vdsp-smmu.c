// SPDX-License-Identifier: GPL-2.0
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
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/iommu.h>
#include "vdsp-smmu-api.h"

int vdsp_smmu_mem_region_map_init(struct device *dev, struct vdsp_mem_region *mem_region)
{
	int rc = 0;
	int i = 0;
	struct iommu_domain *domain;

	if (!dev || !mem_region) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		pr_err("dev has no domain set");
		return -ENODEV;
	}

	for (i = 0; i < VDSP_MEM_TYPE_MAX; i++) {
		switch (i) {
		case VDSP_CODE_MEM:
			rc = iommu_map(domain,
				mem_region->vdsp_start_addr
				+ mem_region->vdsp_mem_info[i].offset,
				mem_region->vdsp_mem_info[i].phys_addr,
				mem_region->vdsp_mem_info[i].size,
				IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV);
			break;
		case VDSP_CODE_BACKUP_MEM:
			break;
		case VDSP_RESERVED_MEM:
		case VDSP_PARAM_MEM:
		case VDSP_SYSDUMP_MEM:
		case VDSP_LOG_SYS_MEM:
		case VDSP_SMD_SYS_MEM:
			rc = iommu_map(domain,
				mem_region->vdsp_start_addr
				+ mem_region->vdsp_mem_info[i].offset,
				mem_region->vdsp_mem_info[i].phys_addr,
				mem_region->vdsp_mem_info[i].size,
				IOMMU_READ | IOMMU_WRITE);
			break;
		case VDSP_DYNAMIC_ALLOC_MEM:
			domain->geometry.aperture_start = mem_region->vdsp_mem_info[i].offset
							+ mem_region->vdsp_start_addr;
			domain->geometry.aperture_end = domain->geometry.aperture_start
							+ mem_region->vdsp_mem_info[i].size;
			break;
		default:
			return -EINVAL;
		}
	}

	return rc;
}
EXPORT_SYMBOL(vdsp_smmu_mem_region_map_init);

int vdsp_smmu_mem_region_map_deinit(struct device *dev, struct vdsp_mem_region *mem_region)
{
	int i = 0;
	struct iommu_domain *domain;

	if (!dev || !mem_region) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		pr_err("dev has no domain set");
		return -ENODEV;
	}

	for (i = 0; i < VDSP_MEM_TYPE_MAX; i++) {
		if (i != VDSP_DYNAMIC_ALLOC_MEM) {
			iommu_unmap(domain, mem_region->vdsp_start_addr
					+ mem_region->vdsp_mem_info[i].offset,
					mem_region->vdsp_mem_info[i].size);
		}
	}

	domain->geometry.aperture_start = 0;
	iommu_detach_device(domain, dev);
	//iommu_domain_free(domain);

	return 0;
}
EXPORT_SYMBOL(vdsp_smmu_mem_region_map_deinit);

MODULE_DESCRIPTION("JLQ vdsp SMMU driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: arm-smmu-v3");
