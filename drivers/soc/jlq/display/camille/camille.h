/* SPDX-License-Identifier: GPL-2.0
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
#ifndef _CAMILLE_H_
#define _CAMILLE_H_

#include <linux/device.h>
#include <linux/clk.h>

#include "camille_regs.h"
#include "camille_agtm_k_lut.h"
#include "camille_cltm_adj_curve.h"
#include "../include/ad_coprocessor_defs.h"

#define AGLT_K_LUT_SIZE   (256)
#define HACTIVE_MIN   (240)
#define VACTIVE_MIN   (240)

struct all_tm_ctrl {
	u8 tm_enable;
	u8 cltm_enable;
	u8 agtm_enable;
	u8 agtm_input_mux;
	u8 cltm_input_mux;
	u8 tm_output_mux;
	u8 reserved[2];
};

struct cltm_ctrl {
	u16 hist_mode;
	u16 curve_transition;
};

struct cltm_image {
	u16 height;
	u16 width;
};

struct cltm_block {
	u16 height;
	u16 width;
	u16 h_num;
	u16 w_num;
};

struct cltm_linear_adj {
	s16 offset;
	s16 ratio;
};

struct cltm_strength {
	u8 interval;
	u8 step;
	u8 target;
	u8 mode;
};

struct pwm_ctrl {
	u32 freq; //in HZ
	u32 div_ratio;
	u32 duty_ratio;
	u32 dly_effect_time;
};

struct camille_dev {
	char name[32];
	struct device *dev;
	struct miscdevice miscdev;
	struct clk *aclk;
	void __iomem *regs_base;
	struct dentry *sw_dir;
	struct ad_coprocessor_funcs *coprocessor_funcs;
	struct all_tm_ctrl  all_tm_ctrl;
	struct cltm_ctrl cltm_ctrl;
	struct cltm_image cltm_image;
	struct cltm_block cltm_block;
	struct cltm_linear_adj cltm_linear_adj;
	struct cltm_strength cltm_strength;
	struct pwm_ctrl pwm_ctrl;
	u16 cltm_adj_curve[65];
	u32 agtm_curve[256];
	u32 is_enabled : 1;
	int irq;
	spinlock_t shadow_lock;
	bool shadow_update_completed;
#ifndef CONFIG_JLQ_BACKLIGHT
	struct led_classdev backlight_led;
#endif
};

#endif /* _CAMILLE_H_ */
