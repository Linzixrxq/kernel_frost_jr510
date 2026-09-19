/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#ifndef _VDSP_INTF_H
#define _VDSP_INTF_H
#include <asm/types.h>

enum {
	VDSP_SYNC_IDLE = 0,
	VDSP_SYNC_HOST_TO_DSP = 0x1,
	VDSP_SYNC_DSP_TO_HOST = 0x3,
	VDSP_SYNC_START = 0x101,
	VDSP_SYNC_DSP_READY_V1 = 0x203,
	VDSP_SYNC_DSP_READY_V2 = 0x303,
};

enum {
	VDSP_SYNC_TYPE_ACCEPT = 0x80000000,
	VDSP_SYNC_TYPE_MASK = 0x00ffffff,

	VDSP_SYNC_TYPE_LAST = 0,
	VDSP_SYNC_TYPE_HW_SPEC_DATA = 1,
	VDSP_SYNC_TYPE_HW_QUEUES = 2,
};

struct vdsp_tlv {
	__u32 type;
	__u32 length;
	__u32 value[0];
};

struct xrp_dsp_sync_v1 {
	__u32 sync;
	__u32 hw_sync_data[0];
};

struct vdsp_sync_v2 {
	__u32 sync;
	__u32 reserved[3];
	struct vdsp_tlv hw_sync_data[0];
};

enum {
	VDSP_BUFFER_FLAG_READ = 0x1,
	VDSP_BUFFER_FLAG_WRITE = 0x2,
};

struct vdsp_buffer {
	/*
	 * When submitted to DSP: types of access allowed
	 * When returned to host: actual access performed
	 */
	__u32 flags;
	__u32 size;
	__u32 addr;
};

enum {
	VDSP_CMD_FLAG_REQUEST_VALID = 0x00000001,
	VDSP_CMD_FLAG_RESPONSE_VALID = 0x00000002,
	VDSP_CMD_FLAG_REQUEST_NSID = 0x00000004,
	VDSP_CMD_FLAG_RESPONSE_DELIVERY_FAIL = 0x00000008,
};

#define VDSP_CMD_INLINE_INDATA_SIZE 512
#define VDSP_CMD_INLINE_DATA_SIZE 64
#define VDSP_CMD_INLINE_BUFFER_COUNT 1
#define VDSP_CMD_NAMESPACE_ID_SIZE 16
#define VDSP_CMD_STRIDE 768

struct vdsp_cmd {
	__u32 flags;
	__u32 in_data_size;
	__u32 out_data_size;
	__u32 buffer_size;
	union {
		__u32 in_data_addr;
		__u8 in_data[VDSP_CMD_INLINE_INDATA_SIZE];
	};
	union {
		__u32 out_data_addr;
		__u8 out_data[VDSP_CMD_INLINE_DATA_SIZE];
	};
	union {
		__u32 buffer_addr;
		struct vdsp_buffer buffer_data[VDSP_CMD_INLINE_BUFFER_COUNT];
		__u8 buffer_alignment[VDSP_CMD_INLINE_DATA_SIZE];
	};
	__u8 nsid[VDSP_CMD_NAMESPACE_ID_SIZE];
};

#endif /* _VDSP_INTF_H */
