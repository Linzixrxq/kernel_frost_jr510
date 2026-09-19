/*
 * ADSP Loader driver for JLQ JR510 chips
 *
 * Copyright (C) 2021 JLQ, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */


#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched/debug.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>


#include <soc/jlq/subsystem_notif.h>
#include <soc/jlq/subsystem_restart.h>
#include "adsp_local.h"

/*debug info*/
#define MSG_TAG	"[adspk][wdt]"

#define WDOG_CONTROL_REG_OFFSET             0x00
#define WDOG_TIMEOUT_RANGE_REG_OFFSET       0x04
#define WDOG_CURRENT_COUNT_REG_OFFSET       0x08
#define WDOG_COUNTER_RESTART_REG_OFFSET     0x0c
#define WDOG_INTR_STAT_REG_OFFSET           0x10
#define WDOG_INTR_CLR_REG_OFFSET            0x14

#define AUDIO_SYSCTRL_WDT_CLK_CTRL_OFFSET   0x700

struct adsp_wdt_dev {
    char name[16];
    void __iomem *base;
    void __iomem *crg_base;
    void __iomem *audio_sysctrl_base;
    struct clk *tclk;
    struct clk *pclk;
    int irq;
    struct device *dev;
};

static struct adsp_wdt_dev *pwdt = NULL;

int adsp_ramdump(void);

/*
static void adsp_subsys_restart_req(void)
{
    struct subsys_device *adsp_subsys = NULL;
    int ret;

    pr_crit(MSG_TAG"%s\n", __func__);

    adsp_subsys = subsystem_get(SUBSYS_ADSP_NAME);
    if (!adsp_subsys) {
        pr_err(MSG_TAG"%s: subsystem_get fail.\n", __func__);
        return;
    }

    ret = subsystem_restart_dev(adsp_subsys);
    if (ret) {
        pr_err(MSG_TAG"%s: subsystem_restart_dev fail.\n", __func__);
        return;
    }

    pr_crit(MSG_TAG"%s: succ\n", __func__);
}
*/

static struct subsys_device *adsp_subsys = NULL;

void adsp_subsystem_get(void)
{
    adsp_subsys = subsystem_get(SUBSYS_ADSP_NAME);
    if (!adsp_subsys) {
        pr_err(MSG_TAG"%s: subsystem_get fail.\n", __func__);
        return;
    }
}
EXPORT_SYMBOL(adsp_subsystem_get);

void adsp_subsys_reset_req(void)
{
    int ret;

    pr_crit(MSG_TAG"%s\n", __func__);

    ret = subsystem_restart_dev(adsp_subsys);
    if (ret) {
        pr_err(MSG_TAG"%s: subsystem_restart_dev fail.\n", __func__);
        return;
    }

    pr_crit(MSG_TAG"%s: succ\n", __func__);
}
EXPORT_SYMBOL(adsp_subsys_reset_req);


#ifdef ADSP_WDT
static irqreturn_t adsp_wdt_bite(int irq, void *dev_id)
{
    //pr_crit(MSG_TAG"ADSP WDT1 BITE !!!\n");

    /* read, clear int */
    readl(pwdt->base + WDOG_INTR_CLR_REG_OFFSET);

#ifdef TMP_DEBUG_ONLY
    {

        struct adsp_wdt_dev *wdt = (struct adsp_wdt_dev *)dev_id;
        unsigned int val;
        void __iomem *wdt_clk_ctrl_addr;

        /* read, clear int */
        readl(wdt->base + WDOG_INTR_CLR_REG_OFFSET);

        /*for debug only*/
        if (wdt->pclk)
            clk_enable(wdt->pclk);
        //print wdt regs
        pr_info(MSG_TAG"off00: 0x%x,", readl(wdt->base + WDOG_CONTROL_REG_OFFSET));
        pr_info(MSG_TAG"off04: 0x%x,", readl(wdt->base + WDOG_TIMEOUT_RANGE_REG_OFFSET));
        pr_info(MSG_TAG"off08: 0x%x,", readl(wdt->base + WDOG_CURRENT_COUNT_REG_OFFSET));
        pr_info(MSG_TAG"off10: 0x%x,", readl(wdt->base + WDOG_INTR_STAT_REG_OFFSET));
        pr_info(MSG_TAG"off14: 0x%x\n", readl(wdt->base + WDOG_INTR_CLR_REG_OFFSET));

#ifdef ADSP_WDT_CLK_BY_ADSP
        wdt_clk_ctrl_addr = wdt->audio_sysctrl_base + AUDIO_SYSCTRL_WDT_CLK_CTRL_OFFSET;
        val = readl(wdt_clk_ctrl_addr);
        writel(val & 0xfffffffc, wdt_clk_ctrl_addr);  /*disable tclk(bit[0]) and pclk(bit[1])*/
#else
        /*disable clk*/
        if (wdt->tclk)
            clk_disable(wdt->tclk);
        if (wdt->pclk)
            clk_disable(wdt->pclk);
#endif
    }
#endif

    return IRQ_WAKE_THREAD;
}

static irqreturn_t adsp_wdt_bite_after(int irq, void *dev_id)
{
    unsigned int val;
    void __iomem *wdt_clk_ctrl_addr;

    pr_crit(MSG_TAG"ADSP WDT1 BITE !!!\n");
    pr_crit(MSG_TAG"%s\n", __func__);

    if(pwdt) {
#ifdef ADSP_WDT_CLK_BY_ADSP
        //clk_unprepare(pwdt->pclk);
        //clk_unprepare(pwdt->tclk);

        wdt_clk_ctrl_addr = pwdt->audio_sysctrl_base + AUDIO_SYSCTRL_WDT_CLK_CTRL_OFFSET;
        val = readl(wdt_clk_ctrl_addr);
        writel(val & 0xfffffffc, wdt_clk_ctrl_addr);  /*disable tclk(bit[0]) and pclk(bit[1])*/
#endif
    }

    pr_crit(MSG_TAG"---IMPORTANT! Please CTRL+C/V follow info!---\n");
    /*adsp pd/pu state*/
    {
        void __iomem *toplpm_base;
        toplpm_base = ioremap(TOP_LPM_BASE, 0x200);
        if (toplpm_base) {
            /*PMU_TOP_FLAG*/
            val = readl(toplpm_base + 0x1f0);
            pr_crit(MSG_TAG"PMU_TOP_FLAG: 0x%x\n", val);

            /*adsp_pd_flag, bit[9]*/
            if((val & 0x00000200) != 0) {
                pr_crit(MSG_TAG"adsp state: power down\n");
            }

            /*adsp_pu_flag, bit[8]*/
            if((val & 0x00000100) != 0) {
                pr_crit(MSG_TAG"adsp state: power on\n");
            }
            iounmap(toplpm_base);
        }
    }

    /*adsp state*/
    {
        void __iomem *loca_adsp_state_base;
        loca_adsp_state_base = ioremap(LOCAL_ADSP_STATE_BASE, 0x80);
        if (loca_adsp_state_base) {
            val = readl(loca_adsp_state_base + 0x00);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_EPC: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x04);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_ECAUSE: 0x%x\n", val);

            pr_crit(MSG_TAG"adsp state, R/D/S/EC: 0x%x/0x%x/0x%x/0x%x\n",
                    readl(loca_adsp_state_base + 0x08),
                    readl(loca_adsp_state_base + 0x0c),
                    readl(loca_adsp_state_base + 0x10),
                    readl(loca_adsp_state_base + 0x14));

            /*crash reason, "adsp: adsp crash,EPC:0x1564168a,ECAUSE:0x0,RUN:0x104201,FLAG:0x82"*/
            {
                char reason[128] = {"adsp: adsp crash,"};
                snprintf(reason + strlen(reason), 20, "EPC:0x%x", readl(loca_adsp_state_base + 0x00));
                snprintf(reason + strlen(reason), 20, ",ECAUSE:0x%x", readl(loca_adsp_state_base + 0x04));
                snprintf(reason + strlen(reason), 20, ",RUN:0x%x", readl(loca_adsp_state_base + 0x08));
                snprintf(reason + strlen(reason), 20, ",FLAG:0x%x", readl(loca_adsp_state_base + 0x14));

                write_crash_reason(reason);
            }
        }
        iounmap(loca_adsp_state_base);
    }

    pr_crit(MSG_TAG"---IMPORTANT! Please CTRL+C/V above info!---\n");

    /*adsp ramdump flush*/
    adsp_ramdump();

    /*
    #ifdef ADSP_PANIC_DUMP
        panic("panic!!!");
    #endif
    */
    adsp_subsys_reset_req();
    return IRQ_HANDLED;
}


#ifdef ADSP_WDT_DEBUG
static int adsp_wdt_bite_irq_state_thread(void *data)
{
    int irq = (int)data;
    bool state;

    pr_info(MSG_TAG"adsp_wdt_bite_irq_state_thread started");

    while(1) {
        irq_get_irqchip_state(irq, IRQCHIP_STATE_PENDING, &state);
        pr_info(MSG_TAG"wdt bite irq(%d) PENDING state: %d\n", irq, state);
        mdelay(100);
    }

    return 0;
}

static void adsp_wdt_bite_irq_state_task(int irq)
{
    struct task_struct	*irq_state_thread_task;

    irq_state_thread_task = kthread_run(adsp_wdt_bite_irq_state_thread, (void *)irq,
                                        "adsp_wdt_bite_irq_state_thread");
    if (IS_ERR(irq_state_thread_task))
        pr_err(MSG_TAG"Failed to start adsp_wdt_bite_irq_state_thread\n");
}
#endif

static int adsp_wdt_parse_dt(struct platform_device *pdev,
                             struct adsp_wdt_dev *wdt)
{
    struct resource *resource;
    char *key;
    int ret = -ENODEV;
#ifdef ADSP_WDT_CTL
    struct device_node *dev_node;
#endif

    if (!wdt)
        return -EINVAL;

    snprintf(wdt->name, sizeof(wdt->name), "adsp_wdt1");

    key = "wdt1_base";
    resource = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
    if (!resource) {
        dev_err(&pdev->dev, "Fail to get adsp wdt1 resource\n");
        ret = -ENODEV;
        goto out;
    }
    wdt->base = devm_ioremap_resource(&pdev->dev, resource);
    if (IS_ERR(wdt->base)) {
        dev_err(&pdev->dev, "Failed to wdt1 ioremap\n");
        ret = -ENOMEM;
        goto out;
    }

    key = "audio_sysctrl_base";
    resource = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
    if (!resource) {
        dev_err(&pdev->dev, "Fail to get audio_sysctrl_base resource\n");
        ret = -ENODEV;
        goto out;
    }
    wdt->audio_sysctrl_base = devm_ioremap_resource(&pdev->dev, resource);
    if (IS_ERR(wdt->audio_sysctrl_base)) {
        dev_err(&pdev->dev, "Failed to audio_sysctrl ioremap\n");
        ret = -ENOMEM;
        goto out;
    }

#ifdef ADSP_WDT_CTL
    dev_node = of_find_compatible_node(NULL, NULL, "jlq,crg-base");
    if (!dev_node) {
        dev_err(&pdev->dev, "jlq,crg-base No compatible node found\n");
        return -ENODEV;
    }
    wdt->crg_base = of_iomap(dev_node, 0);
    if (IS_ERR(wdt->crg_base)) {
        dev_err(&pdev->dev, "crg_base reg ioremap failed\n");
        return -ENOMEM;
    }
    of_node_put(dev_node);
#endif

    wdt->tclk = devm_clk_get(&pdev->dev, "wdt1_tclk");
    if (IS_ERR(wdt->tclk)) {
        dev_err(&pdev->dev, "Failed to get wdt1 tclk\n");
        ret = -ENODEV;
        goto out;
    } else {
        PR_INFO(MSG_TAG"wdt->tclk rate, %ld\n", clk_get_rate(wdt->tclk));
    }

    wdt->pclk = devm_clk_get(&pdev->dev, "wdt1_pclk");
    if (IS_ERR(wdt->pclk)) {
        dev_err(&pdev->dev, "Failed to get wdt1 pclk\n");
        ret = -ENODEV;
        goto out;
    }

#ifdef ADSP_WDT_CLK_BY_ADSP
    /*
        ret = clk_prepare(wdt->tclk);
        if (ret) {
            dev_err(&pdev->dev, "wdt1 tclock prepare failed\n");
            goto out;
        }

        ret = clk_prepare(wdt->pclk);
        if (ret) {
            dev_err(&pdev->dev, "wdt1 pclock prepare failed\n");
            goto out;
        }
    */
#else
    ret = clk_prepare_enable(wdt->tclk);
    if (ret) {
        dev_err(&pdev->dev, "wdt1 tclock enable failed\n");
        goto out;
    }
    PR_INFO(MSG_TAG"clk_prepare_enable(wdt->tclk) ok\n");

    /*PCLK operation by ADSP, for the bite to GIC problem*/
    /*
    ret = clk_prepare_enable(wdt->pclk);
    if (ret) {
        dev_err(&pdev->dev, "wdt1 pclock enable failed\n");
        goto out;
    }
    pr_info(MSG_TAG"clk_prepare_enable(wdt->pclk) ok\n");
    */
#endif

    key = "wdt1_bite";
    wdt->irq = platform_get_irq_byname(pdev, key);
    if (wdt->irq < 0) {
        dev_err(&pdev->dev, "wdt1 get irq fail\n");
        ret = -ENODEV;
        goto out2;
    }
    PR_INFO(MSG_TAG"%s, wdt->irq: %d\n", __func__, wdt->irq);

    if(devm_request_threaded_irq(&pdev->dev, wdt->irq, adsp_wdt_bite, adsp_wdt_bite_after,
                                 IRQF_TRIGGER_RISING, "adsp-wdt-bite", wdt)) {
        dev_err(&pdev->dev, "wdt1 request irq fail\n");
        ret = -EIO;
        goto out2;
    }

    irq_set_irq_wake(wdt->irq, 1);  /*LPM & WDT_BITE -> GIC, set SLPCTL_AP_SLP_INTR2WAKE_MKx to 1*/

    PR_INFO(MSG_TAG"%s, request_irq(IRQF_TRIGGER_RISING)\n", __func__);

#ifdef ADSP_WDT_DEBUG
    /*debug wdt bite irq only*/
    //pr_info(MSG_TAG"%s, touch wdt bite irq by pending\n", __func__);
    //irq_set_irqchip_state(wdt->irq, IRQCHIP_STATE_PENDING, true);
    //adsp_wdt_bite_irq_state_task(wdt->irq);
#endif

    return 0;

out2:
#ifdef ADSP_WDT_CLK_BY_ADSP
    //clk_unprepare(wdt->pclk);
    //clk_unprepare(wdt->tclk);
#else
    clk_disable_unprepare(wdt->tclk);
    /*clk_disable_unprepare(wdt->pclk);*/
#endif

out:
    return ret;
}

static int adsp_wdt_probe(struct platform_device *pdev)
{
    struct adsp_wdt_dev *wdt;
    int ret;

    PR_INFO(MSG_TAG"%s\n", __func__);

    wdt = devm_kzalloc(&pdev->dev, sizeof(struct adsp_wdt_dev),
                       GFP_KERNEL);
    if (wdt == NULL) {
        return -ENOMEM;
    }

    ret = adsp_wdt_parse_dt(pdev, wdt);
    if (ret) {
        pr_err(MSG_TAG"adsp_wdt_parse_dt failed.\n");
        return ret;
    }

    platform_set_drvdata(pdev, wdt);
    wdt->dev = &pdev->dev;

    pwdt = wdt;

    PR_INFO(MSG_TAG"%s: succ\n", __func__);

    return 0;
}

static int adsp_wdt_remove(struct platform_device *pdev)
{
#ifdef ADSP_WDT_CLK_BY_ADSP
    //struct adsp_wdt_dev *wdt = platform_get_drvdata(pdev);
    //clk_unprepare(wdt->pclk);
    //clk_unprepare(wdt->tclk);
#else
    struct adsp_wdt_dev *wdt = platform_get_drvdata(pdev);
    if (wdt->tclk) {
        clk_disable_unprepare(wdt->tclk);
        clk_put(wdt->tclk);
    }

    if (wdt->pclk) {
        clk_disable_unprepare(wdt->pclk);
        clk_put(wdt->pclk);
    }
#endif

    platform_set_drvdata(pdev, NULL);

    return 0;
}

int adsp_wdt_rereset(void)
{
    int ret = -ENODEV;
    struct adsp_wdt_dev *wdt = pwdt;

    pr_info(MSG_TAG"%s\n", __func__);

    if (!wdt)
        return -EINVAL;

    wdt->tclk = devm_clk_get(wdt->dev, "wdt1_tclk");
    if (IS_ERR(wdt->tclk)) {
        dev_err(wdt->dev, "Failed to get wdt1 tclk\n");
        ret = -ENODEV;
        goto out;
    } else {
        pr_info(MSG_TAG"wdt->tclk rate, %ld\n", clk_get_rate(wdt->tclk));
    }

    wdt->pclk = devm_clk_get(wdt->dev, "wdt1_pclk");
    if (IS_ERR(wdt->pclk)) {
        dev_err(wdt->dev, "Failed to get wdt1 pclk\n");
        ret = -ENODEV;
        goto out;
    }

#ifdef ADSP_WDT_CLK_BY_ADSP
    /*
        ret = clk_prepare(wdt->tclk);
        if (ret) {
            dev_err(wdt->dev, "wdt1 tclock prepare failed\n");
            goto out;
        }

        ret = clk_prepare(wdt->pclk);
        if (ret) {
            dev_err(wdt->dev, "wdt1 pclock prepare failed\n");
            goto out;
        }
    */
#endif

    if(devm_request_threaded_irq(wdt->dev, wdt->irq, adsp_wdt_bite, adsp_wdt_bite_after,
                                 IRQF_TRIGGER_RISING, "adsp-wdt-bite", wdt)) {
        dev_err(wdt->dev, "wdt1 request irq fail\n");
        ret = -EIO;
        goto out;
    }

    irq_set_irq_wake(wdt->irq, 1);  /*LPM & WDT_BITE -> GIC, set SLPCTL_AP_SLP_INTR2WAKE_MKx to 1*/

    PR_INFO(MSG_TAG"%s, request_irq(IRQF_TRIGGER_RISING)\n", __func__);

    return 0;

out:
    return ret;
}
EXPORT_SYMBOL(adsp_wdt_rereset);


static const struct of_device_id adsp_wdt_of_match[] = {
    { .compatible  = "jlq,adsp_wdt", },
    {}
};
MODULE_DEVICE_TABLE(of, adsp_wdt_of_match);

static struct platform_driver adsp_wdt_driver = {
    .probe = adsp_wdt_probe,
    .remove = adsp_wdt_remove,
    .driver = {
        .name = "adsp-wdt-bite",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(adsp_wdt_of_match),
    },
};


int adsp_wdt_register(void)
{
    int ret;

    ret = platform_driver_register(&adsp_wdt_driver);
    if (ret) {
        pr_err(MSG_TAG"%s: fail\n", __func__);
        return -1;
    }

    PR_INFO(MSG_TAG"%s: succ\n", __func__);
    return 0;
}
EXPORT_SYMBOL(adsp_wdt_register);


#endif //ADSP_WDT



#define ADSP_RAMDUMP_PATH   "/data/vendor/audio/"
#define ADSP_IRAM_FILE      ADSP_RAMDUMP_PATH"ADSP_IRAM.BIN"
#define ADSP_DRAM_FILE      ADSP_RAMDUMP_PATH"ADSP_DRAM.BIN"
#define ADSP_TOPRAM_FILE    ADSP_RAMDUMP_PATH"ADSP_TOPRAM.BIN"
#define ADSP_REGS_FILE      ADSP_RAMDUMP_PATH"ADSP_REGS.BIN"
#define ADSP_DDR_FILE       ADSP_RAMDUMP_PATH"ADSP_DDR.BIN"

/*ADSP_IRAM.BIN*/
unsigned int ADSP_IRAM_addr = 0x34000000;
unsigned int ADSP_IRAM_size = 0x20000;

/*ADSP_DRAM.BIN*/
unsigned int ADSP_DRAM_addr = 0x34080000;
unsigned int ADSP_DRAM_size = 0x40000;

/*ADSP_TOPRAM.BIN*/
unsigned int ADSP_TOPRAM_addr = 0x33080000;
unsigned int ADSP_TOPRAM_size = 0x40000;

/*ADSP_REGS.BIN*/
unsigned int ADSP_REGS_addr = 0x33081000;
unsigned int ADSP_REGS_size = 0x400;

/*ADSP_DDR.BIN*/
unsigned int ADSP_DDR_addr = 0x95400000;
unsigned int ADSP_DDR_size = 0x1400000;

/*ddr for reserve adsp ram*/
unsigned int ADSP_SAVE_addr = 0x96500000;  //~end: 0x965a03fff
unsigned int ADSP_SAVE_size = (0x20000 + 0x40000 + 0x40000 + 0x400);
unsigned int ADSP_SAVE_IRAM_START_OFF = 0;
unsigned int ADSP_SAVE_DRAM_START_OFF = 0x20000;
unsigned int ADSP_SAVE_TOPRAM_START_OFF = 0x20000 + 0x40000;
unsigned int ADSP_SAVE_REGS_START_OFF = 0x20000 + 0x40000 + 0x40000;

/*ddr for ssr ramdump*/
void *ADSP_SSR_RAMDUMP_buff = NULL;
/*unsigned long ADSP_SSR_RAMDUMP_size = ADSP_SAVE_size+ADSP_DDR_size; //0x14A0400, 21627904*/
void adsp_dev_ramdump(void     *save_base, unsigned int save_size);


int adsp_ramdump(void)
{
    void  *iram_base = NULL;
    void  *dram_base = NULL;
    void  *topram_base = NULL;
    void  *regs_base = NULL;
    void  *ddr_save_base = NULL;

    pr_crit(MSG_TAG"%s\n", __func__);

    ddr_save_base = ioremap_nocache(ADSP_SAVE_addr, ADSP_SAVE_size);
    if (IS_ERR(ddr_save_base)) {
        pr_err(MSG_TAG"ddr_save ioremap fail!\n");
        goto err;
    }

    iram_base = ioremap_nocache(ADSP_IRAM_addr, ADSP_IRAM_size);
    if (IS_ERR(iram_base)) {
        pr_err(MSG_TAG"iram ioremap fail!\n");
        goto err;
    }

    memcpy_toio(ddr_save_base + ADSP_SAVE_IRAM_START_OFF, iram_base, ADSP_IRAM_size);


    dram_base = ioremap_nocache(ADSP_DRAM_addr, ADSP_DRAM_size);
    if (IS_ERR(dram_base)) {
        pr_err(MSG_TAG"dram ioremap fail!\n");
        goto err;
    }

    memcpy_toio(ddr_save_base + ADSP_SAVE_DRAM_START_OFF, dram_base, ADSP_DRAM_size);


    topram_base = ioremap_nocache(ADSP_TOPRAM_addr, ADSP_TOPRAM_size);
    if (IS_ERR(topram_base)) {
        pr_err(MSG_TAG"topram ioremap fail!\n");
        goto err;
    }

    memcpy_toio(ddr_save_base + ADSP_SAVE_TOPRAM_START_OFF, topram_base, ADSP_TOPRAM_size);


    regs_base = ioremap_nocache(ADSP_REGS_addr, ADSP_REGS_size);
    if (IS_ERR(regs_base)) {
        pr_err(MSG_TAG"regs ioremap fail!\n");
        goto err;
    }

    memcpy_toio(ddr_save_base + ADSP_SAVE_REGS_START_OFF, regs_base, ADSP_REGS_size);

#ifdef ADSP_DUMP_DEV
    adsp_dev_ramdump(ddr_save_base, ADSP_SAVE_size);
#endif

    iounmap(iram_base);
    iounmap(dram_base);
    iounmap(topram_base);
    iounmap(regs_base);
    iounmap(ddr_save_base);

    pr_crit(MSG_TAG"%s succ\n", __func__);

    return 0;

err:
    if(!IS_ERR_OR_NULL(iram_base))
        iounmap(iram_base);
    if(!IS_ERR_OR_NULL(dram_base))
        iounmap(dram_base);
    if(!IS_ERR_OR_NULL(topram_base))
        iounmap(topram_base);
    if(!IS_ERR_OR_NULL(regs_base))
        iounmap(regs_base);
    if(!IS_ERR_OR_NULL(ddr_save_base))
        iounmap(ddr_save_base);

    return -1;
}
EXPORT_SYMBOL(adsp_ramdump);

#ifdef TMP_DEBUG_ONLY
static int adsp_ramdump_v0(void)
{
    struct file *filp_iram = NULL;
    struct file *filp_dram = NULL;
    struct file *filp_topram = NULL;
    struct file *filp_regs = NULL;
    struct file *filp_ddr = NULL;
    void  *iram_base = NULL;
    void  *dram_base = NULL;
    void  *topram_base = NULL;
    void  *regs_base = NULL;
    void  *ddr_base = NULL;

    struct inode *inode;
    mm_segment_t fs;
    loff_t fsize = 0, pos = 0;
    ssize_t count;
    int ret;

    pr_crit(MSG_TAG"%s\n", __func__);

    /*ADSP IRAM DUMP*/
    filp_iram = filp_open(ADSP_IRAM_FILE, O_RDWR | O_TRUNC | O_CREAT, 0644);
    if (IS_ERR(filp_iram)) {
        pr_err(MSG_TAG"faild to open %s!\n", ADSP_IRAM_FILE);
        goto err1;
    }

    iram_base = ioremap_nocache(ADSP_IRAM_addr, ADSP_IRAM_size);
    if (IS_ERR(iram_base)) {
        pr_err(MSG_TAG"iram ioremap fail!\n");
        goto err2;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    count = filp_iram->f_op->write(filp_iram, iram_base, ADSP_IRAM_size, &pos);
    if (count < 0) {
        pr_err("failed to read: %d\n", count);
        goto err3;
    }
    set_fs(fs);

    inode = filp_iram->f_inode;
    fsize = inode->i_size;
    pr_crit("ramdump %s(size: 0x%lx) succ\n", ADSP_IRAM_FILE, fsize);

    iounmap(iram_base);
    filp_close(filp_iram, NULL);

    return 0;

err3:
    set_fs(fs);
err2:
err1:
    if(!IS_ERR_OR_NULL(filp_iram))
        filp_close(filp_iram, NULL);

    if(!IS_ERR_OR_NULL(iram_base))
        iounmap(iram_base);

    return -1;
}
#endif

void adsp_wdt_clk_disable(void)
{
    void __iomem *audio_sysctrl = NULL;
    void __iomem *audio_sysctrl_wdt_clk_ctrl = NULL;
    unsigned int val;

    pr_crit(MSG_TAG"%s\n", __func__);

    audio_sysctrl = ioremap_nocache(AUDIO_SYSCTRL_BASE, 0x1000);
    if (IS_ERR(audio_sysctrl)) {
        pr_err(MSG_TAG"audio_sysctrl ioremap fail!\n");
        return;
    }

    audio_sysctrl_wdt_clk_ctrl = audio_sysctrl + AUDIO_SYSCTRL_WDT_CLK_CTRL_OFFSET;
    val = readl(audio_sysctrl_wdt_clk_ctrl);
    writel(val & 0xfffffffc, audio_sysctrl_wdt_clk_ctrl);  /*disable tclk(bit[0]) and pclk(bit[1])*/

    if(!IS_ERR_OR_NULL(audio_sysctrl))
        iounmap(audio_sysctrl);

}
EXPORT_SYMBOL(adsp_wdt_clk_disable);

void adsp_dev_ramdump(void     *save_base, unsigned int save_size)
{
    void *dump_buff;
    size_t dump_size = ADSP_SAVE_size + ADSP_DDR_size;
    void __iomem *ddrmem_base = NULL;

    //pr_crit(MSG_TAG"%s\n", __func__);

    if(!ADSP_SSR_RAMDUMP_buff) {
        ADSP_SSR_RAMDUMP_buff = vmalloc(dump_size);
        if (!ADSP_SSR_RAMDUMP_buff) {
            pr_err(MSG_TAG" %s: ADSP_SSR_RAMDUMP_buff fail\n", __func__);
            return;
        }
    }

    dump_buff = ADSP_SSR_RAMDUMP_buff;

    memset((unsigned char *)dump_buff, 0, dump_size);

    /*adsp save*/
    if(!save_base)
        return;
    memcpy((unsigned char *)dump_buff, save_base, save_size);

    //pr_crit(MSG_TAG"%s: save copy\n", __func__);

    /*adsp ddr*/
    ddrmem_base = ioremap(ADSP_DDR_addr, ADSP_DDR_size);
    if (!ddrmem_base) {
        pr_err(MSG_TAG" %s: ddrmem_base map fail\n", __func__);
        vfree(dump_buff);
        return;
    }
    memcpy((unsigned char *)dump_buff + save_size, ddrmem_base, ADSP_DDR_size);

    //pr_crit(MSG_TAG"%s: mem copy\n", __func__);
    iounmap(ddrmem_base);

    adsp_dump_available(dump_buff, dump_size);

    //pr_crit(MSG_TAG"%s: done\n", __func__);
    return;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JLQ Inc.");
MODULE_DESCRIPTION("JLQ JR510 adsp kernel driver");

