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
#include <linux/slab.h>
#include "vdsp-event.h"
#include <linux/module.h>

static unsigned int event_pos(const struct vdsp_event *e_ctrl,
		unsigned int idx)
{
	idx += e_ctrl->first;
	return idx >= e_ctrl->elems ? idx - e_ctrl->elems : idx;
}

int vdsp_event_pending(struct vdsp_event *e_ctrl)
{
	if (!e_ctrl)
		return -ENOMEM;
	return e_ctrl->navailable;
}
EXPORT_SYMBOL(vdsp_event_pending);

int vdsp_event_init(struct vdsp_event **event_ctrl)
{
	int rc = 0;
	struct vdsp_event *e_ctrl;

	e_ctrl = kzalloc(sizeof(struct vdsp_event), GFP_KERNEL);
	if (!e_ctrl)
		return -ENOMEM;

	e_ctrl->elems = MAX_EVENT_COUNTER;

	INIT_LIST_HEAD(&e_ctrl->available);
	spin_lock_init(&e_ctrl->sp_lock);
	e_ctrl->navailable = 0;
	*event_ctrl = e_ctrl;

	return rc;
}
EXPORT_SYMBOL(vdsp_event_init);

int vdsp_event_queue(struct vdsp_event *e_ctrl,
		struct vdsp_notify_info *e_info)
{
	unsigned long flags;
	struct event_element *e_elemt;

	if (!e_ctrl)
		return -EINVAL;

	spin_lock_irqsave(&e_ctrl->sp_lock, flags);
	/* event queue full? */
	if (e_ctrl->in_use == e_ctrl->elems) {
		/* remove the oldest event */
		e_elemt = e_ctrl->events + event_pos(e_ctrl, 0);
		list_del(&e_elemt->list);
		e_ctrl->in_use--;
		e_ctrl->first = event_pos(e_ctrl, 1);
		e_ctrl->navailable--;
		pr_info("drop the oldest event.\n");
	}

	/* take one and fill it*/
	e_elemt = e_ctrl->events + event_pos(e_ctrl, e_ctrl->in_use);
	memcpy(&e_elemt->event, e_info, sizeof(struct vdsp_notify_info));
	e_ctrl->in_use++;
	list_add_tail(&e_elemt->list, &e_ctrl->available);
	e_ctrl->navailable++;
	spin_unlock_irqrestore(&e_ctrl->sp_lock, flags);

	return 0;
}
EXPORT_SYMBOL(vdsp_event_queue);

int vdsp_event_dequeue(struct vdsp_event *e_ctrl,
		struct vdsp_notify_info *e_info, int nonblocking)
{
	unsigned long flags;
	struct event_element *e_elemt;

	if (!nonblocking) {
		pr_err("Not support block access.\n");
		return -EINVAL;
	}

	if (!e_ctrl || !e_info)
		return -EINVAL;

	spin_lock_irqsave(&e_ctrl->sp_lock, flags);
	if (list_empty(&e_ctrl->available)) {
		spin_unlock_irqrestore(&e_ctrl->sp_lock, flags);
		return -ENOENT;
	}

	WARN_ON(e_ctrl->navailable == 0);
	e_elemt = list_first_entry(&e_ctrl->available,
			struct event_element, list);
	list_del(&e_elemt->list);
	e_ctrl->navailable--;

	memcpy(e_info, &e_elemt->event, sizeof(struct vdsp_notify_info));
	e_ctrl->first = event_pos(e_ctrl, 1);
	e_ctrl->in_use--;
	spin_unlock_irqrestore(&e_ctrl->sp_lock, flags);

	return 0;
}
EXPORT_SYMBOL(vdsp_event_dequeue);

void vdsp_event_uninit(struct vdsp_event *e_ctrl)
{
	if (e_ctrl)
		kzfree(e_ctrl);
}
EXPORT_SYMBOL(vdsp_event_uninit);

MODULE_LICENSE("GPL");
