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

static const struct pin_select_type pin_select[] = {
	{	.pull_offset = 8,	.pull_mask = GENMASK(9, 8),
		.drive_offset = 4,	.drive_mask = GENMASK(5, 4),
		.slew_rate_offset = 3,	.slew_rate_mask = BIT(3),
		.schmit_offset = 2,	.schmit_mask = BIT(2),
		.mux_ctrl_offset = 0,	.mux_ctrl_mask = GENMASK(1, 0),
	},
	{	.pull_offset = 8,	.pull_mask = GENMASK(9, 8),
		.drive_offset = 4,	.drive_mask = GENMASK(6, 4),
		.slew_rate_offset = 3,	.slew_rate_mask = BIT(3),
		.schmit_offset = 2,	.schmit_mask = BIT(2),
		.mux_ctrl_offset = 0,	.mux_ctrl_mask = GENMASK(1, 0),
	},
};

static const struct jlq_desc_pin jr510_pins[] = {
	JLQ_PIN(PINCTRL_PIN(0, "UART1TX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_tx */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[0] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[0] */
	JLQ_PIN(PINCTRL_PIN(1, "UART1RX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_rx */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[1] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[1] */
	JLQ_PIN(PINCTRL_PIN(2, "UART1CTS"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_cts */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[2] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[2] */
	JLQ_PIN(PINCTRL_PIN(3, "UART1RTS"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart1"),/* uart1_rts */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[3] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[3] */
	JLQ_PIN(PINCTRL_PIN(4, "SLIM0CLK"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "slim0")),/* slim0_clk */
	JLQ_PIN(PINCTRL_PIN(5, "SLIM0DATA"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "slim0")),/* slim0_data */
	JLQ_PIN(PINCTRL_PIN(6, "UART0TX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart0"),/* uart0_tx */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[6] */
		JLQ_FUNCTION(0x2, "i2c5")),/* i2c5_scl */
	JLQ_PIN(PINCTRL_PIN(7, "UART0RX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "uart0"),/* uart0_rx */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[7] */
		JLQ_FUNCTION(0x2, "i2c5")),/* i2c5_sda */
	JLQ_PIN(PINCTRL_PIN(8, "GPIO8"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[8] */
		JLQ_FUNCTION(0x1, "com")),/* com_uart_tx */
	JLQ_PIN(PINCTRL_PIN(9, "GPIO9"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[9] */
		JLQ_FUNCTION(0x1, "com")),/* com_uart_rx */
	JLQ_PIN(PINCTRL_PIN(10, "COMUARTTX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "com_uart"),/* com_uart_tx */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[10] */
		JLQ_FUNCTION(0x2, "i2c3"),/* i2c3_scl */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[4] */
	JLQ_PIN(PINCTRL_PIN(11, "COMUARTRX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "com_uart"),/* com_uart_rx */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[11] */
		JLQ_FUNCTION(0x2, "i2c3"),/* i2c3_sda */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[5] */
	JLQ_PIN(PINCTRL_PIN(12, "GPIO12"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[12] */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_tx　 */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[6] */
	JLQ_PIN(PINCTRL_PIN(13, "GPIO13"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[13] */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_rx */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[7] */
	JLQ_PIN(PINCTRL_PIN(14, "GPIO14"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[14] */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_clk */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[8] */
	JLQ_PIN(PINCTRL_PIN(15, "GPIO15"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[15] */
		JLQ_FUNCTION(0x1, "ssi0"),/* ssi0_ssn */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[9] */
	JLQ_PIN(PINCTRL_PIN(16, "I2C3SCL"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c3"),/* i2c3_scl */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_tx */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[16] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[10] */
	JLQ_PIN(PINCTRL_PIN(17, "I2C3SDA"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c3"),/* i2c3_sda */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rx */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[17] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[11] */
	JLQ_PIN(PINCTRL_PIN(18, "GPIO18"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[18] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_cts */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[12] */
	JLQ_PIN(PINCTRL_PIN(19, "GPIO19"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[19] */
		JLQ_FUNCTION(0x1, "uart2"),/* uart2_rts */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[13] */
	JLQ_PIN(PINCTRL_PIN(20, "I2C4SCL"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c4"),/* i2c4_scl */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[20] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[14] */
	JLQ_PIN(PINCTRL_PIN(21, "I2C4SDA"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c4"),/* i2c4_sda */
		JLQ_FUNCTION(0x2, "gpio"),/* gpio[21] */
		JLQ_FUNCTION(0x3, "testpin")),/* testpin[15] */
	JLQ_PIN(PINCTRL_PIN(22, "I2C5SCL"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c5"),/* i2c5_scl */
		JLQ_FUNCTION(0x2, "gpio")),/* gpio[22] */
	JLQ_PIN(PINCTRL_PIN(23, "I2C5SDA"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "i2c5"),/* i2c5_sda */
		JLQ_FUNCTION(0x2, "gpio")),/* gpio[23] */
	JLQ_PIN(PINCTRL_PIN(24, "GPIO24"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[24] */
		JLQ_FUNCTION(0x1, "i2s1"),/* i2s1_sdin */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[1] */
		JLQ_FUNCTION(0x3, "cm4_gpio")),/* cm4_gpio[2] */
	JLQ_PIN(PINCTRL_PIN(25, "GPIO25"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[25] */
		JLQ_FUNCTION(0x1, "i2s1"),/* i2s1_sdout */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[0] */
		JLQ_FUNCTION(0x3, "cm4_gpio")),/* cm4_gpio[3] */
	JLQ_PIN(PINCTRL_PIN(26, "GPIO26"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[26] */
		JLQ_FUNCTION(0x1, "i2s1"),/* i2s1_sclk */
		JLQ_FUNCTION(0x2, "traceclk"),/* traceclk */
		JLQ_FUNCTION(0x3, "cm4_gpio")),/* cm4_gpio[4] */
	JLQ_PIN(PINCTRL_PIN(27, "GPIO27"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[27] */
		JLQ_FUNCTION(0x1, "i2s1"),/* i2s1_ws */
		JLQ_FUNCTION(0x2, "tracectl"),/* tracectl */
		JLQ_FUNCTION(0x3, "cm4_gpio")),/* cm4_gpio[0] */
	JLQ_PIN(PINCTRL_PIN(28, "GPIO28"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[28] */
		JLQ_FUNCTION(0x1, "pwm0"),/* pwm0 */
		JLQ_FUNCTION(0x3, "cm4_gpio")),/* cm4_gpio[1] */
	JLQ_PIN(PINCTRL_PIN(29, "SD0CLK"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_clk */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[29] */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[9] */
		JLQ_FUNCTION(0x3, "uart2")),/* uart2_tx */
	JLQ_PIN(PINCTRL_PIN(30, "SD0CMD"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_cmd */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[30] */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[8] */
		JLQ_FUNCTION(0x3, "uart2")),/* uart2_rx */
	JLQ_PIN(PINCTRL_PIN(31, "SD0DATA3"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[3] */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[31] */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[7] */
		JLQ_FUNCTION(0x3, "uart2")),/* uart2_cts */
	JLQ_PIN(PINCTRL_PIN(32, "SD0DATA2"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[2] */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[32] */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[6] */
		JLQ_FUNCTION(0x3, "uart2")),/* uart2_rts */
	JLQ_PIN(PINCTRL_PIN(33, "SD0DATA1"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[1] */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[33] */
		JLQ_FUNCTION(0x2, "tracedata")),/* tracedata[5] */
	JLQ_PIN(PINCTRL_PIN(34, "SD0DATA0"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd0"),/* sd0_data[0] */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[34] */
		JLQ_FUNCTION(0x2, "tracedata")),/* tracedata[4] */
	JLQ_PIN(PINCTRL_PIN(35, "GPIO35"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[35] */
		JLQ_FUNCTION(0x1, "sd0"),/* sd0_detect_n */
		JLQ_FUNCTION(0x2, "tracedata")),/* tracedata[3] */
	JLQ_PIN(PINCTRL_PIN(36, "GPIO36"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[36] */
		JLQ_FUNCTION(0x1, "sd0"),/* sd0_write_prot */
		JLQ_FUNCTION(0x2, "tracedata")),/* tracedata[2] */
	JLQ_PIN(PINCTRL_PIN(37, "SD1CLK"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd1"),/* sd1_clk */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_tx　 */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[15] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[37] */
	JLQ_PIN(PINCTRL_PIN(38, "SD1CMD"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd1"),/* sd1_cmd */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_rx */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[14] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[38] */
	JLQ_PIN(PINCTRL_PIN(39, "SD1DATA3"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd1"),/* sd1_data[3] */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_clk */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[13] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[39] */
	JLQ_PIN(PINCTRL_PIN(40, "SD1DATA2"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd1"),/* sd1_data[2] */
		JLQ_FUNCTION(0x1, "ssi2"),/* ssi2_ssn */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[12] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[40] */
	JLQ_PIN(PINCTRL_PIN(41, "SD1DATA1"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd1"),/* sd1_data[1] */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[11] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[41] */
	JLQ_PIN(PINCTRL_PIN(42, "SD1DATA0"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "sd1"),/* sd1_data[0] */
		JLQ_FUNCTION(0x2, "tracedata"),/* tracedata[10] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[42] */
	JLQ_PIN(PINCTRL_PIN(43, "GPIO43"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[43] */
		JLQ_FUNCTION(0x2, "i2c6")),/* i2c6_scl */
	JLQ_PIN(PINCTRL_PIN(44, "CLKOUT4"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "clk_out"),/* clk_out4 */
		JLQ_FUNCTION(0x2, "i2c6"),/* i2c6_sda */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[44] */
	JLQ_PIN(PINCTRL_PIN(45, "GPIO51"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio")),/* gpio[51] */
	JLQ_PIN(PINCTRL_PIN(46, "GPIO54"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[54] */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_scl */
		JLQ_FUNCTION(0x2, "pwm0"),/* pwm0 */
		JLQ_FUNCTION(0x3, "ssi1")),/* ssi1_clk */
	JLQ_PIN(PINCTRL_PIN(47, "GPIO55"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[55] */
		JLQ_FUNCTION(0x1, "i2c4"),/* i2c4_sda */
		JLQ_FUNCTION(0x2, "pwm1"),/* pwm1 */
		JLQ_FUNCTION(0x3, "ssi1")),/* ssi1_ssn */
	JLQ_PIN(PINCTRL_PIN(48, "SSI0TX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi0"),/* ssi0_tx　 */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_tx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[56] */
	JLQ_PIN(PINCTRL_PIN(49, "SSI0RX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi0"),/* ssi0_rx */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_rx */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[57] */
	JLQ_PIN(PINCTRL_PIN(50, "SSI0CLK"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi0"),/* ssi0_clk */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_cts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[58] */
	JLQ_PIN(PINCTRL_PIN(51, "SSI0SSN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi0"),/* ssi0_ssn */
		JLQ_FUNCTION(0x2, "uart2"),/* uart2_rts */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[59] */
	JLQ_PIN(PINCTRL_PIN(52, "GPIO60"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[60] */
		JLQ_FUNCTION(0x1, "i2c3"),/* i2c3_scl */
		JLQ_FUNCTION(0x2, "dmic")),/* dmic_data1 */
	JLQ_PIN(PINCTRL_PIN(53, "GPIO61"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[61] */
		JLQ_FUNCTION(0x1, "i2c3"),/* i2c3_sda */
		JLQ_FUNCTION(0x2, "dmic")),/* dmic_clk1 */
	JLQ_PIN(PINCTRL_PIN(54, "SSI1TX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_tx　 */
		JLQ_FUNCTION(0x2, "dmic"),/* dmic_data1 */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[62] */
	JLQ_PIN(PINCTRL_PIN(55, "SSI1RX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_rx */
		JLQ_FUNCTION(0x2, "dmic"),/* dmic_clk1 */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[63] */
	JLQ_PIN(PINCTRL_PIN(56, "SSI1CLK"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_clk */
		JLQ_FUNCTION(0x2, "dmic"),/* dmic_data2 */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[64] */
	JLQ_PIN(PINCTRL_PIN(57, "SSI1SSN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi1"),/* ssi1_ssn */
		JLQ_FUNCTION(0x2, "dmic"),/* dmic_clk2 */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[65] */
	JLQ_PIN(PINCTRL_PIN(58, "SSI2TX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_tx　 */
		JLQ_FUNCTION(0x2, "i2c6"),/* i2c6_scl */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[66] */
	JLQ_PIN(PINCTRL_PIN(59, "SSI2RX"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_rx */
		JLQ_FUNCTION(0x2, "i2c6"),/* i2c6_sda */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[67] */
	JLQ_PIN(PINCTRL_PIN(60, "SSI2CLK"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_clk */
		JLQ_FUNCTION(0x1, "i2c6"),/* i2c6_scl */
		JLQ_FUNCTION(0x2, "pwm0"),/* pwm0 */
		JLQ_FUNCTION(0x3, "camille")),/* camille_pwm */
	JLQ_PIN(PINCTRL_PIN(61, "SSI2SSN"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ssi2"),/* ssi2_ssn */
		JLQ_FUNCTION(0x1, "i2c6"),/* i2c6_sda */
		JLQ_FUNCTION(0x2, "pwm1"),/* pwm1 */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[68] */
	JLQ_PIN(PINCTRL_PIN(62, "GPIO69"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio")),/* gpio[69] */
	JLQ_PIN(PINCTRL_PIN(63, "GPIO70"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[70] */
		JLQ_FUNCTION(0x2, "ssi1")),/* ssi1_tx　 */
	JLQ_PIN(PINCTRL_PIN(64, "GPIO71"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[71] */
		JLQ_FUNCTION(0x2, "ssi1")),/* ssi1_rx */
	JLQ_PIN(PINCTRL_PIN(65, "SRST0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "srst0"),/* srst0 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[72] */
		JLQ_FUNCTION(0x2, "ssi1"),/* ssi1_tx　 */
		JLQ_FUNCTION(0x3, "i2s0")),/* i2s0_sdin */
	JLQ_PIN(PINCTRL_PIN(66, "TCK0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "tck0"),/* tck0 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[73] */
		JLQ_FUNCTION(0x2, "ssi1"),/* ssi1_rx */
		JLQ_FUNCTION(0x3, "i2s0")),/* i2s0_sdout */
	JLQ_PIN(PINCTRL_PIN(67, "TDI0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "tdi0"),/* tdi0 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[74] */
		JLQ_FUNCTION(0x2, "ssi1"),/* ssi1_clk */
		JLQ_FUNCTION(0x3, "i2s0")),/* i2s0_sclk */
	JLQ_PIN(PINCTRL_PIN(68, "TDO0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "tdo0"),/* tdo0 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[75] */
		JLQ_FUNCTION(0x2, "ssi1"),/* ssi1_ssn */
		JLQ_FUNCTION(0x3, "i2s0")),/* i2s0_ws */
	JLQ_PIN(PINCTRL_PIN(69, "TMS0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "tms0"),/* tms0 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[76] */
		JLQ_FUNCTION(0x2, "pwm0"),/* pwm0 */
		JLQ_FUNCTION(0x3, "camille")),/* camille_pwm */
	JLQ_PIN(PINCTRL_PIN(70, "NTRST0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "ntrst0"),/* ntrst0 */
		JLQ_FUNCTION(0x1, "gpio"),/* gpio[77] */
		JLQ_FUNCTION(0x2, "pwm1")),/* pwm1 */
	JLQ_PIN(PINCTRL_PIN(71, "GPIO78"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[78] */
		JLQ_FUNCTION(0x1, "i2c6"),/* i2c6_scl */
		JLQ_FUNCTION(0x2, "dmic"),/* dmic_data2 */
		JLQ_FUNCTION(0x3, "cm4_gpio")),/* cm4_gpio[5] */
	JLQ_PIN(PINCTRL_PIN(72, "GPIO79"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio"),/* gpio[79] */
		JLQ_FUNCTION(0x1, "i2c6"),/* i2c6_sda */
		JLQ_FUNCTION(0x2, "dmic"),/* dmic_clk2 */
		JLQ_FUNCTION(0x3, "cm4_gpio")),/* cm4_gpio[6] */
	JLQ_PIN(PINCTRL_PIN(73, "GPIO80"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "gpio")),/* gpio[80] */
	JLQ_PIN(PINCTRL_PIN(74, "EMMCCLK"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_clk */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[4] */
	JLQ_PIN(PINCTRL_PIN(75, "EMMCCMD"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_cmd */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[5] */
	JLQ_PIN(PINCTRL_PIN(76, "EMMCDATA0"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[0] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[45] */
	JLQ_PIN(PINCTRL_PIN(77, "EMMCDATA1"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[1] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[46] */
	JLQ_PIN(PINCTRL_PIN(78, "EMMCDATA2"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[2] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[47] */
	JLQ_PIN(PINCTRL_PIN(79, "EMMCDATA3"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[3] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[48] */
	JLQ_PIN(PINCTRL_PIN(80, "EMMCDATA4"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[4] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[49] */
	JLQ_PIN(PINCTRL_PIN(81, "EMMCDATA5"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[5] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[50] */
	JLQ_PIN(PINCTRL_PIN(82, "EMMCDATA6"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[6] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[52] */
	JLQ_PIN(PINCTRL_PIN(83, "EMMCDATA7"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_data[7] */
		JLQ_FUNCTION(0x3, "gpio")),/* gpio[53] */
	JLQ_PIN(PINCTRL_PIN(84, "EMMCSTROBE"),
		JLQ_PIN_TYPE(PIN_TYPE1),
		JLQ_FUNCTION(0x0, "emmc"),/* emmc_strobe */
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[81] */
	JLQ_PIN(PINCTRL_PIN(85, "TMOD"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "tmode")),/* tmode */
	JLQ_PIN(PINCTRL_PIN(86, "BOOTCTL0"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "boot"),/* boot_ctl[0] */
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[82] */
	JLQ_PIN(PINCTRL_PIN(87, "BOOTCTL1"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "boot"),/* boot_ctl[1] */
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[83] */
	JLQ_PIN(PINCTRL_PIN(88, "BOOTCTL2"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "boot"),/* boot_ctl[2] */
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[84] */
	JLQ_PIN(PINCTRL_PIN(89, "BOOTCTL3"),
		JLQ_PIN_TYPE(PIN_TYPE0),
		JLQ_FUNCTION(0x0, "boot"),/* boot_ctl[3] */
		JLQ_FUNCTION(0x1, "gpio")),/* gpio[85] */

};

struct jlq_gpio_to_pin jr510_gpio2pin[] =  {
	{.pin_index = 0, .gpio_muxval = 0x1},
	{.pin_index = 1, .gpio_muxval = 0x1},
	{.pin_index = 2, .gpio_muxval = 0x1},
	{.pin_index = 3, .gpio_muxval = 0x1},
	{.pin_index = 74, .gpio_muxval = 0x3},
	{.pin_index = 75, .gpio_muxval = 0x3},
	{.pin_index = 6, .gpio_muxval = 0x1},
	{.pin_index = 7, .gpio_muxval = 0x1},
	{.pin_index = 8, .gpio_muxval = 0x0},
	{.pin_index = 9, .gpio_muxval = 0x0},
	{.pin_index = 10, .gpio_muxval = 0x1},
	{.pin_index = 11, .gpio_muxval = 0x1},
	{.pin_index = 12, .gpio_muxval = 0x0},
	{.pin_index = 13, .gpio_muxval = 0x0},
	{.pin_index = 14, .gpio_muxval = 0x0},
	{.pin_index = 15, .gpio_muxval = 0x0},
	{.pin_index = 16, .gpio_muxval = 0x2},
	{.pin_index = 17, .gpio_muxval = 0x2},
	{.pin_index = 18, .gpio_muxval = 0x0},
	{.pin_index = 19, .gpio_muxval = 0x0},
	{.pin_index = 20, .gpio_muxval = 0x2},
	{.pin_index = 21, .gpio_muxval = 0x2},
	{.pin_index = 22, .gpio_muxval = 0x2},
	{.pin_index = 23, .gpio_muxval = 0x2},
	{.pin_index = 24, .gpio_muxval = 0x0},
	{.pin_index = 25, .gpio_muxval = 0x0},
	{.pin_index = 26, .gpio_muxval = 0x0},
	{.pin_index = 27, .gpio_muxval = 0x0},
	{.pin_index = 28, .gpio_muxval = 0x0},
	{.pin_index = 29, .gpio_muxval = 0x1},
	{.pin_index = 30, .gpio_muxval = 0x1},
	{.pin_index = 31, .gpio_muxval = 0x1},
	{.pin_index = 32, .gpio_muxval = 0x1},
	{.pin_index = 33, .gpio_muxval = 0x1},
	{.pin_index = 34, .gpio_muxval = 0x1},
	{.pin_index = 35, .gpio_muxval = 0x0},
	{.pin_index = 36, .gpio_muxval = 0x0},
	{.pin_index = 37, .gpio_muxval = 0x3},
	{.pin_index = 38, .gpio_muxval = 0x3},
	{.pin_index = 39, .gpio_muxval = 0x3},
	{.pin_index = 40, .gpio_muxval = 0x3},
	{.pin_index = 41, .gpio_muxval = 0x3},
	{.pin_index = 42, .gpio_muxval = 0x3},
	{.pin_index = 43, .gpio_muxval = 0x0},
	{.pin_index = 44, .gpio_muxval = 0x3},
	{.pin_index = 76, .gpio_muxval = 0x3},
	{.pin_index = 77, .gpio_muxval = 0x3},
	{.pin_index = 78, .gpio_muxval = 0x3},
	{.pin_index = 79, .gpio_muxval = 0x3},
	{.pin_index = 80, .gpio_muxval = 0x3},
	{.pin_index = 81, .gpio_muxval = 0x3},
	{.pin_index = 45, .gpio_muxval = 0x0},
	{.pin_index = 82, .gpio_muxval = 0x3},
	{.pin_index = 83, .gpio_muxval = 0x3},
	{.pin_index = 46, .gpio_muxval = 0x0},
	{.pin_index = 47, .gpio_muxval = 0x0},
	{.pin_index = 48, .gpio_muxval = 0x3},
	{.pin_index = 49, .gpio_muxval = 0x3},
	{.pin_index = 50, .gpio_muxval = 0x3},
	{.pin_index = 51, .gpio_muxval = 0x3},
	{.pin_index = 52, .gpio_muxval = 0x0},
	{.pin_index = 53, .gpio_muxval = 0x0},
	{.pin_index = 54, .gpio_muxval = 0x3},
	{.pin_index = 55, .gpio_muxval = 0x3},
	{.pin_index = 56, .gpio_muxval = 0x3},
	{.pin_index = 57, .gpio_muxval = 0x3},
	{.pin_index = 58, .gpio_muxval = 0x3},
	{.pin_index = 59, .gpio_muxval = 0x3},
	{.pin_index = 61, .gpio_muxval = 0x3},
	{.pin_index = 62, .gpio_muxval = 0x0},
	{.pin_index = 63, .gpio_muxval = 0x0},
	{.pin_index = 64, .gpio_muxval = 0x0},
	{.pin_index = 65, .gpio_muxval = 0x1},
	{.pin_index = 66, .gpio_muxval = 0x1},
	{.pin_index = 67, .gpio_muxval = 0x1},
	{.pin_index = 68, .gpio_muxval = 0x1},
	{.pin_index = 69, .gpio_muxval = 0x1},
	{.pin_index = 70, .gpio_muxval = 0x1},
	{.pin_index = 71, .gpio_muxval = 0x0},
	{.pin_index = 72, .gpio_muxval = 0x0},
	{.pin_index = 73, .gpio_muxval = 0x0},
	{.pin_index = 84, .gpio_muxval = 0x1},
	{.pin_index = 86, .gpio_muxval = 0x1},
	{.pin_index = 87, .gpio_muxval = 0x1},
	{.pin_index = 88, .gpio_muxval = 0x1},
	{.pin_index = 89, .gpio_muxval = 0x1}
};

static const char * const jr510_groups[] = {
	"UART1TX",
	"UART1RX",
	"UART1CTS",
	"UART1RTS",
	"SLIM0CLK",
	"SLIM0DATA",
	"UART0TX",
	"UART0RX",
	"GPIO8",
	"GPIO9",
	"COMUARTTX",
	"COMUARTRX",
	"GPIO12",
	"GPIO13",
	"GPIO14",
	"GPIO15",
	"I2C3SCL",
	"I2C3SDA",
	"GPIO18",
	"GPIO19",
	"I2C4SCL",
	"I2C4SDA",
	"I2C5SCL",
	"I2C5SDA",
	"GPIO24",
	"GPIO25",
	"GPIO26",
	"GPIO27",
	"GPIO28",
	"SD0CLK",
	"SD0CMD",
	"SD0DATA3",
	"SD0DATA2",
	"SD0DATA1",
	"SD0DATA0",
	"GPIO35",
	"GPIO36",
	"SD1CLK",
	"SD1CMD",
	"SD1DATA3",
	"SD1DATA2",
	"SD1DATA1",
	"SD1DATA0",
	"GPIO43",
	"CLKOUT4",
	"GPIO51",
	"GPIO54",
	"GPIO55",
	"SSI0TX",
	"SSI0RX",
	"SSI0CLK",
	"SSI0SSN",
	"GPIO60",
	"GPIO61",
	"SSI1TX",
	"SSI1RX",
	"SSI1CLK",
	"SSI1SSN",
	"SSI2TX",
	"SSI2RX",
	"SSI2CLK",
	"SSI2SSN",
	"GPIO69",
	"GPIO70",
	"GPIO71",
	"SRST0",
	"TCK0",
	"TDI0",
	"TDO0",
	"TMS0",
	"NTRST0",
	"GPIO78",
	"GPIO79",
	"GPIO80",
	"EMMCCLK",
	"EMMCCMD",
	"EMMCDATA0",
	"EMMCDATA1",
	"EMMCDATA2",
	"EMMCDATA3",
	"EMMCDATA4",
	"EMMCDATA5",
	"EMMCDATA6",
	"EMMCDATA7",
	"EMMCSTROBE",
	"TMOD",
	"BOOTCTL0",
	"BOOTCTL1",
	"BOOTCTL2",
	"BOOTCTL3"
};

static const char * const jr510_functions[] = {
	"uart1",
	"gpio",
	"testpin",
	"slim0",
	"uart0",
	"i2c5",
	"com",
	"com_uart",
	"i2c3",
	"ssi0",
	"uart2",
	"i2c4",
	"i2s1",
	"tracedata",
	"cm4_gpio",
	"traceclk",
	"tracectl",
	"pwm0",
	"sd0",
	"sd1",
	"ssi2",
	"i2c6",
	"clk_out",
	"ssi1",
	"pwm1",
	"dmic",
	"camille",
	"srst0",
	"i2s0",
	"tck0",
	"tdi0",
	"tdo0",
	"tms0",
	"ntrst0",
	"emmc",
	"tmode",
	"boot"
};

const struct jlq_pinctrl_desc jr510_pinctrl_data = {
	.pin_select = pin_select,
	.pins = jr510_pins,
	.npins = ARRAY_SIZE(jr510_pins),
	.gpio2pin = jr510_gpio2pin,
	.ngpios = ARRAY_SIZE(jr510_gpio2pin),
	.groups = jr510_groups,
	.ngroups = ARRAY_SIZE(jr510_groups),
	.functions = jr510_functions,
	.nfunctions = ARRAY_SIZE(jr510_functions),
};
EXPORT_SYMBOL(jr510_pinctrl_data);

MODULE_LICENSE("GPL");
