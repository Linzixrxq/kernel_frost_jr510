// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018~2021 JLQ Technology Co., Ltd. or its affiliates.
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

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include "pinctrl-jlq.h"
#include <core.h>
#include <pinctrl-utils.h>

#define QTANG_PINCTRL_NAME "qtang-pinctrl"

static const struct jlq_desc_pin qtang_pins[] = {
	JLQ_PIN(PINCTRL_PIN(4, "GPIO4"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[4] */
	JLQ_PIN(PINCTRL_PIN(5, "GPIO5"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[5] */
	JLQ_PIN(PINCTRL_PIN(6, "GPIO6"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cam_mclk")),/* gpio[6] */
	JLQ_PIN(PINCTRL_PIN(7, "GPIO7"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cam_mclk")),/* gpio[7] */
	JLQ_PIN(PINCTRL_PIN(8, "GPIO8"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "cam_cci")),/* i2c0_sda */
	JLQ_PIN(PINCTRL_PIN(9, "GPIO9"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "cam_cci")),/* gs_high_load_i2c0_scl */
	JLQ_PIN(PINCTRL_PIN(10, "GPIO10"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cci_timer")),/* gpio[10] */
	JLQ_PIN(PINCTRL_PIN(11, "GPIO11"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cci_async_in")),/* gpio[11] */
	JLQ_PIN(PINCTRL_PIN(12, "GPIO12"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cci_timer")),/* gpio[12] */
	JLQ_PIN(PINCTRL_PIN(13, "GPIO13"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cam_mclk")),/* gpio[13] */
	JLQ_PIN(PINCTRL_PIN(14, "GPIO14"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cam_mclk")),/* gpio[14] */
	JLQ_PIN(PINCTRL_PIN(15, "GPIO15"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "cam_cci")),/* i2c1_sda */
	JLQ_PIN(PINCTRL_PIN(16, "GPIO16"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "cam_cci")),/* gs_high_load_i2c1_scl */
	JLQ_PIN(PINCTRL_PIN(17, "GPIO17"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cci_timer")),/* gpio[17] */
	JLQ_PIN(PINCTRL_PIN(18, "GPIO18"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x1, "cci_timer")),/* gpio[18] */
	JLQ_PIN(PINCTRL_PIN(19, "GPIO19"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio")),/* gpio[19] */
	JLQ_PIN(PINCTRL_PIN(20, "GPIO20"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio")),/* gpio[20] */
	JLQ_PIN(PINCTRL_PIN(0, "GPIO0"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio")),/* gpio[0] */
	JLQ_PIN(PINCTRL_PIN(1, "GPIO1"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio")),/* gpio[1] */
	JLQ_PIN(PINCTRL_PIN(3, "GPIO3"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[3] */
	JLQ_PIN(PINCTRL_PIN(21, "GPIO21"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[21] */
	JLQ_PIN(PINCTRL_PIN(59, "GPIO59"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[59] */
	JLQ_PIN(PINCTRL_PIN(60, "GPIO60"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[60] */
	JLQ_PIN(PINCTRL_PIN(63, "UFS_RESET_N"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x1, "gpio")),/* UFS_RESET_N */
};

static const char * const qtang_groups[] = {
	"GPIO4",
	"GPIO5",
	"GPIO6",
	"GPIO7",
	"GPIO8",
	"GPIO9",
	"GPIO10",
	"GPIO11",
	"GPIO12",
	"GPIO13",
	"GPIO14",
	"GPIO15",
	"GPIO16",
	"GPIO17",
	"GPIO18",
	"GPIO19",
	"GPIO20",
	"GPIO0",
	"GPIO1",
	"GPIO3",
	"GPIO21",
	"GPIO59",
	"GPIO60",
	"UFS_RESET_N"
};

static const char * const qtang_functions[] = {
	"gpio",
	"cam_mclk",
	"cci_timer",
	"cci_async_in",
	"cci_timer",
	"cam_cci",
};

const struct jlq_pinctrl_desc qtang_pinctrl_data = {
	.pins = qtang_pins,
	.npins = ARRAY_SIZE(qtang_pins),
	.ngpios = 63,
	.groups = qtang_groups,
	.ngroups = ARRAY_SIZE(qtang_groups),
	.functions = qtang_functions,
	.nfunctions = ARRAY_SIZE(qtang_functions),
};

EXPORT_SYMBOL_GPL(qtang_pinctrl_data);

MODULE_LICENSE("GPL");
