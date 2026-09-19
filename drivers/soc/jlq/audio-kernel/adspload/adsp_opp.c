/*
 * ADSP Loader driver for JLQ JR510 chips
 *
 * Copyright (C) 2021 JLQ, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */


#define pr_fmt(fmt) "adsp-devfreq: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/devfreq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <trace/events/power.h>
#include <linux/pm_opp.h>

#include "adsp_subsys.h"
#include "adsp_local.h"


/*debug info*/
#define MSG_TAG	"[adspk][opp]"

static struct device *adsp_dev = NULL;


#ifdef ADSP_OPP_V0

struct userspace_data {
    unsigned long user_frequency;
    bool valid;
};

#define to_devfreq(DEV)	container_of((DEV), struct devfreq, dev)

static void find_freq(struct devfreq_dev_profile *p, unsigned long *freq,
                      u32 flags)
{
    int i;
    unsigned long atmost, atleast, f;

    atleast = p->freq_table[0];
    atmost = p->freq_table[p->max_state - 1];
    for (i = 0; i < p->max_state; i++) {
        f = p->freq_table[i];
        if (f <= *freq)
            atmost = max(f, atmost);
        if (f >= *freq)
            atleast = min(f, atleast);
    }
    if (flags & DEVFREQ_FLAG_LEAST_UPPER_BOUND)
        *freq = atleast;
    else
        *freq = atmost;
}

static int dev_target(struct device *dev, unsigned long *freq, u32 flags)
{
    AdspLoadDrvData *d = dev_get_drvdata(dev);
    unsigned long rfreq;
    int ret = 0;

    find_freq(&d->profile, freq, flags);
    rfreq = clk_round_rate(d->clk, d->freq_in_khz ? *freq * 1000 : *freq);
    if (IS_ERR_VALUE(rfreq)) {
        dev_err(dev, "devfreq: Cannot find matching frequency for %lu\n",
                *freq);
        return rfreq;
    }

    ret = dev_pm_opp_set_rate(dev, rfreq);

    pr_crit(MSG_TAG"%s, rfreq: %lu, ret: %d\n", __func__, rfreq, ret);

    return ret;
}

static int dev_get_cur_freq(struct device *dev, unsigned long *freq)
{
    AdspLoadDrvData *d = dev_get_drvdata(dev);
    unsigned long f;

    f = clk_get_rate(d->clk);
    if (IS_ERR_VALUE(f))
        return f;
    *freq = d->freq_in_khz ? f / 1000 : f;
    return 0;
}

static int parse_freq_table(struct device *dev, AdspLoadDrvData *d)
{
    struct devfreq_dev_profile *p = &d->profile;
    unsigned long freq;
    struct dev_pm_opp *opp;
    struct opp_table *opp_table = NULL;
    struct device_node *np;
    int len, i;
    const char *name = "core-supply";
    int ret = -EINVAL;

    np = of_node_get(dev->of_node);
    if (of_find_property(np, name, NULL)) {
        name = "core";
        opp_table = dev_pm_opp_set_regulators(dev, &name, 1);
        if (IS_ERR(opp_table)) {
            ret = PTR_ERR(opp_table);
            dev_err(dev, "Failed to set regulator: %d\n", ret);
            return ret;
        }
    }

    if (dev_pm_opp_of_add_table(dev)) {
        dev_err(dev, "failed to init OPP table: %d\n", ret);
        return ret;
    }

    len = dev_pm_opp_get_opp_count(dev);
    if (len <= 0)
        return -EPROBE_DEFER;
    PR_INFO(MSG_TAG"%s, opp num: %d\n", __func__, len);

    d->freq_in_khz = false;
    p->freq_table = devm_kzalloc(dev, len * sizeof(*p->freq_table),
                                 GFP_KERNEL);
    if (!p->freq_table)
        return -ENOMEM;

    for (i = 0, freq = ULONG_MAX; i < len; i++, freq--) {
        opp = dev_pm_opp_find_freq_floor(dev, &freq);
        if (IS_ERR(opp)) {
            ret = PTR_ERR(opp);
            goto free_tables;
        }
        p->freq_table[i] = freq;
        dev_pm_opp_put(opp);
    }

    p->max_state = i;
    if (p->max_state == 0)
        return -EINVAL;

    PR_INFO(MSG_TAG"%s succ\n", __func__);
    return 0;

free_tables:
    return ret;
}

int adsp_opp_init(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    AdspLoadDrvData *pdrvdata;
    struct devfreq_dev_profile *p;
    u32 poll;
    const char *gov_name;
    int ret;

    PR_INFO(MSG_TAG"%s\n", __func__);

    pdrvdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!pdrvdata) {
        return -EINVAL;
    }

    adsp_dev = &pdev->dev;

    ret = parse_freq_table(dev, pdrvdata);
    if (ret < 0)
        return ret;

    p = &pdrvdata->profile;
    p->target = dev_target;
    p->get_cur_freq = dev_get_cur_freq;
    ret = dev_get_cur_freq(dev, &p->initial_freq);
    if (ret < 0)
        return ret;

    PR_INFO(MSG_TAG"%s, initial_freq: %lu\n", __func__, p->initial_freq);

    p->polling_ms = 50;
    if (!of_property_read_u32(dev->of_node, "polling-ms", &poll))
        p->polling_ms = poll;

    if (of_property_read_string(dev->of_node, "governor", &gov_name))
        gov_name = "userspace";

    pdrvdata->df = devfreq_add_device(dev, p, gov_name, NULL);
    if (IS_ERR(pdrvdata->df)) {
        dev_err(&pdev->dev, "gov %s not ready, defer\n", gov_name);
        ret = -EPROBE_DEFER;
        goto add_err;
    }

    PR_INFO(MSG_TAG"%s succ\n", __func__);

    return 0;
add_err:
    return ret;
}

int adsp_opp_uninit(struct platform_device *pdev)
{
    AdspLoadDrvData *pdrvdata = platform_get_drvdata(pdev);

    devfreq_remove_device(pdrvdata->df);

    return 0;
}

int adsp_devfreq_set(unsigned long user_frequency)
{
    struct devfreq *devfreq;
    struct userspace_data *data;
    int ret = 0;

    if(!adsp_dev)
        return -1;

    devfreq = to_devfreq(adsp_dev);

    mutex_lock(&devfreq->lock);
    data = devfreq->data;

    data->user_frequency = user_frequency;
    data->valid = true;

    ret = update_devfreq(devfreq);
    if (ret == 0)
        pr_info(MSG_TAG"%s(%lu) succ\n", __func__, user_frequency);
    else
        pr_err(MSG_TAG"%s(%lu) fail(%d)\n", __func__, user_frequency, ret);

    mutex_unlock(&devfreq->lock);
    return ret;
}
//EXPORT_SYMBOL(adsp_devfreq_set);

unsigned long adsp_devfreq_get(void)
{
    struct devfreq *devfreq;
    struct userspace_data *data;
    unsigned long user_frequency;

    if(!adsp_dev)
        return -1;

    devfreq = to_devfreq(adsp_dev);

    mutex_lock(&devfreq->lock);
    data = devfreq->data;

    if (data->valid) {
        user_frequency = data->user_frequency;
        pr_info(MSG_TAG"%s(%lu)\n", __func__, data->user_frequency);
    } else {
        user_frequency = 0;
        pr_err(MSG_TAG"%s, user_frequency undefined\n");
    }
    mutex_unlock(&devfreq->lock);
    return user_frequency;
}
//EXPORT_SYMBOL(adsp_devfreq_get);
#endif

int adsp_devfreq_irq_init(struct platform_device *pdev);
int adsp_devfreq_up_irq_init(struct platform_device *pdev);
int adsp_devfreq_dw_irq_init(struct platform_device *pdev);


static void find_freq(AdspLoadDrvData *d, unsigned long *freq,	u32 flags)
{
    int i;
    unsigned long atmost, atleast, f;

    atleast = d->freq_table[0];
    atmost = d->freq_table[d->max_state - 1];
    for (i = 0; i < d->max_state; i++) {
        f = d->freq_table[i];
        if (f <= *freq)
            atmost = max(f, atmost);
        if (f >= *freq)
            atleast = min(f, atleast);
    }
    if (flags & DEVFREQ_FLAG_LEAST_UPPER_BOUND)
        *freq = atleast;
    else
        *freq = atmost;
}

static int dev_target(struct device *dev, unsigned long *freq, u32 flags)
{
    AdspLoadDrvData *d = dev_get_drvdata(dev);
    unsigned long rfreq;
    int ret = 0;

    find_freq(d, freq, flags);
    rfreq = clk_round_rate(d->clk, d->freq_in_khz ? *freq * 1000 : *freq);
    if (IS_ERR_VALUE(rfreq)) {
        dev_err(dev, "devfreq: Cannot find matching frequency for %lu\n",
                *freq);
        return rfreq;
    }

    //ret = clk_set_rate(d->clk, rfreq);
    ret = dev_pm_opp_set_rate(dev, rfreq);

    pr_crit(MSG_TAG"%s, rfreq: %lu, ret: %d\n", __func__, rfreq, ret);

    return ret;
}

static int dev_get_cur_freq(struct device *dev, unsigned long *freq)
{
    AdspLoadDrvData *d = dev_get_drvdata(dev);
    unsigned long f;

    f = clk_get_rate(d->clk);
    if (IS_ERR_VALUE(f))
        return f;
    *freq = d->freq_in_khz ? f / 1000 : f;
    return 0;
}

static int parse_freq_table(struct device *dev, AdspLoadDrvData *d)
{
    unsigned long freq;
    struct dev_pm_opp *opp;
    struct opp_table *opp_table = NULL;
    struct device_node *np;
    int len, i;
    const char *name = "core-supply";
    int ret = -EINVAL;

    np = of_node_get(dev->of_node);
    if (of_find_property(np, name, NULL)) {
        name = "core";
        opp_table = dev_pm_opp_set_regulators(dev, &name, 1);
        if (IS_ERR(opp_table)) {
            ret = PTR_ERR(opp_table);
            dev_err(dev, "Failed to set regulator: %d\n", ret);
            return ret;
        }
    }

    if (dev_pm_opp_of_add_table(dev)) {
        dev_err(dev, "failed to init OPP table: %d\n", ret);
        return ret;
    }

    len = dev_pm_opp_get_opp_count(dev);
    if (len <= 0)
        return -EPROBE_DEFER;
    PR_INFO(MSG_TAG"%s, opp num: %d\n", __func__, len);

    d->freq_in_khz = false;
    d->freq_table = devm_kzalloc(dev, len * sizeof(*d->freq_table),
                                 GFP_KERNEL);
    if (!d->freq_table)
        return -ENOMEM;

    for (i = 0, freq = ULONG_MAX; i < len; i++, freq--) {
        opp = dev_pm_opp_find_freq_floor(dev, &freq);
        if (IS_ERR(opp)) {
            ret = PTR_ERR(opp);
            goto free_tables;
        }
        d->freq_table[i] = freq;
        dev_pm_opp_put(opp);
    }

    d->max_state = i;
    if (d->max_state == 0)
        return -EINVAL;

    PR_INFO(MSG_TAG"%s succ(max_state:%d)\n", __func__, d->max_state);
    return 0;

free_tables:
    return ret;
}

int adsp_opp_init(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    AdspLoadDrvData *d;
    int ret;

    PR_INFO(MSG_TAG"%s\n", __func__);

    d = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    if(!d) {
        return -EINVAL;
    }

    adsp_dev = &pdev->dev;

    /*google gki don't accept this api*/
    //dev_pm_opp_set_clkname(dev, "adsp_clk");

    ret = parse_freq_table(dev, d);
    if (ret < 0)
        return ret;

    /*init irq for adsp->ap*/
    //adsp_devfreq_irq_init(pdev);
    adsp_devfreq_up_irq_init(pdev);
    adsp_devfreq_dw_irq_init(pdev);

    PR_INFO(MSG_TAG"%s succ\n", __func__);

    return 0;
}

int adsp_opp_uninit(struct platform_device *pdev)
{
    (void)pdev;

    return 0;
}

/*core clk*/
int adsp_devfreq_set(unsigned long will_freq)
{
    struct device *dev = adsp_dev;

    if(!dev)
        return -1;

    pr_crit(MSG_TAG"%s, will_freq: %lu\n", __func__, will_freq);

    return dev_target(dev, &will_freq, 1);
}
EXPORT_SYMBOL(adsp_devfreq_set);

unsigned long adsp_devfreq_get(void)
{
    struct device *dev = adsp_dev;
    unsigned long curr_freq;

    if(!dev)
        return 0;

    dev_get_cur_freq(dev, &curr_freq);

    pr_crit(MSG_TAG"%s, curr freq: %lu\n", __func__, curr_freq);

    return curr_freq;
}
EXPORT_SYMBOL(adsp_devfreq_get);

/*bus clk*/
int adsp_bclk_set(unsigned long will_freq)
{
    struct device *dev = adsp_dev;
    AdspLoadDrvData *d;
    unsigned long freq;
    int ret = 0;

    if(!dev)
        return -1;

    d = dev_get_drvdata(dev);

    ret = clk_set_rate(d->bclk, will_freq);
    if(ret) {
        pr_err(MSG_TAG"%s, Failed to set adsp bclk(%lu)\n", __func__, will_freq);
    }

    freq = clk_get_rate(d->bclk);
    pr_crit(MSG_TAG"%s, curr adsp_bclk, %lu\n", __func__, freq);

    return ret;
}

int adsp_devfreq_down(void)
{
    unsigned long will_freq;
    int ret = 0;

    pr_crit(MSG_TAG"%s\n", __func__);

    adsp_bclk_set(BCLK_FREQ_133330K_HZ);

    will_freq = PLL_COMM_HZ;
    ret = adsp_devfreq_set(will_freq);
    adsp_devfreq_get();
    return ret;
}
EXPORT_SYMBOL(adsp_devfreq_down);

int adsp_devfreq_up(void)
{
    unsigned long will_freq;
    int ret = 0;

    pr_crit(MSG_TAG"%s\n", __func__);

    will_freq = PLL_I2S_HZ;
    ret = adsp_devfreq_set(will_freq);
    if(ret == 0)
        //adsp_bclk_set(BCLK_FREQ_196608K_HZ); //I2S
        adsp_bclk_set(BCLK_FREQ_200000K_HZ);  //COMM
    adsp_devfreq_get();
    return ret;
}
EXPORT_SYMBOL(adsp_devfreq_up);


void t_adsp_devfreq(void)
{
    unsigned long curr_freq;
    unsigned long will_freq;

    will_freq = PLL_OPT_HZ;
    adsp_devfreq_set(will_freq);
    mdelay(10);
    curr_freq = adsp_devfreq_get();

    will_freq = PLL_I2S_HZ;
    adsp_devfreq_set(will_freq);
    mdelay(10);
    curr_freq = adsp_devfreq_get();

    will_freq = PLL_COMM_HZ;
    adsp_devfreq_set(will_freq);
    mdelay(10);
    curr_freq = adsp_devfreq_get();

    adsp_devfreq_up();
    adsp_devfreq_down();

    return;
}

/*devfreq_irq common, now reserve*/
irqreturn_t adsp_devfreq_irq_handler(int irq, void *priv)
{
    u32 value;

    /*debug only*/
    //pr_crit(MSG_TAG"%s\n", __func__);

    /*clean irq*/
    value = readl(adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_STA);
    writel((1 << MBOX_DEVFREQ_ADJUST_FROM_ADSP), adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_STA);

    //now none.

    return IRQ_HANDLED;
}

int adsp_devfreq_irq_init(struct platform_device *pdev)
{
    int ret;
    struct device_node *np = pdev->dev.of_node;
    u32 rxirq;
    u32 value;

    PR_INFO(MSG_TAG"%s\n", __func__);

    rxirq = of_irq_get_byname(np, "devfreq_irq");
    if(rxirq <= 0) {
        pr_err(MSG_TAG"request devfreq_irq fail\n");
        return rxirq;
    }

    ret = request_threaded_irq(rxirq, NULL, adsp_devfreq_irq_handler,
                               IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "devfreq_irq", NULL);
    if (ret) {
        pr_err(MSG_TAG"request devfreq_irq fail\n");
        return ret;
    }

    /*enable SRC int*/
    value = readl(adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN);
    writel(value | (1 << MBOX_DEVFREQ_ADJUST_FROM_ADSP), adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN);

    return 0;
}

/*devfreq_irq up*/
irqreturn_t adsp_devfreq_up_irq_handler(int irq, void *priv)
{
    u32 value;

    //pr_crit(MSG_TAG"%s\n", __func__);

    /*clean irq*/
    value = readl(adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_STA);
    writel((1 << MBOX_DEVFREQ_UP_FROM_ADSP), adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_STA);

    adsp_devfreq_up();

    return IRQ_HANDLED;
}

int adsp_devfreq_up_irq_init(struct platform_device *pdev)
{
    int ret;
    struct device_node *np = pdev->dev.of_node;
    u32 rxirq;
    u32 value;

    PR_INFO(MSG_TAG"%s\n", __func__);

    rxirq = of_irq_get_byname(np, "devfreq_up_irq");
    if(rxirq <= 0) {
        pr_err(MSG_TAG"request devfreq_up_irq fail\n");
        return rxirq;
    }

    ret = request_threaded_irq(rxirq, NULL, adsp_devfreq_up_irq_handler,
                               IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "devfreq_up_irq", NULL);
    if (ret) {
        pr_err(MSG_TAG"request devfreq_up_irq fail\n");
        return ret;
    }

    /*enable SRC int*/
    value = readl(adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN);
    writel(value | (1 << MBOX_DEVFREQ_UP_FROM_ADSP), adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN);

    return 0;
}


/*devfreq_irq down*/
irqreturn_t adsp_devfreq_dw_irq_handler(int irq, void *priv)
{
    u32 value;

    //pr_crit(MSG_TAG"%s\n", __func__);

    /*clean irq*/
    value = readl(adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_STA);
    writel((1 << MBOX_DEVFREQ_DOWN_FROM_ADSP), adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_STA);

    adsp_devfreq_down();

    return IRQ_HANDLED;
}

int adsp_devfreq_dw_irq_init(struct platform_device *pdev)
{
    int ret;
    struct device_node *np = pdev->dev.of_node;
    u32 rxirq;
    u32 value;

    PR_INFO(MSG_TAG"%s\n", __func__);

    rxirq = of_irq_get_byname(np, "devfreq_dw_irq");
    if(rxirq <= 0) {
        pr_err(MSG_TAG"request devfreq_dw_irq fail\n");
        return rxirq;
    }

    ret = request_threaded_irq(rxirq, NULL, adsp_devfreq_dw_irq_handler,
                               IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "devfreq_dw_irq", NULL);
    if (ret) {
        pr_err(MSG_TAG"request devfreq_dw_irq fail\n");
        return ret;
    }

    /*enable SRC int*/
    value = readl(adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN);
    writel(value | (1 << MBOX_DEVFREQ_DOWN_FROM_ADSP), adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN);

    return 0;
}


