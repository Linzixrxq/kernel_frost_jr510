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
#ifndef _VDSP_EVENT_H_
#define _VDSP_EVENT_H_
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/vdsp-ioctl.h>

struct vdsp_event;

struct event_element {
	struct list_head list;
	struct vdsp_event *ctrl;
	struct vdsp_notify_info event;
};

#define MAX_EVENT_COUNTER 100

struct vdsp_event {
	spinlock_t sp_lock;
	struct list_head available;
	unsigned int navailable;
	unsigned int in_use;
	unsigned int first;
	unsigned int elems;
	struct event_element events[MAX_EVENT_COUNTER];
};

int vdsp_event_pending(struct vdsp_event *e_ctrl);
int vdsp_event_init(struct vdsp_event **event_ctrl);
int vdsp_event_queue(struct vdsp_event *e_ctrl,
		struct vdsp_notify_info *e_info);
int vdsp_event_dequeue(struct vdsp_event *e_ctrl,
		struct vdsp_notify_info *e_info, int nonblocking);
void vdsp_event_uninit(struct vdsp_event *e_ctrl);

#endif /* _VDSP_EVENT_H_ */
