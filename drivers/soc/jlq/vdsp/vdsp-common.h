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

#ifndef _VDSP_COMMON_H_
#define _VDSP_COMMON_H_
#include <linux/slab.h>

struct com_queue_head {
	struct list_head list;
	spinlock_t lock;
	int len;
};

static inline void common_init_queue(struct com_queue_head *qhead)
{
	WARN_ON(!qhead);

	INIT_LIST_HEAD(&qhead->list);
	spin_lock_init(&qhead->lock);
	qhead->len = 0;
}

static inline void common_enqueue(struct com_queue_head *qhead,
		struct list_head *entry)
{
	unsigned long flags;

	spin_lock_irqsave(&qhead->lock, flags);
	qhead->len++;
	list_add_tail(entry, &qhead->list);
	spin_unlock_irqrestore(&qhead->lock, flags);
}

#define common_dequeue(qhead, type, member) ({\
	unsigned long flags;\
	struct com_queue_head *__q = (qhead);\
	type *node = 0;\
	spin_lock_irqsave(&__q->lock, flags);\
	if (!list_empty(&__q->list)) {\
		__q->len--;\
		node = list_first_entry(&__q->list,\
			type, member);\
		if ((node) && (&node->member) && (&node->member.next))\
			list_del_init(&node->member);\
	} \
	spin_unlock_irqrestore(&__q->lock, flags);\
	node;\
})

#define common_queue_clean(queue, type, member) do {\
	unsigned long flags;\
	struct com_queue_head *__q = (queue);\
	type *node;\
	spin_lock_irqsave(&__q->lock, flags);\
	while (!list_empty(&__q->list)) {\
		__q->len--;\
		node = list_first_entry(&__q->list,\
				type, member);\
		if (node) {\
			if (&node->member) \
				list_del_init(&node->member);\
			kzfree(node);\
		} \
	} \
	spin_unlock_irqrestore(&__q->lock, flags);\
} while (0)

#endif /*_VDSP_COMMON_H_ */
