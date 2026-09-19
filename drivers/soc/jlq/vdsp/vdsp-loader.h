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

#ifndef _VDSP_LOADER_H_
#define _VDSP_LOADER_H_
#include <linux/platform_device.h>
#include "vdsp-mem.h"

void vdsp_loader_do(void);
void vdsp_loader_unload(void);
int vdsp_loader_wait_done(void);
int vdsp_loader_init(struct platform_device *pdev, struct vdsp_mem_t *mem_info);
void vdsp_loader_uninit(void);
#endif /* _VDSP_LOADER_H_ */
