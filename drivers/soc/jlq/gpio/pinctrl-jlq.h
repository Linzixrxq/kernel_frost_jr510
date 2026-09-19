/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __PINCTRL_JLQ_H
#define __PINCTRL_JLQ_H

#include <linux/kernel.h>

#define JLQ_PIN_TYPE(x) (x)

enum pin_type {
	PIN_TYPE0 = 0,
	PIN_TYPE1,
	PIN_TYPE2,
	UNAVAILABLE_PIN,
};

struct jlq_desc_function {
	const char *name;
	u8 muxval;
};

struct jlq_desc_pin {
	struct pinctrl_pin_desc pin;
	int pin_type;
	struct jlq_desc_function *functions;
};

struct jlq_gpio_to_pin {
	u32 pin_index;
	u32 gpio_muxval;
};

struct pin_select_type {
	u32 schmit_offset;
	u32 schmit_mask;
	u32 pull_offset;
	u32 pull_mask;
	u32 drive_offset;
	u32 drive_mask;
	u32 slew_rate_offset;
	u32 slew_rate_mask;
	u32 mux_ctrl_offset;
	u32 mux_ctrl_mask;
};

struct jlq_pinctrl_desc {
	const struct pin_select_type *pin_select;
	const struct jlq_desc_pin *pins;
	int npins;
	const struct jlq_gpio_to_pin *gpio2pin;
	int ngpios;
	const char *const *groups;
	int ngroups;
	const char *const *functions;
	int nfunctions;
};


#define JLQ_PIN(_pin, _pin_type, ...)					\
	{							\
		.pin = _pin,					\
		.pin_type = _pin_type,					\
		.functions = (struct jlq_desc_function[]){	\
			__VA_ARGS__, { } },			\
	}

#define JLQ_FUNCTION(_val, _name)				\
	{							\
		.name = _name,					\
		.muxval = _val,					\
	}

#define MUXPIN_REG(base, x)	 (base + ((x) * 4))
#define GPIO_REG(base, gpio, num)	(base + ((gpio) / num) * 4)
/* Registers. */
#define GPIO_PORT_DR(gpio)			GPIO_REG(0x00, gpio, 16)
#define GPIO_PORT_DDR(gpio)			GPIO_REG(0x40, gpio, 16)
#define GPIO_EXT_PORT(gpio)			GPIO_REG(0x80, gpio, 32)
#define GPIO_DEBOUNCE(gpio)			GPIO_REG(0x1A0, gpio, 16)
#define GPIO_INTR_CTRL(gpio)		GPIO_REG(0xA0, gpio, 4)
#define GPIO_INTR_RAW(gpio)			GPIO_REG(0x1E0, gpio, 32)
#define GPIO_INTR_CLR(gpio)			GPIO_REG(0x200, gpio, 32)
#define GPIO_INTR_MASK(gpio)		GPIO_REG(0x220, gpio, 16)
#define GPIO_INTR_STATUS(gpio)		GPIO_REG(0x260, gpio, 32)

#endif /* __PINCTRL_JLQ_H */
