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
#include <linux/io.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/pwm.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define PWM_EN	(0x00)
#define PWM_UP	(0x04)
#define PWM_RST	(0x08)
#define PWM_P	(0x0C)
#define PWM_OCPY	(0x10)

#define NS_IN_HZ	(1000000000UL)
#define NS_TO_HZ(x)	(1000000000UL / (x))

#define JLQ_PWM_BASE	(-1)
#define JLQ_PWM_NUM	(1)

struct jlq_pwm_chip {
	struct pwm_chip chip;
	struct clk *mclk;
	struct clk *pclk;
	void __iomem *base;
};

#define to_jlq_pwm_chip(_chip) container_of(_chip, struct jlq_pwm_chip, chip)

static inline void pwm_writel(struct jlq_pwm_chip *jlq_chip,
	struct pwm_device *pwm, unsigned int offset, unsigned int val)
{
	writel(val, jlq_chip->base + offset);
}

static inline u32 pwm_readl(struct jlq_pwm_chip *jlq_chip,
	struct pwm_device *pwm, unsigned int offset)
{
	return readl(jlq_chip->base + offset);
}

static int jlq_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	return 0;
}

static int jlq_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jlq_pwm_chip *jlq_chip = to_jlq_pwm_chip(chip);

	pwm_writel(jlq_chip, pwm, PWM_EN, 0x1);

	return 0;
}

static void jlq_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jlq_pwm_chip *jlq_chip = to_jlq_pwm_chip(chip);

	pwm_writel(jlq_chip, pwm, PWM_EN, 0x0);
}

static int jlq_pwm_config(struct pwm_chip *chip,
	struct pwm_device *pwm, int duty_ns, int period_ns)
{
	struct jlq_pwm_chip *jlq_chip = to_jlq_pwm_chip(chip);
	u64 tmp;
	unsigned long period;
	unsigned long ocpy;

	if (period_ns > NS_IN_HZ || duty_ns > NS_IN_HZ)
		return -ERANGE;


	tmp = (u64)clk_get_rate(jlq_chip->mclk);
	do_div(tmp, NS_TO_HZ(period_ns) * 100);
	period = tmp - 1;
	ocpy = duty_ns * 100 / period_ns;

	if (pwm && test_bit(PWMF_EXPORTED, &pwm->flags))
		pwm_writel(jlq_chip, pwm, PWM_EN, 0x0);

	pwm_writel(jlq_chip, pwm, PWM_P, period);
	pwm_writel(jlq_chip, pwm, PWM_OCPY, ocpy);
	pwm_writel(jlq_chip, pwm, PWM_UP, 0x1);

	if (pwm && test_bit(PWMF_EXPORTED, &pwm->flags))
		pwm_writel(jlq_chip, pwm, PWM_EN, 0x1);

	return 0;
}

static const struct pwm_ops jlq_pwm_ops = {
	.request = jlq_pwm_request,
	.config = jlq_pwm_config,
	.enable = jlq_pwm_enable,
	.disable = jlq_pwm_disable,
	.owner = THIS_MODULE,
};

static int jlq_pwm_probe(struct platform_device *pdev)
{
	struct resource *r;
	struct jlq_pwm_chip *jlq_chip;
	struct device *dev = &pdev->dev;
	int ret = 0;

	dev_info(dev, "%s\n", __func__);

	jlq_chip = devm_kzalloc(dev, sizeof(*jlq_chip), GFP_KERNEL);
	if (!jlq_chip) {
		ret = -ENOMEM;
		goto err_ret;
	}

	jlq_chip->pclk = clk_get(dev, "pwm_pclk");
	if (IS_ERR(jlq_chip->pclk)) {
		dev_err(dev, "can't get pwm_pclk\n");
		ret = PTR_ERR(jlq_chip->pclk);
		goto err_ret;
	}

	jlq_chip->mclk = clk_get(dev, "pwm_mclk");
	if (IS_ERR(jlq_chip->mclk)) {
		dev_err(dev, "can't get pwm_mclk\n");
		ret = PTR_ERR(jlq_chip->mclk);
		goto err_put_pclk;
	}

	clk_prepare_enable(jlq_chip->pclk);
	clk_prepare_enable(jlq_chip->mclk);

	jlq_chip->chip.dev = dev;
	jlq_chip->chip.ops = &jlq_pwm_ops;
	jlq_chip->chip.base = JLQ_PWM_BASE;
	jlq_chip->chip.npwm = JLQ_PWM_NUM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto err_disable_clk;
	}

	jlq_chip->base = devm_ioremap_resource(dev, r);
	if (IS_ERR(jlq_chip->base)) {
		dev_err(dev, "failed to ioremap registers\n");
		ret = -PTR_ERR(jlq_chip->base);
		goto err_disable_clk;
	}

	ret = pwmchip_add(&jlq_chip->chip);
	if (ret < 0) {
		dev_err(dev, "failed to add pwm chip %d\n", ret);
		goto err_disable_clk;
	}

	platform_set_drvdata(pdev, jlq_chip);

	return 0;

err_disable_clk:
	clk_disable_unprepare(jlq_chip->mclk);
	clk_disable_unprepare(jlq_chip->pclk);
	clk_put(jlq_chip->mclk);
err_put_pclk:
	clk_put(jlq_chip->pclk);
err_ret:
	return ret;
}

static int __exit jlq_pwm_remove(struct platform_device *pdev)
{
	struct jlq_pwm_chip *jlq_chip = platform_get_drvdata(pdev);

	if (!jlq_chip)
		return -ENODEV;

	pwmchip_remove(&jlq_chip->chip);
	clk_disable_unprepare(jlq_chip->mclk);
	clk_disable_unprepare(jlq_chip->pclk);
	clk_put(jlq_chip->mclk);
	clk_put(jlq_chip->pclk);
	return 0;
}

static const struct of_device_id pwm_jlq_match_table[] = {
	{ .compatible = "jlq,jlq-pwm", },
	{}
};

static struct platform_driver jlq_pwm_driver = {
	.driver = {
		.name = "jlq-pwm",
		.owner = THIS_MODULE,
		.of_match_table = pwm_jlq_match_table,
	},
	.probe = jlq_pwm_probe,
	.remove = __exit_p(jlq_pwm_remove),
};

static int __init jlq_pwm_init(void)
{
	return platform_driver_register(&jlq_pwm_driver);
}

static void __exit jlq_pwm_exit(void)
{
	platform_driver_unregister(&jlq_pwm_driver);
}

module_init(jlq_pwm_init);
module_exit(jlq_pwm_exit);

MODULE_AUTHOR("jlqer <jlq@jlq.com>");
MODULE_DESCRIPTION("JLQ pwm driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jlq-pwm");
MODULE_SOFTDEP("pre: jlq-clk");
MODULE_SOFTDEP("post: camille_driver jlq_dsi_panel jlq_dsi_transmitter");
