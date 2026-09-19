/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

extern void pet_watchdog(void);
extern void jlq_trigger_wdog_bite(void);
extern int jlq_free_reserve_memory(struct platform_device *pdev, int index);
extern int jlq_sysdump_enabled(void);
