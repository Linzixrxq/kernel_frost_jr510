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

#ifndef _VDSP_DRIVER_H_
#define _VDSP_DRIVER_H_
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/vdsp-ioctl.h>
#include <linux/vdsp-intf.h>
#include "vdsp-event.h"
#include "vdsp-common.h"
#include "ssr/vdsp-ssr.h"

#ifndef ALIGN_UP
#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#endif

#define VDSP_DEFAULT_TIMEOUT 10

enum vdsp_status_t {
	VDSP_POWER_OFF = 0,
	VDSP_POWER_ON,
	VDSP_LOW_POWER,
	VDSP_MAX,
};

enum vdsp_memory_type_t {
	VDSP_MAPPING_NONE = 0x0,
	VDSP_MEMORY_TYPE_USER = 0x1,
	VDSP_MEMORY_TYPE_ALIEN = 0x2,
	VDSP_MEMORY_TYPE_KERNEL = 0x4,
};

struct vdsp_ackq_info {
	struct list_head list;
	uint32_t *result;
};

struct vdsp_ion_mmap_address {
	uint32_t dmafd;
	uint32_t dmaaddr;
	uint64_t kaddr;
};

struct vdsp_dma_buf_info {
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *table;
	enum dma_data_direction dir;
	uint64_t kaddr;
	uint32_t dmaaddr;
	struct list_head list;
	int dmafd;
	size_t len;
	enum vdsp_memory_type_t type;
	unsigned int cache_flag;
};

struct vdsp_request {
	struct vdsp_ioctl_queue ioctl_queue;
	size_t n_buffers;
	struct vdsp_dma_buf_info *buffer_mapping;
	struct vdsp_buffer *dsp_buffer;
	unsigned int in_data_dmaaddr;
	unsigned int out_data_dmaaddr;
	unsigned int dsp_buffer_dmaaddr;
	int dsp_buffer_fd;
	int request_type;
	union {
		struct vdsp_dma_buf_info in_data_mapping;
		u8 in_data[VDSP_CMD_INLINE_INDATA_SIZE];
	};
	union {
		struct vdsp_dma_buf_info out_data_mapping;
		u8 out_data[VDSP_CMD_INLINE_DATA_SIZE];
	};
	union {
		struct vdsp_dma_buf_info dsp_buffer_mapping;
		struct vdsp_buffer buffer_data[VDSP_CMD_INLINE_BUFFER_COUNT];
	};
	u8 nsid[VDSP_CMD_NAMESPACE_ID_SIZE];
};

struct vdsp_comm {
	struct mutex lock;
	void __iomem *comm;
	struct completion completion;
	u32 priority;
};

struct vdsp_file {
	struct vdsp *drvdata;
	wait_queue_head_t waitq;
	struct vdsp_event *e_ctrl;
	struct vdsp_ioctl_params params;
	struct list_head list;
};

struct vdsp {
	struct device *dev;
	struct vdsp_ssr_info *ssr_info;
	struct miscdevice miscdev;
	const struct vdsp_hw_ops *hw_ops;
	void *hw_arg;
	struct vdsp_ioctl_params params;
	unsigned long init_coreclk;
	struct mutex bw_mutex;
	struct mutex restart_mutex;
	struct mutex buf_mutex;
	unsigned int n_queues;
	u32 *queue_priority;
	struct vdsp_comm *queue;
	struct vdsp_comm **queue_ordered;
	void __iomem *comm;
	phys_addr_t comm_phys;
	atomic_t reboot_cycle;
	atomic_t reboot_cycle_complete;
	enum vdsp_status_t status;
	struct list_head smmu_buf_list;
	spinlock_t slock;
	struct list_head file_list;
	spinlock_t flock;
	atomic_t opened;
	atomic_t log_inited;
	atomic_t is_crash;
	struct vdsp_mem_region mem_region;
	struct vdsp_extern_module module;

};

int vdsp_kernel_commit_sync(struct vdsp_request *rq);
void vdsp_panic_notify(void);

#endif /* _VDSP_DRIVER_H_ */
