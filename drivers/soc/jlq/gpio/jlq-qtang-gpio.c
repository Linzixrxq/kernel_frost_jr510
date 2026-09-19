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

static const struct pinconf_generic_params qtang_pinctrl_bindings[] = {
	{"jlq,drive", PIN_CONFIG_DRIVE_STRENGTH, 0},
	{"jlq,pull", PIN_CONFIG_BIAS_PULL_UP, 0},
};

struct qtang_pin_config {
	u16 strength;
	u16 pull_up;
};

struct qtang_pinctrl_pad {
	const struct jlq_desc_pin *pin_desc;
	struct jlq_desc_function *func;
	struct qtang_pin_config pin_config;
	const char *group_name;
	int pin_type;
	int pin_index;
	int is_enabled;
};

struct qtang_pinctrl_state {
	struct device	*dev;
	void __iomem *ufs_reset_mem;
	void __iomem *cam_gpio_mem;
	void __iomem *common_gpio_mem;
	spinlock_t pinctrl_lock;
	spinlock_t gpio_lock;
	struct qtang_pinctrl_pad *pads;
	struct pinctrl_dev *ctrl;
	const struct jlq_pinctrl_desc *pinctrl_data;
	struct gpio_chip chip;
};

static void qtang_pinctrl_set_mux(struct qtang_pinctrl_state *state,
		unsigned int pin, unsigned int mux)
{

}

static void qtang_pinctrl_set_pull(struct qtang_pinctrl_state *state,
		unsigned int pin, unsigned int pull)
{
	struct qtang_pinctrl_pad *pad = state->pads + pin;
	unsigned long flags;
	void __iomem  *reg;
	unsigned int val;
	u32 mask;
	u32 offset;
	u32 pull_val;

	dev_dbg(state->dev, "set pull: pin[%d] is %s, pull is %d\n", pin, pad->group_name, pull);
	spin_lock_irqsave(&state->pinctrl_lock, flags);

	switch (pull) {
	case MUXPIN_PULL_DISABLE:
			pull_val = 0;
			break;
	case MUXPIN_PULL_UP:
			pull_val = 3;
			break;
	case MUXPIN_PULL_DOWN:
			pull_val = 1;
			break;
	default:
			pull_val = 0;
			break;
	}

	switch (pad->pin_type) {
	case PIN_TYPE0:
		reg = state->cam_gpio_mem + pad->pin_index * 4;
		mask = GENMASK(1, 0);
		offset = 0;
		break;
	case PIN_TYPE1:
		reg = state->common_gpio_mem + pad->pin_index * 0x1000;
		mask = GENMASK(1, 0);
		offset = 0;
		break;
	default:
		spin_unlock_irqrestore(&state->pinctrl_lock, flags);
		return;
	}

	val = readl(reg);
	val = (val & (~mask)) | pull_val;
	writel(val, reg);

	spin_unlock_irqrestore(&state->pinctrl_lock, flags);
}

static void qtang_pinctrl_set_strength(struct qtang_pinctrl_state *state,
		unsigned int pin, unsigned int drive)
{
	struct qtang_pinctrl_pad *pad = state->pads + pin;
	unsigned long flags;
	void __iomem  *reg;
	unsigned int val;
	u32 mask;
	u32 offset;
	u32 drive_strength;

	dev_dbg(state->dev, "set drive: pin[%d] is %s, drive is %d\n", pin, pad->group_name, drive);

	spin_lock_irqsave(&state->pinctrl_lock, flags);

	switch (pad->pin_type) {
	case PIN_TYPE0:
		reg = state->cam_gpio_mem + pad->pin_index * 4;
		mask = GENMASK(5, 3);
		offset = 3;
	break;
	case PIN_TYPE1:
		reg = state->common_gpio_mem + pad->pin_index * 0x1000;
		mask = GENMASK(8, 6);
		offset = 6;
		break;
	default:
		spin_unlock_irqrestore(&state->pinctrl_lock, flags);
		return;
	}
	drive_strength = (drive << offset) & mask;
	val = readl(reg);
	val = (val & (~mask)) | drive_strength;
	writel(val, reg);

	spin_unlock_irqrestore(&state->pinctrl_lock, flags);

}

static int qtang_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	/* Every PIN is a group */
	return pctldev->desc->npins;
}

static const char *qtang_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
					    unsigned int pin)
{
	return pctldev->desc->pins[pin].name;
}

static int qtang_pinctrl_get_group_pins(struct pinctrl_dev *pctldev, unsigned int pin,
				    const unsigned int **pins, unsigned int *num_pins)
{
	*pins = &pctldev->desc->pins[pin].number;
	*num_pins = 1;
	return 0;
}

static const struct pinctrl_ops qtang_pinctrl_ops = {
	.get_groups_count	= qtang_pinctrl_get_groups_count,
	.get_group_name		= qtang_pinctrl_get_group_name,
	.get_group_pins		= qtang_pinctrl_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int qtang_pinctrl_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct qtang_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);

	return state->pinctrl_data->nfunctions;
}

static const char *qtang_pinctrl_get_function_name(struct pinctrl_dev *pctldev,
					       unsigned int function)
{
	struct qtang_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);

	return state->pinctrl_data->functions[function];
}

static int qtang_pinctrl_get_function_groups(struct pinctrl_dev *pctldev,
					 unsigned int function,
					 const char *const **groups,
					 unsigned *const num_qgroups)
{
	struct qtang_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);

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

static int qtang_pinmux_enable(struct pinctrl_dev *pctldev, unsigned int function,
				unsigned int pin)
{
	struct qtang_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct qtang_pinctrl_pad *pad = state->pads + pin;
	const char *func_name = state->pinctrl_data->functions[function];
	int mux = get_muxval_by_func_name(pad->func, func_name);

	dev_dbg(state->dev, "pin[%d] is %s,func is %s,muxval=%d\n",
					pin, pad->group_name, func_name, mux);
	if (mux < 0)
		return -EINVAL;

	qtang_pinctrl_set_mux(state, pin, mux);

	return 0;
}


static int qtang_gpio_request_enable(struct pinctrl_dev *pctldev,
	struct pinctrl_gpio_range *range, unsigned int pin)
{
	return 0;
}

static const struct pinmux_ops qtang_pinmux_ops = {
	.get_functions_count	= qtang_pinctrl_get_functions_count,
	.get_function_name	= qtang_pinctrl_get_function_name,
	.get_function_groups	= qtang_pinctrl_get_function_groups,
	.gpio_request_enable = qtang_gpio_request_enable,
	.set_mux		= qtang_pinmux_enable,
};

static int qtang_pinctrl_config_get(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config)
{
	struct qtang_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct qtang_pinctrl_pad *pad = state->pads + pin;
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
	default:
		dev_dbg(state->dev, "not support param [%d]", param);
		return -EOPNOTSUPP;
	}
	*config = pinconf_to_config_packed(param, arg);
	dev_dbg(state->dev, "param = [%d]", param);

	return 0;
}

static int qtang_pinctrl_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned int nconfs)
{
	struct qtang_pinctrl_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct qtang_pinctrl_pad *pad = state->pads + pin;
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
			qtang_pinctrl_set_strength(state, pin, arg);
			pad->pin_config.strength = arg;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_DISABLE:
			qtang_pinctrl_set_pull(state, pin, arg);
			pad->pin_config.pull_up = arg;
			break;
		default:
			dev_dbg(state->dev, "pin[%d] no support param = %d\n", pin, param);
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops qtang_pinconf_ops = {
	.is_generic			= true,
	.pin_config_group_get		= qtang_pinctrl_config_get,
	.pin_config_group_set		= qtang_pinctrl_config_set,
};

static int qtang_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct qtang_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem  *reg;
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&state->gpio_lock, flags);
	if (offset <= 20 && offset >= 4) {
		reg = state->cam_gpio_mem + offset * 4;
		val = readl(reg) & BIT(6);
	} else if (offset == 63) {
		val = 0;
	} else {
		switch (offset) {
		case 0:
		case 1:
		case 3:
		case 10:
		case 21:
		case 22:
		case 59:
		case 60:
			break;
		default:
			spin_unlock_irqrestore(&state->gpio_lock, flags);
			return 0;
		}
		reg = state->common_gpio_mem + offset * 0x1000;
		val = readl(reg) & BIT(9);
	}
	dev_dbg(state->dev, "%s:gpio[%d]=%s\n", __func__, offset, !val ? "in" : "out");
	spin_unlock_irqrestore(&state->gpio_lock, flags);

	return !val;
}

static int qtang_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct qtang_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem  *reg;
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&state->gpio_lock, flags);
	if (offset <= 20 && offset >= 4) {
		reg = state->cam_gpio_mem + offset * 4;
		val = readl(reg) & BIT(7);
	} else if (offset == 63) {
		val = 0;
	} else {
		switch (offset) {
		case 0:
		case 1:
		case 3:
		case 10:
		case 21:
		case 22:
		case 59:
		case 60:
			break;
		default:
			spin_unlock_irqrestore(&state->gpio_lock, flags);
			return 0;
		}
		reg = state->common_gpio_mem + offset * 0x1000 + 4;
		val = readl(reg) & BIT(0);
	}

	dev_dbg(state->dev, "%s:gpio[%d] input = %d\n", __func__, offset, val);

	spin_unlock_irqrestore(&state->gpio_lock, flags);


	return val;
}

static void qtang_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct qtang_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem  *reg;
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&state->gpio_lock, flags);
	if (offset <= 20 && offset >= 4) {
		reg = state->cam_gpio_mem + offset * 4;
		val = readl(reg) | BIT(6);
		if (value)
			val |= BIT(8);
		else
			val &= ~BIT(8);
		writel(val, reg);
	} else if (offset == 63) {
		reg = state->ufs_reset_mem;
		if (value)
			val = BIT(0);
		else
			val = 0;
		writel(val, reg);
	} else {
		switch (offset) {
		case 0:
		case 1:
		case 3:
		case 10:
		case 21:
		case 22:
		case 59:
		case 60:
			break;
		default:
			spin_unlock_irqrestore(&state->gpio_lock, flags);
			return;
		}
		reg = state->common_gpio_mem + offset * 0x1000;
		val = readl(reg) | BIT(9);
		writel(val, reg);
		reg = state->common_gpio_mem + offset * 0x1000 + 4;
		if (value)
			val = BIT(1);
		else
			val = 0;
		writel(val, reg);
	}
	dev_dbg(state->dev, "%s:gpio[%d] output = %d\n", __func__, offset, val);
	spin_unlock_irqrestore(&state->gpio_lock, flags);
}

static int qtang_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct qtang_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem  *reg;
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&state->gpio_lock, flags);
	if (offset <= 20 && offset >= 4) {
		reg = state->cam_gpio_mem + offset * 4;
		val = readl(reg) & ~BIT(6);
		writel(val, reg);
	} else if (offset == 63) {
		val = 0;
	} else {
		switch (offset) {
		case 0:
		case 1:
		case 3:
		case 10:
		case 21:
		case 22:
		case 60:
			break;
		default:
			spin_unlock_irqrestore(&state->gpio_lock, flags);
			return 0;
		}
		reg = state->common_gpio_mem + offset * 0x1000;
		val = readl(reg) & ~BIT(9);
		writel(val, reg);
	}
	dev_dbg(state->dev, "%s:gpio[%d] output = %d\n", __func__, offset, val);
	spin_unlock_irqrestore(&state->gpio_lock, flags);

	return 0;
}

static int qtang_gpio_direction_output(struct gpio_chip *chip,
		unsigned int offset, int value)
{
	struct qtang_pinctrl_state *state = gpiochip_get_data(chip);
	void __iomem  *reg;
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&state->gpio_lock, flags);
	if (offset <= 20 && offset >= 4) {
		reg = state->cam_gpio_mem + offset * 4;
		val = readl(reg) | BIT(6);
		if (value)
			val |= BIT(8);
		else
			val &= ~BIT(8);
		writel(val, reg);
	} else if (offset == 63) {
		reg = state->ufs_reset_mem;
		if (value)
			val = BIT(0);
		else
			val = 0;
		writel(val, reg);
	} else {
		switch (offset) {
		case 0:
		case 1:
		case 3:
		case 10:
		case 21:
		case 22:
		case 59:
		case 60:
			break;
		default:
			spin_unlock_irqrestore(&state->gpio_lock, flags);
			return 0;
		}
		reg = state->common_gpio_mem + offset * 0x1000;
		val = readl(reg) | BIT(9);
		writel(val, reg);
		reg = state->common_gpio_mem + offset * 0x1000 + 4;
		if (value)
			val = BIT(1);
		else
			val = 0;
		writel(val, reg);
	}
	dev_dbg(state->dev, "%s:gpio[%d] output = %d\n", __func__, offset, val);
	spin_unlock_irqrestore(&state->gpio_lock, flags);

	return 0;
}

static const struct gpio_chip qtang_gpio_ops = {
	.direction_input	= qtang_gpio_direction_input,
	.direction_output	= qtang_gpio_direction_output,
	.get			= qtang_gpio_get,
	.set			= qtang_gpio_set,
	.get_direction = qtang_gpio_get_direction,
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,

};
extern const struct jlq_pinctrl_desc qtang_pinctrl_data;
static int qtang_pinctrl_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct pinctrl_pin_desc *pindesc;
	struct pinctrl_desc *pctrldesc;
	struct qtang_pinctrl_pad *pad, *pads;
	struct qtang_pinctrl_state *state;
	const struct jlq_pinctrl_desc *pinctrl_data;
	const struct jlq_desc_pin *qtang_pins;
	struct resource *ufs_reset_mem;
	struct resource *common_gpio_mem;
	struct resource *cam_gpio_mem;
	int npins, i;
	int gpio_num;
	int ret;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	platform_set_drvdata(pdev, state);
	spin_lock_init(&state->pinctrl_lock);
	state->dev = &pdev->dev;

	ufs_reset_mem = platform_get_resource_byname(pdev,
						IORESOURCE_MEM, "ufs_reset_mem");
	if (!ufs_reset_mem) {
		pr_err("Could not get ufs_reset_mem physical address resource\n");
		return -EINVAL;
	}

	state->ufs_reset_mem = devm_ioremap_resource(dev, ufs_reset_mem);
	if (IS_ERR(state->ufs_reset_mem)) {
		pr_err("Failed to IO map ufs_reset_mem registers.\n");
		return -ENOMEM;
	}

	common_gpio_mem = platform_get_resource_byname(pdev,
						IORESOURCE_MEM, "common_gpio_mem");
	if (!common_gpio_mem) {
		pr_err("Could not get common_gpio_mem physical address resource\n");
		return -EINVAL;
	}

	state->common_gpio_mem = devm_ioremap_resource(dev, common_gpio_mem);
	if (IS_ERR(state->common_gpio_mem)) {
		pr_err("Failed to IO map common_gpio_mem registers.\n");
		return -ENOMEM;
	}

	cam_gpio_mem = platform_get_resource_byname(pdev,
						IORESOURCE_MEM, "cam_gpio_mem");
	if (!cam_gpio_mem) {
		pr_err("Could not get cam_gpio_mem physical address resource\n");
		return -EINVAL;
	}

	state->cam_gpio_mem = devm_ioremap_resource(dev, cam_gpio_mem);
	if (IS_ERR(state->cam_gpio_mem)) {
		pr_err("Failed to IO map cam_gpio_mem registers.\n");
		return -ENOMEM;
	}

	if (node) {
		pinctrl_data = (struct jlq_pinctrl_desc *)of_device_get_match_data(dev);
		if (!pinctrl_data)
			return -EINVAL;
	} else {
		pinctrl_data = &qtang_pinctrl_data;
	}

	npins = pinctrl_data->npins;
	qtang_pins = pinctrl_data->pins;
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

	pctrldesc->pctlops = &qtang_pinctrl_ops;
	pctrldesc->pmxops = &qtang_pinmux_ops;
	pctrldesc->confops = &qtang_pinconf_ops;
	pctrldesc->owner = THIS_MODULE;
	pctrldesc->name = dev_name(dev);
	pctrldesc->pins = pindesc;
	pctrldesc->npins = npins;
	pctrldesc->num_custom_params = ARRAY_SIZE(qtang_pinctrl_bindings);
	pctrldesc->custom_params = qtang_pinctrl_bindings;

	for (i = 0; i < npins; i++, pindesc++) {
		pad = pads + i;
		pindesc->drv_data = pad;
		pindesc->number = i;
		pindesc->name = qtang_pins[i].pin.name;
		pad->pin_type = qtang_pins[i].pin_type;
		pad->pin_desc = qtang_pins + i;
		pad->pin_index = qtang_pins[i].pin.number;
		pad->func = qtang_pins[i].functions;
		pad->group_name = qtang_pins[i].pin.name;
	}

	state->pads = pads;

	state->ctrl = devm_pinctrl_register(dev, pctrldesc, state);
	if (IS_ERR(state->ctrl))
		return PTR_ERR(state->ctrl);

	state->chip = qtang_gpio_ops;
	state->chip.parent = dev;
	state->chip.base = -1;
	state->chip.ngpio = gpio_num;
	state->chip.label = dev_name(dev);
	state->chip.of_gpio_n_cells = 2;
	state->chip.can_sleep = false;

	spin_lock_init(&state->gpio_lock);

	ret = gpiochip_add_data(&state->chip, state);
	if (ret) {
		dev_dbg(state->dev, "can't add gpio chip\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id qtang_pinctrl_of_match[] = {
	{ .compatible = "jlq,qtang-gpio", .data = &qtang_pinctrl_data },
	{ },
};

MODULE_DEVICE_TABLE(of, qtang_pinctrl_of_match);

static struct platform_driver qtang_pinctrl_driver = {
	.driver = {
		   .name = "qtang-gpio",
		   .of_match_table = qtang_pinctrl_of_match,
	},
	.probe	= qtang_pinctrl_probe,

};

module_platform_driver(qtang_pinctrl_driver);


MODULE_AUTHOR("yucailiu <yucailiu@qtang.com>");
MODULE_DESCRIPTION("qtang pinctrl & gpio driver");
MODULE_LICENSE("GPL");
