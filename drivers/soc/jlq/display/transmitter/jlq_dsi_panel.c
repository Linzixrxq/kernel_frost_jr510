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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/log2.h>

#include <drm/drmP.h>
#include <drm/jlq_display_cmdline.h>
#include <video/mipi_display.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "jlq_dsi_panel.h"

#define HZ_TO_NS(x) (1000000000UL / (x))
#define PWM_DUTY_LEVEL_ORDER    (10)
#define attr_mode (S_IWUSR | S_IRUGO)
#define ESD_MODE_STRING_MAX_LEN 256
#define STATUS_CHECK_INTERVAL_MS 5000

extern void notify_transmitter_panel_dead(struct drm_connector *connector);


enum jlq_backlight_type {
    BACKLIGHT_TYPE_PWM,
    BACKLIGHT_TYPE_MIPI,
};

enum cabc_mode {
	CABC_OFF,
	CABC_UI_MODE,
	CABC_STILL_MODE,
	CABC_MOVING_MODE,
};

enum panle_ic_vendor {
    PANEL_IC_NONE,
    PANEL_IC_NOVATEK,
    PANEL_IC_FOCALTECH,
};

static struct panel_read_config g_panel_read_cfg;

#if IS_ENABLED(CONFIG_JGKI)  
static struct jlq_panel *g_panel;

int jlq_panel_notifier_register(struct notifier_block *nb) {
        pr_err("GC:jlq_panel_notifier_register %s", __func__);
	return drm_panel_notifier_register(&g_panel->base, nb);
}
EXPORT_SYMBOL_GPL(jlq_panel_notifier_register);

int jlq_panel_notifier_unregister(struct notifier_block *nb) {
        pr_err("GC:jlq_panel_notifier_unregister %s", __func__);
	return drm_panel_notifier_unregister(&g_panel->base, nb);
}
EXPORT_SYMBOL_GPL(jlq_panel_notifier_unregister);

int jlq_panel_status(void)
{
  if(g_panel->base.dev)
  {return 1;}
  return 0;
}
EXPORT_SYMBOL_GPL(jlq_panel_status);
#endif

static bool tp_gesture_enable = false;
void notify_lcd_gesture_state(bool en)
{
		tp_gesture_enable = en;
}
EXPORT_SYMBOL(notify_lcd_gesture_state);

static int jlq_panel_dsi_cmds_write(struct jlq_panel *panel,
				      struct panel_cmds *cmds);

static void jlq_panel_esd_enable(struct jlq_panel *panel);
static void jlq_panel_esd_disable(struct jlq_panel *panel);

static inline struct jlq_panel *to_jlq_panel(struct drm_panel *panel)
{
	return container_of(panel, struct jlq_panel, base);
}

static u16  novatek_map_bl(struct jlq_panel *panel, u16 value)
{
	u16 map;

	map = (value >> 8) & 0xf;
	map |= ((value & 0xff) << 8);

	return map;
}

static u16  focaltech_map_bl(struct jlq_panel *panel, u16 value)
{
	u16 map;
	u32 remain = panel->bl_order - 8;

	map = (value >> remain) & 0xff;
	map |= ((value & GENMASK(remain - 1, 0)) << 8);

	return map;
}

static void jlq_pwm_set_brightnsee(struct jlq_panel *panel, u32 bl_level)
{
	unsigned int period_ns;
	unsigned int duty_ns;
	unsigned int duty_ns_min;
	unsigned int duty_ns_max;
	struct pwm_device *pwm = panel->bl_pwm;
	unsigned int period = pwm->args.period;

	if (bl_level) {
		duty_ns_min =  (1 * HZ_TO_NS(period)) >> PWM_DUTY_LEVEL_ORDER;
		period_ns = HZ_TO_NS(period);
		duty_ns_max = period_ns;

		duty_ns = (bl_level * period_ns) / panel->bl_max_level;
		if (duty_ns < duty_ns_min) {
			duty_ns = duty_ns_min;
			dev_err_ratelimited(pwm->chip->dev,
				"decrease pwm clk, brightness: %d\n", bl_level);
		} else if (duty_ns > duty_ns_max) {
			duty_ns = duty_ns_max;
			dev_err_ratelimited(pwm->chip->dev, "increase pwm clk\n");
		}
		pwm_disable(pwm);
		pwm_config(pwm, duty_ns, period_ns);
		pwm_enable(pwm);
	} else {
		period_ns = HZ_TO_NS(100);
		duty_ns = 0;
		pwm_disable(pwm);
		pwm_config(pwm, duty_ns, period_ns);
		udelay(100);
		pwm_enable(pwm);
		dev_info_ratelimited(pwm->chip->dev, "set bl_level 0\n");
	}
	panel->bl_cur_level = bl_level;
}

void jlq_mipi_set_brightness(struct jlq_panel *panel,
				enum led_brightness value)
{
	int ret;
	u16 bl_map_value;
	struct mipi_dsi_device *dsi = panel->dsi;

	bl_map_value = value & 0xff;

	if (panel->bl_max_level > 255) {
		if (panel->ic_vendor == PANEL_IC_NOVATEK)
			bl_map_value = novatek_map_bl(panel, value);
		else if (panel->ic_vendor == PANEL_IC_FOCALTECH)
			bl_map_value = focaltech_map_bl(panel, value);
	}

	mutex_lock(&panel->panel_lock);
	if (panel->mipi_pre_cmds) {
		ret = jlq_panel_dsi_cmds_write(panel, panel->mipi_pre_cmds);
		if (ret) {
			dev_err(&dsi->dev, "failed to send mipi_pre_cmds\n");
			mutex_unlock(&panel->panel_lock);
			return;
		}
	}

	if (!value && panel->bl_off_pre_cmds) {
		ret = jlq_panel_dsi_cmds_write(panel, panel->bl_off_pre_cmds);
		if (ret) {
			dev_err(&dsi->dev, "failed to send bl_off_pre_cmds\n");
			mutex_unlock(&panel->panel_lock);
			return;
		}
	}

	mipi_dsi_dcs_set_display_brightness(dsi, bl_map_value);

	panel->bl_cur_level = value;  

	mutex_unlock(&panel->panel_lock);
}

#if IS_ENABLED(CONFIG_JGKI)  
void jlq_thermal_brightness_set(int value)
{
  pr_err("GC:brightness_set %s", __func__);
  jlq_mipi_set_brightness(g_panel, value);
  g_panel->bl_led.brightness = value;
}
EXPORT_SYMBOL_GPL(jlq_thermal_brightness_set);
#endif


void jlq_panel_set_brightness(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct jlq_panel *panel = dev_get_drvdata(led_cdev->dev->parent);
	struct device *dev = &panel->dsi->dev;

	if (!panel->enabled) {
		dev_dbg(dev, "No setting brightness when panel is disabled\n");
		return;
	}

	if (panel->hbm_info.support)
		value = value * panel->hbm_info.percent / 100;

	if (value == panel->bl_cur_level) {
		dev_dbg(dev, "brightness has been %d\n", value);
		return;
	}

	if (value > panel->bl_max_level)
		value = panel->bl_max_level;

	if (value < panel->bl_min_level)
		value = panel->bl_min_level;

	if (panel->bl_type == BACKLIGHT_TYPE_PWM)
		jlq_pwm_set_brightnsee(panel, value);
	else
		jlq_mipi_set_brightness(panel, value);

	if (value && !panel->bl_cur_level)
		dev_err(dev, "brightness changed from 0 to %d\n", value);

	if (!value && panel->bl_cur_level)
		dev_err(dev, "brightness changed from %d to 0\n", panel->bl_cur_level);

	dev_err(dev, "display: set brightness value = %d\n", value);

	//panel->bl_cur_level = value;
}

static void jlq_panel_free_panel_cmds(struct jlq_panel *panel, struct panel_cmds *cmds)
{
	struct device *dev = &panel->dsi->dev;

	if (!cmds)
		return;

	if (cmds->cmds)
		kfree(cmds->cmds);

	if (cmds->buf)
		kfree(cmds->buf);

	devm_kfree(dev, cmds);
}

static void jlq_panel_cmds_cleanup(struct jlq_panel *panel)
{
	jlq_panel_free_panel_cmds(panel, panel->on_cmds);

	jlq_panel_free_panel_cmds(panel, panel->off_cmds);

	jlq_panel_free_panel_cmds(panel, panel->mipi_pre_cmds);

	jlq_panel_free_panel_cmds(panel, panel->bl_off_pre_cmds);

	jlq_panel_free_panel_cmds(panel, panel->hbm_info.on_cmds);

	jlq_panel_free_panel_cmds(panel, panel->hbm_info.off_cmds);
}

static int jlq_panel_get_cmd_pkt_count(const char *data, int len, int *count)
{
	const char *bp = data;
	struct cmd_ctrl_hdr *dchdr;
	int cnt = 0;

	while (len > sizeof(*dchdr)) {
		dchdr = (struct cmd_ctrl_hdr *)bp;

		if (dchdr->dlen > len) {
			pr_err("%s: error, dchdr->dlen(%u) > len(%d)", __func__,
				dchdr->dlen, len);
			return -EINVAL;
		}

		bp += sizeof(*dchdr);
		len -= sizeof(*dchdr);
		bp += dchdr->dlen;
		len -= dchdr->dlen;
		cnt++;
	}

	if (len != 0) {
		pr_err("%s:the left data is not match with struct cmd_ctrl_hdr!",
			__func__);
		return -EINVAL;
	}

	*count = cnt;

	return 0;
}

static int jlq_panel_alloc_cmd_packets(struct panel_cmds *pcmds, int cnt)
{
	if (!pcmds || cnt <= 0)
		return -EINVAL;

	pcmds->cmds = kcalloc(cnt, sizeof(struct cmd_desc), GFP_KERNEL);
	if (!pcmds->cmds) {
		return -ENOMEM;
	}

    return 0;
}

static int jlq_panel_create_cmd_packets(char *data, int len, int cnt,
			struct panel_cmds *pcmds)
{
	int i;
	struct cmd_ctrl_hdr *dchdr;
	char *bp = data;

	for (i = 0; i < cnt; i++) {
		dchdr = (struct cmd_ctrl_hdr *)bp;
		len -= sizeof(*dchdr);
		bp += sizeof(*dchdr);
		pcmds->cmds[i].dchdr = *dchdr;
		pcmds->cmds[i].payload = bp;
		bp += dchdr->dlen;
		len -= dchdr->dlen;
	}

	return 0;
}

static int jlq_panel_parse_cmds(struct device *dev,
			const u8 *data, int blen,
			struct panel_cmds *pcmds)
{
	int ret, cnt;
	char *buf;

	if (!pcmds)
		return -EINVAL;

	buf = kmemdup(data, blen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = jlq_panel_get_cmd_pkt_count(buf, blen, &cnt);
	if (ret) {
		dev_err(dev, "%s:failed to get cmd pkt count!", __func__);
		ret = -EINVAL;
		goto error_free_buf;
	}

	ret = jlq_panel_alloc_cmd_packets(pcmds, cnt);
	if (ret) {
		dev_err(dev, "%s:failed to alloc cmd packets!", __func__);
		ret = -ENOMEM;
		goto error_free_buf;
	}

	pcmds->cmd_cnt = cnt;
	pcmds->buf = buf;
	pcmds->blen = blen;

	ret = jlq_panel_create_cmd_packets(buf, blen, cnt, pcmds);
	if (ret) {
		dev_err(dev, "%s:failed to alloc cmd packets!", __func__);
		ret = -ENOMEM;
		goto error_free_cmds;
	}

	dev_dbg(dev, "%s: dcs_cmd=%x len=%d, cmd_cnt=%d\n", __func__,
		 pcmds->buf[0], pcmds->blen, pcmds->cmd_cnt);

	return 0;
error_free_cmds:
	kfree(pcmds->cmds);
error_free_buf:
	kfree(buf);
	return ret;
}

#if IS_ENABLED(CONFIG_DRM_MIPI_DSI)
static int jlq_panel_reg_write(struct jlq_panel *panel,
			struct cmd_desc *cmd)
{
	struct mipi_dsi_device *dsi = panel->dsi;
	int err;

	if (!cmd) {
		dev_err(&dsi->dev, "%s:dsi cmd is null!", __func__);
		return -EINVAL;
	}

	dev_dbg(&dsi->dev, "cmd:type=%#x,len=%d,data=%#x,wait=%dms\n",
		cmd->dchdr.dtype, cmd->dchdr.dlen,
		(u8)*cmd->payload, cmd->dchdr.wait);

	switch (cmd->dchdr.dtype) {
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_GENERIC_LONG_WRITE:
		err = mipi_dsi_generic_write(dsi, cmd->payload,
					cmd->dchdr.dlen);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_LONG_WRITE:
		err = mipi_dsi_dcs_write_buffer(dsi, cmd->payload,
					cmd->dchdr.dlen);
		break;
	default:
		dev_err(&dsi->dev, "unsupport packet type:0x%x\n", cmd->dchdr.dtype);
		return -EINVAL;
	}

	if (err < 0)
		dev_err(&dsi->dev, "dsi cmd write failed: %d\n", err);

	if (cmd->dchdr.wait)
		msleep(cmd->dchdr.wait);

	return 0;
}

static int jlq_panel_dsi_cmds_write(struct jlq_panel *panel,
				      struct panel_cmds *cmds)
{
	struct mipi_dsi_device *dsi = panel->dsi;
	int i, err;

	if (!cmds) {
		dev_err(&dsi->dev, "%s:cmds is null!", __func__);
		return -EINVAL;
	}

	for (i = 0; i < cmds->cmd_cnt; i++) {
		struct cmd_desc *cmd = &cmds->cmds[i];

		err = jlq_panel_reg_write(panel, cmd);
		if (err < 0) {
			dev_err(&dsi->dev, "%s, dcs cmd %d write failed\n", __func__, i);
			return -EINVAL;
		}
	}

	return 0;
}

static int jlq_panel_reg_read(struct jlq_panel *panel,
            struct cmd_desc *cmd, void *data, u32 size)
{
	int ret;
	const struct cmd_ctrl_hdr *header;
	struct mipi_dsi_device *dsi = panel->dsi;

	if (!cmd) {
		dev_err(&dsi->dev, "%s:dsi cmd is null!", __func__);
		return -EINVAL;
	}

	if (!data) {
		dev_err(&dsi->dev, "%s:receive buffer is null!", __func__);
		return -EINVAL;
	}

	if (size) {
		ret = mipi_dsi_set_maximum_return_packet_size(dsi, size);
		if (ret < 0) {
			dev_err(&dsi->dev, "dsi,failed to set maximum return packet size\n");
			return ret;
		}
	} else
		return -EINVAL;

	header = (struct cmd_ctrl_hdr *)cmd;

	switch (header->dtype) {
	case MIPI_DSI_DCS_READ:
		ret = mipi_dsi_dcs_read(dsi, *(cmd->payload), data, size);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		ret = mipi_dsi_generic_read(dsi, cmd->payload, header->dlen, data, size);
		break;
	default:
		dev_err(&dsi->dev, "unsupport read cmd data type: %d\n", header->dtype);
		return -EINVAL;
	}

	if (ret < 0) {
		dev_err(&dsi->dev, "failed to read cmd %d\n", ret);
		return ret;
	}

	if (header->wait)
		mdelay(header->wait);

	return 0;
}

static int jlq_panel_dsi_cmds_read(struct jlq_panel *panel,
            struct panel_cmds *cmds, void *data, u32 size)
{
	int ret;
	struct cmd_desc *cmd;
	struct mipi_dsi_device *dsi = panel->dsi;

	if (!cmds)
		return -EINVAL;

	if (!cmds->cmds)
		return -EINVAL;

	if (!cmds->cmd_cnt)
		return -EINVAL;

	if (cmds->cmd_cnt > 1) {
		u32 i;

		for (i = 0; i < cmds->cmd_cnt - 1; i++) {
			cmd = &cmds->cmds[i];
			ret = jlq_panel_reg_write(panel, cmd);
			if (ret < 0) {
				dev_err(&dsi->dev, "%s, dcs cmd %d write failed\n", __func__, i);
				return -EINVAL;
			}
		}
	}

	cmd = &cmds->cmds[cmds->cmd_cnt - 1];
	ret = jlq_panel_reg_read(panel, cmd, data, size);
	if (ret < 0) {
		dev_err(&dsi->dev, "failed to read cmd %d\n", ret);
		return ret;
	}

	return 0;
}
#else
static inline int jlq_panel_dsi_cmds_write(struct jlq_panel *panel,
					     struct panel_cmds *cmds)
{
	dev_err(&dsi->dev, "can't send dsi cmds\n");

	return -EINVAL;
}

static int jlq_panel_dsi_cmds_read(struct jlq_panel *panel,
            struct panel_cmds *cmds, void *data, unsigned char size)
{
	dev_err(&dsi->dev, "can't read dsi cmds\n");

	return -EINVAL;
}
#endif

static int jlq_panel_get_panel_cmds(struct jlq_panel *panel,
		struct panel_cmds **pcmds, const char *cmds_name)
{
	int len;
	int err;
	const void *data;
	struct panel_cmds *temp;
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;

	if (!cmds_name) {
		dev_err(dev, "%s, cmds_name is null for %s\n", __func__);
		return -EINVAL;
	}

	if (!pcmds) {
		dev_err(dev, "%s, pcmds is null for %s\n", __func__, cmds_name);
		return -EINVAL;
	}

	data = of_get_property(dev->of_node, cmds_name, &len);
	if (data) {
		temp = devm_kzalloc(dev, sizeof(struct panel_cmds), GFP_KERNEL);
		if (!temp)
			return -ENOMEM;

		err = jlq_panel_parse_cmds(dev, data, len, temp);
		if (err) {
			dev_err(dev, "%s, failed to parse panel_cmds:%s\n", __func__, cmds_name);
			devm_kfree(dev, temp);
			return -EINVAL;
		}

		*pcmds = temp;
	}

	return 0;
}

static int jlq_panel_get_cmds(struct jlq_panel *panel)
{
	const void *data;
	int len;
	int err;
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;

	err = jlq_panel_get_panel_cmds(panel, &panel->on_cmds, "panel-init-sequence");
	if (err) {
		dev_err(dev, "failed to parse panel init sequence\n");
		goto err_ret;
	}

	err = jlq_panel_get_panel_cmds(panel, &panel->off_cmds, "panel-exit-sequence");
	if (err) {
		dev_err(dev, "failed to parse panel exit sequence\n");
		goto err_free_on_mem;
	}

	err = jlq_panel_get_panel_cmds(panel, &panel->mipi_pre_cmds, "mipi-pre-sequence");
	if (err) {
		dev_err(dev, "failed to parse mipi-pre-sequence\n");
		goto err_free_off_mem;
	}

	err = jlq_panel_get_panel_cmds(panel, &panel->bl_off_pre_cmds, "bl-off-pre-sequence");
	if (err) {
		dev_err(dev, "failed to parse bl-off-pre-sequence\n");
		goto err_free_mipi_pre_mem;
	}

	err = jlq_panel_get_panel_cmds(panel, &panel->hbm_info.on_cmds, "hbm-on-sequence");
	if (err) {
		dev_err(dev, "failed to parse hbm-on-sequence\n");
		goto err_free_bl_off_pre_mem;
	}

	err = jlq_panel_get_panel_cmds(panel, &panel->hbm_info.off_cmds, "hbm-off-sequence");
	if (err) {
		dev_err(dev, "failed to parse hbm-off-sequence\n");
		goto err_free_hbm_on_mem;
	}

	return 0;

err_free_hbm_on_mem:
	jlq_panel_free_panel_cmds(panel, panel->hbm_info.on_cmds);
err_free_bl_off_pre_mem:
	jlq_panel_free_panel_cmds(panel, panel->bl_off_pre_cmds);
err_free_mipi_pre_mem:
	jlq_panel_free_panel_cmds(panel, panel->mipi_pre_cmds);
err_free_off_mem:
	jlq_panel_free_panel_cmds(panel, panel->off_cmds);
err_free_on_mem:
	jlq_panel_free_panel_cmds(panel, panel->on_cmds);
err_ret:
	return err;
}

static int jlq_panel_parse_read_entry(struct jlq_panel *panel,
			struct device_node *entry,
			struct panel_read_entry *read_entry)
{
	int len;
	const void *data;
	int rc = 0;
	u32 tmp = 0;
	struct device *dev = &panel->dsi->dev;

	if (!entry) {
		dev_err(dev, "read entry node is null\n");
		rc = -EINVAL;
		goto error;
	}

	if (!read_entry) {
		dev_err(dev, "read_entry is null\n");
		rc = -EINVAL;
		goto error;
	}

	rc = of_property_read_u32(entry, "return-packet-size", &tmp);
	if (rc) {
		dev_err(dev, "failed to read return-packet-size, rc = %d\n", rc);
		goto error;
	}

	if (tmp <= 64)
		read_entry->return_packet_size = tmp;
	else {
		dev_err(dev, "return-packet-size must be less than 64\n");
		goto error;
	}

	if (of_find_property(entry, "valid-value-array", &len)) {
		if (len == read_entry->return_packet_size){
			rc = of_property_read_u8_array(entry,"valid-value-array",
					read_entry->vbuf,len);
			if (rc){
				dev_err(dev,"failed to read valid-value-array\n");
				goto error;
			}

                 } else {
			dev_err(dev,"the length of valid-value-array is wrong\n");
			goto error;
			}

        }

	data = of_get_property(entry, "read-sequence", &len);
	if (data) {
		rc = jlq_panel_parse_cmds(dev, data, len,
				&read_entry->read_cmds);
		if (rc) {
			dev_err(dev, "failed to parse read-sequence\n");
			goto error;
		}
	} else {
		dev_err(dev, "no read-sequence present\n");
		rc = -EINVAL;
		goto error;
	}

	return 0;
error:
	read_entry->return_packet_size = 0;
	return rc;
}

static void jlq_panel_free_read_info(struct jlq_panel *panel,
		struct panel_read_info *read_info)
{
	struct panel_cmds *read_cmds;
	struct panel_read_entry *read_entry;
	struct device *dev = &panel->dsi->dev;
	int i;

	if (!read_info) {
		dev_err(dev, "%s, read_info is null\n", __func__);
		return;
	}

	for (i = read_info->count - 1; i >= 0; i--) {
		read_entry = &read_info->read_entries[i];
		read_cmds = &read_entry->read_cmds;

		if (read_cmds->cmds)
			kfree(read_cmds->cmds);

		if (read_cmds->buf)
			kfree(read_cmds->buf);
	}

	if (read_info->read_entries)
		devm_kfree(dev, read_info->read_entries);

	read_info->read_entries = NULL;
	read_info->count = 0;
}

static int jlq_panel_parse_dt_read_info(struct jlq_panel *panel,
		struct panel_read_info *read_info, char *entries_name)
{
	u32 i = 0;
	int rc = 0;
	struct device *dev = &panel->dsi->dev;
	struct device_node *of_node = NULL;
	struct device_node *child_node = NULL;
	struct device_node *root_node = NULL;
	struct panel_read_entry *read_entry = NULL;

	if (!entries_name) {
		dev_err(dev, "%s, entries_name is null\n", __func__);
		return 0;
	}

	if (!read_info) {
		dev_err(dev, "%s, read_info is null\n", __func__);
		return 0;
	}

	of_node = dev->of_node;
	read_info->count = 0;
	root_node = of_get_child_by_name(of_node, entries_name);
	if (!root_node) {
		root_node = of_parse_phandle(of_node, entries_name, 0);
		if (!root_node) {
			dev_err(dev, "No lockdown entry present for %s\n", entries_name);
			return 0;
		}
	}

	for_each_child_of_node(root_node, child_node)
		read_info->count++;

	if (read_info->count == 0) {
		dev_err(dev, "No read_info entry for %s\n", entries_name);
		return 0;
	}

	read_info->read_entries = devm_kcalloc(dev, read_info->count,
				sizeof(*read_info->read_entries),
				GFP_KERNEL);
	if (!read_info->read_entries) {
		read_info->count = 0;
		dev_err(dev, "failed to alloc read_entries mem\n");
		return 0;
	}

	child_node = NULL;
	for_each_child_of_node(root_node, child_node) {
		read_entry = &read_info->read_entries[i];
		rc = jlq_panel_parse_read_entry(panel, child_node, read_entry);
		if (rc) {
			dev_err(dev, "failed to parse read_entries node %u\n", i);
			read_info->count = i;
			jlq_panel_free_read_info(panel, read_info);
			return rc;
		}
		++i;
	}

	return 0;
}

static int jlq_panel_read_info(struct jlq_panel *panel, struct panel_read_info *read_info)
{
	u32 i;
	int rc = 0;
	struct device *dev = &panel->dsi->dev;
	struct panel_read_entry *read_entry = NULL;

	if (!read_info) {
		dev_err(dev, "%s, read_info is null\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	for (i = 0; i < read_info->count; i++) {
		read_entry = &read_info->read_entries[i];
		rc = jlq_panel_dsi_cmds_read(panel, &read_entry->read_cmds,
				&read_entry->rbuf, read_entry->return_packet_size);
		if (rc) {
			dev_err(dev, "%s, failed to read info entry %u\n", __func__, i);
			mutex_unlock(&panel->panel_lock);
			return -EINVAL;
		}
	}

	mutex_unlock(&panel->panel_lock);
	return 0;
}

static int jlq_panel_validate_read_info(struct jlq_panel *panel,
               struct panel_read_info *read_info)
{
       u32 i, j;
       struct device *dev = &panel->dsi->dev;
       struct panel_read_entry *read_entry = NULL;

       if (!read_info) {
               dev_err(dev, "%s, read_info is null\n", __func__);
               return -EINVAL;
       }

       for (i = 0; i < read_info->count; i++) {
               read_entry = &read_info->read_entries[i];

               for (j = 0; j < read_entry->return_packet_size; j++) {
                       if (read_entry->rbuf[j] != read_entry->vbuf[j]) {
                               dev_err(dev, "read_entries[%d], rbuf[%u]=0x%x, vbuf[%u]=0x%x\n",
                                       i, j, read_entry->rbuf[j], j, read_entry->vbuf[j]);
                               return -EINVAL;
                       }
               } 
    }

       return 0;
}

static ssize_t jlq_panel_lockdown_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	u32 i, j;
	char *tmp = NULL;
	struct panel_read_entry *read_entry = NULL;
	struct jlq_panel *panel = dev_get_drvdata(dev);
	struct panel_read_info *lockdown_info = &panel->lockdown_info;

	if (!panel) {
		dev_err(dev, "%s, no panel exist!\n", __func__);
		return -EINVAL;
	}

	if (!lockdown_info->count) {
		dev_err(dev, "%s, no lockdown_info entry\n", __func__);
		return -EINVAL;
	}

	if (!lockdown_info->readed) {
		if (!panel->enabled) {
			dev_err(dev, "No reading lockdown info when panel is disabled!\n");
			return -EAGAIN;
		}

		ret = jlq_panel_read_info(panel, lockdown_info);
		if (ret) {
			dev_err(dev, "failed to get lockdown info from panel!\n");
			return -EINVAL;
		} else
			lockdown_info->readed = true;
	}

	tmp = buf;
	for (i = 0; i < lockdown_info->count; i++) {
		read_entry = &lockdown_info->read_entries[i];

		for (j = 0; j < read_entry->return_packet_size; j++) {
			snprintf(tmp, PAGE_SIZE, "%02X", read_entry->rbuf[j]);
			tmp += 2;
		}
	}

	snprintf(tmp, PAGE_SIZE, "%s", "\n");

	return strlen(buf);
}

static DEVICE_ATTR(lcd_lockdown_info, S_IRUGO, jlq_panel_lockdown_info_show, NULL);

static void novatek_map_whitepoint(struct panel_whitepoint_info *whitepoint_info)
{
	u32 i;
	u8 temp[3];
	struct panel_read_entry *read_entry = NULL;
	struct panel_read_info *read_info = &whitepoint_info->read_info;

	for (i = 0; i < 3; i++) {
		read_entry = &read_info->read_entries[i];
		temp[i] = read_entry->rbuf[0];
	}
	whitepoint_info->cie_x = temp[0] * 100 + temp[1] * 10 + temp[2];

	for (i = 0; i < 3; i++) {
		read_entry = &read_info->read_entries[i + 3];
		temp[i] = read_entry->rbuf[0];
	}
	whitepoint_info->cie_y = temp[0] * 100 + temp[1] * 10 + temp[2];
}

static void focaltech_map_whitepoint(struct panel_whitepoint_info *whitepoint_info)
{
	struct panel_read_entry *read_entry = NULL;
	struct panel_read_info *read_info = &whitepoint_info->read_info;

	read_entry = &read_info->read_entries[0];
	whitepoint_info->cie_x = read_entry->rbuf[0] + 172;
	whitepoint_info->cie_y = read_entry->rbuf[1] + 192;
}

static ssize_t jlq_panel_whitepoint_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	u32 i, j;
	char *tmp = NULL;
	struct panel_read_entry *read_entry = NULL;
	struct jlq_panel *panel = dev_get_drvdata(dev);
	struct panel_read_info *read_info = &panel->whitepoint_info.read_info;

	if (!panel) {
		dev_err(dev, "%s, no panel exist!\n", __func__);
		return -EINVAL;
	}

	if (!read_info->count) {
		dev_err(dev, "%s, no whitepoint_info entry\n", __func__);
		return -EINVAL;
	}

	if (!read_info->readed) {
		if (!panel->enabled) {
			dev_err(dev, "No reading whitepoint info when panel is disabled!\n");
			return -EAGAIN;
		}

		ret = jlq_panel_read_info(panel, read_info);
		if (ret) {
			dev_err(dev, "failed to get whitepoint info from panel!\n");
			return -EINVAL;
		} else {
			if (panel->ic_vendor == PANEL_IC_NOVATEK)
				novatek_map_whitepoint(&panel->whitepoint_info);
			else if (panel->ic_vendor == PANEL_IC_FOCALTECH)
				focaltech_map_whitepoint(&panel->whitepoint_info);

			read_info->readed = true;
		}
	}

	if (panel->ic_vendor == PANEL_IC_NOVATEK ||
			panel->ic_vendor == PANEL_IC_FOCALTECH) {
			snprintf(buf, PAGE_SIZE, "x=%d,y=%d\n",
				panel->whitepoint_info.cie_x,
				panel->whitepoint_info.cie_y);
	} else {
		tmp = buf;
		for (i = 0; i < read_info->count; i++) {
			read_entry = &read_info->read_entries[i];

			for (j = 0; j < read_entry->return_packet_size; j++) {
				snprintf(tmp, PAGE_SIZE, "%02X", read_entry->rbuf[j]);
				tmp += 2;
			}
		}

		snprintf(tmp, PAGE_SIZE, "%s", "\n");
	}

	return strlen(buf);
}

static DEVICE_ATTR(lcd_whitepoint_info, S_IRUGO, jlq_panel_whitepoint_info_show, NULL);

static int jlq_panel_power_enable(struct jlq_panel *panel, bool enable)
{
	int rc = 0, i = 0;
	struct panel_power *power;
	struct device *dev = &panel->dsi->dev;
	struct panel_power_info *power_info = &panel->power_info;

	if (enable) {
		for (i = 0; i < power_info->count; i++) {
			power = &(power_info->powers[i]);

			if (power->pre_on_sleep)
				mdelay(power->pre_on_sleep);

			rc = regulator_set_voltage(power->vreg,
					power->min_voltage,
					power->max_voltage);
			if (rc) {
				dev_err(dev, "failed to set voltage(%s), rc=%d\n",
					power->vreg_name, rc);
				goto error;
			}

			rc = regulator_enable(power->vreg);
			if (rc) {
				dev_err(dev, "failed to enable %s, rc=%d\n",
					power->vreg_name, rc);

				regulator_set_voltage(power->vreg, 0, power->max_voltage);

				goto error;
			}

			if (power->post_on_sleep)
				mdelay(power->post_on_sleep);
		}
	} else {
		for (i = (power_info->count - 1); i >= 0; i--) {
			power = &(power_info->powers[i]);

			if (power->pre_off_sleep)
				mdelay(power->pre_off_sleep);

			regulator_set_voltage(power->vreg, 0, power->max_voltage);

			regulator_disable(power->vreg);

			if (power->post_off_sleep)
				mdelay(power->post_off_sleep);
		}
	}

	return 0;
error:
	for (i--; i >= 0; i--) {
		power = &(power_info->powers[i]);

		if (power->pre_off_sleep)
			mdelay(power->pre_off_sleep);

		regulator_set_voltage(power->vreg, 0, power->max_voltage);

		regulator_disable(power->vreg);

		if (power->post_off_sleep)
			mdelay(power->post_off_sleep);
	}

	return rc;
}

static int jlq_panel_power_get(struct jlq_panel *panel)
{
	int i;
	int rc = 0;
	struct regulator *vreg = NULL;
	struct device *dev = &panel->dsi->dev;

	for (i = 0; i < panel->power_info.count; i++) {
		vreg = devm_regulator_get(dev, panel->power_info.powers[i].vreg_name);
		rc = PTR_RET(vreg);
		if (rc) {
			dev_err(dev, "failed to get regulator %s\n",
			      panel->power_info.powers[i].vreg_name);
			goto error_put;
		}
		panel->power_info.powers[i].vreg = vreg;
	}

	return 0;
error_put:
	for (i = i - 1; i >= 0; i--) {
		devm_regulator_put(panel->power_info.powers[i].vreg);
		panel->power_info.powers[i].vreg = NULL;
	}

	panel->power_info.count = 0;

	return rc;
}

static int jlq_panel_power_put(struct jlq_panel *panel)
{
	int i;

	for (i = panel->power_info.count - 1; i >= 0; i--)
		devm_regulator_put(panel->power_info.powers[i].vreg);

	return 0;
}

static int jlq_panel_parse_power_supply_node(struct jlq_panel *panel,
			struct device_node *root)
{
	int rc = 0;
	int i = 0;
	u32 tmp = 0;
	struct device_node *child = NULL;
	struct device *dev = &panel->dsi->dev;
	struct panel_power_info *power_info = &panel->power_info;

	for_each_child_of_node(root, child) {
		const char *st = NULL;

		rc = of_property_read_string(child, "supply-name", &st);
		if (rc) {
			dev_err(dev, "failed to read name, rc = %d\n", rc);
			goto error;
		}

		snprintf(power_info->powers[i].vreg_name,
			 ARRAY_SIZE(power_info->powers[i].vreg_name),
			 "%s", st);

		rc = of_property_read_u32(child, "supply-min-voltage", &tmp);
		if (rc) {
			dev_err(dev, "failed to read min voltage, rc = %d\n", rc);
			goto error;
		}
		power_info->powers[i].min_voltage = tmp;

		rc = of_property_read_u32(child, "supply-max-voltage", &tmp);
		if (rc) {
			dev_err(dev, "failed to read max voltage, rc = %d\n", rc);
			goto error;
		}
		power_info->powers[i].max_voltage = tmp;

		/* Optional values */
		rc = of_property_read_u32(child, "supply-pre-on-sleep", &tmp);
		if (rc) {
			dev_dbg(dev, "supply-pre-on-sleep is not specified\n");
			rc = 0;
			power_info->powers[i].pre_on_sleep = 0;
		} else {
			power_info->powers[i].pre_on_sleep = tmp;
		}

		rc = of_property_read_u32(child, "supply-pre-off-sleep", &tmp);
		if (rc) {
			dev_dbg(dev, "supply-pre-off-sleep is not specified\n");
			rc = 0;
			power_info->powers[i].pre_off_sleep = 0;
		} else {
			power_info->powers[i].pre_off_sleep = tmp;
		}

		rc = of_property_read_u32(child, "supply-post-on-sleep", &tmp);
		if (rc) {
			dev_dbg(dev, "supply-post-on-sleep is not specified\n");
			rc = 0;
			power_info->powers[i].post_on_sleep = 0;
		} else {
			power_info->powers[i].post_on_sleep = tmp;
		}

		rc = of_property_read_u32(child, "supply-post-off-sleep", &tmp);
		if (rc) {
			dev_dbg(dev, "supply-post-off-sleep is not specified\n");
			rc = 0;
			power_info->powers[i].post_off_sleep = 0;
		} else {
			power_info->powers[i].post_off_sleep = tmp;
		}

		dev_dbg(dev, "power %s, minv=%d, maxv=%d\n",
			 power_info->powers[i].vreg_name,
			 power_info->powers[i].min_voltage,
			 power_info->powers[i].max_voltage);
		++i;
	}

error:
	return rc;
}

static int jlq_panel_parse_dt_power_info(struct jlq_panel *panel)
{
	int rc = 0;
	struct device *dev = &panel->dsi->dev;
	struct device_node *of_node = NULL;
	struct device_node *supply_node = NULL;
	struct device_node *supply_root_node = NULL;
	char *supply_name = "power-supply-entries";
	struct panel_power_info *power_info = &panel->power_info;

	of_node = dev->of_node;
	power_info->count = 0;
	supply_root_node = of_get_child_by_name(of_node, supply_name);
	if (!supply_root_node) {
		supply_root_node = of_parse_phandle(of_node, supply_name, 0);
		if (!supply_root_node) {
			dev_err(dev, "No supply entry present for %s\n", supply_name);
			return 0;
		}
	}

	for_each_child_of_node(supply_root_node, supply_node)
		power_info->count++;

	if (power_info->count == 0) {
		dev_err(dev, "No vregs defined for %s\n", supply_name);
		return 0;
	}

	power_info->powers = devm_kcalloc(dev, power_info->count,
				sizeof(*power_info->powers),
				GFP_KERNEL);
	if (!power_info->powers) {
		power_info->count = 0;
		dev_err(dev, "failed to alloc powers mem\n");
		return 0;
	}

	rc = jlq_panel_parse_power_supply_node(panel, supply_root_node);
	if (rc) {
		dev_err(dev, "failed to parse supply node for %s, rc = %d\n",
		       supply_name, rc);
		devm_kfree(dev, power_info->powers);
		power_info->powers = NULL;
		power_info->count = 0;
	}

	return rc;
}

static int jlq_panel_get_power_info(struct jlq_panel *panel)
{
	int rc = 0;
	struct device *dev = &panel->dsi->dev;

	rc = jlq_panel_parse_dt_power_info(panel);
	if (rc) {
		dev_err(dev, "failed to parse power info: %d\n", rc);
		return rc;
	}

	rc = jlq_panel_power_get(panel);
	if (rc) {
		dev_err(dev, "failed to power get: %d\n", rc);
		return rc;
	}

	return 0;
}

static int jlq_panel_set_pinctrl_state(struct jlq_panel *panel, bool enable)
{
    int rc = 0;
	struct pinctrl_state *state;
	struct device *dev = &panel->dsi->dev;

	if (!IS_ERR_OR_NULL(panel->pinctrl.pinctrl)) {
		if (enable)
			state = panel->pinctrl.active;
		else
			state = panel->pinctrl.suspend;

		if (!IS_ERR_OR_NULL(state)) {
			rc = pinctrl_select_state(panel->pinctrl.pinctrl, state);
			if (rc)
				dev_err(dev, "failed to set pin state, rc=%d\n", rc);
		}
	}

	return rc;
}

static int jlq_panel_request_gpios(struct jlq_panel *panel)
{
	int err = 0;
	struct device *dev = &panel->dsi->dev;

	if (gpio_is_valid(panel->power_enable_gpio)) {
		err = gpio_request(panel->power_enable_gpio, "lcd_power_enable_gpio");
		if (err) {
			dev_err(dev, "failed to request lcd power-enable-gpio %d\n",
				panel->power_enable_gpio);
			goto err;
		}
	}

	if (gpio_is_valid(panel->bias_enp_gpio)) {
		err = gpio_request(panel->bias_enp_gpio, "lcd_bias_enp_gpio");
		if (err) {
			dev_err(dev, "failed to request lcd bias-enp-gpio %d\n",
				panel->bias_enp_gpio);
			goto err_free_lcd_power_enable_gpio;
		}
	}

	if (gpio_is_valid(panel->bias_enn_gpio)) {
		err = gpio_request(panel->bias_enn_gpio, "lcd_bias_enn_gpio");
		if (err) {
			dev_err(dev, "failed to request lcd bias-enn-gpio %d\n",
				panel->bias_enn_gpio);
			goto err_free_lcd_bias_enp_gpio;
		}
	}

	if (gpio_is_valid(panel->reset_gpio)) {
		err = gpio_request(panel->reset_gpio, "lcd_rest");
		if (err) {
			dev_err(dev, "failed to request lcd reset-gpio %d\n",
				panel->reset_gpio);
			goto err_free_lcd_bias_enn_gpio;
		}
	}

	if (gpio_is_valid(panel->bl_enable_gpio)) {
		err = gpio_request(panel->bl_enable_gpio,
			"lcd_enable-backlight-gpio");
		if (err) {
			dev_err(dev, "Failed to request enable-backlight-gpio %d\n",
				panel->bl_enable_gpio);
			goto err_free_lcd_reset_gpio;
		}
	}
	return 0;
err_free_lcd_reset_gpio:
	if (gpio_is_valid(panel->reset_gpio))
		gpio_free(panel->reset_gpio);
err_free_lcd_bias_enn_gpio:
	if (gpio_is_valid(panel->bias_enn_gpio))
		gpio_free(panel->bias_enn_gpio);
err_free_lcd_bias_enp_gpio:
	if (gpio_is_valid(panel->bias_enp_gpio))
		gpio_free(panel->bias_enp_gpio);
err_free_lcd_power_enable_gpio:
	if (gpio_is_valid(panel->power_enable_gpio))
		gpio_free(panel->power_enable_gpio);
err:
	return err;
}

static int jlq_panel_free_gpios(struct jlq_panel *panel)
{
	if (gpio_is_valid(panel->bl_enable_gpio))
		gpio_free(panel->bl_enable_gpio);

	if (gpio_is_valid(panel->reset_gpio))
		gpio_free(panel->reset_gpio);

	if (gpio_is_valid(panel->bias_enn_gpio))
		gpio_free(panel->bias_enn_gpio);

	if (gpio_is_valid(panel->bias_enp_gpio))
		gpio_free(panel->bias_enp_gpio);

	if (gpio_is_valid(panel->power_enable_gpio))
		gpio_free(panel->power_enable_gpio);

	return 0;
}

static int jlq_panel_parse_dt_gpios(struct jlq_panel *panel)
{
	struct device *dev = &panel->dsi->dev;

	panel->power_enable_gpio = of_get_named_gpio(dev->of_node, "power-enable-gpio", 0);
	if (!gpio_is_valid(panel->power_enable_gpio))
		dev_err(dev, "no or invalid lcd power enable gpio number\n");

	panel->bias_enp_gpio = of_get_named_gpio(dev->of_node, "bias-enp-gpio", 0);
	if (!gpio_is_valid(panel->bias_enp_gpio))
		dev_err(dev, "no or invalid lcd bias enp gpio number\n");

	panel->bias_enn_gpio = of_get_named_gpio(dev->of_node, "bias-enn-gpio", 0);
	if (!gpio_is_valid(panel->bias_enn_gpio))
		dev_err(dev, "no or invalid lcd bias enn gpio number\n");

	panel->reset_gpio = of_get_named_gpio(dev->of_node, "reset-gpio", 0);
	if (!gpio_is_valid(panel->reset_gpio))
		dev_err(dev, "no or invalid lcd reset gpio number\n");

	panel->tp_reset_gpio = of_get_named_gpio(dev->of_node, "tp-reset-gpio", 0);
	if (!gpio_is_valid(panel->tp_reset_gpio))
		dev_err(dev, "no or invalid tp reset gpio number\n");

	panel->bl_enable_gpio = of_get_named_gpio(dev->of_node,
					"enable-backlight-gpio", 0);
	if (!gpio_is_valid(panel->bl_enable_gpio))
		dev_err(dev, "no or invalid lcd enable-backlight-gpio number\n");

	return 0;
}

static int jlq_panel_get_gpios(struct jlq_panel *panel)
{
	int ret;
	struct device *dev = &panel->dsi->dev;

	ret = jlq_panel_parse_dt_gpios(panel);
	if (ret) {
		dev_err(dev, "failed to parse gpio info\n");
		return ret;
	}

	ret = jlq_panel_request_gpios(panel);
	if (ret) {
		dev_err(dev, "failed to parse gpio info\n");
		return ret;
	}

	return 0;
}

static int jlq_panel_get_fixed_modes(struct jlq_panel *panel)
{
	struct drm_connector *connector = panel->base.connector;
	struct drm_device *drm = panel->base.drm;
	struct drm_display_mode *mode;
	unsigned int i, num = 0;

	if (!panel->desc)
		return 0;

	for (i = 0; i < panel->desc->num_timings; i++) {
		const struct display_timing *dt = &panel->desc->timings[i];
		struct videomode vm;

		videomode_from_timing(dt, &vm);
		mode = drm_mode_create(drm);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u\n",
				dt->hactive.typ, dt->vactive.typ);
			continue;
		}

		drm_display_mode_from_videomode(&vm, mode);
		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	for (i = 0; i < panel->desc->num_modes; i++) {
		const struct drm_display_mode *m = &panel->desc->modes[i];

		mode = drm_mode_duplicate(drm, m);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, m->vrefresh);
			continue;
		}

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	connector->display_info.bpc = panel->desc->bpc;
	connector->display_info.width_mm = panel->desc->size.width;
	connector->display_info.height_mm = panel->desc->size.height;
	if (panel->desc->bus_format)
		drm_display_info_set_bus_formats(&connector->display_info,
						 &panel->desc->bus_format, 1);

	return num;
}

static int jlq_panel_of_get_native_mode(struct jlq_panel *panel)
{
	struct drm_connector *connector = panel->base.connector;
	struct drm_device *drm = panel->base.drm;
	struct drm_display_mode *mode;
	struct device_node *timings_np;
	struct device *dev = &panel->dsi->dev;
	int ret;

	timings_np = of_get_child_by_name(dev->of_node,
					  "display-timings");
	if (!timings_np) {
		dev_err(dev, "failed to find display-timings node\n");
		return 0;
	}

	of_node_put(timings_np);
	mode = drm_mode_create(drm);
	if (!mode)
		return 0;

	ret = of_get_drm_display_mode(dev->of_node, mode, 0,
				      OF_USE_NATIVE_MODE);
	if (ret) {
		dev_err(dev, "failed to find dts display timings\n");
		drm_mode_destroy(drm, mode);
		return 0;
	}

	drm_mode_set_name(mode);
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static int jlq_panel_disable(struct drm_panel *panel)
{
	struct jlq_panel *jlq = to_jlq_panel(panel);
	struct device *dev = panel->dev;

	dev_err(dev, "%s enter\n", __func__);

	if (!jlq->enabled)
		return 0;

        jlq_panel_esd_disable(jlq);

	if (jlq->desc && jlq->desc->delay.disable) {
		dev_dbg(dev, "jlq->desc->delay.disable=%u\n",
				jlq->desc->delay.disable);

		msleep(jlq->desc->delay.disable);
	}

	jlq->enabled = false;

	return 0;
}

static int jlq_panel_gesture_unprepare(struct jlq_panel *panel)
{
       int ret;
       struct device *dev = &panel->dsi->dev;

       dev_err(dev, "%s enter\n", __func__);

       ret = jlq_panel_set_pinctrl_state(panel, false);
       if (ret)
               dev_err(dev, "failed to set pinctrl suspend state\n");

       return 0;
}

static int jlq_panel_normal_unprepare(struct jlq_panel *panel)
{
       int ret;
       struct device *dev = &panel->dsi->dev;

       dev_err(dev, "%s enter\n", __func__);

       if (gpio_is_valid(panel->reset_gpio)) {
               if (!panel->rst_keep_high) {
                       dev_dbg(dev, "reset gpio(%d) output 0\n", panel->reset_gpio);
                       gpio_direction_output(panel->reset_gpio, 0);
               }
       }

       if (gpio_is_valid(panel->bias_enn_gpio)) {
               dev_dbg(dev, "bias enn gpio(%d) output 0\n",
                       panel->bias_enn_gpio);
               gpio_direction_output(panel->bias_enn_gpio, 0);
       }

       if (gpio_is_valid(panel->bias_enp_gpio)) {
               dev_dbg(dev, "bias enp gpio(%d) output 0\n",
                       panel->bias_enp_gpio);
               gpio_direction_output(panel->bias_enp_gpio, 0);
       }
       if (gpio_is_valid(panel->power_enable_gpio))
               gpio_direction_output(panel->power_enable_gpio, 0);

//       ret = jlq_panel_set_pinctrl_state(panel, false);
//       if (ret)
//               dev_err(dev, "failed to set pinctrl suspend state\n");

       ret = jlq_panel_power_enable(panel, false);
       if (ret)
               dev_err(dev, "fail to disable power\n");

       return 0;
}



static int jlq_panel_unprepare(struct drm_panel *panel)
{
        bool normal;
	int ret, mode;
	unsigned long event;
	struct drm_panel_notifier notifier_data;
	struct jlq_panel *jlq = to_jlq_panel(panel);
	struct device *dev = &jlq->dsi->dev;

	if (!jlq->prepared)
		return 0;

	dev_err(dev, "%s enter\n", __func__);

	if (gpio_is_valid(jlq->bl_enable_gpio)) {
		dev_dbg(dev, "enable backlight gpio(%d) output 0\n",
			jlq->bl_enable_gpio);
		gpio_direction_output(jlq->bl_enable_gpio, 0);
	}

	event = DRM_PANEL_EARLY_EVENT_BLANK;
	mode = DRM_PANEL_BLANK_POWERDOWN;
	notifier_data.data = &mode;
	drm_panel_notifier_call_chain(panel, event, &notifier_data);

	if (jlq->desc && jlq->desc->delay.unprepare)
		mdelay(jlq->desc->delay.unprepare);

	mutex_lock(&jlq->panel_lock);
	if (jlq->off_cmds) {
		ret = jlq_panel_dsi_cmds_write(jlq, jlq->off_cmds);
		if (ret)
			dev_err(dev, "failed to send off cmds\n");
	}
	mutex_unlock(&jlq->panel_lock);
#if 0
	if (gpio_is_valid(jlq->reset_gpio)) {
		if (tp_gesture_enable || jlq->rst_keep_high) {
			dev_dbg(dev, "reset gpio(%d) keep high\n", jlq->reset_gpio);
		} else {
			dev_dbg(dev, "reset gpio(%d) output 0\n", jlq->reset_gpio);
			gpio_direction_output(jlq->reset_gpio, 0);
		}
	}
#endif
       normal = atomic_read(&jlq->esd_recovery_pending) || (!tp_gesture_enable);
       if (normal)
               jlq_panel_normal_unprepare(jlq);
       else
               jlq_panel_gesture_unprepare(jlq);

#if 0
	if (gpio_is_valid(jlq->bias_enn_gpio)) {
		if (tp_gesture_enable) {
			dev_dbg(dev, "bias enn gpio(%d) keep high\n",
				jlq->bias_enn_gpio);
		} else {
			dev_dbg(dev, "bias enn gpio(%d) output 0\n",
				jlq->bias_enn_gpio);
			gpio_direction_output(jlq->bias_enn_gpio, 0);
		}
	}

	if (gpio_is_valid(jlq->bias_enp_gpio)) {
		if (tp_gesture_enable) {
			dev_dbg(dev, "bias enp gpio(%d) keep high\n",
				jlq->bias_enp_gpio);
		} else {
			dev_dbg(dev, "bias enp gpio(%d) output 0\n",
				jlq->bias_enp_gpio);
			gpio_direction_output(jlq->bias_enp_gpio, 0);
		}
	}

	if (gpio_is_valid(jlq->power_enable_gpio)) {
		if (tp_gesture_enable) {
			dev_dbg(dev, "power enable gpio(%d) keep high\n",
				jlq->power_enable_gpio);
		} else {
			gpio_direction_output(jlq->power_enable_gpio, 0);
		}
	}

	ret = jlq_panel_set_pinctrl_state(jlq, false);
	if (ret)
		dev_err(dev, "failed to set pinctrl suspend state\n");

	if (tp_gesture_enable) {
		dev_dbg(dev, "skip powerdown when tp gesturn on\n");
	} else {
		ret = jlq_panel_power_enable(jlq, false);
		if (ret)
			dev_err(dev, "fail to disable power\n");
	}
#endif
	jlq->prepared = false;

	return 0;
}

static int jlq_panel_gesture_prepare(struct jlq_panel *panel)
{
       //int ret;
       struct device *dev = &panel->dsi->dev;

       dev_err(dev, "%s enter\n", __func__);

//       ret = jlq_panel_set_pinctrl_state(panel, true);
//        if (ret)
//               dev_err(dev, "failed to set pinctrl active state\n");

        return 0;
 }


static int jlq_panel_normal_prepare(struct jlq_panel *panel)
{
        int ret;
        struct device *dev = &panel->dsi->dev;

        dev_err(dev, "%s enter\n", __func__);
        ret = jlq_panel_power_enable(panel, true);
        if (ret)
            dev_err(dev, "fail to enable power\n");

//        ret = jlq_panel_set_pinctrl_state(panel, true);
//        if (ret)
//               dev_err(dev, "failed to set pinctrl active state\n");

        if (gpio_is_valid(panel->power_enable_gpio)) {
                       gpio_direction_output(panel->power_enable_gpio, 1);
        }

        if (panel->desc && panel->desc->delay.bias) {
                       mdelay(panel->desc->delay.bias);
        }
        if (gpio_is_valid(panel->bias_enp_gpio)) {
                       gpio_direction_output(panel->bias_enp_gpio, 1);
        }

        if (gpio_is_valid(panel->bias_enn_gpio)) {
                       gpio_direction_output(panel->bias_enn_gpio, 1);
        }

        if (panel->desc && panel->desc->delay.reset) {
            dev_dbg(dev, "reset gpio(%d) after delay=%ums\n",
                panel->reset_gpio, panel->desc->delay.reset);
            mdelay(panel->desc->delay.reset);
        }
#if 0
       if (gpio_is_valid(jlq->reset_gpio)) {
               for (i = 0; i < jlq->rst_seq_len; ++i) {
                       gpio_direction_output(jlq->reset_gpio,
                                       jlq->rst_seq[i]);

                       if (jlq->rst_seq[++i])
                               mdelay(jlq->rst_seq[i]);
               }
       }

       if (jlq->desc && jlq->desc->delay.init)
               mdelay(jlq->desc->delay.init);

       if (jlq->on_cmds) {
               ret = jlq_panel_dsi_cmds_write(jlq, jlq->on_cmds);
               if (ret)
                       dev_err(dev, "failed to send on cmds\n");
       }

       if (jlq->desc && jlq->desc->delay.prepare)
               mdelay(jlq->desc->delay.prepare);

       event = DRM_PANEL_EVENT_BLANK;
       mode = DRM_PANEL_BLANK_UNBLANK;
       notifier_data.data = &mode;
       drm_panel_notifier_call_chain(panel, event, &notifier_data);

       if (gpio_is_valid(jlq->bl_enable_gpio))
               gpio_direction_output(jlq->bl_enable_gpio, 1);

       jlq->prepared = true;
#endif
       return 0;
}

static int jlq_tp_reset_gpio_output(struct jlq_panel *panel, int value)
{
	int ret = 0;
	struct device *dev = &panel->dsi->dev;

	ret = gpio_request(panel->tp_reset_gpio, "tp_reset_gpio");
	if (ret) {
		dev_err(dev, "failed to request tp reset gpio\n");
		return ret;
	}

	if (value)
		gpio_direction_output(panel->tp_reset_gpio, 1);
	else
		gpio_direction_output(panel->tp_reset_gpio, 0);

	gpio_free(panel->tp_reset_gpio);

	return 0;
}

static int jlq_panel_prepare(struct drm_panel *panel)
{
	bool normal;
	int i, ret, mode;
	unsigned long event;
	struct drm_panel_notifier notifier_data;
	struct jlq_panel *jlq = to_jlq_panel(panel);
	struct device *dev = panel->dev;

	dev_err(dev, "%s enter\n", __func__);

	if (jlq->prepared)
		return 0;

	ret = jlq_panel_set_pinctrl_state(jlq, true);
	if (ret)
		dev_err(dev, "failed to set pinctrl active state\n");

	if (gpio_is_valid(jlq->tp_reset_gpio)) {
		jlq_tp_reset_gpio_output(jlq, 0);
		mdelay(5);
	}

	if (gpio_is_valid(jlq->reset_gpio)) {
		gpio_direction_output(jlq->reset_gpio, 0);
		mdelay(5);
	}

       normal = atomic_read(&jlq->esd_recovery_pending) || (!tp_gesture_enable);
       if (normal)
               jlq_panel_normal_prepare(jlq);
       else
               jlq_panel_gesture_prepare(jlq);
#if 0


	if (tp_gesture_enable) {
		dev_dbg(dev, "skip powerdown when tp gesturn on\n");
	} else {
		ret = jlq_panel_power_enable(jlq, true);
		if (ret)
			dev_err(dev, "fail to enable power\n");
	}

	ret = jlq_panel_set_pinctrl_state(jlq, true);
	if (ret)
		dev_err(dev, "failed to set pinctrl active state\n");

	if (gpio_is_valid(jlq->power_enable_gpio)) {
		if (tp_gesture_enable) {
			dev_dbg(dev, "skip power_enable_gpio when tp gesturn on\n");
		} else {
			gpio_direction_output(jlq->power_enable_gpio, 1);
		}
	}

	if (jlq->desc && jlq->desc->delay.bias) {
		if (tp_gesture_enable) {
			dev_dbg(dev, "skip delay.bias when tp gesturn on\n");
		} else {
			mdelay(jlq->desc->delay.bias);
		}
	}

	if (gpio_is_valid(jlq->bias_enp_gpio)) {
		if (tp_gesture_enable) {
			dev_dbg(dev, "skip bias_enp_gpio when tp gesturn on\n");
		} else {
			gpio_direction_output(jlq->bias_enp_gpio, 1);
		}
	}

	if (gpio_is_valid(jlq->bias_enn_gpio)) {
		if (tp_gesture_enable) {
			dev_dbg(dev, "skip bias_enn_gpio when tp gesturn on\n");
		} else {
			gpio_direction_output(jlq->bias_enn_gpio, 1);
		}
	}

	if (tp_gesture_enable) {
		dev_dbg(dev, "skip delay.reset when tp gesturn on\n");
	} else {
		if (jlq->desc && jlq->desc->delay.reset) {
			dev_dbg(dev, "reset gpio(%d) after delay=%ums\n",
					jlq->reset_gpio, jlq->desc->delay.reset);
			mdelay(jlq->desc->delay.reset);
		}
	}

#endif
	if (gpio_is_valid(jlq->reset_gpio)) {
		for (i = 0; i < jlq->rst_seq_len; ++i) {
			gpio_direction_output(jlq->reset_gpio,
					jlq->rst_seq[i]);

			if (jlq->rst_seq[++i])
				mdelay(jlq->rst_seq[i]);

			if (i == 3  && gpio_is_valid(jlq->tp_reset_gpio)) {
				jlq_tp_reset_gpio_output(jlq, 1);
				mdelay(1);
			}
		}
	}

	if (jlq->desc && jlq->desc->delay.init)
		mdelay(jlq->desc->delay.init);

	mutex_lock(&jlq->panel_lock);
	if (jlq->on_cmds) {
		ret = jlq_panel_dsi_cmds_write(jlq, jlq->on_cmds);
		if (ret)
			dev_err(dev, "failed to send on cmds\n");
	}
	mutex_unlock(&jlq->panel_lock);

	if (jlq->desc && jlq->desc->delay.prepare)
		mdelay(jlq->desc->delay.prepare);

	event = DRM_PANEL_EVENT_BLANK;
	mode = DRM_PANEL_BLANK_UNBLANK;
	notifier_data.data = &mode;
	drm_panel_notifier_call_chain(panel, event, &notifier_data);

	if (gpio_is_valid(jlq->bl_enable_gpio))
		gpio_direction_output(jlq->bl_enable_gpio, 1);

	jlq->prepared = true;

	return 0;
}

static int jlq_panel_enable(struct drm_panel *panel)
{
	struct jlq_panel *jlq = to_jlq_panel(panel);
	struct device *dev = panel->dev;

	dev_err(dev, "%s enter\n", __func__);

	if (jlq->enabled){
               jlq_panel_esd_enable(jlq);
		return 0;
        }

	if (jlq->desc && jlq->desc->delay.enable) {
		dev_dbg(dev, "jlq->desc->delay.enable=%u\n",
				jlq->desc->delay.enable);
		mdelay(jlq->desc->delay.enable);
	}

        jlq_panel_esd_enable(jlq);

	jlq->enabled = true;

	return 0;
}

static int jlq_panel_get_modes(struct drm_panel *panel)
{
	struct jlq_panel *p = to_jlq_panel(panel);
	int num = 0;

	/* add device node plane modes */
	num += jlq_panel_of_get_native_mode(p);

	/* add hard-coded panel modes */
	num += jlq_panel_get_fixed_modes(p);

	dev_dbg(panel->dev, "mode num is %d\n", num);

	return num;
}

static int jlq_panel_get_timings(struct drm_panel *panel,
				    unsigned int num_timings,
				    struct display_timing *timings)
{
	struct jlq_panel *p = to_jlq_panel(panel);
	unsigned int i;

	if (!p->desc)
		return 0;

	if (p->desc->num_timings < num_timings)
		num_timings = p->desc->num_timings;

	if (timings)
		for (i = 0; i < num_timings; i++)
			timings[i] = p->desc->timings[i];

	return p->desc->num_timings;
}

static const struct drm_panel_funcs jlq_panel_funcs = {
	.disable = jlq_panel_disable,
	.unprepare = jlq_panel_unprepare,
	.prepare = jlq_panel_prepare,
	.enable = jlq_panel_enable,
	.get_modes = jlq_panel_get_modes,
	.get_timings = jlq_panel_get_timings,
};

static const struct of_device_id jlq_of_match[] = {
	{ .compatible = "jlq,dsi-panel", },
	{ }
};
MODULE_DEVICE_TABLE(of, jlq_of_match);

static int jlq_panel_parse_reset_info(struct jlq_panel *panel)
{
	int num = 0, i;
	int rc;
	struct property *data;
	u32 tmp[RST_SEQ_LEN];
	const char *name = "reset-sequence";
	struct device *dev = &panel->dsi->dev;

	panel->rst_keep_high = of_property_read_bool(dev->of_node, "reset-gpio-keep-high");

	panel->rst_seq_len = 0;
	data = of_find_property(dev->of_node, "reset-sequence", &num);
	num /= sizeof(u32);
	if (!data || !num || num > RST_SEQ_LEN || num % 2) {
		pr_err("%s:%d, error reading reset-sequence, length found = %d\n",
			   __func__, __LINE__, num);
	} else {
		rc = of_property_read_u32_array(dev->of_node, name, tmp, num);
		if (rc) {
			pr_err("%s:%d, error reading reset-sequence, rc = %d\n",
				   __func__, __LINE__, rc);
		} else {
			panel->rst_seq_len = num;
			for (i = 0; i < num; ++i)
				panel->rst_seq[i] = tmp[i];
		}
	}

	return 0;
}

static void jlq_panel_pinctrl_put(struct jlq_panel *panel)
{
	if (!(IS_ERR_OR_NULL(panel->pinctrl.pinctrl)))
		devm_pinctrl_put(panel->pinctrl.pinctrl);
}

static int jlq_panel_parse_pinctrl(struct jlq_panel *panel)
{
	int rc = 0;
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;

	/* TODO:  pinctrl is defined in dsi dt node */
	panel->pinctrl.pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(panel->pinctrl.pinctrl)) {
		rc = PTR_ERR(panel->pinctrl.pinctrl);
		dev_err(dev, "failed to get pinctrl, rc=%d\n", rc);
		return 0;
	}

	panel->pinctrl.active = pinctrl_lookup_state(panel->pinctrl.pinctrl,
				"panel_active");
	if (IS_ERR_OR_NULL(panel->pinctrl.active)) {
		rc = PTR_ERR(panel->pinctrl.active);
		pr_err("failed to get pinctrl active state, rc=%d\n", rc);
	}

	panel->pinctrl.suspend = pinctrl_lookup_state(panel->pinctrl.pinctrl,
				"panel_suspend");

	if (IS_ERR_OR_NULL(panel->pinctrl.suspend)) {
		rc = PTR_ERR(panel->pinctrl.suspend);
		pr_err("failed to get pinctrl suspend state, rc=%d\n", rc);
	}

	return 0;
}

static void jlq_panel_esd_recovery(struct jlq_panel *panel)
{
       struct device *dev = &panel->dsi->dev;

       dev_err(dev, "%s\n", __func__);

       atomic_set(&panel->esd_recovery_pending, 1);

       notify_transmitter_panel_dead(panel->base.connector);

       if (panel->enabled)
               jlq_mipi_set_brightness(panel, panel->bl_cur_level);

       atomic_set(&panel->esd_recovery_pending, 0);
}

static void jlq_panel_esd_status_work(struct work_struct *work)
{
       int ret;
       struct device *dev;
       struct jlq_panel *panel;
       struct panel_esd_info *esd;
       struct panel_read_info *read_info;

       esd = container_of(to_delayed_work(work),
               struct panel_esd_info, status_work);

       panel = container_of(esd, struct jlq_panel, esd_info);
       dev = &panel->dsi->dev;

       if (!panel->enabled) {
               dev_err(dev, "No esd checking when panel is disabled!\n");
               return;
       }

       /* Prevent another ESD check,when ESD recovery is underway */
       if (atomic_read(&panel->esd_recovery_pending)) {
               dev_err(dev, "No esd checking when ESD recovery is underway!\n");
               return;
       }

       read_info = &esd->read_info;
       ret = jlq_panel_read_info(panel, read_info);
       if (ret) {
               dev_err(dev, "failed to read esd regs and jump to esd recovery!\n");
               goto esd_recovery;
       }

       ret = jlq_panel_validate_read_info(panel, read_info);
       if (ret) {
               dev_err(dev, "failed to validate esd regs and jump to esd recovery!\n");
               goto esd_recovery;
       }

       schedule_delayed_work(&esd->status_work,
               msecs_to_jiffies(esd->status_interval));

       return;
esd_recovery:
       jlq_panel_esd_recovery(panel);
}

static int jlq_panel_parse_esd_reg_read_info(struct jlq_panel *panel)
{
       u32 val;
       int rc = 0;
       struct device *dev = &panel->dsi->dev;
       struct panel_esd_info *esd_info = &panel->esd_info;
       struct panel_read_info *read_info = &esd_info->read_info;

       rc = jlq_panel_parse_dt_read_info(panel, read_info, "esd-entries");
       if (rc) {
               dev_err(dev, "failed to parse esd read info: %d\n", rc);
               return rc;
       }

       esd_info->status_interval = STATUS_CHECK_INTERVAL_MS;
       if (!of_property_read_u32(dev->of_node, "esd-check-period", &val)) {
               if ( val > STATUS_CHECK_INTERVAL_MS)
                       esd_info->status_interval = val;
               else {
                       dev_err(dev, "specified esd interval %u is less than %d\n",
                               val, STATUS_CHECK_INTERVAL_MS);
               }
       }

       INIT_DELAYED_WORK(&esd_info->status_work,
                       jlq_panel_esd_status_work);

       return 0;
}

static void jlq_panel_esd_irq_enable(struct jlq_panel *panel, bool enable)
{
       struct irq_desc *desc;
       struct device *dev = &panel->dsi->dev;
       struct panel_esd_info *esd_info = &panel->esd_info;

       if (enable == esd_info->esd_err_irq_enabled) {
               dev_dbg(dev, "request esd irq state has been setted\n");
               return;
       }

       if (enable) {
               enable_irq(esd_info->esd_err_irq);
               esd_info->esd_err_irq_enabled = true;
       } else {
               disable_irq(esd_info->esd_err_irq);
               esd_info->esd_err_irq_enabled = false;
       }

       desc = irq_to_desc(esd_info->esd_err_irq);
       dev_err(dev, "enable=%d, desc->depth=%d\n", enable, desc->depth);
}

static irqreturn_t jlq_panel_esd_err_irq_handle(int irq, void *data)
{
       struct panel_esd_info *esd_info;
       struct jlq_panel *panel = data;

       if (!panel) {
               pr_err("%s, panel is null\n", __func__);
               return IRQ_HANDLED;
       }

       dev_err(&panel->dsi->dev, "%s\n", __func__);

       esd_info = &panel->esd_info;
       disable_irq_nosync(esd_info->esd_err_irq);
       esd_info->esd_err_irq_enabled = false;

       jlq_panel_esd_recovery(panel);

       return IRQ_HANDLED;
}

static int jlq_panel_parse_esd_err_irq_info(struct jlq_panel *panel)
{
       int rc = 0;
       struct device *dev = &panel->dsi->dev;
       struct panel_esd_info *esd_info = &panel->esd_info;

       /* esd-err-flag method will be prefered */
       esd_info->esd_err_irq_gpio = of_get_named_gpio(
                       dev->of_node, "esd-err-irq-gpio", 0);

       if (gpio_is_valid(esd_info->esd_err_irq_gpio)) {
               rc = gpio_request(esd_info->esd_err_irq_gpio, "esd_err_irq_gpio");
               if (!rc)
                       gpio_direction_input(esd_info->esd_err_irq_gpio);
               else {
                       dev_err(dev, "%s: Failed to request esd irq gpio %d (code: %d)",
                               __func__, esd_info->esd_err_irq_gpio, rc);
                       goto error;
               }
       } else {
               dev_err(dev, "%s: Failed to get esd-err-irq-gpio", __func__);
               rc = -EINVAL;
               goto error;
       }

       esd_info->esd_err_irq = gpio_to_irq(esd_info->esd_err_irq_gpio);
       rc = devm_request_irq(dev, esd_info->esd_err_irq,
                       jlq_panel_esd_err_irq_handle, 0, "panel_esd", panel);
       if (rc) {
               rc = -EIO;
               dev_err(dev, "request panel esd irq failed\n");
               goto error;
       }

       return 0;
error:
       esd_info->esd_err_irq = -EINVAL;
       return rc;
}

static int jlq_panel_parse_esd_info(struct jlq_panel *panel)
{
       int rc = 0;
       const char *string;
       struct device *dev = &panel->dsi->dev;
       struct panel_esd_info *esd_info = &panel->esd_info;

       atomic_set(&panel->esd_recovery_pending, 0);

       rc = of_property_read_string(dev->of_node, "esd-check-mode", &string);
       if (!rc) {
               if (!strcmp(string, "reg_read")) {
                       esd_info->mode = ESD_MODE_REG_READ;
               } else if (!strcmp(string, "err_irq")) {
                       esd_info->mode = ESD_MODE_ERROR_IRQ;
               } else {
                       dev_err(dev, "No valid esd-check-mode string\n");
                       rc = -EINVAL;
                       goto error;
               }
       } else {
               dev_err(dev, "failed to read esd-check-mode!\n");
               rc = 0;
               goto error;
       }

       if (esd_info->mode == ESD_MODE_REG_READ)
               rc = jlq_panel_parse_esd_reg_read_info(panel);
       else
               rc = jlq_panel_parse_esd_err_irq_info(panel);

       if (rc) {
               dev_err(dev, "failed to parse esd info: %d\n", rc);
               goto error;
       }

       dev_err(dev, "ESD enabled with mode: %s\n", string);

       return 0;
error:
       esd_info->mode = ESD_MODE_NONE;
       return rc;
}

static void jlq_panel_free_esd_resource(struct jlq_panel *panel)
{
       struct device *dev = &panel->dsi->dev;
       struct panel_esd_info *esd_info = &panel->esd_info;

       if (esd_info->mode == ESD_MODE_NONE)
               return;

       if (esd_info->mode == ESD_MODE_REG_READ)
               jlq_panel_free_read_info(panel, &esd_info->read_info);
       else
               devm_free_irq(dev, esd_info->esd_err_irq, panel);

       return;
}

static void jlq_panel_esd_enable(struct jlq_panel *panel)
{
       struct device *dev = &panel->dsi->dev;
       struct panel_esd_info *esd_info = &panel->esd_info;

       if (esd_info->mode == ESD_MODE_NONE)
               return;

       dev_err(dev, "%s\n", __func__);

       if (esd_info->mode == ESD_MODE_REG_READ) {
               /* Schedule ESD status check */
               schedule_delayed_work(&esd_info->status_work,
                       msecs_to_jiffies(esd_info->status_interval));
       } else
               jlq_panel_esd_irq_enable(panel, true);
}

static void jlq_panel_esd_disable(struct jlq_panel *panel)
{
       struct device *dev = &panel->dsi->dev;
       struct panel_esd_info *esd_info = &panel->esd_info;

       if (esd_info->mode == ESD_MODE_NONE)
               return;

       if (atomic_read(&panel->esd_recovery_pending))
               return;

       dev_err(dev, "%s\n", __func__);

       /* Cancel any pending ESD status check */
       if (esd_info->mode == ESD_MODE_REG_READ)
               cancel_delayed_work_sync(&esd_info->status_work);
       else
               jlq_panel_esd_irq_enable(panel, false);
}

static int jlq_panel_parse_dt(struct jlq_panel *panel)
{
	u32 val = 0;
	int err;
	const char *inited_str;
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;
	struct panel_desc *desc = panel->desc;

	if (of_property_read_string(dev->of_node, "panel_name", &panel->name))
		dev_err(dev, "failed to get panel name\n");

	if (!of_property_read_u32(dev->of_node, "bus-format", &val))
		desc->bus_format = val;

	if (!of_property_read_u32(dev->of_node, "bpc", &val))
		desc->bpc = val;

	if (!of_property_read_u32(dev->of_node, "prepare-delay-ms", &val))
		desc->delay.prepare = val;

	if (!of_property_read_u32(dev->of_node, "enable-delay-ms", &val))
		desc->delay.enable = val;

	if (!of_property_read_u32(dev->of_node, "disable-delay-ms", &val))
		desc->delay.disable = val;

	if (!of_property_read_u32(dev->of_node, "unprepare-delay-ms", &val))
		desc->delay.unprepare = val;

	if (!of_property_read_u32(dev->of_node, "reset-delay-ms", &val))
		desc->delay.reset = val;

	if (!of_property_read_u32(dev->of_node, "init-delay-ms", &val))
		desc->delay.init = val;

	if (!of_property_read_u32(dev->of_node, "bias-delay-ms", &val))
		desc->delay.bias = val;

	if (!of_property_read_u32(dev->of_node, "width-mm", &val))
		desc->size.width = val;

	if (!of_property_read_u32(dev->of_node, "height-mm", &val))
		desc->size.height = val;

	if (!of_property_read_u32(dev->of_node, "dsi,flags", &val))
		dsi->mode_flags = val;
	else {
		dev_err(dev, "invalid property: dsi,flags\n");
		return -EINVAL;
	}

	if (!of_property_read_u32(dev->of_node, "dsi,format", &val))
		dsi->format = val;
	else {
		dev_err(dev, "invalid property: dsi,format\n");
		return -EINVAL;
	}

	if (!of_property_read_u32(dev->of_node, "dsi,lanes", &val))
		dsi->lanes = val;
	else {
		dev_err(dev, "invalid property: dsi,lanes\n");
		return -EINVAL;
	}

	if (!of_property_read_u32(dev->of_node, "ic-vendor", &val))
		panel->ic_vendor = val;
	else
		panel->ic_vendor = PANEL_IC_NONE;

	if (!of_property_read_u32(dev->of_node, "backlight-type", &val))
		panel->bl_type = val;
	else
		panel->bl_type = BACKLIGHT_TYPE_PWM;

	if (!of_property_read_u32(dev->of_node, "backlight-max-level", &val))
		panel->bl_max_level = val;
	else
		panel->bl_max_level = 255;

	val = roundup_pow_of_two(panel->bl_max_level);
	panel->bl_order = ilog2(val);

	if (!of_property_read_u32(dev->of_node, "backlight-min-level", &val))
		panel->bl_min_level = val;
	else
		panel->bl_min_level = 0;

	panel->cabc_support = of_property_read_bool(dev->of_node, "cabc-support");
  
	if (panel->cabc_support)
		panel->cabc_mode = CABC_UI_MODE;
	else
		panel->cabc_mode = CABC_OFF;

	panel->hbm_info.percent = 100;
	panel->hbm_info.support = of_property_read_bool(dev->of_node, "hbm-support");
	if (panel->hbm_info.support) {
		if (!of_property_read_u32(dev->of_node, "hbm-percent", &val)) {
			if ( val > 0 && val < 100)
				panel->hbm_info.percent = val;
		}
	}

	err = jlq_panel_get_cmds(panel);
	if (err) {
		dev_err(dev, "failed to get init cmd: %d\n", err);
		return err;
	}

	err = jlq_panel_parse_dt_read_info(panel, &panel->lockdown_info, "lockdown-entries");
	if (err) {
		dev_err(dev, "failed to get lockdown info: %d\n", err);
		goto err_free_cmds;
	}

	err = jlq_panel_parse_dt_read_info(panel, &panel->whitepoint_info.read_info,
				"whitepoint-entries");
	if (err) {
		dev_err(dev, "failed to get whitepoint info: %d\n", err);
		goto err_free_lockdown;
	}

       err = jlq_panel_parse_esd_info(panel);
       if (err) {
               dev_err(dev, "failed to get esd info: %d\n", err);
               goto err_free_whitepoint;
       }


	err = jlq_panel_get_power_info(panel);
	if (err) {
		dev_err(dev, "failed to get power info: %d\n", err);
//		goto err_free_whitepoint;
                goto err_free_esd;
	}

	panel->inited_in_bootloader = false;
	if (!of_property_read_string(dev->of_node, "inited", &inited_str)) {
		if (!strcmp(inited_str, "true"))
			panel->inited_in_bootloader = true;
	}

	if (panel->inited_in_bootloader) {
		panel->enabled  = true;
		panel->prepared = true;
		err  = jlq_panel_power_enable(panel, true);
		if (err)
			goto err_power_put;
	} else {
		panel->enabled  = false;
		panel->prepared = false;
	}

	err = jlq_panel_parse_pinctrl(panel);
	if (err) {
		dev_err(dev, "failed to set pinctrl state\n");
		goto err_disable_power;
	}

	err = jlq_panel_get_gpios(panel);
	if (err) {
		dev_err(dev, "failed to get gpio info: %d\n", err);
		goto err_put_pinctl;
	}

	err = jlq_panel_parse_reset_info(panel);
	if (err) {
		dev_err(dev, "failed to parse reset info: %d\n", err);
		goto err_free_gpios;
	}

	if (panel->bl_type == BACKLIGHT_TYPE_PWM) {
		panel->bl_pwm = devm_of_pwm_get(dev, dev->of_node, "backlight");
		if (IS_ERR_OR_NULL(panel->bl_pwm)) {
			pr_err("%s get pwm chip failed\n", __func__);
			err = -ENODEV;
			goto err_free_gpios;
		}
	}

	panel->bl_led.name = "lcd-backlight";
	panel->bl_led.brightness = LED_HALF;
	panel->bl_led.brightness_set = jlq_panel_set_brightness;
	panel->bl_led.max_brightness = panel->bl_max_level;
	err = led_classdev_register(dev, &panel->bl_led);
	if (err) {
		dev_err(dev, "Can't register led class device\n");
		goto err_put_pwm;
	}
	return 0;
err_put_pwm:
	if (panel->bl_pwm)
		devm_pwm_put(dev, panel->bl_pwm);
err_free_gpios:
	jlq_panel_free_gpios(panel);
err_put_pinctl:
	jlq_panel_pinctrl_put(panel);
err_disable_power:
	if (panel->inited_in_bootloader)
		jlq_panel_power_enable(panel, false);
err_power_put:
	jlq_panel_power_put(panel);
err_free_esd:
        jlq_panel_free_esd_resource(panel);
err_free_whitepoint:
	jlq_panel_free_read_info(panel, &panel->whitepoint_info.read_info);
err_free_lockdown:
	jlq_panel_free_read_info(panel, &panel->lockdown_info);
err_free_cmds:
	jlq_panel_cmds_cleanup(panel);
	return err;
}

static int jlq_panel_trigger_esd_attack(struct jlq_panel *panel)
{
       struct device *dev = &panel->dsi->dev;

       if (gpio_is_valid(panel->reset_gpio)) {
               gpio_set_value(panel->reset_gpio, 0);
               dev_err(dev, "GPIO pulled low to simulate ESD\n");
               return 0;
       }

       dev_err(dev, "failed to pull down reset gpio\n");

       return -EINVAL;
}

static ssize_t jlq_panel_esd_trigger_store(struct device *dev,
               struct device_attribute *attr,
               const char *buf, size_t count)
{
       int rc;
       u32 esd_trigger;
       struct panel_esd_info *esd_info;
       struct jlq_panel *panel = dev_get_drvdata(dev);

       esd_info = &panel->esd_info;
       if (esd_info->mode == ESD_MODE_NONE) {
               dev_err(dev, "%s, ESD feature is not enabled\n", __func__);
               return -EINVAL;
       }

       if (!panel->enabled) {
               dev_err(dev, "No triggering when panel is disabled\n");
               return -EINVAL;
       }

       if (atomic_read(&panel->esd_recovery_pending)) {
               dev_err(dev, "No triggering when panel is doing esd recovery\n");
               return -EINVAL;
       }

       rc = kstrtouint(buf, 10, &esd_trigger);
       if (rc) {
               dev_err(dev, "esd_trigger parameter is not a number\n");
               goto error;
       }

       if (esd_trigger != 1) {
               rc = -EINVAL;
               goto error;
       }

       rc = jlq_panel_trigger_esd_attack(panel);
       if (rc) {
               dev_err(dev, "Failed to trigger ESD attack\n");
               goto error;
       }

       return count;
error:
       return rc;
}

static DEVICE_ATTR(esd_trigger, S_IWUSR, NULL, jlq_panel_esd_trigger_store);

static ssize_t jlq_panel_esd_mode_show(struct device *dev,
               struct device_attribute *attr, char *buf)
{
       int rc;
       struct panel_esd_info *esd_info;
       struct jlq_panel *panel = dev_get_drvdata(dev);

       esd_info = &panel->esd_info;
       switch (esd_info->mode) {
       case ESD_MODE_NONE:
               rc = snprintf(buf, ESD_MODE_STRING_MAX_LEN, "none");
               break;
       case ESD_MODE_REG_READ:
               rc = snprintf(buf, ESD_MODE_STRING_MAX_LEN, "reg_read");
               break;
       case ESD_MODE_ERROR_IRQ:
               rc = snprintf(buf, ESD_MODE_STRING_MAX_LEN, "error_irq");
               break;
       default:
               rc = snprintf(buf, ESD_MODE_STRING_MAX_LEN, "invalid");
               break;
       }

       return rc;
}

static DEVICE_ATTR(esd_mode, S_IRUGO, jlq_panel_esd_mode_show, NULL);


static ssize_t jlq_panel_cabc_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct jlq_panel *panel = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", panel->cabc_mode);
}

static ssize_t jlq_panel_cabc_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret = 0;
	unsigned int val = 0;
	u8 cabc_payload[2];
	struct jlq_panel *panel = dev_get_drvdata(dev);

	if (!panel->cabc_support) {
		dev_err(dev, "cabc not supported\n");
		goto cabc_ret;
	}

	if (!panel->enabled) {
		dev_err(dev, "can't change cabc mode when panel disabled\n");
		goto cabc_ret;
	}

	mutex_lock(&panel->panel_lock);
	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		dev_err(dev, "cabc parameter is not a number\n");
		goto cabc_ret;
	}

	switch(val) {
		case CABC_OFF:
		case CABC_UI_MODE:
		case CABC_STILL_MODE:
		case CABC_MOVING_MODE:
			break;
		default:
			dev_err(dev, "cabc mode %d is not supportted\n", val);
			goto cabc_ret;
	}

	if (panel->cabc_mode != val) {
		if (panel->mipi_pre_cmds) {
			ret = jlq_panel_dsi_cmds_write(panel, panel->mipi_pre_cmds);
			if (ret) {
				dev_err(dev, "failed to send mipi_pre_cmds\n");
				goto cabc_ret;
			}
		}

		cabc_payload[0] = 0x55;
		cabc_payload[1] = val;
		ret = mipi_dsi_generic_write(panel->dsi, cabc_payload, 2);
		if (ret < 0)
			dev_err(dev, "failed to set cabc mode %d\n", val);
		else
			panel->cabc_mode = val;
	}

cabc_ret:
	mutex_unlock(&panel->panel_lock);
	return snprintf((char *)buf, count, "%d\n", panel->cabc_mode);
}

static DEVICE_ATTR(lcd_cabc_mode, attr_mode, jlq_panel_cabc_mode_show, jlq_panel_cabc_mode_store);

static ssize_t jlq_panel_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct jlq_panel *panel = dev_get_drvdata(dev);

	if (panel->name)
		return snprintf(buf, PAGE_SIZE, "%s\n", panel->name);
	else
		return snprintf(buf, PAGE_SIZE, "panel name is null, please check\n");
}
static DEVICE_ATTR(lcd_panel_name, S_IRUGO, jlq_panel_name_show, NULL);

static ssize_t jlq_panel_mipi_reg_store(struct device *device,
                              struct device_attribute *attr,
                              const char *buf, size_t count)
{
	int retval = 0;
	char *token, *input_copy, *input_dup = NULL;
	const char *delim = " ";
	char *buffer = NULL;
	int buf_size = 0;
	u32 tmp_data = 0;
	struct panel_cmds mipi_cmds;
	struct jlq_panel *panel = dev_get_drvdata(device);

	if (!panel || !panel->enabled) {
		pr_err("[LCD] panel not ready!\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);

	pr_debug("input buffer:{%s}\n", buf);

	input_copy = kstrdup(buf, GFP_KERNEL);
	if (!input_copy) {
		retval = -ENOMEM;
		goto exit_unlock;
	}

	input_dup = input_copy;
	/* removes leading and trailing whitespace from input_copy */
	input_copy = strim(input_copy);

	/* Split a string into token */
	token = strsep(&input_copy, delim);
	if (token) {
		retval = kstrtoint(token, 10, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free0;
		}

		g_panel_read_cfg.enabled= !!tmp_data;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	token = strsep(&input_copy, delim);
	if (token) {
		retval = kstrtoint(token, 10, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free0;
		}

		if (tmp_data > sizeof(g_panel_read_cfg.rbuf)) {
			pr_err("read size exceeding the limit %d\n",
			sizeof(g_panel_read_cfg.rbuf));
			goto exit_free0;
		}

		g_panel_read_cfg.cmds_rlen = tmp_data;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	buffer = kzalloc(strlen(input_copy), GFP_KERNEL);
	if (!buffer) {
		retval = -ENOMEM;
		goto exit_free0;
	}

	token = strsep(&input_copy, delim);
	while (token) {
		retval = kstrtoint(token, 16, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free1;
		}
		pr_debug("[lzl-test]buffer[%d] = 0x%02x\n", buf_size, tmp_data);
		buffer[buf_size++] = (tmp_data & 0xff);
		/* Removes leading whitespace from input_copy */
		if (input_copy) {
			input_copy = skip_spaces(input_copy);
			token = strsep(&input_copy, delim);
		} else {
			token = NULL;
		}
	}

	retval = jlq_panel_parse_cmds(device, buffer, buf_size, &mipi_cmds);
	if (retval) {
		pr_err("parse mipi_cmds failed!\n");
		goto exit_free1;
	}

	if (g_panel_read_cfg.enabled) {
		retval = jlq_panel_dsi_cmds_read(panel, &mipi_cmds,
		g_panel_read_cfg.rbuf,
		g_panel_read_cfg.cmds_rlen);
		if (retval) {
			pr_err("[%s]failed to read cmds, rc=%d\n", __func__, retval);
			goto exit_free2;
		}
	} else {
		retval = jlq_panel_dsi_cmds_write(panel, &mipi_cmds);
		if (retval) {
			pr_err("[%s] failed to send cmds, rc=%d\n", __func__, retval);
			goto exit_free2;
		}
	}

	pr_debug("[%s]: done!\n", __func__);
	retval = count;

exit_free2:
	kfree(mipi_cmds.buf);
	kfree(mipi_cmds.cmds);
exit_free1:
	kfree(buffer);
exit_free0:
	kfree(input_dup);
exit_unlock:
	mutex_unlock(&panel->panel_lock);
return retval;
}

static ssize_t jlq_panel_mipi_reg_show(struct device *device,
                              struct device_attribute *attr,
                              char *buf)
{
	int i = 0;
	ssize_t count = 0;
	struct jlq_panel *panel = dev_get_drvdata(device);

	if (!panel || !panel->enabled) {
		pr_err("[LCD] panel not ready!\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);
	if (g_panel_read_cfg.enabled) {
		for (i = 0; i < g_panel_read_cfg.cmds_rlen; i++) {
			if (i == g_panel_read_cfg.cmds_rlen - 1) {
				count += snprintf(buf + count, PAGE_SIZE - count, "0x%02x\n",
				g_panel_read_cfg.rbuf[i]);
			} else {
				count += snprintf(buf + count, PAGE_SIZE - count, "0x%02x ",
				g_panel_read_cfg.rbuf[i]);
			}
		}
	}
	mutex_unlock(&panel->panel_lock);

	return count;
}

static DEVICE_ATTR(mipi_reg, attr_mode, jlq_panel_mipi_reg_show, jlq_panel_mipi_reg_store);

static ssize_t jlq_panel_hbm_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	u32 bl_hbm_off;
	int ret = 0;
	unsigned int val = 0;
	struct panel_cmds *hbm_cmds = NULL;
	struct jlq_panel *panel = dev_get_drvdata(dev);

	if (!panel->hbm_info.support) {
		dev_err(dev, "hbm mode is not supported\n");
		return -EINVAL;
	}

	if (!panel->enabled) {
		dev_err(dev, "can't change hbm mode when panel disabled\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);
	ret = kstrtouint(buf, 10, &val);
	if (ret) {
		dev_err(dev, "hbm parameter is invalid\n");
		goto hbm_ret;
	}

	dev_err(dev, "%s, set hbm mode:%u\n", __func__, val);

	if (val)
		hbm_cmds = panel->hbm_info.on_cmds;
	else {
		bl_hbm_off = panel->bl_max_level * panel->hbm_info.percent / 100;
		if (panel->bl_cur_level < bl_hbm_off) {
			dev_err(dev, "skip hbm off when cur_bl less than bl_hbm_off\n");
			ret = count;
			goto hbm_ret;
		}

		hbm_cmds = panel->hbm_info.off_cmds;
	}

	ret = jlq_panel_dsi_cmds_write(panel, hbm_cmds);
	if (ret) {
		dev_err(dev, "failed to send hbm_cmds\n");
		ret = -EINVAL;
		goto hbm_ret;
	}

	ret = count;

hbm_ret:
	mutex_unlock(&panel->panel_lock);
	return ret;
}

static DEVICE_ATTR(lcd_hbm_mode, S_IWUSR, NULL, jlq_panel_hbm_mode_store);

static struct attribute *jlq_panel_attrs[] = {
	&dev_attr_lcd_lockdown_info.attr,
	&dev_attr_lcd_whitepoint_info.attr,
	&dev_attr_lcd_cabc_mode.attr,
	&dev_attr_lcd_panel_name.attr,
	&dev_attr_lcd_hbm_mode.attr,
	&dev_attr_mipi_reg.attr,
        &dev_attr_esd_trigger.attr,
        &dev_attr_esd_mode.attr,
	NULL,
};

static struct attribute_group jlq_panel_attr_group = {
	.attrs = jlq_panel_attrs,
};

static int jlq_panel_probe(struct mipi_dsi_device *dsi)
{
	int err;
	struct jlq_panel *panel;
	const struct of_device_id *id;
	const struct panel_desc_dsi *desc;
	const struct panel_desc *pdesc;
	struct device *dev = &dsi->dev;

	dev_info(dev, "%s enter\n", __func__);

	id = of_match_node(jlq_of_match, dsi->dev.of_node);
	if (!id)
		return -ENODEV;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	panel->dsi = dsi;

	desc = id->data;
	if (desc) {
		dsi->mode_flags = desc->flags;
		dsi->format = desc->format;
		dsi->lanes = desc->lanes;
		pdesc = &desc->desc;
		panel->desc = devm_kmemdup(dev, pdesc,
				sizeof(*pdesc), GFP_KERNEL);
	} else {
		pdesc = NULL;
		panel->desc = devm_kzalloc(dev, sizeof(*pdesc), GFP_KERNEL);
	}

	err = jlq_panel_parse_dt(panel);
	if (err < 0)
		return err;

	drm_panel_init(&panel->base);
	panel->base.dev = dev;
	panel->base.funcs = &jlq_panel_funcs;
	mutex_init(&panel->panel_lock);

	err = drm_panel_add(&panel->base);
	if (err < 0)
		return err;

	err = sysfs_create_group(&dev->kobj, &jlq_panel_attr_group);
	if (err) {
		dev_err(dev, "failed to create sysfs for panel\n");
		return err;
	}
#if IS_ENABLED(CONFIG_JGKI)  
	g_panel  = panel;
#endif          
	dev_set_drvdata(dev, panel);

	return mipi_dsi_attach(dsi);
}

static int jlq_panel_remove(struct mipi_dsi_device *dsi)
{
	struct jlq_panel *jlq = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n",
			ret);

	drm_panel_detach(&jlq->base);
	drm_panel_remove(&jlq->base);

        jlq_panel_free_esd_resource(jlq);

	jlq_panel_free_read_info(jlq, &jlq->lockdown_info);

	jlq_panel_free_read_info(jlq, &jlq->whitepoint_info.read_info);

	jlq_panel_cmds_cleanup(jlq);

	return 0;
}

static void jlq_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct jlq_panel *jlq = mipi_dsi_get_drvdata(dsi);

	jlq_panel_disable(&jlq->base);
	jlq_panel_unprepare(&jlq->base);
}

static struct mipi_dsi_driver jlq_panel_driver = {
	.driver = {
		.name = "jlq-dsi-panel",
		.of_match_table = jlq_of_match,
	},
	.probe = jlq_panel_probe,
	.remove = jlq_panel_remove,
};
module_mipi_dsi_driver(jlq_panel_driver);

MODULE_DESCRIPTION("JLQ DSI PANEL DRIVER");
MODULE_LICENSE("GPL v2");
