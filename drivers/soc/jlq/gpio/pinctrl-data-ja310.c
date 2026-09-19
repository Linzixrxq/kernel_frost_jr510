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
#include <dt-bindings/pinctrl/jlq_pinctrl.h>
#include "pinctrl-jlq.h"
#include <core.h>

static const struct pin_select_type pin_select[] = {
	{	.schmit_offset = 0,	.schmit_mask = 0,
		.pull_offset = 4,	.pull_mask = GENMASK(5, 4),
		.drive_offset = 2,	.drive_mask = GENMASK(3, 2),
		.slew_rate_offset = 0,	.slew_rate_mask = 0,
		.mux_ctrl_offset = 0,	.mux_ctrl_mask = GENMASK(1, 0),

	},
	{	.schmit_offset = 8,	.schmit_mask = BIT(8),
		.pull_offset = 6,	.pull_mask = GENMASK(7, 6),
		.drive_offset = 3,	.drive_mask = GENMASK(5, 3),
		.slew_rate_offset = 2,	.slew_rate_mask = BIT(2),
		.mux_ctrl_offset = 0,	.mux_ctrl_mask = GENMASK(1, 0),
	},
	{	.schmit_offset = 7,	.schmit_mask = BIT(7),
		.pull_offset = 5,	.pull_mask = GENMASK(6, 5),
		.drive_offset = 3,	.drive_mask = GENMASK(4, 3),
		.slew_rate_offset = 2,	.slew_rate_mask = BIT(2),
		.mux_ctrl_offset = 0,	.mux_ctrl_mask = GENMASK(1, 0),
	},
	{},
};

static const struct jlq_desc_pin ja310_pins[] = {
	JLQ_PIN(PINCTRL_PIN(0, "GPIO0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[0] */
		JLQ_FUNCTION(0x1, "i2c2"),/* i2c2_scl */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[0] */
		JLQ_FUNCTION(0x3, "uart0")),/* uart0_tx */
	JLQ_PIN(PINCTRL_PIN(1, "GPIO1"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[1] */
		JLQ_FUNCTION(0x1, "i2c2"),/* i2c2_sda */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[3] */
		JLQ_FUNCTION(0x3, "uart0")),/* uart0_rx */
	JLQ_PIN(PINCTRL_PIN(2, "GPIO2"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[2] */
		JLQ_FUNCTION(0x1, "i2c3"),/* i2c3_scl */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[4] */
		JLQ_FUNCTION(0x3, "uart1")),/* uart1_tx */
	JLQ_PIN(PINCTRL_PIN(3, "GPIO3"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[3] */
		JLQ_FUNCTION(0x1, "i2c3"),/* i2c3_sda */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[5] */
		JLQ_FUNCTION(0x3, "uart1")),/* uart1_rx */
	JLQ_PIN(PINCTRL_PIN(4, "GPIO6"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[6] */
		JLQ_FUNCTION(0x1, "sd0"),/* sd0_detect_n */
		JLQ_FUNCTION(0x2, "isp0")),/* fl_trig_isp0 */
	JLQ_PIN(PINCTRL_PIN(5, "GPIO9"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[9] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_tx */
		JLQ_FUNCTION(0x2, "sd1"),/* sd1_clk */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[4] */
	JLQ_PIN(PINCTRL_PIN(6, "GPIO10"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[10] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_rx */
		JLQ_FUNCTION(0x2, "sd1"),/* sd1_cmd */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[5] */
	JLQ_PIN(PINCTRL_PIN(7, "GPIO25"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[25] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_cts */
		JLQ_FUNCTION(0x2, "sd1"),/* sd1_data[3] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[6] */
	JLQ_PIN(PINCTRL_PIN(8, "GPIO28"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[28] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_rts */
		JLQ_FUNCTION(0x2, "sd1"),/* sd1_data[2] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[7] */
	JLQ_PIN(PINCTRL_PIN(9, "GPIO29"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[29] */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_scl */
		JLQ_FUNCTION(0x2, "sd1"),/* sd1_data[1] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[8] */
	JLQ_PIN(PINCTRL_PIN(10, "GPIO30"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[30] */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_sda */
		JLQ_FUNCTION(0x2, "sd1"),/* sd1_data[0] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[9] */
	JLQ_PIN(PINCTRL_PIN(11, "GPIO11"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[11] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_tx */
		JLQ_FUNCTION(0x2, "pwm0"),/* Pwm0 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_txen */
	JLQ_PIN(PINCTRL_PIN(12, "GPIO12"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[12] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rx */
		JLQ_FUNCTION(0x2, "pwm1"),/* Pwm1 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_txer */
	JLQ_PIN(PINCTRL_PIN(13, "GPIO13"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[13] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_cts */
		JLQ_FUNCTION(0x2, "pwm2"),/* Pwm2 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_txd[0] */
	JLQ_PIN(PINCTRL_PIN(14, "GPIO14"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[14] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rts */
		JLQ_FUNCTION(0x2, "pwm3"),/* Pwm3 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_txd[1] */
	JLQ_PIN(PINCTRL_PIN(15, "GPIO15"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[15] */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_tx */
		JLQ_FUNCTION(0x2, "pwm4"),/* Pwm4 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_txd[2] */
	JLQ_PIN(PINCTRL_PIN(16, "GPIO16"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[16] */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_rx */
		JLQ_FUNCTION(0x2, "pwm5"),/* Pwm5 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_txd[3] */
	JLQ_PIN(PINCTRL_PIN(17, "GPIO17"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[17] */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_cts */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[1] */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_rxdv */
	JLQ_PIN(PINCTRL_PIN(18, "GPIO18"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[18] */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_rts */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[2] */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_rxer */
	JLQ_PIN(PINCTRL_PIN(19, "GPIO19"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[19] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_tx */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[6] */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_rxd[0] */
	JLQ_PIN(PINCTRL_PIN(20, "GPIO20"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[20] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_rx */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[7] */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_rxd[1] */
	JLQ_PIN(PINCTRL_PIN(21, "GPIO21"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[21] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_cts */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[8] */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_rxd[2] */
	JLQ_PIN(PINCTRL_PIN(22, "GPIO22"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[22] */
		JLQ_FUNCTION(0x1, "uart4"),/* uart4_rts */
		JLQ_FUNCTION(0x2, "test_pin"),/* test_pin[9] */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_rxd[3] */
	JLQ_PIN(PINCTRL_PIN(23, "GPIO26"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[26] */
		JLQ_FUNCTION(0x1, "usbdrv"),/* usbdrv[0] */
		JLQ_FUNCTION(0x2, "isp1"),/* fl_trig_isp1 */
		JLQ_FUNCTION(0x3, "sd2")),/* sd2_clk */
	JLQ_PIN(PINCTRL_PIN(24, "GPIO27"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[27] */
		JLQ_FUNCTION(0x1, "usbdrv"),/* usbdrv[1] */
		JLQ_FUNCTION(0x2, "isp1"),/* prelight_trig_isp1 */
		JLQ_FUNCTION(0x3, "sd2")),/* sd2_cmd */
	JLQ_PIN(PINCTRL_PIN(25, "GPIO31"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[31] */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_scl */
		JLQ_FUNCTION(0x2, "isp0"),/* shutter_trig_isp0 */
		JLQ_FUNCTION(0x3, "sd2")),/* sd2_data[3] */
	JLQ_PIN(PINCTRL_PIN(26, "GPIO32"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[32] */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_sda */
		JLQ_FUNCTION(0x2, "isp1"),/* shutter_trig_isp1 */
		JLQ_FUNCTION(0x3, "sd2")),/* sd2_data[2] */
	JLQ_PIN(PINCTRL_PIN(27, "GPIO33"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[33] */
		JLQ_FUNCTION(0x1, "i2c2"),/* i2c2_scl */
		JLQ_FUNCTION(0x2, "isp0"),/* flash_trig_isp0 */
		JLQ_FUNCTION(0x3, "sd2")),/* sd2_data[1] */
	JLQ_PIN(PINCTRL_PIN(28, "GPIO34"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[34] */
		JLQ_FUNCTION(0x1, "i2c2"),/* i2c2_sda */
		JLQ_FUNCTION(0x2, "isp1"),/* flash_trig_isp1 */
		JLQ_FUNCTION(0x3, "sd2")),/* sd2_data[0] */
	JLQ_PIN(PINCTRL_PIN(29, "GPIO35"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[35] */
		JLQ_FUNCTION(0x1, "gpio")),/* Gpio[36] */
	JLQ_PIN(PINCTRL_PIN(30, "SSI1_SSn"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_ssn */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[37] */
		JLQ_FUNCTION(0x2, "sd2"),/* sd2_clk */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[10] */
	JLQ_PIN(PINCTRL_PIN(31, "SSI1_RX"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_rx */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[38] */
		JLQ_FUNCTION(0x2, "sd2"),/* sd2_cmd */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[11] */
	JLQ_PIN(PINCTRL_PIN(32, "SSI1_TX"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_tx */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[39] */
		JLQ_FUNCTION(0x2, "sd2"),/* sd2_data[3] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[12] */
	JLQ_PIN(PINCTRL_PIN(33, "SSI1_CLK"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_clk */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[40] */
		JLQ_FUNCTION(0x2, "sd2"),/* sd2_data[2] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[13] */
	JLQ_PIN(PINCTRL_PIN(34, "GPIO23"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[23] */
		JLQ_FUNCTION(0x2, "sd2"),/* sd2_data[1] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[14] */
	JLQ_PIN(PINCTRL_PIN(35, "GPIO24"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[24] */
		JLQ_FUNCTION(0x2, "sd2"),/* sd2_data[0] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[15] */
	JLQ_PIN(PINCTRL_PIN(36, "PWM6"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "pwm6"),/* Pwm6 */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[42] */
		JLQ_FUNCTION(0x2, "pwm0")),/* pwm0 */
	JLQ_PIN(PINCTRL_PIN(37, "SSI2_SSn"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_ssn */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[43] */
		JLQ_FUNCTION(0x2, "pwm1"),/* pwm1 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_crs */
	JLQ_PIN(PINCTRL_PIN(38, "SSI2_RX"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_rx */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[44] */
		JLQ_FUNCTION(0x2, "pwm2"),/* pwm2 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_col */
	JLQ_PIN(PINCTRL_PIN(39, "SSI2_TX"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_tx */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[45] */
		JLQ_FUNCTION(0x2, "pwm3"),/* pwm3 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_gmii_mdc */
	JLQ_PIN(PINCTRL_PIN(40, "SSI2_CLK"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_clk */
		JLQ_FUNCTION(0x1, "gpio"),/* Gpio[46] */
		JLQ_FUNCTION(0x2, "pwm4"),/* pwm4 */
		JLQ_FUNCTION(0x3, "eth")),/* eth_gmii_mdio */
	JLQ_PIN(PINCTRL_PIN(41, "SSI3_TX"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi3"),/* ssi3_tx */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_ssn */
		JLQ_FUNCTION(0x2, "i2c2"),/* i2c2_scl */
		JLQ_FUNCTION(0x3, "eth")),/* eth_clk_tx */
	JLQ_PIN(PINCTRL_PIN(42, "SSI3_RX"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi3"),/* ssi3_rx */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_rx */
		JLQ_FUNCTION(0x2, "i2c2"),/* i2c2_sda */
		JLQ_FUNCTION(0x3, "eth")),/* eth_clk_rx */
	JLQ_PIN(PINCTRL_PIN(43, "SSI3_CLK"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi3"),/* ssi3_clk */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_tx */
		JLQ_FUNCTION(0x2, "gpio")),/* gpio[47] */
	JLQ_PIN(PINCTRL_PIN(44, "SSI3_SSn"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "ssi3"),/* ssi3_ssn */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_clk */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[48] */
		JLQ_FUNCTION(0x3, "eth")),/* eth_phy_intr */
	JLQ_PIN(PINCTRL_PIN(45, "PWM0"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "pwm0"),/* Pwm0 */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_scl */
		JLQ_FUNCTION(0x2, "jtag0"),/* jtag0_tck */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[49] */
	JLQ_PIN(PINCTRL_PIN(46, "PWM1"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "pwm1"),/* Pwm1 */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_sda */
		JLQ_FUNCTION(0x2, "jtag0"),/* jtag0_tdi */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[50] */
	JLQ_PIN(PINCTRL_PIN(47, "PWM2"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "pwm2"),/* Pwm2 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[51] */
		JLQ_FUNCTION(0x2, "jtag0"),/* jtag0_tdo */
		JLQ_FUNCTION(0x3, "i2s3")),/* i2s3_sdin */
	JLQ_PIN(PINCTRL_PIN(48, "PWM3"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "pwm3"),/* Pwm3 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[52] */
		JLQ_FUNCTION(0x2, "jtag0"),/* jtag0_tms */
		JLQ_FUNCTION(0x3, "i2s3")),/* i2s3_sdout */
	JLQ_PIN(PINCTRL_PIN(49, "PWM4"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "pwm4"),/* Pwm4 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[53] */
		JLQ_FUNCTION(0x2, "jtag0"),/* jtag0_ntrs */
		JLQ_FUNCTION(0x3, "i2s3")),/* i2s3_sclk */
	JLQ_PIN(PINCTRL_PIN(50, "PWM5"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "pwm5"),/* Pwm5 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[54] */
		JLQ_FUNCTION(0x3, "i2s3")),/* i2s3_ws */
	JLQ_PIN(PINCTRL_PIN(51, "U1TXD"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_tx */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_ssn */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[55] */
	JLQ_PIN(PINCTRL_PIN(52, "U1RXD"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_rx */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[56] */
	JLQ_PIN(PINCTRL_PIN(53, "U1CTS"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_cts */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[57] */
	JLQ_PIN(PINCTRL_PIN(54, "U1RTS"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_rts */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_clk */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[58] */
	JLQ_PIN(PINCTRL_PIN(55, "U0TXD"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart0"),/* uart0_tx */
		JLQ_FUNCTION(0x2, "i2s2"),/* i2s2_sclk */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[59] */
	JLQ_PIN(PINCTRL_PIN(56, "U0RXD"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart0"),/* uart0_rx */
		JLQ_FUNCTION(0x2, "i2s2"),/* i2s2_ws */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[60] */
	JLQ_PIN(PINCTRL_PIN(57, "U5TXD"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "com_uart"),/* com_uart_tx */
		JLQ_FUNCTION(0x1, "uart0"),/* uart0_tx */
		JLQ_FUNCTION(0x2, "uart1"),/* uart1_tx */
		JLQ_FUNCTION(0x3, "i2s4")),/* i2s4_sclk */
	JLQ_PIN(PINCTRL_PIN(58, "U5RXD"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "com_uart"),/* com_uart_rx */
		JLQ_FUNCTION(0x1, "uart0"),/* uart0_rx */
		JLQ_FUNCTION(0x2, "uart1"),/* uart1_rx */
		JLQ_FUNCTION(0x3, "i2s4")),/* i2s4_ws */
	JLQ_PIN(PINCTRL_PIN(59, "IIC2SCL0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c0"),/* i2c0_scl */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_ssn */
		JLQ_FUNCTION(0x2, "uart3"),/* uart3_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[61] */
	JLQ_PIN(PINCTRL_PIN(60, "IIC2SDA0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c0"),/* i2c0_sda */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_rx */
		JLQ_FUNCTION(0x2, "uart3"),/* uart3_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[62] */
	JLQ_PIN(PINCTRL_PIN(61, "IIC2SCL1"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c1"),/* i2c1_scl */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_tx */
		JLQ_FUNCTION(0x2, "uart3"),/* uart3_cts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[63] */
	JLQ_PIN(PINCTRL_PIN(62, "IIC2SDA1"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c1"),/* i2c1_sda */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_clk */
		JLQ_FUNCTION(0x2, "uart3"),/* uart3_rts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[64] */
	JLQ_PIN(PINCTRL_PIN(63, "IIC2SCL2"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c2"),/* i2c2_scl */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_tx */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[65] */
	JLQ_PIN(PINCTRL_PIN(64, "IIC2SDA2"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c2"),/* i2c2_sda */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_rx */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_rx */
		JLQ_FUNCTION(0x3, "isp0")),/* prelight_trig_isp0 */
	JLQ_PIN(PINCTRL_PIN(65, "GPIO7"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[7] */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_cts */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_clk */
		JLQ_FUNCTION(0x3, "isp0")),/* shutter_open_isp0 */
	JLQ_PIN(PINCTRL_PIN(66, "GPIO8"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[8] */
		JLQ_FUNCTION(0x1, "uart3"),/* uart3_rts */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_ssn */
		JLQ_FUNCTION(0x3, "isp1")),/* shutter_open_isp1 */
	JLQ_PIN(PINCTRL_PIN(67, "GPIO4"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[4] */
		JLQ_FUNCTION(0x1, "i2s4"),/* i2s4_sdin */
		JLQ_FUNCTION(0x2, "uart1"),/* uart1_tx */
		JLQ_FUNCTION(0x3, "pwm7")),/* pwm7 */
	JLQ_PIN(PINCTRL_PIN(68, "GPIO5"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "gpio"),/* Gpio[5] */
		JLQ_FUNCTION(0x1, "i2s4"),/* i2s4_sdout */
		JLQ_FUNCTION(0x2, "uart1"),/* uart1_rx */
		JLQ_FUNCTION(0x3, "sd0")),/* sd0_write_prot */
	JLQ_PIN(PINCTRL_PIN(69, "IIC2SCL3"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c3"),/* i2c3_scl */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[66] */
	JLQ_PIN(PINCTRL_PIN(70, "IIC2SDA3"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c3"),/* i2c3_sda */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[67] */
	JLQ_PIN(PINCTRL_PIN(71, "IIC2SCL4"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "i2c4"),/* i2c4_scl */
		JLQ_FUNCTION(0x1, "i2c5"),/* i2c5_scl */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[68] */
	JLQ_PIN(PINCTRL_PIN(72, "IIC2SDA4"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "i2c4"),/* i2c4_sda */
		JLQ_FUNCTION(0x1, "i2c5"),/* i2c5_sda */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[69] */
	JLQ_PIN(PINCTRL_PIN(73, "IIC2SCL5"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c5"),/* i2c5_scl */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_ssn */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_cts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[70] */
	JLQ_PIN(PINCTRL_PIN(74, "IIC2SDA5"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c5"),/* i2c5_sda */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_rx */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_rts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[71] */
	JLQ_PIN(PINCTRL_PIN(75, "I2S2SDIN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s2"),/* i2s2_sdin */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[72] */
	JLQ_PIN(PINCTRL_PIN(76, "I2S2SDOUT"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s2"),/* i2s2_sdout */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_clk */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[73] */
	JLQ_PIN(PINCTRL_PIN(77, "CLKO0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "clk_out"),/* clk_out[0] */
		JLQ_FUNCTION(0x1, "pwm0"),/* pwm0 */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[74] */
	JLQ_PIN(PINCTRL_PIN(78, "CLKO1"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "clk_out"),/* clk_out[1] */
		JLQ_FUNCTION(0x1, "pwm1"),/* pwm1 */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[75] */
	JLQ_PIN(PINCTRL_PIN(79, "CLKO2"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "clk_out"),/* clk_out[2] */
		JLQ_FUNCTION(0x1, "pwm2"),/* pwm2 */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_cts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[76] */
	JLQ_PIN(PINCTRL_PIN(80, "CLKO3"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "clk_out"),/* clk_out[3] */
		JLQ_FUNCTION(0x1, "pwm3"),/* pwm3 */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_rts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[77] */
	JLQ_PIN(PINCTRL_PIN(81, "CLKO4"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "clk_out"),/* clk_out[4] */
		JLQ_FUNCTION(0x1, "pwm4"),/* pwm4 */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[78] */
	JLQ_PIN(PINCTRL_PIN(82, "CLKO5"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "clk_out"),/* clk_out[5] */
		JLQ_FUNCTION(0x1, "pwm5"),/* pwm5 */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[79] */
	JLQ_PIN(PINCTRL_PIN(83, "I2S0SDIN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s0"),/* i2s0_sdin */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_tx */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[80] */
	JLQ_PIN(PINCTRL_PIN(84, "I2S0SDOUT"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s0"),/* i2s0_sdout */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rx */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[81] */
	JLQ_PIN(PINCTRL_PIN(85, "I2S0SCLK"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s0"),/* i2s0_sclk */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_cts */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_clk */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[82] */
	JLQ_PIN(PINCTRL_PIN(86, "I2S0WS"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s0"),/* i2s0_ws */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rts */
		JLQ_FUNCTION(0x2, "ssi3"),/* ssi3_ssn */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[83] */
	JLQ_PIN(PINCTRL_PIN(87, "I2S1SDIN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s1"),/* i2s1_sdin */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_ssn */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[84] */
	JLQ_PIN(PINCTRL_PIN(88, "I2S1SDOUT"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s1"),/* i2s1_sdout */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_rx */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[85] */
	JLQ_PIN(PINCTRL_PIN(89, "I2S1SCLK"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s1"),/* i2s1_sclk */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_tx */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_cts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[86] */
	JLQ_PIN(PINCTRL_PIN(90, "I2S1WS"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2s1"),/* i2s1_ws */
		JLQ_FUNCTION(0x1, "ssi1"),/* ssi1_clk */
		JLQ_FUNCTION(0x2, "uart4"),/* uart4_rts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[87] */
	JLQ_PIN(PINCTRL_PIN(91, "SD0CLK"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_clk */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_tx */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[88] */
		JLQ_FUNCTION(0x3, "trace")),/* traceclk */
	JLQ_PIN(PINCTRL_PIN(92, "SD0CMD"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_cmd */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rx */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[89] */
		JLQ_FUNCTION(0x3, "trace")),/* tracectl */
	JLQ_PIN(PINCTRL_PIN(93, "SD0D3"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[3] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_cts */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[90] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[0] */
	JLQ_PIN(PINCTRL_PIN(94, "SD0D2"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[2] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rts */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[91] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[1] */
	JLQ_PIN(PINCTRL_PIN(95, "SD0D1"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[1] */
		JLQ_FUNCTION(0x1, "i2c1"),/* i2c1_scl */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[92] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[2] */
	JLQ_PIN(PINCTRL_PIN(96, "SD0D0"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[0] */
		JLQ_FUNCTION(0x1, "i2c1"),/* i2c1_sda */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[93] */
		JLQ_FUNCTION(0x3, "trace")),/* tracedata[3] */
	JLQ_PIN(PINCTRL_PIN(97, "SPIFLSCS"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "spi_flash"),/* spi_flash_cs */
		JLQ_FUNCTION(0x1, "i2s2"),/* i2s2_sdin */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[94] */
	JLQ_PIN(PINCTRL_PIN(98, "SPIFLSCLK"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "spi_flash"),/* spi_flash_clk */
		JLQ_FUNCTION(0x1, "i2s2"),/* i2s2_sdout */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[95] */
	JLQ_PIN(PINCTRL_PIN(99, "SPIFLSMOSI"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "spi_flash"),/* spi_flash_mosi */
		JLQ_FUNCTION(0x1, "i2s2"),/* i2s2_sclk */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_cts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[96] */
	JLQ_PIN(PINCTRL_PIN(100, "SPIFLSMISO"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "spi_flash"),/* spi_flash_miso */
		JLQ_FUNCTION(0x1, "i2s2"),/* i2s2_ws */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_rts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[97] */
	JLQ_PIN(PINCTRL_PIN(101, "SPIFLSWP"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "spi_flash"),/* spi_flash_wp */
		JLQ_FUNCTION(0x1, "i2c2"),/* i2c2_scl */
		JLQ_FUNCTION(0x2, "uart0"),/* uart0_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[98] */
	JLQ_PIN(PINCTRL_PIN(102, "SPIFLSHOLD"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "spi_flash"),/* spi_flash_hold */
		JLQ_FUNCTION(0x1, "i2c2"),/* i2c2_sda */
		JLQ_FUNCTION(0x2, "uart0"),/* uart0_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[99] */
	JLQ_PIN(PINCTRL_PIN(103, "OSCEN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "osc_en")),/* osc_en */
	JLQ_PIN(PINCTRL_PIN(104, "PWEN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "pwen")),/* pwen */
	JLQ_PIN(PINCTRL_PIN(105, "RSTOUT"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "rstout"),/* rstout */
		JLQ_FUNCTION(0x3, "gpio")),/* Gpio[41] */
	JLQ_PIN(PINCTRL_PIN(106, "WMRSTN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "wmprst_n")),/* wmprst_n */
	JLQ_PIN(PINCTRL_PIN(107, "PRSTN"),
		JLQ_PIN_TYPE(UNAVAILABLE_PIN),
		JLQ_FUNCTION(0x0, "prst_n")),/* prst_n */
	JLQ_PIN(PINCTRL_PIN(108, "TMOD0"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "tmode")),/* tmode */
	JLQ_PIN(PINCTRL_PIN(109, "BOOTCTL2"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "boot_ctl"),/* boot_ctl[2] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[100] */
	JLQ_PIN(PINCTRL_PIN(110, "BOOTCTL1"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "boot_ctl"),/* boot_ctl[1] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[101] */
	JLQ_PIN(PINCTRL_PIN(111, "BOOTCTL0"),
		JLQ_PIN_TYPE(PIN_TYPE2),
		JLQ_FUNCTION(0x0, "boot_ctl"),/* boot_ctl[0] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[102] */
	JLQ_PIN(PINCTRL_PIN(112, "CLK32K"),
		JLQ_PIN_TYPE(UNAVAILABLE_PIN),
		JLQ_FUNCTION(0x0, "clk32k")),/* clk32k */
	JLQ_PIN(PINCTRL_PIN(113, "CLKOSC"),
		JLQ_PIN_TYPE(UNAVAILABLE_PIN),
		JLQ_FUNCTION(0x0, "clkosc")),/* clkosc */
	JLQ_PIN(PINCTRL_PIN(114, "EMMC0D7"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[7] */
	JLQ_PIN(PINCTRL_PIN(115, "EMMC0D6"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[6] */
	JLQ_PIN(PINCTRL_PIN(116, "EMMC0D5"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[5] */
	JLQ_PIN(PINCTRL_PIN(117, "EMMC0D4"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[4] */
	JLQ_PIN(PINCTRL_PIN(118, "EMMC0D3"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[3] */
	JLQ_PIN(PINCTRL_PIN(119, "EMMC0D2"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[2] */
	JLQ_PIN(PINCTRL_PIN(120, "EMMC0D1"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[1] */
	JLQ_PIN(PINCTRL_PIN(121, "EMMC0D0"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_data[0] */
	JLQ_PIN(PINCTRL_PIN(122, "EMMC0CLK"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_clk */
	JLQ_PIN(PINCTRL_PIN(123, "EMMC0STR"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_strobe */
	JLQ_PIN(PINCTRL_PIN(124, "EMMC0CMD"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc")),/* emmc0_cmd */
};


struct jlq_gpio_to_pin ja310_gpio2pin[] =  {
	{.pin_index = 0, .gpio_muxval = 0x0},
	{.pin_index = 1, .gpio_muxval = 0x0},
	{.pin_index = 2, .gpio_muxval = 0x0},
	{.pin_index = 3, .gpio_muxval = 0x0},
	{.pin_index = 67, .gpio_muxval = 0x0},
	{.pin_index = 68, .gpio_muxval = 0x0},
	{.pin_index = 4, .gpio_muxval = 0x0},
	{.pin_index = 65, .gpio_muxval = 0x0},
	{.pin_index = 66, .gpio_muxval = 0x0},
	{.pin_index = 5, .gpio_muxval = 0x0},
	{.pin_index = 6, .gpio_muxval = 0x0},
	{.pin_index = 11, .gpio_muxval = 0x0},
	{.pin_index = 12, .gpio_muxval = 0x0},
	{.pin_index = 13, .gpio_muxval = 0x0},
	{.pin_index = 14, .gpio_muxval = 0x0},
	{.pin_index = 15, .gpio_muxval = 0x0},
	{.pin_index = 16, .gpio_muxval = 0x0},
	{.pin_index = 17, .gpio_muxval = 0x0},
	{.pin_index = 18, .gpio_muxval = 0x0},
	{.pin_index = 19, .gpio_muxval = 0x0},
	{.pin_index = 20, .gpio_muxval = 0x0},
	{.pin_index = 21, .gpio_muxval = 0x0},
	{.pin_index = 22, .gpio_muxval = 0x0},
	{.pin_index = 34, .gpio_muxval = 0x0},
	{.pin_index = 35, .gpio_muxval = 0x0},
	{.pin_index = 7, .gpio_muxval = 0x0},
	{.pin_index = 23, .gpio_muxval = 0x0},
	{.pin_index = 24, .gpio_muxval = 0x0},
	{.pin_index = 8, .gpio_muxval = 0x0},
	{.pin_index = 9, .gpio_muxval = 0x0},
	{.pin_index = 10, .gpio_muxval = 0x0},
	{.pin_index = 25, .gpio_muxval = 0x0},
	{.pin_index = 26, .gpio_muxval = 0x0},
	{.pin_index = 27, .gpio_muxval = 0x0},
	{.pin_index = 28, .gpio_muxval = 0x0},
	{.pin_index = 29, .gpio_muxval = 0x0},
	{.pin_index = 29, .gpio_muxval = 0x1},
	{.pin_index = 30, .gpio_muxval = 0x1},
	{.pin_index = 31, .gpio_muxval = 0x1},
	{.pin_index = 32, .gpio_muxval = 0x1},
	{.pin_index = 33, .gpio_muxval = 0x1},
	{.pin_index = 105, .gpio_muxval = 0x3},
	{.pin_index = 36, .gpio_muxval = 0x1},
	{.pin_index = 37, .gpio_muxval = 0x1},
	{.pin_index = 38, .gpio_muxval = 0x1},
	{.pin_index = 39, .gpio_muxval = 0x1},
	{.pin_index = 40, .gpio_muxval = 0x1},
	{.pin_index = 43, .gpio_muxval = 0x2},
	{.pin_index = 44, .gpio_muxval = 0x2},
	{.pin_index = 45, .gpio_muxval = 0x3},
	{.pin_index = 46, .gpio_muxval = 0x3},
	{.pin_index = 47, .gpio_muxval = 0x1},
	{.pin_index = 48, .gpio_muxval = 0x1},
	{.pin_index = 49, .gpio_muxval = 0x1},
	{.pin_index = 50, .gpio_muxval = 0x1},
	{.pin_index = 51, .gpio_muxval = 0x3},
	{.pin_index = 52, .gpio_muxval = 0x3},
	{.pin_index = 53, .gpio_muxval = 0x3},
	{.pin_index = 54, .gpio_muxval = 0x3},
	{.pin_index = 55, .gpio_muxval = 0x3},
	{.pin_index = 56, .gpio_muxval = 0x3},
	{.pin_index = 59, .gpio_muxval = 0x3},
	{.pin_index = 60, .gpio_muxval = 0x3},
	{.pin_index = 61, .gpio_muxval = 0x3},
	{.pin_index = 62, .gpio_muxval = 0x3},
	{.pin_index = 63, .gpio_muxval = 0x3},
	{.pin_index = 69, .gpio_muxval = 0x3},
	{.pin_index = 70, .gpio_muxval = 0x3},
	{.pin_index = 71, .gpio_muxval = 0x3},
	{.pin_index = 72, .gpio_muxval = 0x3},
	{.pin_index = 73, .gpio_muxval = 0x3},
	{.pin_index = 74, .gpio_muxval = 0x3},
	{.pin_index = 75, .gpio_muxval = 0x3},
	{.pin_index = 76, .gpio_muxval = 0x3},
	{.pin_index = 77, .gpio_muxval = 0x3},
	{.pin_index = 78, .gpio_muxval = 0x3},
	{.pin_index = 79, .gpio_muxval = 0x3},
	{.pin_index = 80, .gpio_muxval = 0x3},
	{.pin_index = 81, .gpio_muxval = 0x3},
	{.pin_index = 82, .gpio_muxval = 0x3},
	{.pin_index = 83, .gpio_muxval = 0x3},
	{.pin_index = 84, .gpio_muxval = 0x3},
	{.pin_index = 85, .gpio_muxval = 0x3},
	{.pin_index = 86, .gpio_muxval = 0x3},
	{.pin_index = 87, .gpio_muxval = 0x3},
	{.pin_index = 88, .gpio_muxval = 0x3},
	{.pin_index = 89, .gpio_muxval = 0x3},
	{.pin_index = 90, .gpio_muxval = 0x3},
	{.pin_index = 91, .gpio_muxval = 0x2},
	{.pin_index = 92, .gpio_muxval = 0x2},
	{.pin_index = 93, .gpio_muxval = 0x2},
	{.pin_index = 94, .gpio_muxval = 0x2},
	{.pin_index = 95, .gpio_muxval = 0x2},
	{.pin_index = 96, .gpio_muxval = 0x2},
	{.pin_index = 97, .gpio_muxval = 0x3},
	{.pin_index = 98, .gpio_muxval = 0x3},
	{.pin_index = 99, .gpio_muxval = 0x3},
	{.pin_index = 100, .gpio_muxval = 0x3},
	{.pin_index = 101, .gpio_muxval = 0x3},
	{.pin_index = 102, .gpio_muxval = 0x3},
	{.pin_index = 109, .gpio_muxval = 0x3},
	{.pin_index = 110, .gpio_muxval = 0x3},
	{.pin_index = 111, .gpio_muxval = 0x3}
};

static const char * const ja310_groups[] = {
	"GPIO0",
	"GPIO1",
	"GPIO2",
	"GPIO3",
	"GPIO6",
	"GPIO9",
	"GPIO10",
	"GPIO25",
	"GPIO28",
	"GPIO29",
	"GPIO30",
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
	"GPIO21",
	"GPIO22",
	"GPIO26",
	"GPIO27",
	"GPIO31",
	"GPIO32",
	"GPIO33",
	"GPIO34",
	"GPIO35",
	"SSI1_SSn",
	"SSI1_RX",
	"SSI1_TX",
	"SSI1_CLK",
	"GPIO23",
	"GPIO24",
	"PWM6",
	"SSI2_SSn",
	"SSI2_RX",
	"SSI2_TX",
	"SSI2_CLK",
	"SSI3_TX",
	"SSI3_RX",
	"SSI3_CLK",
	"SSI3_SSn",
	"PWM0",
	"PWM1",
	"PWM2",
	"PWM3",
	"PWM4",
	"PWM5",
	"U1TXD",
	"U1RXD",
	"U1CTS",
	"U1RTS",
	"U0TXD",
	"U0RXD",
	"U5TXD",
	"U5RXD",
	"IIC2SCL0",
	"IIC2SDA0",
	"IIC2SCL1",
	"IIC2SDA1",
	"IIC2SCL2",
	"IIC2SDA2",
	"GPIO7",
	"GPIO8",
	"GPIO4",
	"GPIO5",
	"IIC2SCL3",
	"IIC2SDA3",
	"IIC2SCL4",
	"IIC2SDA4",
	"IIC2SCL5",
	"IIC2SDA5",
	"I2S2SDIN",
	"I2S2SDOUT",
	"CLKO0",
	"CLKO1",
	"CLKO2",
	"CLKO3",
	"CLKO4",
	"CLKO5",
	"I2S0SDIN",
	"I2S0SDOUT",
	"I2S0SCLK",
	"I2S0WS",
	"I2S1SDIN",
	"I2S1SDOUT",
	"I2S1SCLK",
	"I2S1WS",
	"SD0CLK",
	"SD0CMD",
	"SD0D3",
	"SD0D2",
	"SD0D1",
	"SD0D0",
	"SPIFLSCS",
	"SPIFLSCLK",
	"SPIFLSMOSI",
	"SPIFLSMISO",
	"SPIFLSWP",
	"SPIFLSHOLD",
	"OSCEN",
	"PWEN",
	"RSTOUT",
	"WMRSTN",
	"PRSTN",
	"TMOD0",
	"BOOTCTL2",
	"BOOTCTL1",
	"BOOTCTL0",
	"CLK32K",
	"CLKOSC",
	"EMMC0D7",
	"EMMC0D6",
	"EMMC0D5",
	"EMMC0D4",
	"EMMC0D3",
	"EMMC0D2",
	"EMMC0D1",
	"EMMC0D0",
	"EMMC0CLK",
	"EMMC0STR",
	"EMMC0CMD"
};

static const char * const ja310_functions[] = {
	"gpio",
	"i2c2",
	"test_pin",
	"uart0",
	"i2c3",
	"uart1",
	"sd0",
	"isp0",
	"uart4",
	"sd1",
	"trace",
	"i2c4",
	"uart2",
	"pwm0",
	"eth",
	"pwm1",
	"pwm2",
	"pwm3",
	"uart3",
	"pwm4",
	"pwm5",
	"usbdrv",
	"isp1",
	"sd2",
	"ssi1",
	"pwm6",
	"ssi2",
	"ssi3",
	"jtag0",
	"i2s3",
	"ssi0",
	"i2s2",
	"com_uart",
	"i2s4",
	"i2c0",
	"i2c1",
	"pwm7",
	"i2c5",
	"clk_out",
	"i2s0",
	"i2s1",
	"spi_flash",
	"osc_en",
	"pwen",
	"rstout",
	"wmprst_n",
	"prst_n",
	"tmode",
	"boot_ctl",
	"clk32k",
	"clkosc",
	"emmc"
};

const struct jlq_pinctrl_desc ja310_pinctrl_data = {
	.pin_select = pin_select,
	.pins = ja310_pins,
	.npins = ARRAY_SIZE(ja310_pins),
	.gpio2pin = ja310_gpio2pin,
	.ngpios = ARRAY_SIZE(ja310_gpio2pin),
	.groups = ja310_groups,
	.ngroups = ARRAY_SIZE(ja310_groups),
	.functions = ja310_functions,
	.nfunctions = ARRAY_SIZE(ja310_functions),
};
EXPORT_SYMBOL(ja310_pinctrl_data);

MODULE_LICENSE("GPL");
