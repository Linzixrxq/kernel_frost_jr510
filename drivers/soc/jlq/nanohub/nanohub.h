/*
 * Copyright (c)2019-2021   JLQ Technology Co.,Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */
#ifndef _NANOHUB_MAIN_H
#define _NANOHUB_MAIN_H

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
//#include <linux/wakelock.h>
#include <soc/jlq/jr510/jlq-bridge.h>

#include "comms.h"

#define MBOX_CM4F_NINTR_SET        (0x60)
#define MBOX_GEN_REG0              (0x150) //RESET
#define MBOX_GEN_REG1              (0x154) //MCU LOCK
#define MBOX_GEN_REG2              (0x158) //AP WAKEUP
#define MBOX_GEN_REG3              (0x15C) //AP NON-WAKEUP
#define MBOX_GEN_REG4              (0x160) //AP VDDR/VDDC
#define MBOX_GEN_REG5              (0x164) //CM4 VDDR/VDDC
#define MBOX_GEN_REG6              (0x168) //LOAD ADDR HIGH
#define MBOX_GEN_REG7              (0x16C) //LOAD ADDR LOW

#define MBOX_CM42AP_NINTR_STA      (0x3C) //NINTR

#define CM4GPIO_INTR_CLR0          (0x200)
#define CM4GPIO_INTR_MASK_CPU20    (0x2E0)

struct nanohub_platform_data {
	int wakelock_id;
	int apwakeup_irq;
	int apnonwakeup_irq;
	void __iomem *top_mbox_base;
	void __iomem *cm4_gpio_base;
	uint32_t cm4gpios;

	struct regulator *sh_power;
};

#define NANOHUB_NAME "nanohub"

struct nanohub_buf {
	struct list_head list;
	uint32_t seq;
	uint32_t reason;
	uint8_t buffer[255];
	uint8_t length;
};

struct nanohub_data;

struct nanohub_io {
	struct device *dev;
	struct nanohub_data *data;
	wait_queue_head_t buf_wait;
	struct list_head buf_list;
};

static inline struct nanohub_data *dev_get_nanohub_data(struct device *dev)
{
	struct nanohub_io *io = dev_get_drvdata(dev);

	return io->data;
}

struct nanohub_data {
	/* indices for io[] array */
	#define ID_NANOHUB_SENSOR 0
	#define ID_NANOHUB_COMMS 1
	#define ID_NANOHUB_MAX 2

	struct device *dev;
	struct bridge_channel *ch;
	struct nanohub_io io[ID_NANOHUB_MAX];
	uint32_t seq;
	uint8_t *rx_buf;

	//struct nanohub_comms comms;
	const struct nanohub_platform_data *pdata;
	int apwakeup;
	int apnonwakeup;

	atomic_t kthread_run;
	atomic_t thread_state;
	wait_queue_head_t kthread_wait;

	//struct wake_lock wakelock_read;

	struct nanohub_io free_pool;

	atomic_t lock_mode;
	/* these 3 vars should be accessed only with wakeup_wait.lock held */
	atomic_t wakeup_cnt;
	atomic_t wakeup_lock_cnt;
	atomic_t wakeup_acquired;
	atomic_t notify_cnt;
	wait_queue_head_t wakeup_wait;
	wait_queue_head_t notify_wait;
	unsigned long wakeup_flags;
	unsigned long notify_flags;

	uint32_t interrupts[8];
	uint32_t load_addr;
	void __iomem *dst_addr;

	ktime_t wakeup_err_ktime;
	int wakeup_err_cnt;

	ktime_t kthread_err_ktime;
	int kthread_err_cnt;

	void *vbuf;
	struct task_struct *thread;
	const struct firmware *fw_entry;
};

enum {
	KEY_WAKEUP_NONE,
	KEY_WAKEUP,
	KEY_WAKEUP_LOCK,
};

enum {
	LOCK_MODE_NONE,
	LOCK_MODE_NORMAL,
	LOCK_MODE_IO,
	LOCK_MODE_IO_BL,
	LOCK_MODE_RESET,
	LOCK_MODE_SUSPEND_RESUME,
};

/* the following fragment is based on wait_event_* code from wait.h */
#define wait_event_interruptible_timeout_locked(q, cond, tmo, flag)	\
({									\
	long __ret = (tmo);						\
	DEFINE_WAIT(__wait);						\
	if (!(cond)) {							\
		for (;;) {						\
			__wait.flags &= ~WQ_FLAG_EXCLUSIVE;		\
			if (list_empty(&__wait.entry))			\
				__add_wait_queue_entry_tail(&(q), &__wait); \
			set_current_state(TASK_INTERRUPTIBLE);		\
			if ((cond))					\
				break;					\
			if (signal_pending(current)) {			\
				__ret = -ERESTARTSYS;			\
				break;					\
			}						\
			spin_unlock_irqrestore(&(q).lock, flag);	\
			__ret = schedule_timeout(__ret);		\
			spin_lock_irqsave(&(q).lock, flag);		\
			if (!__ret) {					\
				if ((cond))				\
					__ret = 1;			\
				break;					\
			}						\
		}							\
		__set_current_state(TASK_RUNNING);			\
		if (!list_empty(&__wait.entry))				\
			list_del_init(&__wait.entry);			\
		else if (__ret == -ERESTARTSYS &&			\
			 /*reimplementation of wait_abort_exclusive() */\
			 waitqueue_active(&(q)))			\
			__wake_up_locked_key(&(q), TASK_INTERRUPTIBLE,	\
			NULL);						\
	} else {							\
		__ret = 1;						\
	}								\
	__ret;								\
})									\

int request_wakeup_ex(struct nanohub_data *data, long timeout,
		      int key, int lock_mode);
void release_wakeup_ex(struct nanohub_data *data, int key, int lock_mode);
int nanohub_wait_for_interrupt(struct nanohub_data *data);
int nanohub_wakeup_eom(struct nanohub_data *data, bool repeat);

static inline int nanohub_notify_fired(struct nanohub_data *data)
{
	return atomic_read(&data->notify_cnt);
}

static inline int nanohub_irq1_fired(struct nanohub_data *data)
{
	return data->apwakeup && !readl(data->pdata->top_mbox_base + MBOX_GEN_REG2);
}

static inline int nanohub_irq2_fired(struct nanohub_data *data)
{
	return data->apnonwakeup && !readl(data->pdata->top_mbox_base + MBOX_GEN_REG3);
}

static inline int request_wakeup_timeout(struct nanohub_data *data, int timeout)
{
	return request_wakeup_ex(data, timeout, KEY_WAKEUP, LOCK_MODE_NORMAL);
}

static inline int request_wakeup(struct nanohub_data *data)
{
	return request_wakeup_ex(data, MAX_SCHEDULE_TIMEOUT, KEY_WAKEUP,
				 LOCK_MODE_NORMAL);
}

static inline void release_wakeup(struct nanohub_data *data)
{
	release_wakeup_ex(data, KEY_WAKEUP, LOCK_MODE_NORMAL);
}
#endif
