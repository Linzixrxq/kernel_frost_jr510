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

#ifndef _VDSP_WDT_H_
#define _VDSP_WDT_H_

int vdsp_wdt_hw_init(void);
int vdsp_wdt_hw_deinit(void);
int dump_vdsp_wdt_reg(void);

#endif /* _VDSP_WDT_H_ */
