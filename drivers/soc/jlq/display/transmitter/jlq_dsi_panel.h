/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.	4
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

#ifndef __JLQ_DSI_PANEL_H_
#define __JLQ_DSI_PANEL_H_
#include <linux/fs.h>

#include <linux/pwm.h>
#include <linux/leds.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <drm/drm_mipi_dsi.h>
#include <video/display_timing.h>
#include <linux/regulator/consumer.h>

#define RST_SEQ_LEN             16

struct cmd_ctrl_hdr {
	u8 dtype;	/* data type */
	u8 wait;	/* ms */
	u8 dlen;	/* payload len */
} __packed;

struct cmd_desc {
	struct cmd_ctrl_hdr dchdr;
	u8 *payload;
};

struct panel_cmds {
	u8 *buf;
	int blen;
	struct cmd_desc *cmds;
	int cmd_cnt;
};

struct panel_read_entry {
	u8 rbuf[64];
	u8 vbuf[64];
	u32 return_packet_size;
	struct panel_cmds read_cmds;
};

struct panel_read_info {
	u32 count;
	bool readed;
	struct panel_read_entry *read_entries;
};

struct panel_whitepoint_info {
	u32 cie_x;
	u32 cie_y;
	struct panel_read_info read_info;
};

enum esd_mode {
       ESD_MODE_NONE,
       ESD_MODE_REG_READ,
       ESD_MODE_ERROR_IRQ,
};

struct panel_esd_info {
       u32 status_interval;
       enum esd_mode mode;
       struct panel_read_info read_info;

       int esd_err_irq;
       int esd_err_irq_gpio;
       bool esd_err_irq_enabled;

       struct delayed_work status_work;
};


struct panel_read_config {
	bool enabled;
	u32 cmds_rlen;
	u8 rbuf[64];
};

/**
 * struct panel_power - power information of panel
 * @vreg:            Handle to the regulator.
 * @vreg_name:       Regulator name.
 * @min_voltage:     Minimum voltage in uV.
 * @max_voltage:     Maximum voltage in uV.
 * @voltage:         Voltage in uV.
 * @pre_on_sleep:    Sleep, in ms, before enabling the regulator.
 * @post_on_sleep:   Sleep, in ms, after enabling the regulator.
 * @pre_off_sleep:   Sleep, in ms, before disabling the regulator.
 * @post_off_sleep:  Sleep, in ms, after disabling the regulator.
 */
struct panel_power {
	struct regulator *vreg;
	char vreg_name[32];
	u32 min_voltage;
	u32 max_voltage;
	u32 pre_on_sleep;
	u32 post_on_sleep;
	u32 pre_off_sleep;
	u32 post_off_sleep;
};

struct panel_power_info {
	struct panel_power *powers;
	u32 count;
};

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct display_timing *timings;
	unsigned int num_timings;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	/**
	 * @reset: the time (in milliseconds) indicates the delay time
	 *         after the panel to operate reset gpio
	 * @init: the time (in milliseconds) that it takes for the panel to
	 *           power on and dsi host can send command to panel
	 * @prepare: the time (in milliseconds) that it takes for the panel to
	 *           become ready and start receiving video data
	 * @enable: the time (in milliseconds) that it takes for the panel to
	 *          display the first valid frame after starting to receive
	 *          video data
	 * @disable: the time (in milliseconds) that it takes for the panel to
	 *           turn the display off (no content is visible)
	 * @unprepare: the time (in milliseconds) that it takes for the panel
	 *             to power itself down completely
	 */
	struct {
		unsigned int bias;
		unsigned int reset;
		unsigned int init;
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;

	u32 bus_format;
};

struct panel_desc_dsi {
	struct panel_desc desc;

	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
};

struct panel_pinctrl_info {
    struct pinctrl *pinctrl;
    struct pinctrl_state *active;
    struct pinctrl_state *suspend;
};

struct panel_hbm_info {
	u32 percent;
	bool support;
	struct panel_cmds *on_cmds;
	struct panel_cmds *off_cmds;
};

struct jlq_panel {
	const char *name;
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct panel_power_info power_info;

	u32 ic_vendor;
	u32 cabc_mode;
	u32 bl_type;
	u32 bl_order;
	u32 bl_cur_level;
	u32 bl_max_level;
	u32 bl_min_level;

	u32 rst_seq_len;
	u32 rst_seq[RST_SEQ_LEN];
	bool rst_keep_high;
	int reset_gpio;
	int tp_reset_gpio;
	int bias_enp_gpio;
	int bias_enn_gpio;
	int power_enable_gpio;
	int bl_enable_gpio;

	bool prepared;
	bool enabled;
	bool cabc_support;
	bool inited_in_bootloader;
	struct panel_desc *desc;
	struct mutex panel_lock;
	struct panel_cmds *on_cmds;
	struct panel_cmds *off_cmds;
	struct panel_cmds *mipi_pre_cmds;
	struct panel_cmds *bl_off_pre_cmds;
	struct panel_hbm_info hbm_info;
	struct panel_read_info lockdown_info;
	struct panel_whitepoint_info whitepoint_info;
	struct panel_pinctrl_info pinctrl;
	struct pwm_device *bl_pwm;
	struct led_classdev bl_led;
        struct dentry *root; /* DEBUG FS */
        struct panel_esd_info esd_info;
        atomic_t esd_recovery_pending;
};

#endif /* __JLQ_DSI_PANEL_H_ */
