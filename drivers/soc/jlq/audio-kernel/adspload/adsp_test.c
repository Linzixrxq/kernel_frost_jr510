/*
 * ADSP Loader driver for JLQ JR510 chips
 *
 * Copyright (C) 2021 JLQ, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
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
#include <asm/uaccess.h>

#include "adsp_local.h"

/***debug info***/
#define MSG_TAG	"[adspk][test]"

/***driver points***/
#define ADSP_TEST_COMPATIBLE	"jlq,adsp-test"

/*test,
    # cd /sys/kernel/adsp_test
    # echo 1 > test
    or,
    # echo 1 > /sys/kernel/adsp_test/test
*/

/*dt platform device*/
typedef struct _AdspTestDrvData {
    struct platform_device *pdev;
    struct work_struct twork;
    struct kobject *tkobj;
} AdspTestDrvData;
static AdspTestDrvData *drvdata;

static void touch_irq_to_adsp_test(struct platform_device *pdev);
static void touch_irq_to_adsp_abort(void);


/***attr value***/
#define ADSP_TEST_COMMON	    (1)
#define ADSP_TEST_STATUS	    (2)
#define ADSP_TEST_RUNINFO	    (3)
#define ADSP_TEST_GKINFO	    (4)
#define ADSP_TEST_ABORT		    (5)


/***attr***/
/* /sys/kernel/adsp_test/test */
#define ADSP_TEST_SYSDIR    "adsp_test"
#define ATTR_TEST       test
#define ATTR_TEST_NAME  "test"
static ssize_t test_show(struct kobject *kobj, struct kobj_attribute *attr,
                         char *buf);

static ssize_t test_store(struct kobject *kobj, struct kobj_attribute *attr,
                          const char *buf, size_t count);

static struct kobj_attribute _attr_test =
    __ATTR(ATTR_TEST, S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP, test_show, test_store);

static struct attribute *_attrs[] = {
    &_attr_test.attr,
    NULL,
};

static struct attribute_group _attr_group = {
    .attrs = _attrs,
};

/***sysfs func***/
static int op_value = 0;
static ssize_t test_show(struct kobject *kobj, struct kobj_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "%d\n", op_value);
}
static ssize_t test_store(struct kobject *kobj, struct kobj_attribute *attr,
                          const char *buf, size_t count)
{
    int ret;

    if (strcmp(attr->attr.name, ATTR_TEST_NAME) != 0)
        return 0;

    ret = kstrtoint(buf, 10, &op_value);
    if (ret < 0)
        return ret;

    if(op_value == ADSP_TEST_COMMON) {
        /*adsp glink*/
        //schedule_work(&drvdata->twork);
        //touch_irq_to_adsp_test(drvdata->pdev);

        /*ap glink with adsp*/
        //extern void t_ap_flink_work(void);
        //t_ap_flink_work();

        /*adsp devfreq*/
        //extern void t_adsp_devfreq(void);
        //t_adsp_devfreq();

        /*submod noti*/
        //extern void t_submod_noti(void);
        //t_submod_noti();
    } else if(op_value == ADSP_TEST_STATUS) {
        void __iomem *loca_adsp_state_base;
        unsigned int val;
        loca_adsp_state_base = ioremap(LOCAL_ADSP_STATE_BASE, 0x80);
        if (loca_adsp_state_base) {
            val = readl(loca_adsp_state_base + 0x00);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_EPC: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x04);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_ECAUSE: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x08);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_RUNNING: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x0c);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_DDR: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x10);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_SLEEP: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x14);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_EXCE: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x18);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_INTENABLE: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x1c);
            pr_crit(MSG_TAG"LOCAL_ADSP_STATE_INTERRUPT: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x68);
            pr_crit(MSG_TAG"LOCAL_ADSP_UNDEF_INT_BIT: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x6c);
            pr_crit(MSG_TAG"LOCAL_ADSP_UNDEF_INT_CNT: 0x%x\n", val);
        }
        iounmap(loca_adsp_state_base);
    } else if(op_value == ADSP_TEST_RUNINFO) {
        touch_irq_to_adsp_test(drvdata->pdev);
    } else if(op_value == ADSP_TEST_GKINFO) {
        void __iomem *loca_adsp_state_base;
        unsigned int val;
        loca_adsp_state_base = ioremap(LOCAL_ADSP_STATE_BASE, 0x80);
        if (loca_adsp_state_base) {
            val = readl(loca_adsp_state_base + 0x20);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_TX_DESC: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x24);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_RX_DESC: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x28);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_TX_FIFO: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x2c);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_RX_FIFO: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x30);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_TX_DATA_NUM: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x34);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_RX_DATA_NUM: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x38);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_TX_DATA_TIM: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x3c);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_RX_DATA_TIM: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x40);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_RX_DATA_CB_TIM: 0x%x\n", val);

            val = readl(loca_adsp_state_base + 0x44);
            pr_crit(MSG_TAG"LOCAL_ADSP_GK_RX_DATA_APR_TIM: 0x%x\n", val);
        }
        iounmap(loca_adsp_state_base);
    } else if(op_value == ADSP_TEST_ABORT) {
        touch_irq_to_adsp_abort();
    } else {
        /*nothing.*/
    }

    return count;
}

static int adsp_test_init_sysfs(struct platform_device *pdev)
{
    int ret;
    AdspTestDrvData *privdata = (AdspTestDrvData *)platform_get_drvdata(pdev);

    PR_INFO(MSG_TAG"adsp_test_init_sysfs\n");

    /*init sysfs*/
    privdata->tkobj = kobject_create_and_add(ADSP_TEST_SYSDIR, kernel_kobj);
    if (!privdata->tkobj) {
        pr_err(MSG_TAG"create adsp test kobj fail\n");
        return -ENOMEM;
    }

    ret = sysfs_create_group(privdata->tkobj, &_attr_group);
    if(ret != 0) {
        pr_err(MSG_TAG"create adsp test sysfs fail\n");
        kobject_put(privdata->tkobj);
        privdata->tkobj = NULL;
    }

    return ret;
}

static void adsp_test_uninit_sysfs(struct platform_device *pdev)
{
    AdspTestDrvData *privdata = (AdspTestDrvData *)platform_get_drvdata(pdev);

    if(privdata->tkobj) {
        sysfs_remove_group(privdata->tkobj, &_attr_group);
        kobject_put(privdata->tkobj);
        privdata->tkobj = NULL;
    }
}

#define TOP_MAILBOX_BASE                    0x34500000
#define TOP_MAILBOX_ADSP_NINTR_SET          (0x340)
#define TOP_MAILBOX_ADSP_NINTR_EN           (0x344)
#define TOP_MAILBOX_ADSP_NINTR_SRC_EN       (0x348)

static void touch_irq_to_adsp_test(struct platform_device *pdev)
{
    void __iomem *mb_base;
    unsigned int irq_index;

    struct device_node *np = pdev->dev.of_node;

    /*tx irq index*/
    if(of_property_read_u32(np, "touch-adsp-test-irq", &irq_index)) {
        pr_err(MSG_TAG"read node touch-adsp-test-irq fail\n");
        return;
    }
    pr_info(MSG_TAG"touch-adsp-test-irq: %d \n", irq_index);

    mb_base = ioremap(TOP_MAILBOX_BASE, 0x1000);

    writel(irq_index, mb_base + TOP_MAILBOX_ADSP_NINTR_SET);

    iounmap(mb_base);
}

static void touch_irq_to_adsp_abort(void)
{
    void __iomem *mb_base;
    unsigned int irq_index = MBOX_TOUCH_ABORT_TO_ADSP;

    pr_info(MSG_TAG"touch_irq_to_adsp_abort, irq: %d \n", irq_index);

    mb_base = ioremap(TOP_MAILBOX_BASE, 0x1000);

    writel(irq_index, mb_base + TOP_MAILBOX_ADSP_NINTR_SET);

    iounmap(mb_base);
}

/*
static void adsp_test_work(struct work_struct *work)
{
    AdspTestDrvData *privdata = container_of(work, AdspTestDrvData, twork);

    pr_info(MSG_TAG"adsp_test_work\n");

    touch_irq_to_adsp_test(privdata->pdev);

    return;
}
*/

static int adsp_test_probe(struct platform_device *pdev)
{
    int ret = 0;

    PR_INFO(MSG_TAG"adsp_test_probe\n");

    drvdata = devm_kzalloc(&pdev->dev, sizeof(AdspTestDrvData), GFP_KERNEL);

    /*init drvdata*/
    drvdata->pdev = pdev;
    platform_set_drvdata(pdev, drvdata);

    /*init loader*/
    ret = adsp_test_init_sysfs(pdev);

    /*init work*/
    //INIT_WORK(&drvdata->twork, adsp_test_work);

    return ret;
}

static int adsp_test_remove(struct platform_device *pdev)
{
    PR_INFO(MSG_TAG"adsp_test_remove\n");

    cancel_work_sync(&drvdata->twork);

    adsp_test_uninit_sysfs(pdev);

    devm_kfree(&pdev->dev, drvdata);
    drvdata = NULL;

    return 0;
}

static const struct of_device_id adsp_test_ids[] = {
    { .compatible = ADSP_TEST_COMPATIBLE },
    {},
};

MODULE_DEVICE_TABLE(of, adsp_test_ids);

static struct platform_driver adsp_test_drv = {
    .probe = adsp_test_probe,
    .remove = adsp_test_remove,
    .driver =
    {
        .name = "adsp-test",
        .of_match_table = adsp_test_ids,
        .owner = THIS_MODULE,
    }
};

//module_platform_driver(adsp_test_drv);
static int __init adsp_test_init(void)
{
    int r = platform_driver_register(&adsp_test_drv);

    if (r < 0)
        pr_err("register failed %d", r);

    return r;
}

static void __exit adsp_test_exit(void)
{
    platform_driver_unregister(&adsp_test_drv);
}

module_init(adsp_test_init);
module_exit(adsp_test_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("JLQ Inc.");
MODULE_DESCRIPTION("JLQ JR510 adsp kernel driver");


