/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2014, 2017-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _VDSP_RAMDUMP_HEADER
#define _VDSP_RAMDUMP_HEADER
#include <linux/cdev.h>

struct device;

enum ramdump_status {
	RAMDUMP_NOMAL,
	RAMDUMP_DUMPING,
	RAMDUMP_DONE,
};

struct vdsp_ramdump_device {
	char name[256];
	enum ramdump_status status;
	struct vdsp_subsys_desc *desc;
	struct completion ramdump_complete;
	struct mutex consumer_lock;
	struct cdev cdev;
	struct device *dev;
	wait_queue_head_t dump_wait_q;
};

void ramdump_happened(struct vdsp_ramdump_device *dev);
void *vdsp_create_ramdump_device(const char *dev_name, struct device *parent);
void vdsp_destroy_ramdump_device(void *dev);
#endif
