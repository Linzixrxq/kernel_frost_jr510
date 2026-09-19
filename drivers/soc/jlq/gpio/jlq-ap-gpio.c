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

#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/types.h>
#include "pinctrl-jlq.h"
#include <core.h>
#include <pinctrl-utils.h>
#include <dt-bindings/pinctrl/jlq_pinctrl.h>

static const struct pinconf_generic_params jlq_pinctrl_bindings[] = {
	{"jlq,drive", PIN_CONFIG_DRIVE_STRENGTH, 0},
	{"jlq,pull", PIN_CONFIG_BIAS_PULL_UP, 0},
	{"jlq,schmitt", PIN_CONFIG_INPUT_SCHMITT, 0},
	{"jlq,slew_rate", PIN_CONFIG_SLEW_RATE, 0},
};

struct jlq_pin_config {
	u16 strength;
	u16 pull_up;
	u16 schmitt;
	u16 slew_rate;
};

struct jlq_pinctrl_pad {
	const struct jlq_desc_pin *pin_desc;
	struct jlq_desc_function *func;
	struct jlq_pinctrl_to_pin *gpio2pin;
	const struct pin_select_type *bit_map;
	struct jlq_pin_config pin_config;
	const char *group_name;
	int is_enabled;
};

struct jlq_pinctrl_state {
	struct device	*dev;
	void __iomem *pintcrl_reg_base;
	void __iomem *gpio_reg_base;
	spinlock_t pinctrl_lock;
	spinlock_t gpio_lock;
	struct jlq_pinctrl_pad *pads;
	struct pinctrl_dev *ctrl;
	const struct jlq_pinctrl_desc *pinctrl_data;
	struct gpio_chip chip;
	unsigned int irq;
	struct irq_domain *domain;
	unsigned int irq_clksrc[4];
};

static void jlq_pinctrl_set_mux(struct jlq_pinctrl_state *state,
		unsigned int pin, unsigned int mux)
{
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	unsigned long flags;
	void __iomem  *reg;
	unsigned int val;
	u32 mask = pad->bit_map->mux_ctrl_mask;
	u32 offset = pad->bit_map->mux_ctrl_offset;
	u32 mux_val = (mux << offset) & mask;

	dev_dbg(state->dev, "set mux: pin[%d] is %s, mux is %d\n", pin, pad->group_name, mux);

	spin_lock_irqsave(&state->pinctrl_lock, flags);

	reg = MUXPIN_REG(state->pintcrl_reg_base, pin);
	val = readl(reg);
	val = (val & (~mask)) | mux_val;
	writel(val, reg);

	spin_unlock_irqrestore(&state->pinctrl_lock, flags);
}

static void jlq_pinctrl_set_pull(struct jlq_pinctrl_state *state,
		unsigned int pin, unsigned int pull)
{
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	unsigned long flags;
	void __iomem  *reg;
	unsigned int val;
	u32 mask = pad->bit_map->pull_mask;
	u32 offset = pad->bit_map->pull_offset;
	u32 pull_val = (pull << offset) & mask;

	dev_dbg(state->dev, "set pull: pin[%d] is %s, pull is %d\n", pin, pad->group_name, pull);

	spin_lock_irqsave(&state->pinctrl_lock, flags);

	reg = MUXPIN_REG(state->pintcrl_reg_base, pin);
	val = readl(reg);
	val = (val & (~mask)) | pull_val;
	writel(val, reg);

	spin_unlock_irqrestore(&state->pinctrl_lock, flags);
}

static void jlq_pinctrl_set_strength(struct jlq_pinctrl_state *state,
		unsigned int pin, unsigned int drive)
{
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	unsigned long flags;
	void __iomem  *reg;
	unsigned int val;
	u32 mask = pad->bit_map->drive_mask;
	u32 offset = pad->bit_map->drive_offset;
	u32 drive_strength = (drive << offset) & mask;

	dev_dbg(state->dev, "set drive: pin[%d] is %s, drive is %d\n", pin, pad->group_name, drive);

	spin_lock_irqsave(&state->pinctrl_lock, flags);

	reg = MUXPIN_REG(state->pintcrl_reg_base, pin);
	val = readl(reg);
	val = (val & (~mask)) | drive_strength;
	writel(val, reg);

	spin_unlock_irqrestore(&state->pinctrl_lock, flags);
}

static void jlq_pinctrl_schmitt_enable(struct jlq_pinctrl_state *state,
		unsigned int pin, unsigned int enable)
{
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	unsigned long flags;
	void __iomem  *reg;
	unsigned int val;
	u32 mask = pad->bit_map->schmit_mask;

	dev_dbg(state->dev, "enable schmit: pin is pin[%d] is %s, is %s\n",
						pin, pad->group_name, enable ? "enab1e" : "false");

	spin_lock_irqsave(&state->pinctrl_lock, flags);

	reg = MUXPIN_REG(state->pintcrl_reg_base, pin);
	val = readl(reg);
	val = val & (~mask);
	if (enable)
		val = val | mask;
	writel(val, reg);

	spin_unlock_irqrestore(&state->pinctrl_lock, flags);
}

static void jlq_pinctrl_slew_rate_enable(struct jlq_pinctrl_state *state,
		unsigned int pin, unsigned int enable)
{
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	unsigned long flags;
	void __iomem  *reg;
	unsigned int val;
	u32 mask = pad->bit_map->slew_rate_mask;

	dev_dbg(state->dev, "enable slew rate: pin is pin[%d] is %s, is %s\n",
						pin, pad->group_name, enable ? "enab1e" : "false");

	spin_lock_irqsave(&state->pinctrl_lock, flags);

	reg = MUXPIN_REG(state->pintcrl_reg_base, pin);
	val = readl(reg);
	val = val & (~mask);
	if (enable)
		val = val | mask;
	writel(val, reg);

	spin_unlock_irqrestore(&state->pinctrl_lock, flags);
}


static int jlq_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	/* Every PIN is a group */
	return pctldev->desc->npins;
}

static const char *jlq_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
					    unsigned int pin)
{
	return pctldev->desc->pins[pin].name;
}

static int jlq_pinctrl_get_group_pins(struct pinctrl_dev *pctldev, unsigned int pin,
				    const unsigned int **pins, unsigned int *num_pins)
{
	*pins = &pctldev->desc->pins[pin].number;
	*num_pins = 1;
	return 0;
}

static const struct pinctrl_ops jlq_pinctrl_ops = {
	.get_groups_count	= jlq_pinctrl_get_groups_count,
	.get_group_name		= jlq_pinctrl_get_group_name,
	.get_group_pins		= jlq_pinctrl_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int jlq_pinctrl_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct jlq_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);

	return state->pinctrl_data->nfunctions;
}

static const char *jlq_pinctrl_get_function_name(struct pinctrl_dev *pctldev,
					       unsigned int function)
{
	struct jlq_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);

	return state->pinctrl_data->functions[function];
}

static int jlq_pinctrl_get_function_groups(struct pinctrl_dev *pctldev,
					 unsigned int function,
					 const char *const **groups,
					 unsigned *const num_qgroups)
{
	struct jlq_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);

	*groups = state->pinctrl_data->groups;
	*num_qgroups = state->pinctrl_data->ngroups;

	return 0;
}

static int get_muxval_by_func_name(struct jlq_desc_function *func,
				    const char *name)
{
	int i = 0;

	if (!func || !func->name) {
		WARN_ON(1);
		return -1;
	}

	while (func->name && i < 4) {
		if (strcmp(func->name, name) == 0)
			return func->muxval;
		func++;
		i++;
	}

	return -EINVAL;
}

static int jlq_pinmux_enable(struct pinctrl_dev *pctldev, unsigned int function,
				unsigned int pin)
{
	struct jlq_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	const char *func_name = state->pinctrl_data->functions[function];
	int mux = get_muxval_by_func_name(pad->func, func_name);

	dev_dbg(state->dev, "pin[%d] is %s,func is %s,muxval=%d\n",
					pin, pad->group_name, func_name, mux);
	if (mux < 0)
		return -EINVAL;

	jlq_pinctrl_set_mux(state, pin, mux);

	return 0;
}

static int jlq_gpio_request_enable(struct pinctrl_dev *pctldev,
	struct pinctrl_gpio_range *range, unsigned int pin)
{
	struct jlq_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);
	int gpio2pin;
	struct jlq_pinctrl_pad *pad;
	int mux;

	if (pin >= state->pinctrl_data->ngpios || pin < 0) {
		dev_err(state->dev, "unknown gpio[%d]\n", pin);
		return -EINVAL;
	}
	gpio2pin = state->pinctrl_data->gpio2pin[pin].pin_index;
	pad = state->pads + gpio2pin;
	mux = get_muxval_by_func_name(pad->func, "gpio");

	if (mux < 0)
		return -EINVAL;
	dev_dbg(state->dev, "gpio[%d], pin is %s, muxval=%d\n",
					pin, pad->group_name, mux);

	jlq_pinctrl_set_mux(state, gpio2pin, mux);

	return 0;
}

static const struct pinmux_ops jlq_pinmux_ops = {
	.get_functions_count	= jlq_pinctrl_get_functions_count,
	.get_function_name	= jlq_pinctrl_get_function_name,
	.get_function_groups	= jlq_pinctrl_get_function_groups,
	.gpio_request_enable = jlq_gpio_request_enable,
	.set_mux		= jlq_pinmux_enable,
};

static int jlq_pinctrl_config_get(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config)
{
	struct jlq_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	enum pin_config_param param = pinconf_to_config_param(*config);
	u16 arg;

	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = pad->pin_config.strength;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		arg = pad->pin_config.pull_up;
		break;
	case PIN_CONFIG_INPUT_SCHMITT:
		arg = pad->pin_config.schmitt;
		break;
	case PIN_CONFIG_SLEW_RATE:
		arg = pad->pin_config.slew_rate;
		break;
	default:
		dev_dbg(state->dev, "not support param [%d]", param);
		return -EOPNOTSUPP;
	}
	*config = pinconf_to_config_packed(param, arg);
	dev_dbg(state->dev, "param = [%d]", param);

	return 0;
}

static int jlq_pinctrl_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned int nconfs)
{
	struct jlq_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct jlq_pinctrl_pad *pad = state->pads + pin;
	enum pin_config_param param;
	u16 arg;
	int i;

	for (i = 0; i < nconfs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		dev_dbg(state->dev, "i=%d,pin[%d]=param[%d], arg[%d]", i, pin, param, arg);

		switch (param) {
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (arg == MUXPIN_DRIVE_DEFAULT)
				break;
			if (arg > MUXPIN_DRIVE_MAX)
				return -EINVAL;
			jlq_pinctrl_set_strength(state, pin, arg);
			pad->pin_config.strength = arg;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_DISABLE:
			jlq_pinctrl_set_pull(state, pin, arg);
			pad->pin_config.pull_up = arg;
			break;

		case PIN_CONFIG_INPUT_SCHMITT:
			jlq_pinctrl_schmitt_enable(state, pin,
						arg);
			pad->pin_config.schmitt = arg;
			break;
		case PIN_CONFIG_SLEW_RATE:
			jlq_pinctrl_slew_rate_enable(state, pin,
						arg);
			pad->pin_config.slew_rate = arg;
			break;
		default:
			dev_dbg(state->dev, "pin[%d] no support param = %d\n", pin, param);
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops jlq_pinconf_ops = {
	.is_generic			= true,
	.pin_config_group_get		= jlq_pinctrl_config_get,
	.pin_config_group_set		= jlq_pinctrl_config_set,
};

static int jlq_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct jlq_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int bit;
	unsigned int val;

	reg = state->gpio_reg_base + GPIO_PORT_DDR(offset);
	bit = offset % 16;

	val = readl(reg) & (1 << bit);
	return !val;
}

static int jlq_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct jlq_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int bit;
	unsigned int val;

	reg = state->gpio_reg_base + GPIO_PORT_DDR(offset);
	bit = offset % 16;

	val = readl(reg) & (1 << bit);

	if (!!val) {
		reg = state->gpio_reg_base + GPIO_PORT_DR(offset);
		bit = offset % 16;
		val = readl(reg) & (1 << bit);

	} else {
		reg = state->gpio_reg_base + GPIO_EXT_PORT(offset);
		bit = offset % 32;
		val = readl(reg) & (1 << bit);
	}

	return !!val;
}

static void jlq_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct jlq_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int bit;
	unsigned int val;

	reg = state->gpio_reg_base + GPIO_PORT_DR(offset);
	bit = offset % 16;

	val = (1 << (bit + 16));
	if (value)
		val |= 1 << bit;
	writel(val, reg);
}

static void jlq_gpio_set_multiple(struct gpio_chip *chip,
				unsigned long *mask, unsigned long *bits)
{
	struct jlq_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int bit;
	unsigned int val;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&state->gpio_lock, flags);

	for (i = 0; i < chip->ngpio; i++) {
		if (test_bit(i, mask)) {
			reg = state->gpio_reg_base + GPIO_PORT_DR(i);
			bit = i % 16;
			val = (1 << (bit + 16));
			if (test_bit(i, bits))
				val |= 1 << bit;
			else
				val &= ~(1 << bit);
			writel(val, reg);
		}
	}

	spin_unlock_irqrestore(&state->gpio_lock, flags);
}

static int jlq_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct jlq_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int bit;
	unsigned int val;

	reg = state->gpio_reg_base + GPIO_PORT_DDR(offset);
	bit = offset % 16;

	val = (1 << (bit + 16));
	writel(val, reg);

	return 0;
}

static int jlq_gpio_direction_output(struct gpio_chip *chip,
		unsigned int offset, int value)
{
	struct jlq_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int bit;
	unsigned int val;

	/* Set output value. */
	jlq_gpio_set(chip, offset, value);

	/* Set direction. */
	reg = state->gpio_reg_base + GPIO_PORT_DDR(offset);
	bit = offset % 16;

	val = (1 << (bit + 16)) | (1 << bit);
	writel(val, reg);

	return 0;
}

static void jlq_gpio_irq_ack(struct irq_data *d)
{
	struct jlq_pinctrl_state *state = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);
	unsigned int val;
	void __iomem *reg;

	if (gpio < 0)
		return;

	val = (1 << (gpio % 32));
	reg = (state->gpio_reg_base + GPIO_INTR_CLR(gpio));

	writel(val, reg);
}

static void jlq_gpio_irq_mask(struct irq_data *d)
{
	struct jlq_pinctrl_state *state = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);
	unsigned int bit;
	unsigned int val;
	void __iomem *reg;

	if (gpio < 0)
		return;

	bit = gpio % 16;
	val = (1 << (bit + 16)) | (1 << bit);
	reg = state->gpio_reg_base + GPIO_INTR_MASK(gpio);

	writel(val, reg);
}

static void jlq_gpio_irq_unmask(struct irq_data *d)
{
	struct jlq_pinctrl_state *state = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);
	unsigned int bit;
	unsigned int val;
	void __iomem *reg;

	if (gpio < 0)
		return;

	bit = gpio % 16;
	val = (1 << (bit + 16));
	reg = state->gpio_reg_base + GPIO_INTR_MASK(gpio);

	writel(val, reg);
}

static int jlq_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct jlq_pinctrl_state *state = irq_data_get_irq_chip_data(d);
	int gpio = irqd_to_hwirq(d);
	unsigned int trigger = 0xff;
	unsigned int add_bit = 3;
	unsigned int bit;
	unsigned int val;
	void __iomem *reg;
	unsigned long flags;

	if (gpio >= state->chip.ngpio)
		return 0;

	spin_lock_irqsave(&state->gpio_lock, flags);

	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		trigger = 4 + add_bit;
	else if (type & IRQ_TYPE_EDGE_RISING)
		trigger = 2 + add_bit;
	else if (type & IRQ_TYPE_EDGE_FALLING)
		trigger = 3 + add_bit;
	else if (type & IRQ_TYPE_LEVEL_HIGH)
		trigger = 0;
	else if (type & IRQ_TYPE_LEVEL_LOW)
		trigger = 1;

	reg = state->gpio_reg_base + GPIO_INTR_CTRL(gpio);
	bit = gpio % 4;

	if (trigger != 0xff)
		val = (0x1 << (16 + bit)) | (((trigger << 1) + 1) << bit * 4);
	else
		val = (0x1 << (16 + bit));

	writel(val, reg);

	spin_unlock_irqrestore(&state->gpio_lock, flags);

	return 0;
}

static int jlq_gpio_irq_set_wake(struct irq_data *d, unsigned int enable)
{
	struct irq_desc *desc;
	struct jlq_pinctrl_state *state;
	int irq;
	int group;
	int ret = 0;

	if (!d)
		return -EINVAL;

	state = irq_data_get_irq_chip_data(d);

	return irq_set_irq_wake(state->irq, enable);
}

static struct irq_chip jlq_gpio_irq_chip = {
	.name		= "GPIO",
	.irq_ack	= jlq_gpio_irq_ack,
	.irq_mask	= jlq_gpio_irq_mask,
	.irq_unmask	= jlq_gpio_irq_unmask,
	.irq_set_type	= jlq_gpio_irq_set_type,
	.irq_set_wake	= jlq_gpio_irq_set_wake,
};

static int jlq_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct jlq_pinctrl_state *state = gpiochip_get_data(chip);
	unsigned int virq;

	if (!state->domain)
		return -ENXIO;

	virq = irq_create_mapping(state->domain, offset);

	return virq ? virq : -ENXIO;
}


static const struct gpio_chip jlq_gpio_ops = {
	.direction_input	= jlq_gpio_direction_input,
	.direction_output	= jlq_gpio_direction_output,
	.get			= jlq_gpio_get,
	.set			= jlq_gpio_set,
	.set_multiple = jlq_gpio_set_multiple,
	.get_direction = jlq_gpio_get_direction,
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.to_irq			= jlq_gpio_to_irq,
};

static void jlq_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct jlq_pinctrl_state *state;
	unsigned long status_val;
	void __iomem *status_reg;
	void __iomem *clear_reg;
	void __iomem *reg;
	unsigned int unmasked = 0;
	unsigned int offset;
	unsigned int type;
	unsigned int i, j;

	chained_irq_enter(chip, desc);

	state = irq_desc_get_handler_data(desc);
	for (i = 0; i < DIV_ROUND_UP(state->chip.ngpio, 32); i++) {
		status_reg = state->gpio_reg_base + GPIO_INTR_STATUS(0) + (i * 4);
		clear_reg = state->gpio_reg_base + GPIO_INTR_CLR(0) + (i * 4);
		status_val = readl(status_reg);
		for_each_set_bit(j, &status_val, 32) {
			writel(1 << j, clear_reg);
			offset = i * 32 + j;

			/* if gpio is edge triggered, clear condition
			 * before executing the handler so that we don't
			 * miss edges
			 */
			reg = state->gpio_reg_base + GPIO_INTR_CTRL(offset);
			type = (readl(reg) >> ((offset % 4) + 1)) & 0x7;
			if (type >= 2) {
				unmasked = 1;
				chained_irq_exit(chip, desc);
			}

			generic_handle_irq(irq_find_mapping(state->domain, offset));
		}
	}

	if (!unmasked)
		chained_irq_exit(chip, desc);
}

static int  __maybe_unused jlq_gpio_suspend(struct device *dev)
{
	struct jlq_pinctrl_state *state = dev_get_drvdata(dev);
	unsigned int trigger;
	unsigned int bit;
	unsigned int val;
	unsigned int intr_enable;
	unsigned long flags;
	void __iomem *reg;
	int i;
	int gpio;

	spin_lock_irqsave(&state->gpio_lock, flags);
	for (gpio = 0; gpio < state->chip.ngpio; gpio++) {
		reg = state->gpio_reg_base + GPIO_INTR_CTRL(gpio);
		bit = gpio % 4;
		trigger = (readl(reg) >> (bit * 4)) & 0xf;
		intr_enable = trigger & 0x1;
		trigger = trigger >> 1;
		if (trigger >= 5) {
			trigger = trigger - 5 + 2;
			if (intr_enable)
				val = (0x1 << (16 + bit)) | (((trigger << 1) + 1) << bit * 4);
			else
				val = (0x1 << (16 + bit)) | ((trigger << 1) << bit * 4);
			writel(val, reg);
		}
	}
	spin_unlock_irqrestore(&state->gpio_lock, flags);

	return 0;
}

static int __maybe_unused jlq_gpio_resume(struct device *dev)
{
	struct jlq_pinctrl_state *state = dev_get_drvdata(dev);
	unsigned int trigger;
	unsigned int bit;
	unsigned int val;
	unsigned int intr_enable;
	unsigned long flags;
	void __iomem *reg;
	int gpio;

	spin_lock_irqsave(&state->gpio_lock, flags);
	for (gpio = 0; gpio < state->chip.ngpio; gpio++) {
		reg = state->gpio_reg_base + GPIO_INTR_CTRL(gpio);
		bit = gpio % 4;
		trigger = (readl(reg) >> (bit*4)) & 0xf;
		intr_enable = trigger & 0x1;
		trigger = trigger >> 1;

		if (trigger < 5 && trigger >= 2) {
			trigger = trigger - 2 + 5;
			if (intr_enable)
				val = (0x1 << (16 + bit)) | (((trigger << 1) + 1) << bit * 4);
			else
				val = (0x1 << (16 + bit)) | ((trigger << 1) << bit * 4);
			writel(val, reg);
		}
	}
	spin_unlock_irqrestore(&state->gpio_lock, flags);

	return 0;
}

static const struct dev_pm_ops jlq_gpio_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(jlq_gpio_suspend,
				jlq_gpio_resume)
};

static struct lock_class_key gpio_lock_class;
static struct lock_class_key gpio_request_class;

extern const struct jlq_pinctrl_desc ja310_pinctrl_data, jr510_pinctrl_data;
static int jlq_pinctrl_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct pinctrl_pin_desc *pindesc;
	struct pinctrl_desc *pctrldesc;
	struct jlq_pinctrl_pad *pad, *pads;
	struct jlq_pinctrl_state *state;
	const struct jlq_pinctrl_desc *pinctrl_data;
	const struct jlq_desc_pin *jlq_pins;
	struct resource *pinctrl_mem;
	struct resource *gpio_mem;
	int npins, i;
	int gpio_num;
	void __iomem *reg;
	int ret;
	int irq;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	platform_set_drvdata(pdev, state);
	spin_lock_init(&state->pinctrl_lock);
	state->dev = &pdev->dev;

	pinctrl_mem = platform_get_resource_byname(pdev,
						IORESOURCE_MEM, "pinctrl_mem");
	if (!pinctrl_mem) {
		pr_err("Could not get pinctrl mem physical address resource\n");
		return -EINVAL;
	}

	state->pintcrl_reg_base = devm_ioremap_resource(dev, pinctrl_mem);
	if (IS_ERR(state->pintcrl_reg_base)) {
		dev_err(dev, "Failed to IO map pinctrl registers.\n");
		return -ENOMEM;
	}

	gpio_mem = platform_get_resource_byname(pdev,
						IORESOURCE_MEM, "gpio_mem");
	if (!gpio_mem) {
		dev_err(dev, "Could not get gpio mem physical address resource\n");
		return -EINVAL;
	}

	state->gpio_reg_base = devm_ioremap_resource(dev, gpio_mem);
	if (IS_ERR(state->gpio_reg_base)) {
		dev_err(dev, "Failed to IO map gpio registers.\n");
		return -ENOMEM;
	}

	if (node) {
		pinctrl_data = (struct jlq_pinctrl_desc *)of_device_get_match_data(dev);
		if (!pinctrl_data)
			return -EINVAL;
	} else {
		pinctrl_data = &jr510_pinctrl_data;
	}

	npins = pinctrl_data->npins;
	jlq_pins = pinctrl_data->pins;
	gpio_num = pinctrl_data->ngpios;
	state->pinctrl_data = pinctrl_data;

	pindesc = devm_kcalloc(dev, npins, sizeof(*pindesc), GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;

	pads = devm_kcalloc(dev, npins, sizeof(*pads), GFP_KERNEL);
	if (!pads)
		return -ENOMEM;

	pctrldesc = devm_kzalloc(dev, sizeof(*pctrldesc), GFP_KERNEL);
	if (!pctrldesc)
		return -ENOMEM;

	pctrldesc->pctlops = &jlq_pinctrl_ops;
	pctrldesc->pmxops = &jlq_pinmux_ops;
	pctrldesc->confops = &jlq_pinconf_ops;
	pctrldesc->owner = THIS_MODULE;
	pctrldesc->name = dev_name(dev);
	pctrldesc->pins = pindesc;
	pctrldesc->npins = npins;
	pctrldesc->num_custom_params = ARRAY_SIZE(jlq_pinctrl_bindings);
	pctrldesc->custom_params = jlq_pinctrl_bindings;

	for (i = 0; i < npins; i++, pindesc++) {
		pad = pads + i;
		pindesc->drv_data = pad;
		pindesc->number = i;
		pindesc->name = jlq_pins[i].pin.name;
		pad->bit_map = pinctrl_data->pin_select + jlq_pins[i].pin_type;
		pad->pin_desc = jlq_pins + i;
		pad->func = jlq_pins[i].functions;
		pad->group_name = jlq_pins[i].pin.name;
	}

	state->pads = pads;

	state->ctrl = devm_pinctrl_register(dev, pctrldesc, state);
	if (IS_ERR(state->ctrl))
		return PTR_ERR(state->ctrl);

	state->chip = jlq_gpio_ops;
	state->chip.parent = dev;
	state->chip.base = -1;
	state->chip.ngpio = gpio_num;
	state->chip.label = dev_name(dev);
	state->chip.of_gpio_n_cells = 2;
	state->chip.can_sleep = false;

	spin_lock_init(&state->gpio_lock);

	/* Mask all gpio interrupt. */
	for (i = 0; i < DIV_ROUND_UP(gpio_num, 16); i++) {
		reg = state->gpio_reg_base + GPIO_INTR_MASK(0) + (i * 4);
		writel(~0x0, reg);
	}
	/* Disable all gpio interrupt. */
	for (i = 0; i < DIV_ROUND_UP(gpio_num, 4); i++) {
		reg = state->gpio_reg_base + GPIO_INTR_CTRL(0) + (i * 4);
		writel(0x0, reg);
	}

	state->irq = irq_of_parse_and_map(node, 0);

	state->domain = irq_domain_add_linear(node, gpio_num,
					&irq_domain_simple_ops, state);

	if (!state->domain) {
		dev_dbg(&pdev->dev, "Couldn't register IRQ domain\n");
		return -ENOMEM;
	}

	irq_set_chained_handler_and_data(state->irq,
						 jlq_gpio_irq_handler, state);
	for (i = 0; i < gpio_num; i++) {
		irq = irq_create_mapping(state->domain, i);
		irq_set_lockdep_class(irq, &gpio_lock_class, &gpio_request_class);
		irq_set_chip_data(irq, state);
		irq_set_chip_and_handler_name(irq, &jlq_gpio_irq_chip,
					      handle_simple_irq, "jlq_gpio");
	}

	ret = gpiochip_add_data(&state->chip, state);
	if (ret) {
		dev_dbg(state->dev, "can't add gpio chip\n");
		return ret;
	}

        jlq_pinctrl_set_pull(state, 6, 0);
        jlq_pinctrl_set_pull(state, 36, 0);
	return 0;
}

static const struct of_device_id jlq_pinctrl_of_match[] = {
	{ .compatible = "jlq,ja310-gpio", .data = &ja310_pinctrl_data },
	{ .compatible = "jlq,jr510-gpio", .data = &jr510_pinctrl_data },
	{ },
};

MODULE_DEVICE_TABLE(of, jlq_pinctrl_of_match);

static struct platform_driver jlq_pinctrl_driver = {
	.driver = {
		   .name = "jlq-gpio",
		   .pm = &jlq_gpio_pm_ops,
		   .of_match_table = jlq_pinctrl_of_match,
	},
	.probe	= jlq_pinctrl_probe,

};

module_platform_driver(jlq_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: qtang_gpio");
