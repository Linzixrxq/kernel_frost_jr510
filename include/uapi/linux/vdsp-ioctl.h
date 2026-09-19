/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 */

#ifndef _VDSP_IOCTL_H
#define _VDSP_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define VDSP_IOCTL_MAGIC 'r'
#define VDSP_IOCTL_ALLOC       _IO(VDSP_IOCTL_MAGIC, 1)
#define VDSP_IOCTL_FREE        _IO(VDSP_IOCTL_MAGIC, 2)
#define VDSP_IOCTL_QUEUE       _IO(VDSP_IOCTL_MAGIC, 3)
#define VDSP_IOCTL_QUEUE_NS    _IO(VDSP_IOCTL_MAGIC, 4)
#define VDSP_IOCTL_INIT        _IO(VDSP_IOCTL_MAGIC, 5)
#define VDSP_IOCTL_RESET       _IO(VDSP_IOCTL_MAGIC, 6)
#define VDSP_IOCTL_DVFS        _IO(VDSP_IOCTL_MAGIC, 7)
#define VDSP_IOCTL_GET_LOADING _IO(VDSP_IOCTL_MAGIC, 8)
#define VDSP_IOCTL_DQEVENT     _IO(VDSP_IOCTL_MAGIC, 9)
#define VDSP_IOCTL_MMAP       _IO(VDSP_IOCTL_MAGIC, 10)
#define VDSP_IOCTL_MUNMAP     _IO(VDSP_IOCTL_MAGIC, 11)

#define MEM_FLAG_DMA_RW (1<<0)
#define MEM_FLAG_KERNEL (1<<1)

enum {
	VDSP_RESET_TYPE_NORMAL,
	VDSP_RESET_TYPE_PANIC,
};

struct vdsp_dvfs_params {
	__u64 core_clk;
	__u64 bps;
	__u64 mips;
};

struct vdsp_ioctl_params {
	struct vdsp_dvfs_params dvfs_params; /* dvfs vote parameters */
	__u32 loading; /* vdsp core loading */
	__u32 reset_type;
};

struct vdsp_ioctl_alloc {
	__u32 size;
	__u32 align;
	__u32 flags;
	__u32 dmafd;
	__u64 uaddr;
	__u32 heap_id_mask;
};

enum {
	VDSP_FLAG_READ = 0x1,
	VDSP_FLAG_WRITE = 0x2,
	VDSP_FLAG_READ_WRITE = 0x3,
};

struct vdsp_ioctl_buffer {
	__u32 flags;
	__u32 size;
	__u32 dmafd;
	__u64 addr;
};

enum {
	VDSP_QUEUE_FLAG_NSID = 0x4,
	VDSP_QUEUE_FLAG_PRIO = 0xff00,
	VDSP_QUEUE_FLAG_PRIO_SHIFT = 8,

	VDSP_QUEUE_VALID_FLAGS =
		VDSP_QUEUE_FLAG_NSID |
		VDSP_QUEUE_FLAG_PRIO,
};

struct vdsp_ioctl_queue {
	__u32 flags;
	__u32 in_data_dmafd;
	__u32 out_data_dmafd;
	__u32 in_data_size;
	__u32 out_data_size;
	__u32 buffer_size;
	__u64 in_data_addr;
	__u64 out_data_addr;
	__u64 buffer_addr;
	__u64 nsid_addr;
};

enum vdsp_notify_type {
	VDSP_EVENT_PANIC = 1,
	VDSP_EVENT_MAX = 0xFFFFFFFF,
};

struct vdsp_notify_info {
	enum vdsp_notify_type type;
};

#endif /* _VDSP_IOCTL_H */
