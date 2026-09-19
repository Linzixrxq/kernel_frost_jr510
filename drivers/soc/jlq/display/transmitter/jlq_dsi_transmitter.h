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

#ifndef __JLQ_DSI_TRANSMITTER_H_
#define __JLQ_DSI_TRANSMITTER_H_

#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>

#define DUMP_DSI_REGS	0

struct dw_mipi_dsi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct mipi_dsi_host dsi_host;
	struct drm_panel *panel;
	struct device *dev;
	void __iomem *base;
	void __iomem *dpu_base;
	void __iomem *dphy_base;
	int irq;
	struct clk *pllref_clk;
	struct clk *pclk;
	struct clk *dpipclk;

	bool is_cmd_mode;
	bool inited_in_bootloader;
	int dpms_mode;
	unsigned int lane_mbps; /* Mbit per second per lane */
	u32 esc_clk_division;
	u32 hw_version;
	u32 channel;
	u32 lanes;
	u32 format;
	u32 lp2hs_time;
	u32 hs2lp_time;
	u32 clklp2hs_time;
	u32 clkhs2lp_time;
	unsigned long mode_flags;
	struct mutex mipi_lock;
        struct mutex encoder_lock;
	struct regulator *dphy_vdd08;
	struct regulator *dphy_vdd12;
	struct regulator *dphy_vdd18;
};
#endif
