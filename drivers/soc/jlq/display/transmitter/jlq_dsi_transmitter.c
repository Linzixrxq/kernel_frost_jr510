// SPDX-License-Identifier: GPL-2.0
/*
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
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>

#include <drm/drm_of.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/jlq_display_cmdline.h>

#include <video/mipi_display.h>

#include "jlq_dphy.h"

#define IRQ_USED	0

#define DRIVER_NAME    "jlq-mipi-dsi"

/** Version supported by this driver */
#define DSI_VERSION_140  0x3134302A
#define DSI_VERSION_141  0x3134312A

#define DSI_VERSION			0x00
#define DSI_PWR_UP			0x04
#define RESET				0
#define POWERUP				BIT(0)

#define DSI_CLKMGR_CFG			0x08
#define TO_CLK_DIVISION(div)		(((div) & 0xff) << 8)
#define TX_ESC_CLK_DIVISION(div)	(((div) & 0xff) << 0)

#define DSI_DPI_VCID			0x0c
#define DPI_VID(vid)			(((vid) & 0x3) << 0)

#define DSI_DPI_COLOR_CODING		0x10
#define EN18_LOOSELY			BIT(8)
#define DPI_COLOR_CODING_16BIT_1	0x0
#define DPI_COLOR_CODING_16BIT_2	0x1
#define DPI_COLOR_CODING_16BIT_3	0x2
#define DPI_COLOR_CODING_18BIT_1	0x3
#define DPI_COLOR_CODING_18BIT_2	0x4
#define DPI_COLOR_CODING_24BIT		0x5

#define DSI_DPI_CFG_POL			0x14
#define COLORM_ACTIVE_LOW		BIT(4)
#define SHUTD_ACTIVE_LOW		BIT(3)
#define HSYNC_ACTIVE_LOW		BIT(2)
#define VSYNC_ACTIVE_LOW		BIT(1)
#define DATAEN_ACTIVE_LOW		BIT(0)

#define DSI_DPI_LP_CMD_TIM		0x18
#define OUTVACT_LPCMD_TIME(p)		(((p) & 0xff) << 16)
#define INVACT_LPCMD_TIME(p)		((p) & 0xff)

#define DSI_DBI_CFG			0x20
#define DSI_DBI_CMDSIZE			0x28

#define DSI_PCKHDL_CFG			0x2c
#define EN_CRC_RX			BIT(4)
#define EN_ECC_RX			BIT(3)
#define EN_BTA				BIT(2)
#define EN_EOTP_RX			BIT(1)
#define EN_EOTP_TX			BIT(0)

#define DSI_MODE_CFG			0x34
#define ENABLE_VIDEO_MODE		0
#define ENABLE_CMD_MODE			BIT(0)

#define DSI_VID_MODE_CFG		0x38
#define FRAME_BTA_ACK			BIT(14)
#define ENABLE_LOW_POWER		(0x3f << 8)
#define ENABLE_LOW_POWER_MASK		(0x3f << 8)
#define VID_MODE_TYPE_NON_BURST_SYNC_PULSES	0x0
#define VID_MODE_TYPE_NON_BURST_SYNC_EVENTS	0x1
#define VID_MODE_TYPE_BURST			0x2
#define VID_MODE_TYPE_MASK			0x3

#define DSI_VID_PKT_SIZE		0x3c
#define VID_PKT_SIZE(p)			(((p) & 0x3fff) << 0)
#define VID_PKT_MAX_SIZE		0x3fff

#define DSI_VID_HSA_TIME		0x48
#define DSI_VID_HBP_TIME		0x4c
#define DSI_VID_HLINE_TIME		0x50
#define DSI_VID_VSA_LINES		0x54
#define DSI_VID_VBP_LINES		0x58
#define DSI_VID_VFP_LINES		0x5c
#define DSI_VID_VACTIVE_LINES		0x60
#define DSI_CMD_MODE_CFG		0x68
#define MAX_RD_PKT_SIZE_LP		BIT(24)
#define DCS_LW_TX_LP			BIT(19)
#define DCS_SR_0P_TX_LP			BIT(18)
#define DCS_SW_1P_TX_LP			BIT(17)
#define DCS_SW_0P_TX_LP			BIT(16)
#define GEN_LW_TX_LP			BIT(14)
#define GEN_SR_2P_TX_LP			BIT(13)
#define GEN_SR_1P_TX_LP			BIT(12)
#define GEN_SR_0P_TX_LP			BIT(11)
#define GEN_SW_2P_TX_LP			BIT(10)
#define GEN_SW_1P_TX_LP			BIT(9)
#define GEN_SW_0P_TX_LP			BIT(8)
#define EN_ACK_RQST			BIT(1)
#define EN_TEAR_FX			BIT(0)

#define CMD_MODE_ALL_LP			(MAX_RD_PKT_SIZE_LP | \
					 DCS_LW_TX_LP | \
					 DCS_SR_0P_TX_LP | \
					 DCS_SW_1P_TX_LP | \
					 DCS_SW_0P_TX_LP | \
					 GEN_LW_TX_LP | \
					 GEN_SR_2P_TX_LP | \
					 GEN_SR_1P_TX_LP | \
					 GEN_SR_0P_TX_LP | \
					 GEN_SW_2P_TX_LP | \
					 GEN_SW_1P_TX_LP | \
					 GEN_SW_0P_TX_LP)

#define DSI_GEN_HDR			0x6c
#define GEN_HDATA(data)			(((data) & 0xffff) << 8)
#define GEN_HDATA_MASK			(0xffff << 8)
#define GEN_HTYPE(type)			(((type) & 0xff) << 0)
#define GEN_HTYPE_MASK			0xff

#define DSI_GEN_PLD_DATA		0x70

#define DSI_CMD_PKT_STATUS		0x74
#define GEN_CMD_EMPTY			BIT(0)
#define GEN_CMD_FULL			BIT(1)
#define GEN_PLD_W_EMPTY			BIT(2)
#define GEN_PLD_W_FULL			BIT(3)
#define GEN_PLD_R_EMPTY			BIT(4)
#define GEN_PLD_R_FULL			BIT(5)
#define GEN_RD_CMD_BUSY			BIT(6)

#define DSI_TO_CNT_CFG			0x78
#define HSTX_TO_CNT(p)			(((p) & 0xffff) << 16)
#define LPRX_TO_CNT(p)			((p) & 0xffff)

#define DSI_BTA_TO_CNT			0x8c
#define DSI_LPCLK_CTRL			0x94
#define AUTO_CLKLANE_CTRL		BIT(1)
#define PHY_TXREQUESTCLKHS		BIT(0)

#define DSI_PHY_TMR_LPCLK_CFG		0x98
#define PHY_CLKHS2LP_TIME(lbcc)		(((lbcc) & 0x3ff) << 16)
#define PHY_CLKLP2HS_TIME(lbcc)		((lbcc) & 0x3ff)

#define DSI_PHY_TMR_CFG			0x9c
#define PHY_HS2LP_TIME(lbcc)		(((lbcc) & 0x3ff) << 16)
#define PHY_LP2HS_TIME(lbcc)		((lbcc) & 0x3ff)
#define MAX_RD_TIME(lbcc)		((lbcc) & 0x7fff)

#define DSI_PHY_RSTZ			0xa0
#define PHY_DISFORCEPLL			0
#define PHY_ENFORCEPLL			BIT(3)
#define PHY_DISABLECLK			0
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			0
#define PHY_UNRSTZ			BIT(1)
#define PHY_SHUTDOWNZ			0
#define PHY_UNSHUTDOWNZ			BIT(0)

#define DSI_PHY_IF_CFG			0xa4
#define N_LANES(n)			((((n) - 1) & 0x3) << 0)
#define PHY_STOP_WAIT_TIME(cycle)	(((cycle) & 0xff) << 8)

#define DSI_INT_ST0			0xbc
#define DSI_INT_ST1			0xc0
#define DSI_INT_MSK0			0xc4
#define DSI_INT_MSK1			0xc8
#define DSI_PHY_TMR_RD_CFG		0xf4

#define CMD_PKT_STATUS_TIMEOUT_US	20000

#define CONFIG_VALID 0xd4
#define DOU_STATUS 0x18B0
#define BS_CONTROL 0x1ed0

enum dw_mipi_dsi_mode {
	DW_MIPI_DSI_CMD_MODE,
	DW_MIPI_DSI_VID_MODE,
};

static bool first_open_encoder;

static inline struct dw_mipi_dsi *host_to_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct dw_mipi_dsi, dsi_host);
}

static inline struct dw_mipi_dsi *con_to_dsi(struct drm_connector *con)
{
	return container_of(con, struct dw_mipi_dsi, connector);
}

static inline struct dw_mipi_dsi *encoder_to_dsi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct dw_mipi_dsi, encoder);
}

static inline u32 dsi_read(struct dw_mipi_dsi *dsi, u32 reg)
{
	return readl(dsi->base + reg);
}

static inline void dsi_write(struct dw_mipi_dsi *dsi, u32 reg, u32 val)
{
	writel(val, dsi->base + reg);
}

static int dw_mipi_dsi_get_lane_bps(struct dw_mipi_dsi *dsi,
				    struct drm_display_mode *mode)
{
	unsigned long mpclk;
	int bpp;
	unsigned int bit_rate;

	bit_rate = (dsi->mode_flags & 0xfff00000) >> 20;
	if (bit_rate) {
		dsi->lane_mbps = bit_rate;
		dev_err(dsi->dev, "panel specify the bit rate:%lu\n", bit_rate);
	} else {
		bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
		if (bpp < 0) {
			dev_err(dsi->dev, "failed to get bpp for pixel format %d\n",
				dsi->format);
			return bpp;
		}

		mpclk = DIV_ROUND_UP(mode->clock, MSEC_PER_SEC);

		dsi->lane_mbps = mpclk * bpp / dsi->lanes;
	}

	jlq_dphy_adjusted_pllout(dsi);

	return 0;
}
static int dw_mipi_dsi_host_attach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	if (device->lanes > 4 || device->lanes < 1) {
		dev_err(dsi->dev, "invalid data lanes number:%u\n",
			device->lanes);
		return -EINVAL;
	}

	dsi->lanes = device->lanes;
	dsi->channel = device->channel;
	dsi->format = device->format;
	dsi->mode_flags = device->mode_flags;
	dsi->panel = of_drm_find_panel(device->dev.of_node);
	if (dsi->panel)
		return drm_panel_attach(dsi->panel, &dsi->connector);

	return -EINVAL;
}

static int dw_mipi_dsi_host_detach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);

	drm_panel_detach(dsi->panel);

	return 0;
}

static void dw_mipi_message_config(struct dw_mipi_dsi *dsi,
				   const struct mipi_dsi_msg *msg)
{
	bool lpm = msg->flags & MIPI_DSI_MSG_USE_LPM;
	u32 val = 0;

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK)
		val |= EN_ACK_RQST;
	if (lpm)
		val |= CMD_MODE_ALL_LP;

	if (dsi->is_cmd_mode)
		dsi_write(dsi, DSI_LPCLK_CTRL, lpm ? 0 : PHY_TXREQUESTCLKHS);

	dsi_write(dsi, DSI_CMD_MODE_CFG, val);
}

static int dw_mipi_dsi_gen_pkt_hdr_write(struct dw_mipi_dsi *dsi, u32 hdr_val)
{
	int ret;
	u32 val, mask;

	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, !(val & GEN_CMD_FULL), 1000,
				 CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(dsi->dev, "failed to get available command FIFO\n");
		return ret;
	}

	dsi_write(dsi, DSI_GEN_HDR, hdr_val);

	mask = GEN_CMD_EMPTY | GEN_PLD_W_EMPTY;
	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, (val & mask) == mask,
				 1000, CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		dev_err(dsi->dev, "failed to write command FIFO\n");
		return ret;
	}

	return 0;
}

static int dw_mipi_dsi_write(struct dw_mipi_dsi *dsi,
			     const struct mipi_dsi_packet *packet)
{
	const u8 *tx_buf = packet->payload;
	int len = packet->payload_length, pld_data_bytes = sizeof(u32), ret;
	u32 remainder;
	u32 val;

	while (DIV_ROUND_UP(len, pld_data_bytes)) {
		if (len < pld_data_bytes) {
			remainder = 0;
			memcpy(&remainder, tx_buf, len);
			dsi_write(dsi, DSI_GEN_PLD_DATA, remainder);
			len = 0;
		} else {
			memcpy(&remainder, tx_buf, pld_data_bytes);
			dsi_write(dsi, DSI_GEN_PLD_DATA, remainder);
			tx_buf += pld_data_bytes;
			len -= pld_data_bytes;
		}

		ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
					 val, !(val & GEN_PLD_W_FULL), 1000,
					 CMD_PKT_STATUS_TIMEOUT_US);
		if (ret < 0) {
			dev_err(dsi->dev,
				"failed to get available write payload FIFO\n");
			return ret;
		}
	}

	remainder = 0;
	memcpy(&remainder, packet->header, sizeof(packet->header));
	return dw_mipi_dsi_gen_pkt_hdr_write(dsi, remainder);
}

static int dw_mipi_dsi_read(struct dw_mipi_dsi *dsi,
			    const struct mipi_dsi_msg *msg)
{
	int i, j, ret, len = msg->rx_len;
	u8 *buf = msg->rx_buf;
	u32 val;

	/* Wait end of the read operation */
	ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
				 val, !(val & GEN_RD_CMD_BUSY), 1000,
				 CMD_PKT_STATUS_TIMEOUT_US);
	if (ret) {
		dev_err(dsi->dev, "Timeout during read operation\n");
		return ret;
	}

	for (i = 0; i < len; i += 4) {
		/* Read fifo must not be empty before all bytes are read */
		ret = readl_poll_timeout(dsi->base + DSI_CMD_PKT_STATUS,
					 val, !(val & GEN_PLD_R_EMPTY), 1000,
					 CMD_PKT_STATUS_TIMEOUT_US);
		if (ret) {
			dev_err(dsi->dev, "Read payload FIFO is empty\n");
			return ret;
		}

		val = dsi_read(dsi, DSI_GEN_PLD_DATA);
		for (j = 0; j < 4 && j + i < len; j++)
			buf[i + j] = val >> (8 * j);
	}

	return ret;
}

static ssize_t dw_mipi_dsi_host_transfer(struct mipi_dsi_host *host,
					 const struct mipi_dsi_msg *msg)
{
	struct dw_mipi_dsi *dsi = host_to_dsi(host);
	struct mipi_dsi_packet packet;
	int ret, nb_bytes;

	mutex_lock(&dsi->mipi_lock);
	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		dev_err(dsi->dev, "failed to create packet: %d\n", ret);
		mutex_unlock(&dsi->mipi_lock);
		return ret;
	}

	dw_mipi_message_config(dsi, msg);

	ret = dw_mipi_dsi_write(dsi, &packet);
	if (ret) {
		mutex_unlock(&dsi->mipi_lock);
		return ret;
	}

	if (msg->rx_buf && msg->rx_len) {
		ret = dw_mipi_dsi_read(dsi, msg);
		if (ret) {
			mutex_unlock(&dsi->mipi_lock);
			return ret;
		}
		nb_bytes = msg->rx_len;
	} else {
		nb_bytes = packet.size;
	}

	mutex_unlock(&dsi->mipi_lock);

	return nb_bytes;
}

static const struct mipi_dsi_host_ops dw_mipi_dsi_host_ops = {
	.attach = dw_mipi_dsi_host_attach,
	.detach = dw_mipi_dsi_host_detach,
	.transfer = dw_mipi_dsi_host_transfer,
};

static void dw_mipi_dsi_video_mode_config(struct dw_mipi_dsi *dsi)
{
	u32 val;

	val = ENABLE_LOW_POWER;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		val |= VID_MODE_TYPE_BURST;
	else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		val |= VID_MODE_TYPE_NON_BURST_SYNC_PULSES;
	else
		val |= VID_MODE_TYPE_NON_BURST_SYNC_EVENTS;

	dsi_write(dsi, DSI_VID_MODE_CFG, val);
}

static void dw_mipi_dsi_set_mode(struct dw_mipi_dsi *dsi,
				 enum dw_mipi_dsi_mode mode)
{
	dsi_write(dsi, DSI_PWR_UP, RESET);

	if (mode == DW_MIPI_DSI_VID_MODE) {
		u32 val = AUTO_CLKLANE_CTRL | PHY_TXREQUESTCLKHS;

		dsi_write(dsi, DSI_MODE_CFG, ENABLE_VIDEO_MODE);
		dw_mipi_dsi_video_mode_config(dsi);
		dsi_write(dsi, DSI_LPCLK_CTRL, val);
		dsi->is_cmd_mode = false;
	} else {
		dsi_write(dsi, DSI_MODE_CFG, ENABLE_CMD_MODE);
		dsi->is_cmd_mode = true;
	}

	dsi_write(dsi, DSI_PWR_UP, POWERUP);
}

static void dw_mipi_dsi_disable(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_RSTZ);
	dphy_write_mask(dsi, DISPLAY_CRG_RST_CTL, 0x0, 0x40);
}

static void dw_mipi_dsi_init(struct dw_mipi_dsi *dsi)
{
	/*
	 * The maximum permitted escape clock is 20MHz and it is derived from
	 * lanebyteclk, which is running at "lane_mbps / 8".  Thus we want:
	 *
	 *     (lane_mbps >> 3) / esc_clk_division < 20
	 * which is:
	 *     esc_clk_division > (lane_mbps >> 3) / 20
	 */
	u32 esc_clk_division = 0x07;

	esc_clk_division = (dsi->lane_mbps >> 3) / 20 + 1;

	dsi->esc_clk_division = esc_clk_division;

	dphy_write_mask(dsi, DISPLAY_CRG_RST_CTL, 0x40, 0x40);
	dsi_write(dsi, DSI_PWR_UP, RESET);
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_DISFORCEPLL | PHY_DISABLECLK
		  | PHY_RSTZ | PHY_SHUTDOWNZ);
	dsi_write(dsi, DSI_CLKMGR_CFG, TO_CLK_DIVISION(101) |
		  TX_ESC_CLK_DIVISION(esc_clk_division));
}

static void dw_mipi_dsi_dpi_config(struct dw_mipi_dsi *dsi,
				   struct drm_display_mode *mode)
{
	u32 val = 6, color = 0;

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		color = DPI_COLOR_CODING_24BIT;
		break;
	case MIPI_DSI_FMT_RGB666:
		color = DPI_COLOR_CODING_18BIT_2 | EN18_LOOSELY;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		color = DPI_COLOR_CODING_18BIT_1;
		break;
	case MIPI_DSI_FMT_RGB565:
		color = DPI_COLOR_CODING_16BIT_1;
		break;
	}

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val &= ~VSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val &= ~HSYNC_ACTIVE_LOW;

	dsi_write(dsi, DSI_DPI_VCID, DPI_VID(dsi->channel));
	dsi_write(dsi, DSI_DPI_COLOR_CODING, color);
	dsi_write(dsi, DSI_DPI_CFG_POL, val);
	dsi_write(dsi, DSI_DPI_LP_CMD_TIM, OUTVACT_LPCMD_TIME(0xff)
		  | INVACT_LPCMD_TIME(0xff));
}

static void dw_mipi_dsi_packet_handler_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PCKHDL_CFG, 0x3f);
}

static void dw_mipi_dsi_video_packet_config(struct dw_mipi_dsi *dsi,
					    struct drm_display_mode *mode)
{
	dsi_write(dsi, DSI_VID_PKT_SIZE, VID_PKT_SIZE(mode->hdisplay));
}

static void dw_mipi_dsi_command_mode_config(struct dw_mipi_dsi *dsi)
{
	u32 hs_timeout, hstx_to_cnt, lprx_to_cnt;
	struct drm_encoder *encoder = &(dsi->encoder);
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;

	/* HS timeout */
	hs_timeout = mode->htotal * mode->vdisplay;
	hstx_to_cnt = hs_timeout / 100;
	lprx_to_cnt = hs_timeout / 100;

	dsi_write(dsi, DSI_TO_CNT_CFG, HSTX_TO_CNT(hstx_to_cnt) | LPRX_TO_CNT(lprx_to_cnt));
	dsi_write(dsi, DSI_BTA_TO_CNT, 0xd00);
	dsi_write(dsi, DSI_MODE_CFG, ENABLE_CMD_MODE);
}

/* Get lane byte clock cycles. */
static u32 dw_mipi_dsi_get_hcomponent_lbcc(struct dw_mipi_dsi *dsi,
					   struct drm_display_mode *mode,
					   u32 hcomponent)
{
	u32 frac, lbcc;

	lbcc = hcomponent * dsi->lane_mbps * MSEC_PER_SEC / 8;

	frac = lbcc % mode->clock;
	lbcc = lbcc / mode->clock;
	if (frac)
		lbcc++;

	return lbcc;
}

static void dw_mipi_dsi_line_timer_config(struct dw_mipi_dsi *dsi,
					  struct drm_display_mode *mode)
{
	u32 htotal, hsa, hbp, lbcc;

	htotal = mode->htotal;
	hsa = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, htotal);
	dsi_write(dsi, DSI_VID_HLINE_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, hsa);
	dsi_write(dsi, DSI_VID_HSA_TIME, lbcc);

	lbcc = dw_mipi_dsi_get_hcomponent_lbcc(dsi, mode, hbp);
	dsi_write(dsi, DSI_VID_HBP_TIME, lbcc);
}

static void dw_mipi_dsi_vertical_timing_config(struct dw_mipi_dsi *dsi,
					       struct drm_display_mode *mode)
{
	u32 vactive, vsa, vfp, vbp;

	vactive = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;

	dsi_write(dsi, DSI_VID_VACTIVE_LINES, vactive);
	dsi_write(dsi, DSI_VID_VSA_LINES, vsa);
	dsi_write(dsi, DSI_VID_VFP_LINES, vfp);
	dsi_write(dsi, DSI_VID_VBP_LINES, vbp);
}

static void dw_mipi_dsi_dphy_timing_config(struct dw_mipi_dsi *dsi)
{
	jlq_dphy_get_transition_time(dsi);

	dsi_write(dsi, DSI_PHY_TMR_CFG, PHY_HS2LP_TIME(dsi->hs2lp_time)
		  | PHY_LP2HS_TIME(dsi->lp2hs_time));

	dsi_write(dsi, DSI_PHY_TMR_LPCLK_CFG, PHY_CLKHS2LP_TIME(dsi->clkhs2lp_time)
		  | PHY_CLKLP2HS_TIME(dsi->clklp2hs_time));

	dsi_write(dsi, DSI_PHY_TMR_RD_CFG, 0xfff);
}

static void dw_mipi_dsi_dphy_interface_config(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PHY_IF_CFG, PHY_STOP_WAIT_TIME(0x2) |
		  N_LANES(dsi->lanes));
}

static void dw_mipi_dsi_dphy_enable(struct dw_mipi_dsi *dsi)
{
	dsi_write(dsi, DSI_PHY_RSTZ, PHY_ENABLECLK |
		  PHY_UNRSTZ | PHY_UNSHUTDOWNZ);
}

static void dw_mipi_dsi_clear_err(struct dw_mipi_dsi *dsi)
{
	dsi_read(dsi, DSI_INT_ST0);
	dsi_read(dsi, DSI_INT_ST1);
	dsi_write(dsi, DSI_INT_MSK0, 0);
	dsi_write(dsi, DSI_INT_MSK1, 0);
}

static int dw_mipi_dsi_clk_enable(struct dw_mipi_dsi *dsi)
{
	int ret = 0;

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		dev_err(dsi->dev, "%s: Failed to enable pclk\n", __func__);
		goto err;
	}

	ret = clk_prepare_enable(dsi->dpipclk);
	if (ret) {
		dev_err(dsi->dev, "%s: Failed to enable pclk\n", __func__);
		goto unprepare_pclk;
	}

	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret) {
		dev_err(dsi->dev, "%s: Failed to enable pclk\n", __func__);
		goto unprepare_dpipclk;
	}

	return 0;
unprepare_dpipclk:
	clk_disable_unprepare(dsi->dpipclk);
unprepare_pclk:
	clk_disable_unprepare(dsi->pclk);
err:
	return ret;
}

static void dw_mipi_dsi_clk_disable(struct dw_mipi_dsi *dsi)
{
	clk_disable_unprepare(dsi->pclk);
	clk_disable_unprepare(dsi->dpipclk);
	clk_disable_unprepare(dsi->pllref_clk);
}

static int dw_mipi_dsi_power_on(struct dw_mipi_dsi *dsi)
{
	int ret;
	struct device *dev = dsi->dev;

	ret = regulator_set_voltage(dsi->dphy_vdd08, 800000, 800000);
	if (ret) {
		dev_err(dev, "failed to set dphy_vdd08's voltage%d\n", ret);
		goto exit;
	}

	ret = regulator_enable(dsi->dphy_vdd08);
	if (ret < 0) {
		dev_err(dev, "failed to enable dphy_vdd08, %d\n", ret);
		goto set_voltage_dphy_vdd08;
	}

	ret = regulator_set_voltage(dsi->dphy_vdd18, 1800000, 1800000);
	if (ret) {
		dev_err(dev, "failed to set dphy_vdd18's voltage%d\n", ret);
		goto disable_dphy_vdd08;
	}

	ret = regulator_enable(dsi->dphy_vdd18);
	if (ret < 0) {
		dev_err(dev, "failed to enable dphy_vdd18, %d\n", ret);
		goto set_voltage_dphy_vdd18;
	}

	ret = regulator_set_voltage(dsi->dphy_vdd12, 1200000, 1200000);
	if (ret) {
		dev_err(dev, "failed to set dphy_vdd12's voltage%d\n", ret);
		goto disable_dphy_vdd18;
	}

	ret = regulator_enable(dsi->dphy_vdd12);
	if (ret < 0) {
		dev_err(dev, "failed to enable dphy_vdd12, %d\n", ret);
		goto set_voltage_dphy_vdd12;
	}

	return 0;
set_voltage_dphy_vdd12:
	regulator_set_voltage(dsi->dphy_vdd12, 0, 1200000);
disable_dphy_vdd18:
	regulator_disable(dsi->dphy_vdd18);
set_voltage_dphy_vdd18:
	regulator_set_voltage(dsi->dphy_vdd18, 0, 1800000);
disable_dphy_vdd08:
	regulator_disable(dsi->dphy_vdd08);
set_voltage_dphy_vdd08:
	regulator_set_voltage(dsi->dphy_vdd08, 0, 800000);
exit:
	return ret;
}

static void dw_mipi_dsi_power_off(struct dw_mipi_dsi *dsi)
{
	int ret;
	struct device *dev = dsi->dev;
/*
	ret = regulator_set_voltage(dsi->dphy_vdd12, 0, 1200000);
	if (ret)
		dev_err(dev, "failed to set dphy_vdd12's voltage%d\n", ret);
*/
	ret = regulator_disable(dsi->dphy_vdd12);
	if (ret < 0)
		dev_err(dev, "regulator disable dphy_vdd12 failed, %d\n", ret);
/*
	ret = regulator_set_voltage(dsi->dphy_vdd18, 0, 1800000);
	if (ret)
		dev_err(dev, "failed to set dphy_vdd18's voltage%d\n", ret);
*/
	ret = regulator_disable(dsi->dphy_vdd18);
	if (ret < 0)
		dev_err(dev, "regulator disable dphy_vdd18 failed, %d\n", ret);
/*
	ret = regulator_set_voltage(dsi->dphy_vdd08, 0, 800000);
	if (ret)
		dev_err(dev, "failed to set dphy_vdd08's voltage%d\n", ret);
*/
	ret = regulator_disable(dsi->dphy_vdd08);
	if (ret < 0)
		dev_err(dev, "regulator disable dphy_vdd08 failed, %d\n", ret);
}

static void jlq_display_dou_enable(struct dw_mipi_dsi *dsi)
{
	//u32 val;
	u32 status;

	//val = readl(dsi->dpu_base + BS_CONTROL);
	//dev_err(dsi->dev, "%s: BS_CONTROL=0x%x\n", __func__, val);

	//val = readl(dsi->dpu_base + DOU_STATUS);
	//dev_err(dsi->dev, "%s: DOU_STATUS=0x%x\n", __func__, val);

	writel(0x3, dsi->dpu_base + BS_CONTROL);
	writel(0x1, dsi->dpu_base + CONFIG_VALID);
	mdelay(20);
	status = readl(dsi->dpu_base + DOU_STATUS);
	if (status & BIT(31))
		dev_err(dsi->dev, "%s: dou is active, status=0x%x\n", __func__, status);
	else
		dev_err(dsi->dev, "%s: dou is idle, status=0x%x\n", __func__, status);
}

static void jlq_display_dou_disable(struct dw_mipi_dsi *dsi)
{
	//u32 val;
	u32 status;

	//val = readl(dsi->dpu_base + BS_CONTROL);
	//dev_err(dsi->dev, "%s: BS_CONTROL=0x%x\n", __func__, val);

	//val = readl(dsi->dpu_base + DOU_STATUS);
	//dev_err(dsi->dev, "%s: DOU_STATUS=0x%x\n", __func__, val);

	writel(0x2, dsi->dpu_base + BS_CONTROL);
	writel(0x1, dsi->dpu_base + CONFIG_VALID);
	mdelay(20);
	status = readl(dsi->dpu_base + DOU_STATUS);
	if (status & BIT(31))
		dev_err(dsi->dev, "%s: dou is active, status=0x%x\n", __func__, status);
	else
		dev_err(dsi->dev, "%s: dou is idle, status=0x%x\n", __func__, status);
}

static void jlq_dsi_transmitter_disable(struct drm_encoder *encoder)
{
       struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);

       if (dsi->dpms_mode != DRM_MODE_DPMS_ON)
               return;

       if (clk_prepare_enable(dsi->pclk)) {
               dev_err(dsi->dev, "%s: Failed to enable pclk\n", __func__);
               return;
       }

       mutex_lock(&dsi->mipi_lock);
       jlq_display_dou_disable(dsi);
       dw_mipi_dsi_set_mode(dsi, DW_MIPI_DSI_CMD_MODE);
       mutex_unlock(&dsi->mipi_lock);

       drm_panel_disable(dsi->panel);

       drm_panel_unprepare(dsi->panel);

       dw_mipi_dsi_disable(dsi);
       jlq_dphy_deinit(dsi);
       clk_disable_unprepare(dsi->pclk);
       dw_mipi_dsi_clk_disable(dsi);
       dw_mipi_dsi_power_off(dsi);
       dsi->dpms_mode = DRM_MODE_DPMS_OFF;
}


static void dw_mipi_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);
#if 0
	if (dsi->dpms_mode != DRM_MODE_DPMS_ON)
		return;

	if (clk_prepare_enable(dsi->pclk)) {
		dev_err(dsi->dev, "%s: Failed to enable pclk\n", __func__);
		return;
	}

	drm_panel_disable(dsi->panel);

	dw_mipi_dsi_set_mode(dsi, DW_MIPI_DSI_CMD_MODE);
	drm_panel_unprepare(dsi->panel);

	dw_mipi_dsi_disable(dsi);
	jlq_dphy_deinit(dsi);
	clk_disable_unprepare(dsi->pclk);
	dw_mipi_dsi_clk_disable(dsi);
	dw_mipi_dsi_power_off(dsi);
	dsi->dpms_mode = DRM_MODE_DPMS_OFF;
#endif
        mutex_lock(&dsi->encoder_lock);
        jlq_dsi_transmitter_disable(encoder);
        mutex_unlock(&dsi->encoder_lock);
}

static void jlq_dsi_dump_registers(struct dw_mipi_dsi *dsi)
{
#if DUMP_DSI_REGS
	int reg;

	dev_err(dsi->dev, "mipi host dump regs\n");
	for (reg = 0x0; reg < 0xfc;) {
		dev_err(dsi->dev, "reg[0x%04x]=0x%08x\n", reg,
		dsi_read(dsi, reg));
		reg += 4;
	}

	jlq_dphy_dump_registers(dsi);

#endif
}

static void jlq_dsi_transmitter_enable(struct drm_encoder *encoder)
{
       struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);
       struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
       int ret;

       if (dsi->dpms_mode == DRM_MODE_DPMS_ON){
               if (first_open_encoder) {
                       drm_panel_prepare(dsi->panel);
                       drm_panel_enable(dsi->panel);
                       first_open_encoder = false;
               }
               return;
       }
       ret = dw_mipi_dsi_get_lane_bps(dsi, mode);
       if (ret < 0)
               return;

       if (dw_mipi_dsi_power_on(dsi)) {
               dev_err(dsi->dev, "%s: Failed to poweron\n", __func__);
               return;
       }

       if (dw_mipi_dsi_clk_enable(dsi)) {
               dev_err(dsi->dev, "%s: Failed to enable clk\n", __func__);
               return;
       }

       dw_mipi_dsi_init(dsi);
       dw_mipi_dsi_dpi_config(dsi, mode);
       dw_mipi_dsi_video_mode_config(dsi);
       dw_mipi_dsi_packet_handler_config(dsi);
       dw_mipi_dsi_video_packet_config(dsi, mode);
       dw_mipi_dsi_command_mode_config(dsi);
       dw_mipi_dsi_line_timer_config(dsi, mode);
       dw_mipi_dsi_vertical_timing_config(dsi, mode);
       dw_mipi_dsi_dphy_timing_config(dsi);
       dw_mipi_dsi_dphy_interface_config(dsi);
       jlq_dphy_init(dsi);
       dw_mipi_dsi_dphy_enable(dsi);
       dw_mipi_dsi_clear_err(dsi);

       dw_mipi_dsi_set_mode(dsi, DW_MIPI_DSI_CMD_MODE);
       if (drm_panel_prepare(dsi->panel))
               dev_err(dsi->dev, "failed to prepare panel\n");

       dw_mipi_dsi_set_mode(dsi, DW_MIPI_DSI_VID_MODE);
       //dsi_write(dsi,0xb8,0x2);
       //dsi_write(dsi,0xb4,0x0);
       //dsi_write(dsi, 0xb0, 0x1fb8);
       //dsi_write(dsi, 0x68, 0xf7f00);
       //dsi_write(dsi, 0x94, 0x3);
       drm_panel_enable(dsi->panel);
       dsi->dpms_mode = DRM_MODE_DPMS_ON;
       jlq_dsi_dump_registers(dsi);
}

static void dw_mipi_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);
#if 0
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	int ret;

	if (dsi->dpms_mode == DRM_MODE_DPMS_ON)
		return;

	ret = dw_mipi_dsi_get_lane_bps(dsi, mode);
	if (ret < 0)
		return;

	if (dw_mipi_dsi_power_on(dsi)) {
		dev_err(dsi->dev, "%s: Failed to poweron\n", __func__);
		return;
	}

	if (dw_mipi_dsi_clk_enable(dsi)) {
		dev_err(dsi->dev, "%s: Failed to enable clk\n", __func__);
		return;
	}

	dw_mipi_dsi_init(dsi);
	dw_mipi_dsi_dpi_config(dsi, mode);
	dw_mipi_dsi_video_mode_config(dsi);
	dw_mipi_dsi_packet_handler_config(dsi);
	dw_mipi_dsi_video_packet_config(dsi, mode);
	dw_mipi_dsi_command_mode_config(dsi);
	dw_mipi_dsi_line_timer_config(dsi, mode);
	dw_mipi_dsi_vertical_timing_config(dsi, mode);
	dw_mipi_dsi_dphy_timing_config(dsi);
	dw_mipi_dsi_dphy_interface_config(dsi);
	jlq_dphy_init(dsi);
	dw_mipi_dsi_dphy_enable(dsi);
	dw_mipi_dsi_clear_err(dsi);

	dw_mipi_dsi_set_mode(dsi, DW_MIPI_DSI_CMD_MODE);
	if (drm_panel_prepare(dsi->panel))
		dev_err(dsi->dev, "failed to prepare panel\n");

	dw_mipi_dsi_set_mode(dsi, DW_MIPI_DSI_VID_MODE);
	//dsi_write(dsi,0xb8,0x2);
	//dsi_write(dsi,0xb4,0x0);
	//dsi_write(dsi, 0xb0, 0x1fb8);
	//dsi_write(dsi, 0x68, 0xf7f00);
	//dsi_write(dsi, 0x94, 0x3);
	drm_panel_enable(dsi->panel);
	dsi->dpms_mode = DRM_MODE_DPMS_ON;
	jlq_dsi_dump_registers(dsi);
#endif
       mutex_lock(&dsi->encoder_lock);
       jlq_dsi_transmitter_enable(encoder);
       mutex_unlock(&dsi->encoder_lock);
}

void notify_transmitter_panel_dead(struct drm_connector *connector)
{
       struct dw_mipi_dsi *dsi = con_to_dsi(connector);
       struct drm_encoder *encoder = &dsi->encoder;
       const struct drm_encoder_helper_funcs *funcs = encoder->helper_private;

       mutex_lock(&dsi->encoder_lock);
       if (dsi->dpms_mode == DRM_MODE_DPMS_OFF) {
               dev_err(dsi->dev,"%s No esd recovery when dsi is disabled\n",__func__);
               mutex_unlock(&dsi->encoder_lock);
               return;
       }

       jlq_dsi_transmitter_disable(encoder);
       jlq_dsi_transmitter_enable(encoder);
       jlq_display_dou_enable(dsi);
       mutex_unlock(&dsi->encoder_lock);
}
EXPORT_SYMBOL(notify_transmitter_panel_dead);


static int
dw_mipi_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct dw_mipi_dsi *dsi = encoder_to_dsi(encoder);

	switch (dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		break;
	case MIPI_DSI_FMT_RGB666:
		break;
	case MIPI_DSI_FMT_RGB565:
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_encoder_helper_funcs
dw_mipi_dsi_encoder_helper_funcs = {
	.enable = dw_mipi_dsi_encoder_enable,
	.disable = dw_mipi_dsi_encoder_disable,
	.atomic_check = dw_mipi_dsi_encoder_atomic_check,
};

static const struct drm_encoder_funcs dw_mipi_dsi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int dw_mipi_dsi_connector_get_modes(struct drm_connector *connector)
{
	struct dw_mipi_dsi *dsi = con_to_dsi(connector);

	return drm_panel_get_modes(dsi->panel);
}

static struct drm_connector_helper_funcs dw_mipi_dsi_connector_helper_funcs = {
	.get_modes = dw_mipi_dsi_connector_get_modes,
};

static void dw_mipi_dsi_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs dw_mipi_dsi_atomic_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dw_mipi_dsi_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int dw_mipi_dsi_register(struct drm_device *drm,
				struct dw_mipi_dsi *dsi)
{
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_connector *connector = &dsi->connector;
	struct device *dev = dsi->dev;
	int ret;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm,
							     dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(&dsi->encoder,
			       &dw_mipi_dsi_encoder_helper_funcs);
	ret = drm_encoder_init(drm, &dsi->encoder, &dw_mipi_dsi_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
	if (ret) {
		dev_err(dev, "Failed to initialize encoder with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &dw_mipi_dsi_connector_helper_funcs);

	drm_connector_init(drm, &dsi->connector,
			   &dw_mipi_dsi_atomic_connector_funcs,
			   DRM_MODE_CONNECTOR_DSI);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

irqreturn_t dwc_mipi_dsi_handler(int irq, void *data)
{
	struct dw_mipi_dsi *dsi = NULL;
	uint32_t status_0;
	uint32_t status_1;

	if (!data)
		return IRQ_NONE;

	dsi = data;

	status_0 = dsi_read(dsi, DSI_INT_ST0) & 0xffffffff;
	status_1 = dsi_read(dsi, DSI_INT_ST1) & 0xffffffff;

	dev_info_ratelimited(dsi->dev, "IRQ 0 %X IRQ 1 %X\n", status_0,
			     status_1);

	return IRQ_HANDLED;
}

static int dw_mipi_parse_dt(struct dw_mipi_dsi *dsi)
{
	struct resource *res;
	const char *inited_str;
	struct device_node *dpu_np;
	struct device_node *dphy_np;
	int ret = 0;
	struct device *dev = dsi->dev;
	struct platform_device *pdev = to_platform_device(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	dsi->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dsi->base)) {
		dev_err(dsi->dev, "%s:failed to get dsi address\n", __func__);
		return PTR_ERR(dsi->base);
	}

	dpu_np = of_find_compatible_node(NULL, NULL, "arm,mali-d32");
	if (!dpu_np) {
		DRM_ERROR("No arm,mali-d32 registers defined.\n");
		return -ENXIO;
	}

	dsi->dpu_base = of_iomap(dpu_np, 0);
	if (IS_ERR(dsi->dpu_base)) {
		dev_err(dsi->dev, "%s:failed to get dpu address\n", __func__);
		return PTR_ERR(dsi->dpu_base);
	} else
		dev_err(dsi->dev, "%s:succeed to get dpu address\n", __func__);

	dphy_np = of_find_compatible_node(NULL, NULL, "jlq,display_sysctrl");
	if (!dphy_np) {
		DRM_ERROR("No display_sysctrl registers defined.\n");
		return -ENXIO;
	}

	dsi->dphy_base = of_iomap(dphy_np, 0);
	if (IS_ERR(dsi->dphy_base)) {
		dev_err(dsi->dev, "%s:failed to get dphy address\n", __func__);
		return PTR_ERR(dsi->dphy_base);
	}

	dsi->inited_in_bootloader = false;
	if (!of_property_read_string(dev->of_node, "inited", &inited_str)) {
		if (!strcmp(inited_str, "true"))
			dsi->inited_in_bootloader = true;
	}

	if (dsi->inited_in_bootloader)
		dsi->dpms_mode = DRM_MODE_DPMS_ON;
	else
		dsi->dpms_mode = DRM_MODE_DPMS_OFF;

#if IRQ_USED
	dsi->irq = platform_get_irq(pdev, 0);
	if (dsi->irq <= 0) {
		dev_err(dsi->dev, "%s:IRQ number %d invalid.\n", __func__, 0);
		return -ENXIO;
	}

	ret = request_irq(dsi->irq, dwc_mipi_dsi_handler, IRQF_SHARED,
			  "dwc_mipi_dsi_handler", dev);
	if (ret) {
		dev_err(dsi->dev, "%s:can't register interrupt\n", __func__);
		return -ENXIO;
	}
#endif
	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk)) {
		ret = PTR_ERR(dsi->pclk);
		dev_err(dev, "Unable to get pclk: %d\n", ret);
		return ret;
	}

	dsi->dpipclk = devm_clk_get(dev, "dpipclk");
	if (IS_ERR(dsi->dpipclk)) {
		ret = PTR_ERR(dsi->dpipclk);
		dev_err(dev, "Unable to get dpipclk: %d\n", ret);
		return ret;
	}

	dsi->pllref_clk = devm_clk_get(dev, "pllref_clk");
	if (IS_ERR(dsi->pllref_clk)) {
		ret = PTR_ERR(dsi->pllref_clk);
		dev_err(dev, "Unable to get pll reference clock: %d\n", ret);
		return ret;
	}

	dsi->dphy_vdd08 = devm_regulator_get(dev, "vdd08");
	if (IS_ERR(dsi->dphy_vdd08)) {
		ret = PTR_ERR(dsi->dphy_vdd08);
		dev_err(dev, "failed to get regulator vdd08\n");
		return ret;
	}

	dsi->dphy_vdd12 = devm_regulator_get(dev, "vdd12");
	if (IS_ERR(dsi->dphy_vdd12)) {
		ret = PTR_ERR(dsi->dphy_vdd12);
		dev_err(dev, "failed to get regulator vdd12\n");
		return ret;
	}

	dsi->dphy_vdd18 = devm_regulator_get(dev, "vdd18");
	if (IS_ERR(dsi->dphy_vdd18)) {
		ret = PTR_ERR(dsi->dphy_vdd18);
		dev_err(dev, "failed to get regulator vdd18\n");
		return ret;
	}

	ret = dw_mipi_dsi_power_on(dsi);
	if (ret < 0) {
		dev_err(dsi->dev, "%s,failed to poweron\n", __func__);
		return ret;
	}

	ret = dw_mipi_dsi_clk_enable(dsi);
	if (ret) {
		dev_err(dev, "%s,failed to enable clk\n", __func__);
		return ret;
	}

	dsi->hw_version = dsi_read(dsi, DSI_VERSION);

	switch (dsi->hw_version) {
	case DSI_VERSION_140:
		pr_info("%s:HW Version 1.40a\n", __func__);
		break;
	case DSI_VERSION_141:
		dev_info(dev, "%s:HW Version 1.41a\n", __func__);
		break;
	default:
		dev_err(dev, "%s:invalid version:0x%x!\n", __func__, dsi->hw_version);
		ret = -ENODEV;
		break;
	}

        first_open_encoder = true;

	return ret;
}

static const struct of_device_id dw_mipi_dsi_dt_ids[] = {
	{
	 .compatible = "jlq,dw-mipi-dsi",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_dt_ids);

static int dw_mipi_dsi_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct drm_device *drm = data;
	struct dw_mipi_dsi *dsi;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;

	ret = dw_mipi_parse_dt(dsi);
	if (ret)
		return ret;

	ret = dw_mipi_dsi_register(drm, dsi);
	if (ret) {
		dev_err(dev, "Failed to register mipi_dsi: %d\n", ret);
		goto err_clk;
	}

	dsi->dsi_host.ops = &dw_mipi_dsi_host_ops;
	dsi->dsi_host.dev = dev;
	ret = mipi_dsi_host_register(&dsi->dsi_host);
	if (ret) {
		dev_err(dev, "Failed to register MIPI host: %d\n", ret);
		goto err_cleanup;
	}

	if (!dsi->panel) {
		dev_err(dev, "dsi panel is NULL\n");
		ret = -EPROBE_DEFER;
		goto err_mipi_dsi_host;
	}

	mutex_init(&dsi->mipi_lock);
	dev_set_drvdata(dev, dsi);
	return 0;

err_mipi_dsi_host:
	mipi_dsi_host_unregister(&dsi->dsi_host);
err_cleanup:
	drm_encoder_cleanup(&dsi->encoder);
	drm_connector_cleanup(&dsi->connector);
err_clk:
	dw_mipi_dsi_clk_disable(dsi);
	dw_mipi_dsi_power_off(dsi);
	return ret;
}

static void dw_mipi_dsi_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

	mipi_dsi_host_unregister(&dsi->dsi_host);
	clk_disable_unprepare(dsi->pllref_clk);
}

static const struct component_ops dw_mipi_dsi_ops = {
	.bind	= dw_mipi_dsi_bind,
	.unbind	= dw_mipi_dsi_unbind,
};

static int dw_mipi_dsi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dw_mipi_dsi_ops);
}

static int dw_mipi_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_mipi_dsi_ops);
	return 0;
}

static void dw_mipi_dsi_shutdown(struct platform_device *pdev)
{
       struct device *dev = &pdev->dev;
       struct dw_mipi_dsi *dsi = dev_get_drvdata(dev);

       dev_err(dev,"%s\n",__func__);

       dw_mipi_dsi_encoder_disable(&dsi->encoder);
}

struct platform_driver dw_mipi_dsi_driver = {
	.probe		= dw_mipi_dsi_probe,
	.remove		= dw_mipi_dsi_remove,
	.shutdown = dw_mipi_dsi_shutdown,
	.driver		= {
		.of_match_table = dw_mipi_dsi_dt_ids,
		.name	= DRIVER_NAME,
	},
};
module_platform_driver(dw_mipi_dsi_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("dw dsi driver");
