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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/pwm.h>

#define HZ_TO_NS(x) (1000000000UL / (x))
#define PWM_DUTY_LEVEL_ORDER    (10)

struct jlq_backlight {
	struct device *dev;
	struct pwm_device *pwm_bl;
	struct led_classdev backlight_led;
};

static void jlq_backlight_ctrl_pwm(struct pwm_device *pwm, u32 bl_level)
{
	unsigned int period_ns;
	unsigned int duty_ns;
	unsigned int duty_ns_min;
	unsigned int duty_ns_max;
	unsigned int period = pwm->args.period;

	if (bl_level) {
		duty_ns_min =  (1 * HZ_TO_NS(period)) >> PWM_DUTY_LEVEL_ORDER;
		period_ns = HZ_TO_NS(period);
		duty_ns_max = period_ns;

		duty_ns = (bl_level * period_ns) / 255;
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
}

void jlq_backlight_set_brightness(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct jlq_backlight *backlight = dev_get_drvdata(dev);

	if (backlight->pwm_bl)
		jlq_backlight_ctrl_pwm(backlight->pwm_bl, value);
}

static int jlq_backlight_probe(struct platform_device *pdev)
{
	int ret;
	struct jlq_backlight *backlight;
	struct device *dev = &pdev->dev;

	dev_info(dev, "%s\n", __func__);

	backlight = devm_kzalloc(dev, sizeof(*backlight), GFP_KERNEL);
	if (!backlight)
		return -ENOMEM;

	backlight->dev = dev;
	backlight->pwm_bl = devm_of_pwm_get(dev, dev->of_node, "backlight");
	if (IS_ERR_OR_NULL(backlight->pwm_bl)) {
		pr_err("%s get pwm chip failed\n", __func__);
		return -ENODEV;
	}

	jlq_backlight_ctrl_pwm(backlight->pwm_bl, 200);

	backlight->backlight_led.name = "lcd-backlight",
	backlight->backlight_led.brightness = LED_HALF,
	backlight->backlight_led.brightness_set = jlq_backlight_set_brightness,
	backlight->backlight_led.max_brightness = LED_FULL,
	ret = led_classdev_register(dev, &backlight->backlight_led);
	if (ret) {
		dev_err(dev, "Can't register led class device\n");
		return ret;
	}

	dev_set_drvdata(dev, backlight);

	return 0;

}

static int jlq_backlight_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id jlq_backlight_ids[] = {
	{
		.compatible = "jlq,jlq-backlight",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jlq_backlight_ids);

static struct platform_driver jlq_backlight_driver = {
	.driver = {
		.name = "jlq-backlight",
		.of_match_table = jlq_backlight_ids,
	},
	.probe = jlq_backlight_probe,
	.remove = jlq_backlight_remove,
};

module_platform_driver(jlq_backlight_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("jlq backlight driver");
