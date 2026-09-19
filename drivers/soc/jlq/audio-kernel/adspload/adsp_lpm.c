/*
 * Copyright (c)2019-2021   JLQ Technology Co.,Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/stat.h>
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <asm/uaccess.h>

#include "adsp_subsys.h"
#include "adsp_local.h"

/*debug info*/
#define MSG_TAG	"[adspk][lpm]"

/*ctrl reg base*/
static unsigned int toplpm_base;
static unsigned int audiosys_base;

/*icc node*/
#define ICC_NAME_KEY "adsp-icc"

#ifdef ADSP_LPM

static struct icc_path *adsp_icc_path = NULL;

int adsp_lpm_init(struct platform_device *pdev)
{
    AdspLoadDrvData *pdrvdata;

    PR_INFO(MSG_TAG"adsp_lpm_init\n");

    if(!pdev)
        return -ENODEV;

    pdrvdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!pdrvdata)
        return -EINVAL;

    pdrvdata->icc = of_icc_get(&pdev->dev, ICC_NAME_KEY);
    if (IS_ERR(pdrvdata->icc)) {
        pr_err("Error: (%d) failed getting %s path\n",
               PTR_ERR(pdrvdata->icc), ICC_NAME_KEY);
        return -EINVAL;
    }

    adsp_icc_path = pdrvdata->icc;

    return 0;
}

void adsp_lpm_uninit(struct platform_device *pdev)
{
    AdspLoadDrvData *pdrvdata;

    if(!pdev)
        return;

    pdrvdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!pdrvdata)
        return;

    if(pdrvdata->icc)
        icc_put(pdrvdata->icc);

    adsp_icc_path = NULL;
}

int adsp_icc_set_bw(struct platform_device *pdev, u32 avg_bw, u32 peak_bw)
{
    int ret;
    AdspLoadDrvData *pdrvdata;
    struct icc_path *icc;

    pr_info(MSG_TAG"adsp_icc_set_bw(avg_bw:%ld, peak_bw:%ld)\n", avg_bw, peak_bw);

    if(!pdev)
        return -ENODEV;

    pdrvdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!pdrvdata)
        return -EINVAL;

    icc = pdrvdata->icc;

    ret = icc_set_bw(icc, avg_bw, peak_bw);

    return ret;
}

/*
API for adjusting ADSP frequency and voltage according to the avg_bw and peak_bw.
the avg_bw and peak_bw is bps. Maybe compute this by adsp duty ratio, or by worker.
Maybe through mailbox irq from ADSP to KERNEL.
*/
int adsp_icc_set_bw_from_hifi3(u32 avg_bw, u32 peak_bw)
{
    if(!adsp_icc_path)
        return -1;

    pr_info(MSG_TAG"adsp_icc_set_bw_from_hifi3(avg_bw:%ld, peak_bw:%ld)\n", avg_bw, peak_bw);

    return icc_set_bw(adsp_icc_path, avg_bw, peak_bw);
}

#endif

#ifdef ADSP_REGU

static struct regulator *adsp_regu = NULL;

int adsp_regu_get(struct platform_device *pdev)
{
    AdspLoadDrvData *pdrvdata;

    PR_INFO(MSG_TAG"adsp_regu_get\n");

    if(!pdev)
        return -ENODEV;

    pdrvdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!pdrvdata)
        return -EINVAL;

    pdrvdata->regu = regulator_get(&pdev->dev, "adsp_gdsc");
    if (IS_ERR(pdrvdata->regu)) {
        pr_err(MSG_TAG"fail to get adsp regulator\n");
        return -EINVAL;
    }
    adsp_regu = pdrvdata->regu;
    return 0;
}

void adsp_regu_put(struct platform_device *pdev)
{
    AdspLoadDrvData *pdrvdata;

    PR_INFO(MSG_TAG"adsp_regu_put\n");

    if(!pdev)
        return;

    pdrvdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!pdrvdata)
        return;

    if(pdrvdata->regu)
        regulator_put(pdrvdata->regu);
}

int adsp_regu_enable(void)
{
    int ret;

    if(!adsp_regu)
        return -EINVAL;

    ret = regulator_is_enabled(adsp_regu);
    if (ret > 0) {
        pr_info(MSG_TAG"adsp regulator is enabled\n");
        return 0;
    }

    ret = regulator_enable(adsp_regu);
    if (ret) {
        pr_err(MSG_TAG"fail to enable adsp regulator\n");
        return -1;
    } else
        pr_info(MSG_TAG"adsp regulator enabled, succ\n");

    return 0;
}
EXPORT_SYMBOL(adsp_regu_enable);

int adsp_regu_disable(void)
{
    int ret;

    if(!adsp_regu)
        return -EINVAL;

    ret = regulator_is_enabled(adsp_regu);
    if (ret <= 0) {
        pr_info(MSG_TAG"adsp regulator is disabled\n");
        return 0;
    }

    ret = regulator_disable(adsp_regu);
    if (ret) {
        pr_err(MSG_TAG"fail to disable adsp regulator\n");
        return -1;
    } else
        pr_info(MSG_TAG"adsp regulator disabled, succ\n");

    return 0;
}
EXPORT_SYMBOL(adsp_regu_disable);

#else
int adsp_regu_enable(void)
{
    pr_info(MSG_TAG"ADSP_REGU is closed!\n");
    return 0;
}
EXPORT_SYMBOL(adsp_regu_enable);

int adsp_regu_disable(void)
{
    pr_info(MSG_TAG"ADSP_REGU is closed!\n");
    return 0;
}
EXPORT_SYMBOL(adsp_regu_disable);

#endif

int adsp_ctrl_regs_base_init(struct platform_device *pdev)
{
    int ret;
    AdspLoadDrvData *pdrvdata;

    PR_INFO(MSG_TAG"adsp_ctrl_regs_base_init\n");

    if(!pdev)
        return -ENODEV;

    pdrvdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!pdrvdata)
        return -EINVAL;

    ret = of_property_read_u32(pdev->dev.of_node, "toplpm-base", &toplpm_base);
    if (ret) {
        pr_err(MSG_TAG"toplpm-base error\n");
        return -EINVAL;
    }

    ret = of_property_read_u32(pdev->dev.of_node, "audiosys-base", &audiosys_base);
    if (ret) {
        pr_err(MSG_TAG"audiosys-base error\n");
        return -EINVAL;
    }

    return 0;
}

int adsp_dereset(void)
{
    void __iomem *v;
    int ret;
    int poweron_try = 50;
    u32 value;

    pr_info(MSG_TAG"adsp_dereset\n");

    v = ioremap(toplpm_base, 0x200);
    if (!v) {
        pr_err(MSG_TAG"toplpm_base ioremap fail\n");
        return -1;
    }

    while(poweron_try--) {

        /*lpm power on*/
#ifdef ADSP_REGU
        adsp_regu_disable();
        ret = adsp_regu_enable();
        if(ret < 0)
            return ret;
#else
        /*
        v = ioremap(toplpm_base, 0x200);
        if (!v) {
            pr_info(MSG_TAG"toplpm_base ioremap fail\n");
            return -1;
        }
        writel(0x20002, v + 0x100);
        iounmap(v);
        */
        writel(0x20002, v + 0x100);
#endif

        /*PMU_TOP_FLAG*/
        value = readl(v + 0x1f0);

        /*adsp_pu_flag, bit[8]*/
        if((value & 0x00000100) != 0) {
            pr_info(MSG_TAG"adsp power on succ(poweron_try:%d)\n", poweron_try);
            break;
        }
    }

    iounmap(v);

    if(!poweron_try) {
        pr_crit(MSG_TAG"adsp power on fail!!!\n");
        return -1;
    }

    /*stall*/
    v = ioremap(audiosys_base, 0x200);
    if (!v) {
        pr_err(MSG_TAG"audiosys_base ioremap fail\n");
        return -1;
    }
    writel(0x1, v + 0xc);
    iounmap(v);

    /*dereset*/
    v = ioremap(toplpm_base + 0x1000, 0x400);
    if (!v) {
        pr_err(MSG_TAG"toplpm_base(+CRG) ioremap fail\n");
        return -1;
    }
    writel(0x3800200, v + 0x21c);
    iounmap(v);

    /*disable ap sleep fsm, dont mask irq to gic, only for debugging wdt bite*/
    /*
    v = ioremap(toplpm_base, 0x1000);
    if (!v) {
        pr_info(MSG_TAG"toplpm_base ioremap fail\n");
        return -1;
    }
    writel(0, v + 0x020);  //SLPCTL_AP_SLP_CTL0
    writel(0, v + 0x024);  //SLPCTL_AP_SLP_CTL1
    writel(0, v + 0x040);  //SLPCTL_AP_SLP_INTR2WAKE_MK0

    pr_info(MSG_TAG"SLPCTL_AP_SLP_CTL0: 0x%x\n", readl(v + 0x020));
    pr_info(MSG_TAG"SLPCTL_AP_SLP_CTL1: 0x%x\n", readl(v + 0x024));
    pr_info(MSG_TAG"SLPCTL_AP_SLP_INTR2WAKE_MK0: 0x%x\n", readl(v + 0x040));
    iounmap(v);
    */

    pr_info(MSG_TAG"adsp_dereset succ\n");
    return 0;
}

void adsp_run(void)
{
    void __iomem *v;

    v = ioremap_nocache(audiosys_base, 0x200);
    if (!v) {
        pr_err(MSG_TAG"audiosys_base ioremap fail\n");
        return;
    }
    writel(0x0, v + 0xc);
    iounmap(v);
}

int adsp_rereset(void)
{
    void __iomem *v, *v1;
    int ret;
    int poweron_try = 50;
    u32 value;

    pr_crit(MSG_TAG"adsp_rereset begin\n");

    v = ioremap(toplpm_base, 0x200);
    if (!v) {
        pr_err(MSG_TAG"toplpm_base ioremap fail\n");
        return -1;
    }

    v1 = ioremap(toplpm_base + 0x1000, 0x400);
    if (!v1) {
        pr_err(MSG_TAG"toplpm_base(+CRG) ioremap fail\n");
        return -1;
    }

    while(poweron_try--) {
        /*force bolero powerdown*/
        writel(0x110001, v + 0x110);

        /*PMU_TOP_FLAG*/
        value = readl(v + 0x1f0);
        pr_crit(MSG_TAG"PMU_TOP_FLAG(1): 0x%x\n", value);

        /*reset audio sub and adsp*/
        writel(0x3870180, v1 + 0x21c);
        value = readl(v1 + 0x21c);
        pr_crit(MSG_TAG"TOP_AUDIO_RST_CTL: 0x%x\n", value);

#ifdef ADSP_REGU
        adsp_regu_disable();
#endif

        /*force adsp powerdown*/
        writel(0x110001, v + 0x100);

        /*PMU_TOP_FLAG*/
        value = readl(v + 0x1f0);
        pr_crit(MSG_TAG"PMU_TOP_FLAG(2): 0x%x\n", value);

        /*adsp_pd_flag, bit[9]*/
        if((value & 0x00000200) != 0) {
            pr_crit(MSG_TAG"adsp_rereset power down succ\n");
        } else {
            pr_crit(MSG_TAG"adsp_rereset is not powerdown status\n");
            continue;
        }

        /*force adsp poweron*/
        writel(0x120002, v + 0x100);

#ifdef ADSP_REGU
        ret = adsp_regu_enable();
        if(ret < 0)
            return ret;
#endif

        /*PMU_TOP_FLAG*/
        value = readl(v + 0x1f0);
        pr_crit(MSG_TAG"PMU_TOP_FLAG(3): 0x%x\n", value);

        /*adsp_pu_flag, bit[8]*/
        if((value & 0x00000100) != 0) {
            pr_crit(MSG_TAG"adsp_rereset power on succ(poweron_try:%d)\n", poweron_try);
            break;
        }
    }/*while(poweron_try--)*/

    iounmap(v);

    if(!poweron_try) {
        pr_crit(MSG_TAG"adsp_rereset power on fail!!!\n");
        return -1;
    }

    /*audio sub dereset*/
    writel(0x70007, v1 + 0x21c);

    /*stall*/
    v = ioremap(audiosys_base, 0x200);
    if (!v) {
        pr_err(MSG_TAG"audiosys_base ioremap fail\n");
        return -1;
    }
    writel(0x1, v + 0xc);
    iounmap(v);

    /*adsp dereset*/
    writel(0x3800200, v1 + 0x21c);
    //writel(0x3870207, v1 + 0x21c);  /*dereset audio sub and adsp*/

    iounmap(v1);

    pr_crit(MSG_TAG"adsp_rereset succ\n");
    return 0;
}


void adsp_stall(void)
{
    void __iomem *v;

    pr_crit(MSG_TAG"%s\n", __func__);

    /*stall*/
    v = ioremap(AUDIO_SYSCTRL_BASE, 0x200);
    if (!v) {
        pr_err(MSG_TAG"audiosys_base ioremap fail\n");
        return;
    }
    writel(0x1, v + 0xc);
    iounmap(v);
}

MODULE_SOFTDEP("pre: jlq_regulator rpm-smd-regulator");
