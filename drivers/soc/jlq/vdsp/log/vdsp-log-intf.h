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

#ifndef _VDSP_LOG_INTF_H_
#define _VDSP_LOG_INTF_H_
#include "vdsp-driver.h"
#include "vdsp-mem.h"

int vdsp_log_set_status(enum vdsp_status_t status);
int vdsp_log_start(void);
void vdsp_log_stop(void);
int vdsp_log_init(struct vdsp_mem_t *mem_info, struct log_init_params *params);
void vdsp_log_read_last(void);
extern struct blocking_notifier_head vdsp_log_notify_list;

enum vdsp_log_event {
	LOG_EVENT_SETTING,
};

#endif /* _VDSP_LOG_INTF_H_ */
