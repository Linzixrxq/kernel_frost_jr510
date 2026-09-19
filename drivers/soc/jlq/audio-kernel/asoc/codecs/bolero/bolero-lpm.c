// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <soc/snd_event.h>
#include <linux/pm_runtime.h>
#include <soc/jlq-info.h>
#include <linux/regulator/consumer.h>
#include "bolero-lpm.h"
#include "bolero-cdc.h"


#ifdef BOLERO_REGU

static struct regulator *bolero_regu = NULL;
static struct regulator *va_regu = NULL;

int bolero_regu_get(struct platform_device *pdev)
{
    bolero_regu= regulator_get(&pdev->dev, "bolero_gdsc");
    if (IS_ERR(bolero_regu)) {
        pr_err("fail to get bolero regulator\n");
        return -EINVAL;
    }
    pr_info("%s: bolero_regu_get done 0x%x\n",__func__,bolero_regu);
    return 0;
}
EXPORT_SYMBOL_GPL(bolero_regu_get);

void bolero_regu_put(struct platform_device *pdev)
{
    if(bolero_regu)
        regulator_put(bolero_regu);
}
EXPORT_SYMBOL_GPL(bolero_regu_put);

int bolero_regu_enable(void)
{
    int ret;

    if(!bolero_regu)
        return -EINVAL;

    ret = regulator_is_enabled(bolero_regu);
    if (ret > 0) {
        pr_err("bolero regulator have been enabled\n");
        return 0;
    }

    ret = regulator_enable(bolero_regu);
    if (ret) {
        pr_err("fail to enable bolero regulator\n");
        return -1;
    } else
        pr_info("bolero regulator enabled, succ 0x%x\n",bolero_regu);

    return 0;
}
EXPORT_SYMBOL_GPL(bolero_regu_enable);

int bolero_regu_disable(void)
{
    int ret;

    if(!bolero_regu)
        return -EINVAL;

    ret = regulator_is_enabled(bolero_regu);
    if (ret <= 0) {
        pr_err("bolero regulator have been disabled\n");
        return 0;
    }

    ret = regulator_disable(bolero_regu);
    if (ret) {
        pr_err("fail to disable bolero regulator\n");
        return -1;
    } else
        pr_info("bolero regulator disabled, succ\n");

    return 0;
}
EXPORT_SYMBOL_GPL(bolero_regu_disable);

int va_regu_get(struct platform_device *pdev)
{
    va_regu = regulator_get(&pdev->dev, "bolero_va_gdsc");
    if (IS_ERR(va_regu)) {
        pr_err("fail to get va regulator\n");
        return -EINVAL;
    }

    pr_info("%s: va_regu_get done 0x%x\n",__func__,va_regu);
    return 0;
}
EXPORT_SYMBOL_GPL(va_regu_get);

void va_regu_put(struct platform_device *pdev)
{
    if(va_regu)
        regulator_put(va_regu);
}
EXPORT_SYMBOL_GPL(va_regu_put);


int va_regu_enable(void)
{
    int ret;

    if(!va_regu)
        return -EINVAL;

    ret = regulator_is_enabled(va_regu);
    if (ret > 0) {
        pr_err("va regulator have been enabled\n");
        return 0;
    }

    ret = regulator_enable(va_regu);
    if (ret) {
        pr_err("fail to enable va regulator\n");
        return -1;
    } else
        pr_info("va regulator enabled, succ 0x%x\n",va_regu);

    return 0;
}
EXPORT_SYMBOL_GPL(va_regu_enable);

int va_regu_disable(void)
{
    int ret;

    if(!va_regu)
        return -EINVAL;

    ret = regulator_is_enabled(va_regu);
    if (ret <= 0) {
        pr_err("va regulator have been disabled\n");
        return 0;
    }

    ret = regulator_disable(va_regu);
    if (ret) {
        pr_err("fail to disable va regulator\n");
        return -1;
    } else
        pr_info("va regulator disabled, succ\n");

    return 0;
}
EXPORT_SYMBOL_GPL(va_regu_disable);

#endif

