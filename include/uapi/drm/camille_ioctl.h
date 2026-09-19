/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */
#ifndef _UAPI_JLQ_CAMILLE_H
#define _UAPI_JLQ_CAMILLE_H

#include <linux/ioctl.h>
#include <linux/types.h>
/**
 * struct camille_init_data - metadata passed from userspace for camille
 * @lut:	the value of camille init data
 * Provided by userspace as an argument to the ioctl
 */
struct camille_mode {
	__u32 tm_mode;
	__u32 blk_width;
	__u32 blk_height;
	__u32 blk_h_num;
	__u32 blk_w_num;
	__u32 hist_mode;
	__u32 curve_transition;
	__s32 linear_adj_offset;
	__s32 linear_adj_ratio;
	__u32 strength_mode;
	__u32 strength_target;
	__u32 strength_step;
	__u32 strength_interval;
	__u32 hist_adj_curve[65];
	__u32 agtm_curve[256];
};

/**
 * struct camille_lut_data - metadata passed from userspace for camille
 * @lut:	the lut value of camille k ram
 * Provided by userspace as an argument to the ioctl
 */
struct camille_lut_data {
	__u32 lut[256];
};

#define CAMILLE_IOC_MAGIC	'C'

/**
 * DOC: CAMILLE_IOC_INIT - INIT DATA
 *
 * Takes a camille_init_data struct and returns the count written
 */
#define CAMILLE_IOC_MODE		_IOW(CAMILLE_IOC_MAGIC, 0, \
				      struct camille_mode)

/**
 * DOC: CAMILLE_IOC_LUT - write lut
 *
 * Takes a camille_lut_data struct and returns the count written
 */
#define CAMILLE_IOC_LUT		_IOW(CAMILLE_IOC_MAGIC, 1, \
				      struct camille_lut_data)

/**
 * DOC: CAMILLE_IOC_ENABLE - enable camille
 *
 * Takes a unsigned int and returns a none zero
 */
#define CAMILLE_IOC_ENABLE		_IOW(CAMILLE_IOC_MAGIC, 2, \
				      unsigned int)

/**
 * DOC: CAMILLE_IOC_DISABLE - disable camille
 *
 * Takes a unsigned int and returns a none zero
 */
#define CAMILLE_IOC_DISABLE		_IOW(CAMILLE_IOC_MAGIC, 3, \
				      unsigned int)

#endif /* _UAPI_JLQ_CAMILLE_H */
