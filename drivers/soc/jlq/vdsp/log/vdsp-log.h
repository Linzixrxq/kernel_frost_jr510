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

#ifndef _JLQ_VDSP_LOG_H
#define _JLQ_VDSP_LOG_H
#include <linux/of_irq.h>
#include "vdsp-driver.h"
#include "vdsp-runtime-type.h"

#define VDSP_LOG_NAME "vdsp_log"
#define VDSP_LOG_NAME_MAX_LEN 32

#define VDSP_LOG_MAX_LEN       256
#define DEDAULT_FIFO_WIDTH     VDSP_LOG_MAX_LEN
#define DEDAULT_FIFO_NUM       256
#define LOG_FILE_MAX_SIZE (10 * 1024 * 1024)

#define VDSP_LOG_HEAD_SIZE    128
#define VDSP_LOG_PARAM_SIZE   64

struct jlq_log_ops;

enum log_state {
	STOPPED = 0,
	RUNNING,
};

struct log_param_t {
	uint32_t is_open;
	uint32_t log_level;
	uint32_t water_level;
	uint64_t log_info_iova;
	uint64_t log_info_phya;
	uint64_t fifo_num;
	uint64_t fifo_width;
};

struct log_buf_head_info_t {
	//uint32_t is_lock;
	uint64_t write_log_id;
	uint64_t read_log_id;
	uint64_t log_pool_paddr;
	uint64_t log_pool_iovaddr;
	uint64_t log_pool_cpu_addr;
	//uint64_t fifo_num;
	//uint64_t fifo_width;
};

struct log_runtime_parameters_t {
	unsigned int log_enable;
	unsigned int log_level;
	unsigned int output_mode;
	unsigned int output_modules;
};

struct log_handle_t {
	//struct log_runtime_parameters_t runtime_params;
	struct log_common_params_t common_params;
	struct log_buf_head_info_t *buf_head;
	uint64_t head_paddr;
	char log_name[VDSP_LOG_NAME_MAX_LEN];
	spinlock_t log_lock;
	wait_queue_head_t log_irq_wait;
	atomic_t log_irq_count;
	struct task_struct *log_thread;
	loff_t pos;
	int file_index;
	struct file *file;
};

struct jlq_log_ops {
	int (*init)(struct log_handle_t *log_handle);
	int (*config)(u8 log_level);
	irqreturn_t (*handle)(struct log_handle_t *log_handle);
	int (*process_function)(void *data);
	int (*set_param)(struct log_handle_t *log_handle);
	int (*destroy)(struct log_handle_t *log_handle);
};

struct vdsp_log_drvdata {
	struct cdev cdev;
	struct device *dev;
	struct class *vdsp_log_class;
	struct completion wait_complete;
	wait_queue_head_t waitq;
	struct mutex mutex;
	struct log_handle_t *log_handle;
	unsigned int log_irq_id;
	bool is_irq_enable;
	bool is_last_read;
	struct kobject *vdsp_log_obj;
	struct log_init_params *init_params;
	void __iomem *baseaddr;
	phys_addr_t phys_addr;
	void __iomem *irq_base;
};

struct log_address_info_t {
	u64 virt_addr;
	u64 phy_addr;
};

#endif /*_JLQ_VDSP_LOG_H*/
