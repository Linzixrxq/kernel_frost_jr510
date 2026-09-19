// SPDX-License-Identifier: GPL-2.0
/*
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
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/pm_runtime.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/component.h>
#include <drm/drm_device.h>
#include <uapi/drm/camille_ioctl.h>
#include "camille_regs.h"
#include "camille_cltm_adj_curve.h"

#define CAMILLE_NAME "camille"

#define CAMILLE_ERROR   0x01
#define CAMILLE_DEBUG   0x02
#define CAMILLE_VERBOSE   0x04

static unsigned int camille_log_level;

static inline struct camille_dev *misc_to_camille(struct miscdevice *misc)
{
	return container_of(misc, struct camille_dev, miscdev);
}

static void camille_dbg(unsigned int category, const char *format, ...)
{
    struct va_format vaf;
    va_list args;

    if (!(camille_log_level & category))
        return;

    va_start(args, format);
    vaf.fmt = format;
    vaf.va = &args;

    printk(KERN_ERR"[camille] %pV", &vaf);

    va_end(args);
}

static int
camille_mode_set(struct ad_coprocessor *ad, struct drm_display_mode *mode)
{
	struct camille_dev *camille_dev = dev_get_drvdata(ad->dev);

	dev_info(ad->dev, "%s.\n", __func__);

	if ((mode->crtc_hdisplay % 2) || (mode->crtc_vdisplay % 2)) {
		dev_err(ad->dev, "display size is not even number.\n");
		return -EINVAL;
	}

	if ((mode->crtc_hdisplay < HACTIVE_MIN) ||
			(mode->crtc_vdisplay < VACTIVE_MIN)) {
		dev_err(ad->dev, "display size must be bigger than 240.\n");
		return -EINVAL;
	}

	camille_dev->cltm_image.width = mode->crtc_hdisplay;
	camille_dev->cltm_image.height = mode->crtc_vdisplay;

	return 0;
}

static int
camille_get_all_tm_status(struct camille_dev *camille_dev)
{
	u32 val;
	int ret = 0;

	val = camille_read(camille_dev, ALL_TM_STATUS1);
	if (val & 0x1f) {
		if (val & 0x1)
			camille_dbg(CAMILLE_ERROR, "adj_ratio_neg_1024_err.\n");

		if (val & 0x2)
			camille_dbg(CAMILLE_ERROR, "block_size_err.\n");

		if (val & 0x4)
			camille_dbg(CAMILLE_ERROR, "block_num_err.\n");

		if (val & 0x8)
			camille_dbg(CAMILLE_ERROR, "image_size_err.\n");

		if (val & 0x10)
			camille_dbg(CAMILLE_ERROR, "strength_step_err.\n");

		ret = -EINVAL;
	}

	return ret;
}

static irqreturn_t camille_interrupt_handler(int irq, void *dev_id)
{
	u32 val;
	struct camille_dev *camille_dev = dev_id;

	val = camille_read(camille_dev, ALL_TM_IRQ_STATUS);
	if (val & 0xff) {
		if (val & 0x1) {
			camille_dbg(CAMILLE_DEBUG, "shadow_update_ok.\n");
			spin_lock(&camille_dev->shadow_lock);
			camille_dev->shadow_update_completed = true;
			camille_write(camille_dev, ALL_TM_CTRL3, 0x0);
			spin_unlock(&camille_dev->shadow_lock);
		}

		if (val & 0x2)
			camille_dbg(CAMILLE_ERROR, "tm_disable_ok.\n");

		if (val & 0x4)
			camille_dbg(CAMILLE_ERROR, "tm_underrun.\n");

		if (val & 0x8)
			camille_dbg(CAMILLE_VERBOSE, "cltm_cdf_ok.\n");

		if (val & 0x10)
			camille_dbg(CAMILLE_VERBOSE, "cltm_hist_ok.\n");

		if (val & 0x20)
			camille_dbg(CAMILLE_VERBOSE, "cltm_smooth_ok.\n");

		if (val & 0x40)
			camille_dbg(CAMILLE_VERBOSE, "tm_mapping_ok.\n");

		if (val & 0x80) {
			u32 temp;

			camille_dbg(CAMILLE_ERROR, "pixel_too_early_err.\n");

			temp = camille_read(camille_dev, LTM_STATUS1);
			camille_dbg(CAMILLE_ERROR, "LTM_STATUS1=0x%x\n", temp);

			temp = camille_read(camille_dev, LTM_STATUS2);
			camille_dbg(CAMILLE_ERROR, "LTM_STATUS2=0x%x\n", temp);

			temp = camille_read(camille_dev, LTM_STATUS3);
			camille_dbg(CAMILLE_ERROR, "LTM_STATUS3=0x%x\n", temp);
		}

		camille_write(camille_dev, ALL_TM_IRQ_RAW, val);
	}

	return IRQ_HANDLED;
}

static int camille_tm_clear(struct camille_dev *camille)
{
	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	camille_write(camille, ALL_TM_CTRL2, 1);

	return 0;
}

static int camille_interrupt_init(struct camille_dev *camille, u8 mask)
{
	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	camille_write(camille, ALL_TM_IRQ_MASK, mask);

	return 0;
}

static int
camille_all_tm_ctrl_init(struct camille_dev *camille)
{
	u32 val;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	val = camille->all_tm_ctrl.tm_output_mux;
	val |= camille->all_tm_ctrl.cltm_input_mux << 2;
	val |= camille->all_tm_ctrl.agtm_input_mux << 3;
	val |= camille->all_tm_ctrl.agtm_enable << 4;
	val |= camille->all_tm_ctrl.cltm_enable << 5;
	val |= camille->all_tm_ctrl.tm_enable << 6;

	camille_write(camille, ALL_TM_CTRL1, val);

	return 0;
}

/*
static void
camille_clear(struct camille_dev *camille_dev)
{
	pr_err("%s.\n", __func__);

	camille_write(camille_dev, ALL_TM_CTRL2, 0x1);
}
*/

static void
camille_shadow_update_force(struct camille_dev *camille, bool enable)
{
	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	if (enable)
		camille_write(camille, ALL_TM_CTRL3, 0x1);
	else
		camille_write(camille, ALL_TM_CTRL3, 0x0);
}

static void
camille_shadow_update_en(struct camille_dev *camille, bool enable)
{
	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	if (enable)
		camille_write(camille, ALL_TM_CTRL3, 0x2);
	else
		camille_write(camille, ALL_TM_CTRL3, 0x0);
}

static int
camille_cltm_ctrl_init(struct camille_dev *camille)
{
	u32 val;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	val =  camille->cltm_ctrl.hist_mode;
	camille_write(camille, LTM_CTRL1, val);

	val = camille->cltm_ctrl.curve_transition;
	camille_write(camille, LTM_CTRL2, val);

	return 0;
}

static int
camille_image_init(struct camille_dev *camille)
{
	u32 val;
	u32 img_width, img_height;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	img_width = camille->cltm_image.width;
	img_height = camille->cltm_image.height;

	val = img_width | (img_height << 16);
	camille_write(camille, LTM_IMAGE_SIZE, val);

	return 0;
}

static int
camille_block_init(struct camille_dev *camille)
{
	u32 val;
	u32 blk_width, blk_height;
	u32 blk_h_num, blk_w_num;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	blk_width = camille->cltm_block.width;
	blk_height = camille->cltm_block.height;
	blk_h_num = camille->cltm_block.h_num;
	blk_w_num = camille->cltm_block.w_num;

	val = blk_width | (blk_height << 16);
	camille_write(camille, LTM_BLOCK_SIZE, val);

	val = blk_w_num | (blk_h_num << 16);
	camille_write(camille, LTM_BLOCK_NUM, val);

	return 0;
}

static int
camille_cltm_linear_adj_init(struct camille_dev *camille)
{
	u32 val;
	int offset, ratio;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	offset =  camille->cltm_linear_adj.offset;
	if (offset < 0) {
		offset = -offset;
		offset = (~offset) + 1;
		offset = (offset & 0x7ff);
		offset = offset | (1 << 10);
	} else
		offset = offset & 0x7ff;

	ratio =  camille->cltm_linear_adj.ratio;
	if (ratio < 0) {
		ratio = -ratio;
		ratio = (~ratio) + 1;
		ratio = (ratio & 0x7ff);
		ratio = ratio | (1 << 10);
	} else
		ratio = ratio & 0x7ff;

	val = ratio | (offset << 16);
	camille_write(camille, LTM_LINEAR_ADJ, val);

	return 0;
}

static int
camille_cltm_strength_init(struct camille_dev *camille)
{
	u32 val;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	val =  camille->cltm_strength.interval;
	val |=  camille->cltm_strength.step << 8;
	val |=  camille->cltm_strength.target << 16;
	val |=  camille->cltm_strength.mode << 24;

	camille_write(camille, LTM_STRENGTH, val);

	return 0;
}

static int
camille_cltm_adj_curve_update(struct camille_dev *camille)
{
	u32 i, val;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	for (i = 0; i < 64; i += 2) {
		val = camille->cltm_adj_curve[i + 1] & 0xffff;
		val |= ((camille->cltm_adj_curve[i] & 0xffff) << 16);
		camille_write(camille, LTM_ADJ_CURVE0_ADDR + i * 2, val);
	}

	val = ((camille->cltm_adj_curve[64] & 0xffff) << 16);
	camille_write(camille, LTM_ADJ_CURVE0_ADDR + 0x80, val);

	return 0;
}

static int
camille_agtm_curve_update(struct camille_dev *camille)
{
	u32 i, val;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	for (i = 0; i < 256; i++) {
		val = 1 << 27;
		val |= i << 19;
		val |= (camille->agtm_curve[i] & 0x7ffff);
		camille_write(camille, AGTM_KRAM_WPORT, val);
	}

	val = 1 << 28;
	camille_write(camille, AGTM_KRAM_WPORT, val);

	return 0;
}

#if defined(CONFIG_CAMILLE_PWM)
static int
camille_pwm_init(struct camille_dev *camille)
{
	u32 clk_rate;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	clk_rate = clk_get_rate(camille->aclk);
	camille->pwm_ctrl.div_ratio = clk_rate / camille->pwm_ctrl.freq;
	if (clk_rate % camille->pwm_ctrl.freq)
		camille->pwm_ctrl.div_ratio += 1;

	camille_write(camille, PWM_CTRL1, camille->pwm_ctrl.div_ratio);
	camille_write(camille, PWM_CTRL0, 1);

	return 0;
}

static void
camille_pwm_config(struct camille_dev *camille, u32 duty)
{
	u32 val;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	if (duty > 255)
		duty = 255;

	val = camille->pwm_ctrl.div_ratio;
	camille->pwm_ctrl.duty_ratio = val * duty / 255;
	if (val * duty % 255)
		camille->pwm_ctrl.duty_ratio += 1;

	camille_write(camille, PWM_CTRL2, camille->pwm_ctrl.duty_ratio);
}

void camille_backlight_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct camille_dev *camille = dev_get_drvdata(dev);

	if (!camille->is_enabled) {
		pr_err("can't set pwm when camille is closed\n");
		return;
	}

	camille_pwm_config(camille, value);
	camille_shadow_update_force(camille, true);
}
#endif

static void camille_dump_regs(struct camille_dev *camille)
{
	u32 reg;

	if (camille->is_enabled == false) {
		camille_dbg(CAMILLE_ERROR, "no reading registers when camille is disabled.\n");
		return;
	}

	pr_err("camille dump regs\n");
	for (reg = 0x0; reg < 0x60;) {
		pr_err("reg[0x%04x]=0x%08x\n", reg,
			camille_read(camille, reg));
		reg += 4;
	}

	pr_err("camille dump cltm adj curve regs\n");
	for (reg = 0x200; reg < 0x284;) {
		pr_err("reg[0x%04x]=0x%08x\n", reg,
			camille_read(camille, reg));
		reg += 4;
	}
}

static int camille_enable(struct camille_dev *camille)
{
	int ret;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	if (camille->is_enabled == true) {
		camille_dbg(CAMILLE_ERROR, "camille has been already abled.\n");
		return 0;
	}

	camille->shadow_update_completed = false;
	clk_prepare_enable(camille->aclk);

	camille_tm_clear(camille);
	camille_interrupt_init(camille, 0);
	camille_image_init(camille);

	if (camille->all_tm_ctrl.cltm_enable) {
		camille_cltm_ctrl_init(camille);
		camille_block_init(camille);
		camille_cltm_linear_adj_init(camille);
		camille_cltm_strength_init(camille);
		camille_cltm_adj_curve_update(camille);
	}

	if (camille->all_tm_ctrl.agtm_enable)
		camille_agtm_curve_update(camille);

#if defined(CONFIG_CAMILLE_PWM)
	camille_pwm_init(camille);
#endif

	camille_all_tm_ctrl_init(camille);
	camille_shadow_update_force(camille, true);

	ret = camille_get_all_tm_status(camille);
	if (ret)
		return ret;

	enable_irq(camille->irq);
	camille->is_enabled = true;

	return 0;
}

static int camille_disable(struct camille_dev *camille)
{
	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	if (camille->is_enabled == false) {
		camille_dbg(CAMILLE_ERROR, "camille has been already disabled.\n");
		return 0;
	}

	camille_write(camille, ALL_TM_CTRL1, 0);
	camille_shadow_update_force(camille, true);
	//mdelay(20);
	disable_irq(camille->irq);
	clk_disable_unprepare(camille->aclk);
	camille->is_enabled = false;

	return 0;
}

struct ad_coprocessor_funcs camille_coprocessor_funcs = {
	.mode_set	= camille_mode_set,
};

static int camille_validate_data_flow(struct camille_dev *camille, struct camille_mode *mode)
{
	struct device *dev = camille->dev;

	if (mode->tm_mode == 0 || mode->tm_mode > 6) {
		dev_err(dev, "%s, invalid tm_mode parameters:%u.\n", __func__, mode->tm_mode);
		return -EINVAL;
	}

	return 0;
}

static int camille_parse_data_flow(struct camille_dev *camille, struct camille_mode *mode)
{
	struct device *dev = camille->dev;

	camille_dbg(CAMILLE_DEBUG, "camille tm_mode:%u\n", mode->tm_mode);

	switch (mode->tm_mode) {
	case 1:
		camille->all_tm_ctrl.tm_enable = 1;
		camille->all_tm_ctrl.cltm_enable = 1;
		camille->all_tm_ctrl.agtm_enable = 1;
		camille->all_tm_ctrl.agtm_input_mux = 1;
		camille->all_tm_ctrl.cltm_input_mux = 0;
		camille->all_tm_ctrl.tm_output_mux = 1;
		break;
	case 2:
		camille->all_tm_ctrl.tm_enable = 1;
		camille->all_tm_ctrl.cltm_enable = 1;
		camille->all_tm_ctrl.agtm_enable = 1;
		camille->all_tm_ctrl.agtm_input_mux = 0;
		camille->all_tm_ctrl.cltm_input_mux = 1;
		camille->all_tm_ctrl.tm_output_mux = 2;
		break;
	case 3:
		camille->all_tm_ctrl.tm_enable = 1;
		camille->all_tm_ctrl.cltm_enable = 1;
		camille->all_tm_ctrl.agtm_enable = 0;
		camille->all_tm_ctrl.agtm_input_mux = 0;
		camille->all_tm_ctrl.cltm_input_mux = 0;
		camille->all_tm_ctrl.tm_output_mux = 2;
		break;
	case 4:
		camille->all_tm_ctrl.tm_enable = 1;
		camille->all_tm_ctrl.cltm_enable = 0;
		camille->all_tm_ctrl.agtm_enable = 1;
		camille->all_tm_ctrl.agtm_input_mux = 0;
		camille->all_tm_ctrl.cltm_input_mux = 0;
		camille->all_tm_ctrl.tm_output_mux = 1;
		break;
	case 5:
		camille->all_tm_ctrl.tm_enable = 1;
		camille->all_tm_ctrl.cltm_enable = 0;
		camille->all_tm_ctrl.agtm_enable = 0;
		camille->all_tm_ctrl.agtm_input_mux = 0;
		camille->all_tm_ctrl.cltm_input_mux = 0;
		camille->all_tm_ctrl.tm_output_mux = 0;
		break;
	case 6:
		camille->all_tm_ctrl.tm_enable = 0;
		camille->all_tm_ctrl.cltm_enable = 0;
		camille->all_tm_ctrl.agtm_enable = 0;
		camille->all_tm_ctrl.agtm_input_mux = 0;
		camille->all_tm_ctrl.cltm_input_mux = 0;
		camille->all_tm_ctrl.tm_output_mux = 0;
	default:
		dev_err(dev, "%s, invalid tm_mode parameters:%u.\n", __func__, mode->tm_mode);
		return -EINVAL;
	}

	camille_dbg(CAMILLE_DEBUG, "camille data flow:%u\n", mode->tm_mode);

	camille_dbg(CAMILLE_DEBUG, "tm_enable:%u, cltm_enable:%u, agtm_enable:%u\n",
		camille->all_tm_ctrl.tm_enable,
		camille->all_tm_ctrl.cltm_enable,
		camille->all_tm_ctrl.agtm_enable);

	camille_dbg(CAMILLE_DEBUG, "agtm_input_mux:%u, cltm_input_mux:%u, tm_output_mux:%u\n",
		camille->all_tm_ctrl.agtm_input_mux,
		camille->all_tm_ctrl.cltm_input_mux,
		camille->all_tm_ctrl.tm_output_mux);

	return 0;
}

static int camille_validate_cltm_ctrl(struct camille_dev *camille, struct camille_mode *mode)
{
	struct device *dev = camille->dev;

	if (mode->hist_mode > 1) {
		dev_err(dev, "%s, invalid hist_mode parameters:%u.\n", __func__, mode->hist_mode);
		return -EINVAL;
	}

	if (mode->curve_transition > 8091) {
		dev_err(dev, "%s, invalid curve_transition parameters:%u.\n", __func__, mode->curve_transition);
		return -EINVAL;
	}

	return 0;
}

static int camille_parse_cltm_ctrl(struct camille_dev *camille, struct camille_mode *mode)
{
	camille->cltm_ctrl.hist_mode = mode->hist_mode;
	camille_dbg(CAMILLE_DEBUG, "camille hist_mode:%u\n", mode->hist_mode);

	camille->cltm_ctrl.curve_transition = mode->curve_transition;
	camille_dbg(CAMILLE_DEBUG, "cltm curve_transition:%u\n", mode->curve_transition);

	return 0;
}

static int camille_validate_cltm_block(struct camille_dev *camille, struct camille_mode *mode)
{
	struct device *dev = camille->dev;

	switch (mode->blk_width) {
	case 64:
	case 128:
	case 256:
		break;
	default:
		dev_err(dev, "%s, invalid blk_width parameters:%u.\n", __func__, mode->blk_width);
		return -EINVAL;
	}

	switch (mode->blk_height) {
	case 64:
	case 128:
	case 256:
		break;
	default:
		dev_err(dev, "%s, invalid blk_height parameters:%u.\n", __func__, mode->blk_height);
		return -EINVAL;
	}

	if (mode->blk_w_num == 0 || mode->blk_w_num  > 5) {
		dev_err(dev, "%s, invalid blk_w_num parameters:%u.\n", __func__, mode->blk_w_num);
		return -EINVAL;
	}

	if (mode->blk_h_num == 0 || mode->blk_h_num  > 10) {
		dev_err(dev, "%s, invalid blk_h_num parameters:%u.\n", __func__, mode->blk_h_num);
		return -EINVAL;
	}

	if (mode->blk_width * mode->blk_w_num < camille->cltm_image.width) {
		dev_err(dev, "cltm block width(%u) * w_num(%u) is smaller than hactive(%u).\n",
				mode->blk_width, mode->blk_w_num,
				camille->cltm_image.width);
		return -EINVAL;
	}


	if (mode->blk_height * mode->blk_h_num < camille->cltm_image.height) {
		dev_err(dev, "cltm block height(%u) * h_num(%u) is smaller than vactive(%u).\n",
				mode->blk_height,  mode->blk_h_num,
				camille->cltm_image.height);
		return -EINVAL;
	}

	return 0;
}

static int camille_parse_cltm_block(struct camille_dev *camille, struct camille_mode *mode)
{
	camille->cltm_block.width = mode->blk_width;
	camille->cltm_block.height = mode->blk_height;
	camille->cltm_block.w_num = mode->blk_w_num;
	camille->cltm_block.h_num = mode->blk_h_num;

	camille_dbg(CAMILLE_DEBUG, "cltm blk_width:%u, blk_height:%u, blk_w_num:%u, blk_h_num:%u\n",
			mode->blk_width, mode->blk_height, mode->blk_w_num, mode->blk_h_num);

	return 0;
}

static int camille_validate_cltm_linear_adj(struct camille_dev *camille, struct camille_mode *mode)
{
	struct device *dev = camille->dev;

	if (mode->linear_adj_offset > 1023 || mode->linear_adj_offset  < -1023) {
		dev_err(dev, "%s, invalid linear_adj_offset parameters:%d.\n",
				__func__, mode->linear_adj_offset);
		return -EINVAL;
	}

	if (mode->linear_adj_ratio > 1023 || mode->linear_adj_ratio  < -1023) {
		dev_err(dev, "%s, invalid linear_adj_ratio parameters:%d.\n",
				__func__, mode->linear_adj_ratio);
		return -EINVAL;
	}

	return 0;
}

static int camille_parse_cltm_linear_adj(struct camille_dev *camille, struct camille_mode *mode)
{

	camille->cltm_linear_adj.offset = mode->linear_adj_offset;
	camille_dbg(CAMILLE_DEBUG, "cltm linear_adj_offset:%d\n",
			mode->linear_adj_offset);

	camille->cltm_linear_adj.ratio = mode->linear_adj_ratio;
	camille_dbg(CAMILLE_DEBUG, "cltm linear_adj_ratio:%d\n",
			mode->linear_adj_ratio);

	return 0;
}

static int camille_validate_cltm_adj_curve(struct camille_dev *camille, struct camille_mode *mode)
{
	int i;
	struct device *dev = camille->dev;

	for (i = 0; i < 65; i++) {
		if (mode->hist_adj_curve[i] > 0xffff) {
			dev_err(dev, "%s, invalid cltm_adj_curve[%d] parameters:%u.\n",
					__func__, i, mode->hist_adj_curve[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int camille_parse_cltm_adj_curve(struct camille_dev *camille, struct camille_mode *mode)
{
	int i;

	for (i = 0; i < 65; i++) {
		camille->cltm_adj_curve[i] = mode->hist_adj_curve[i];
		camille_dbg(CAMILLE_DEBUG, "cltm_adj_curve[%d]:0x%x\n",
				i, mode->hist_adj_curve[i]);
	}

	return 0;
}

static int camille_validate_cltm_strength(struct camille_dev *camille, struct camille_mode *mode)
{
	struct device *dev = camille->dev;

	if (mode->strength_interval > 255) {
		dev_err(dev, "%s, invalid strength_interval parameters:%u.\n",
				__func__, mode->strength_interval);
		return -EINVAL;
	}

	if (mode->strength_step > 255) {
		dev_err(dev, "%s, invalid strength_step parameters:%u.\n",
				__func__, mode->strength_step);
		return -EINVAL;
	}

	if (mode->strength_target > 255) {
		dev_err(dev, "%s, invalid strength_target parameters:%u.\n",
				__func__, mode->strength_target);
		return -EINVAL;
	}

	if (mode->strength_mode > 1) {
		dev_err(dev, "%s, invalid strength_mode parameters:%u.\n",
				__func__, mode->strength_mode);
		return -EINVAL;
	}

	return 0;
}

static int camille_parse_cltm_strength(struct camille_dev *camille, struct camille_mode *mode)
{
	camille->cltm_strength.interval = mode->strength_interval;
	camille_dbg(CAMILLE_DEBUG, "cltm strength_interval:%u\n",
			mode->strength_interval);

	camille->cltm_strength.step = mode->strength_step;
	camille_dbg(CAMILLE_DEBUG, "cltm strength_step:%u\n",
			mode->strength_step);

	camille->cltm_strength.target = mode->strength_target;
	camille_dbg(CAMILLE_DEBUG, "cltm strength_target:%u\n",
			mode->strength_target);

	camille->cltm_strength.mode = mode->strength_mode;
	camille_dbg(CAMILLE_DEBUG, "cltm strength_mode:%u\n",
			mode->strength_mode);

	return 0;
}

static int camille_validate_agtm_curve(struct camille_dev *camille, struct camille_mode *mode)
{
	int i;
	struct device *dev = camille->dev;

	for (i = 0; i < 256; i++) {
		if (mode->agtm_curve[i] > 0x7ffff) {
			dev_err(dev, "%s, invalid agtm_curve[%d] parameters:%u.\n",
					__func__, i, mode->agtm_curve[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int camille_parse_agtm_curve(struct camille_dev *camille, struct camille_mode *mode)
{
	int i;

	for (i = 0; i < 256; i++) {
		camille->agtm_curve[i] = mode->agtm_curve[i];
		camille_dbg(CAMILLE_DEBUG, "agtm_curve[%d]:%u\n",
				i, mode->agtm_curve[i]);
	}

	return 0;
}

static void camille_parse_pwm(struct camille_dev *camille)
{
	u32 val = 0;
	int ret = 0;
	struct device *dev = camille->dev;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32(np, "pwm_frequency", &val);
	if (val == 0 || val > 260000000)
		val = 260000000;
	camille->pwm_ctrl.freq = val;
	camille_dbg(CAMILLE_DEBUG, "pwm_ctrl freq: %u\n", val);
}

static int camille_get_resources(struct camille_dev *camille)
{
	int ret = 0;
	struct device *dev = camille->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;

	camille->aclk = devm_clk_get(dev, "aclk");
	if (IS_ERR(camille->aclk)) {
		dev_err(dev, "failed to get aclk.\n");
		return PTR_ERR(camille->aclk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	camille->regs_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(camille->regs_base)) {
		dev_err(dev, "failed to ioremap regs base\n");
		return PTR_ERR(camille->regs_base);
	}

	camille->irq  = platform_get_irq_byname(pdev, "CAMILLE");
	if (camille->irq < 0) {
		dev_err(dev, "could not get IRQ number.\n");
		return camille->irq;
	}

	ret = devm_request_threaded_irq(dev, camille->irq, NULL,
			camille_interrupt_handler, IRQF_ONESHOT,
			"camille_irq", camille);
	if (ret < 0) {
		dev_err(dev, "could not request irq handler.\n");
		return -EBUSY;
	}

	camille->coprocessor_funcs = &camille_coprocessor_funcs;

	camille_parse_pwm(camille);

	spin_lock_init(&camille->shadow_lock);
	disable_irq(camille->irq);
	camille->is_enabled = false;

	return ret;
}

static int camille_validate_mode(struct camille_dev *camille, struct camille_mode *mode)
{
	int ret = 0;
	bool tm_enabled = true;
	bool cltm_enabled = false;
	bool agtm_enabled = false;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	ret = camille_validate_data_flow(camille, mode);
	if (ret)
		return ret;

	switch (mode->tm_mode) {
	case 1:
	case 2:
		cltm_enabled = true;
		agtm_enabled = true;
		break;
	case 3:
		cltm_enabled = true;
		break;
	case 4:
		agtm_enabled = true;
		break;
	case 6:
		tm_enabled = false;
		break;
	default:
		break;
	}

	if (!tm_enabled)
		return 0;

	if (cltm_enabled) {
		ret = camille_validate_cltm_ctrl(camille, mode);
		if (ret)
			return ret;

		ret = camille_validate_cltm_block(camille, mode);
		if (ret)
			return ret;

		ret = camille_validate_cltm_linear_adj(camille, mode);
		if (ret)
			return ret;

		ret = camille_validate_cltm_adj_curve(camille, mode);
		if (ret)
			return ret;

		ret = camille_validate_cltm_strength(camille, mode);
		if (ret)
			return ret;
	}

	if (agtm_enabled) {
		ret = camille_validate_agtm_curve(camille, mode);
		if (ret)
			return ret;
	}

	return 0;
}

static int camille_parse_mode(struct camille_dev *camille, struct camille_mode *mode)
{
	int ret = 0;

	camille_dbg(CAMILLE_DEBUG, "%s", __func__);

	ret = camille_parse_data_flow(camille, mode);
	if (ret)
		return ret;

	if (!camille->all_tm_ctrl.tm_enable)
		return 0;

	if (camille->all_tm_ctrl.cltm_enable) {
		ret = camille_parse_cltm_ctrl(camille, mode);
		if (ret)
			return ret;

		ret = camille_parse_cltm_block(camille, mode);
		if (ret)
			return ret;

		ret = camille_parse_cltm_linear_adj(camille, mode);
		if (ret)
			return ret;

		ret = camille_parse_cltm_adj_curve(camille, mode);
		if (ret)
			return ret;

		ret = camille_parse_cltm_strength(camille, mode);
		if (ret)
			return ret;
	}

	if (camille->all_tm_ctrl.agtm_enable) {
		ret = camille_parse_agtm_curve(camille, mode);
		if (ret)
			return ret;
	}

	return 0;
}

static int camille_file_open(struct inode *inode, struct file *file)
{
	struct miscdevice *c = file->private_data;
	struct camille_dev *camille = misc_to_camille(c);
	struct device *dev = camille->dev;

	dev_info(dev, "camille is opened.\n");

	return 0;
}

static int camille_file_close(struct inode *i, struct file *file)
{
	struct miscdevice *c = file->private_data;
	struct camille_dev *camille = misc_to_camille(c);
	struct device *dev = camille->dev;

	dev_info(dev, "camille is closed.\n");

	return 0;
}

static ssize_t
camille_file_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct miscdevice *c = file->private_data;
	struct camille_dev *camille = misc_to_camille(c);
	struct device *dev = camille->dev;

	dev_dbg(dev, "camille read.\n");

	return 0;
}

static ssize_t
camille_file_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct miscdevice *c = file->private_data;
	struct camille_dev *camille = misc_to_camille(c);
	struct device *dev = camille->dev;

	dev_dbg(dev, "camille write.\n");

	return 0;
}

static long camille_file_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int i;
	int ret = 0;
	struct miscdevice *c = file->private_data;
	struct camille_dev *camille = misc_to_camille(c);
	struct device *dev = camille->dev;

	dev_dbg(dev, "%s, cmd = 0x%x, arg=0x%lx\n",
			__func__, cmd, arg);

	switch (cmd) {
	case CAMILLE_IOC_ENABLE:
	{
		camille_enable(camille);
		break;
	}
	case CAMILLE_IOC_DISABLE:
	{
		camille_disable(camille);
		break;
	}
	case CAMILLE_IOC_MODE:
	{
		struct camille_mode mode;

		if (camille->is_enabled == true) {
			dev_err(dev, "%s, No updating mode when enabled.\n", __func__);
			return -EINVAL;
		}

		if (_IOC_SIZE(cmd) > sizeof(mode)) {
			dev_err(dev, "%s, invalid mode cmd size\n", __func__);
			return -EINVAL;
		}
		/*
		 * The copy_from_user is unconditional here for both read and write
		 * to do the validate. If there is no write for the ioctl, the
		 * buffer is cleared
		 */
		if (copy_from_user(&mode, (void __user *)arg, _IOC_SIZE(cmd))) {
			dev_err(dev, "%s, failed to copy mode data from user\n", __func__);
			return -EFAULT;
		}

		ret = camille_validate_mode(camille, &mode);
		if (ret)
			dev_err(dev, "%s, invalid camille mode parameters.\n", __func__);

		ret = camille_parse_mode(camille, &mode);
		if (ret)
			dev_err(dev, "%s, failed to parse camille mode parameters.\n", __func__);

		break;
	}
	case CAMILLE_IOC_LUT:
	{
		struct camille_lut_data data;

		if (camille->is_enabled == false) {
			dev_err(dev, "%s, No updating agtm_curve when disabled.\n", __func__);
			return -EINVAL;
		}

		if (_IOC_SIZE(cmd) > sizeof(data)) {
			dev_err(dev, "%s, invalid lut cmd size\n", __func__);
			return -EINVAL;
		}
		/*
		 * The copy_from_user is unconditional here for both read and write
		 * to do the validate. If there is no write for the ioctl, the
		 * buffer is cleared
		 */
		if (copy_from_user(&data.lut, (void __user *)arg, _IOC_SIZE(cmd))) {
			dev_err(dev, "%s, failed to copy lut data from user\n", __func__);
			return -EFAULT;
		}

		spin_lock_irq(&camille->shadow_lock);

		for (i = 0; i < AGLT_K_LUT_SIZE; i++)
			if (data.lut[i] > 0x7ffff) {
				dev_err(dev, "%s, invalid agtm_curve[%d] parameters:%u.\n",
						__func__, i, data.lut[i]);
				spin_unlock_irq(&camille->shadow_lock);
				return -EINVAL;
			} else {
				camille->agtm_curve[i] = data.lut[i];
				camille_dbg(CAMILLE_DEBUG, "agtm_curve[%d]:%u\n",
						i, data.lut[i]);
			}
		}

		if (camille->shadow_update_completed)
			camille->shadow_update_completed = false;
		else {
			dev_err(dev, "%s, no updating agtm_curve when shadow_update incompleted\n",
					__func__);
			spin_unlock_irq(&camille->shadow_lock);
			return -EBUSY;
		}

		camille_shadow_update_en(camille, false);
		camille_agtm_curve_update(camille);
		camille_shadow_update_en(camille, true);
		spin_unlock_irq(&camille->shadow_lock);

		break;
	default:
		dev_err(dev, "%s, invalid cmd:0x%x\n", __func__, cmd);
		return -EINVAL;
	}

	return ret;
}

static const struct file_operations camille_fops = {
	.owner   = THIS_MODULE,
	.open    = camille_file_open,
	.release = camille_file_close,
	.read    = camille_file_read,
	.write   = camille_file_write,
	.unlocked_ioctl = camille_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = camille_file_ioctl,
#endif
};

static ssize_t camille_agtm_curve_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	u32 i, val;
	u32 kram_used_index = 0;
	u32 agtm_kram_raddr, agtm_kram_rdata;
	struct camille_dev *camille = dev_get_drvdata(dev);

	clk_prepare_enable(camille->aclk);

	val = camille_read(camille, AGTM_KRAM1_RDATA);
	if (val & BIT(24))
		kram_used_index = 1;
	else {
		val = camille_read(camille, AGTM_KRAM2_RDATA);
		if (val & BIT(24))
			kram_used_index = 2;
	}

	if (kram_used_index)
		dev_err(dev, "%s, kram %u is used\n", __func__, kram_used_index);
	else {
		dev_err(dev, "%s, no kram is used\n", __func__);
		return 1;
	}

	if (kram_used_index == 1) {
		agtm_kram_raddr = AGTM_KRAM1_RADDR;
		agtm_kram_rdata = AGTM_KRAM1_RDATA;
	} else {
		agtm_kram_raddr = AGTM_KRAM2_RADDR;
		agtm_kram_rdata = AGTM_KRAM2_RDATA;
	}

	for (i = 0; i <= 256; i++) {
		val = i | BIT(8);
		camille_write(camille, AGTM_KRAM1_RADDR, val);
		val = camille_read(camille, AGTM_KRAM1_RDATA) & 0x7ffff;
		dev_err(dev, "0x%x, ", val);
	}

	clk_disable_unprepare(camille->aclk);

	return 256;
}

static DEVICE_ATTR(agtm_curve, S_IRUSR,
		   camille_agtm_curve_show, NULL);

static ssize_t camille_registers_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct camille_dev *camille = dev_get_drvdata(dev);

	camille_dump_regs(camille);

	return 1;
}

static DEVICE_ATTR(registers, S_IRUSR,
		   camille_registers_show, NULL);

static ssize_t camille_enable_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t size)
{
	int ret;
	u32 value;
	struct camille_dev *camille = dev_get_drvdata(dev);

	ret = kstrtou32(buf, 0, &value);
	if (ret) {
		dev_err(dev, "invalid enable parameter\n");
		return ret;
	}

	if (value)
		camille_enable(camille);
	else
		camille_disable(camille);

	return size;
}

static DEVICE_ATTR(enable, S_IWUSR,
		   NULL, camille_enable_store);

static ssize_t camille_log_level_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "camille_log_level=%u\n", camille_log_level);
}

static ssize_t camille_log_level_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t size)
{
	int ret;
	u32 value;

	ret = kstrtou32(buf, 0, &value);
	if (ret) {
		dev_err(dev, "invalid log_level parameter.\n");
		return ret;
	}

	camille_log_level = value;

	return size;
}

static DEVICE_ATTR(log_level, S_IRUSR | S_IWUSR,
		   camille_log_level_show, camille_log_level_store);

static struct attribute *camille_attrs[] = {
    &dev_attr_enable.attr,
    &dev_attr_registers.attr,
    &dev_attr_log_level.attr,
	&dev_attr_agtm_curve.attr,
    NULL,
};

static struct attribute_group camille_attr_group = {
    .attrs = camille_attrs,
};

static int camille_driver_bind(struct device *dev,
			struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct ad_list *ad_head = drm->dev_private;
	struct camille_dev *camille = dev_get_drvdata(dev);
	struct ad_coprocessor *ad;

	dev_info(dev, "%s.\n", __func__);

	ad = devm_kzalloc(dev, sizeof(*ad), GFP_KERNEL);
	if (!ad)
		return -ENOMEM;

	ad->dev = dev;
	ad->funcs = camille->coprocessor_funcs;
	list_add_tail(&ad->ad_node, &ad_head->head);

	return 0;
}

static void camille_driver_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct drm_device *drm = data;
	struct ad_list *ad_head = drm->dev_private;
	struct ad_coprocessor *ad;

	dev_info(dev, "%s.\n", __func__);

	list_for_each_entry(ad, &ad_head->head, ad_node)
		if (ad->dev == dev) {
			list_del(&ad->ad_node);
			devm_kfree(dev, ad);
			break;
		}
}

static const struct component_ops camille_component_ops = {
	.bind = camille_driver_bind,
	.unbind = camille_driver_unbind,
};

static int camille_probe(struct platform_device *pdev)
{
	int ret;
	struct camille_dev *camille;
	struct device *dev = &pdev->dev;

	camille = devm_kzalloc(dev, sizeof(*camille), GFP_KERNEL);
	if (!camille)
		return -ENOMEM;

	camille->dev = dev;
	dev_set_drvdata(dev, camille);

	ret = camille_get_resources(camille);
	if (ret) {
		dev_err(dev, "Failed to get camille resources!\n");
		goto get_res_failed;
	}

	ret = sysfs_create_group(&dev->kobj, &camille_attr_group);
	if (ret) {
		dev_err(dev, "failed to create sysfs for camille\n");
		goto get_res_failed;
	}

#if defined(CONFIG_CAMILLE_PWM)
	camille->backlight_led.name = "lcd-backlight",
	camille->backlight_led.brightness = LED_HALF,
	camille->backlight_led.brightness_set = camille_backlight_set_brightness,
	camille->backlight_led.max_brightness = LED_FULL,
	ret = led_classdev_register(dev, &camille->backlight_led);
	if (ret) {
		dev_err(dev, "camille can't register led class device\n");
		goto register_led_cdev_failed;
	}
#endif

	sprintf(camille->name, "camille_display%d", 0);
	camille->miscdev.minor = MISC_DYNAMIC_MINOR;
	camille->miscdev.name = camille->name;
	camille->miscdev.fops = &camille_fops;
	camille->miscdev.parent = dev;

	ret = misc_register(&camille->miscdev);
	if (ret) {
		dev_err(dev, "Failed to register misc device: 0x%x!\n", ret);
		goto register_miscdev_failed;
	}

	ret = component_add(dev, &camille_component_ops);
	if (ret) {
		dev_err(dev, "Failed to add component: 0x%x!\n", ret);
		goto add_component_failed;
	}

	camille_log_level = 0x1;

	dev_err(dev, "%s, finished.\n", __func__);

	return ret;

add_component_failed:
	misc_deregister(&camille->miscdev);
register_miscdev_failed:
#if defined(CONFIG_CAMILLE_PWM)
	led_classdev_unregister(&camille->backlight_led);
register_led_cdev_failed:
#endif
	sysfs_remove_group(&dev->kobj, &camille_attr_group);
get_res_failed:
	devm_kfree(dev, camille);
	return ret;
}

static int camille_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct camille_dev *camille = platform_get_drvdata(pdev);

	component_del(dev, &camille_component_ops);

	misc_deregister(&camille->miscdev);

#if defined(CONFIG_CAMILLE_PWM)
	led_classdev_unregister(&camille->backlight_led);
#endif

	sysfs_remove_group(&dev->kobj, &camille_attr_group);

	devm_kfree(dev, camille);

	return 0;
}

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
static int camille_driver_runtime_suspend(struct device *dev)
{
	return 0;
}

static int camille_driver_runtime_resume(struct device *dev)
{
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int camille_driver_pm_suspend_late(struct device *dev)
{
	return 0;
}

static int camille_driver_pm_resume_early(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops camille_pm_ops = {
	SET_RUNTIME_PM_OPS(camille_driver_runtime_suspend,
			   camille_driver_runtime_resume,
			   NULL)
#ifdef CONFIG_PM_SLEEP
	.suspend_late = camille_driver_pm_suspend_late,
	.resume_early = camille_driver_pm_resume_early,
#endif
};

static const struct of_device_id camille_match[] = {
	{
		.compatible = "jlq,jlq-camille",
	},
	{},
};
MODULE_DEVICE_TABLE(of, camille_match);

static struct platform_driver camille_driver = {
	.probe = camille_probe,
	.remove = camille_remove,
	.driver = {
		.of_match_table = camille_match,
		.name = CAMILLE_NAME,
		.pm   = &camille_pm_ops,
	},
};

module_platform_driver(camille_driver);
MODULE_DESCRIPTION("JLQ DSI PANEL DRIVER");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: pwm_jlq arm-smmu-v3 komeda");

