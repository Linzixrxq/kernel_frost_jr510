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
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/of_address.h>
#include <asm/uaccess.h>
#include <linux/delay.h>

#include <soc/jlq/subsystem_notif.h>
#include <soc/jlq/subsystem_restart.h>
#include "adsp_subsys.h"
#include "adsp_local.h"
#include "../asoc/jlq_audio_calibration.h"
#include <ipc/apr.h>
#ifdef ADSP_IMG_AUTH
#include "soc/jlq/jlq_verifier.h"
#endif

/*debug info*/
#define MSG_TAG	"[adspk]"

/*driver points*/
#define ADSP_LOAD_COMPATIBLE	"jlq,adsp-loader"
#define ADSP_LOAD_SYSDIR		"adsp_load"  /* /sys/kernel/adsp_load/loader */


#define LOAD_PATH_RAMDISK       0   /*if use ramdisk*/
#define LOAD_PATH_VENDOR        1   /*if use dsp partition*/
#define LOAD_PATH_BACKDOOR      0

/*default adsp bin filename, dts at first*/
#define HIFI3_ADSP_BIN	"adsp.bin"
#define ADSP_LMA_BIN	"adsp.lma"
#define HIFI3_ADSP_SIG	"adsp.sig"
#if LOAD_PATH_RAMDISK
#define ADSP_BIN_PATH        ""
#endif
#if LOAD_PATH_VENDOR
#define ADSP_BIN_PATH        ""     //"image/"  /*according to product path define*/
#endif
#if LOAD_PATH_BACKDOOR
#define ADSP_BIN_PATH        ""
#endif


/*LMA data*/
typedef struct _LMA_IDX_ST LMA_IDX_ST;
LMA_IDX_ST lma_idx[LMA_IDX_MAX];

/*driver data*/
static AdspLoadDrvData *drvdata = NULL;

/*mbox0*/
#define TOP_MAILBOX_ADSP_NINTR_SET  (0x340)
void __iomem *adsp_top_mbox_base = NULL;

/*mdm irq*/
static unsigned int mdm_state_notify_adsp_irq_index;

/*reset enable*/
static unsigned int reset_enable = 0;

/*loader information*/
#define LOADER_START	(1)
#define LOADER_GOING	(2)
#define LOADER_END		(3)

static int loader_op = -1;

/*adsp state*/
static int adsp_state = ADSP_STAT_UNINIT;

/*gk tx/rx clean*/
#define GK_DESC_TX_CLEAN	(1)
#define GK_DESC_RX_CLEAN	(2)
#define GK_DESC_ALL_CLEAN	(3)


/*func declare*/
int adsp_ctrl_regs_base_init(struct platform_device *pdev);
int adsp_dereset(void);
void adsp_run(void);
int adsp_rereset(void);
void adsp_gk_desc_clean(int flag);
unsigned int adsp_reset_enable(void);



#ifdef ADSP_LPM
int adsp_lpm_init(struct platform_device *pdev);
void adsp_lpm_uninit(struct platform_device *pdev);
int adsp_icc_set_bw(struct platform_device *pdev, u32 avg_bw, u32 peak_bw);
#endif

#ifdef ADSP_REGU
int adsp_regu_get(struct platform_device *pdev);
void adsp_regu_put(struct platform_device *pdev);
int adsp_regu_enable(void);
int adsp_regu_disable(void);
#endif

#ifdef ADSP_OPP
int adsp_opp_init(struct platform_device *pdev);
int adsp_opp_uninit(struct platform_device *pdev);
#endif

#ifdef ADSP_WDT
int adsp_wdt_register(void);
int adsp_wdt_rereset(void);
#endif

int adsp_ramdump(void);
void adsp_wdt_clk_disable(void);
void adsp_stall(void);

extern int apr_tal_pd_clean(void);


/*** adsp state attribute define start ***/
static ssize_t adsp_state_show(struct kobject *kobj, struct kobj_attribute *attr,
                               char *buf);

static struct kobj_attribute adsp_state_attribute =
    __ATTR(adspState, S_IRUSR | S_IRGRP, adsp_state_show, NULL);

static ssize_t adsp_state_show(struct kobject *kobj, struct kobj_attribute *attr,
                               char *buf)
{
    return sprintf(buf, "%d\n", adsp_state);
}
/*** adsp state attribute define end ***/

int adsp_state_get(void)
{
    return adsp_state;
}
EXPORT_SYMBOL(adsp_state_get);


#ifdef ADSP_SSR

/*** adsp ssr uevent notify start***/
#ifdef ADSP_SSR_UEVENT_NOTIFY
struct adsp_reset_uevent_data {
    struct kobject kobj;
    struct kobj_type ktype;
};

struct adsp_reset_uevent_data adsp_reset_uevent;
static struct kset *adsp_uevent_kset;

static int adsp_init_uevent_kset(void)
{
    int ret = 0;

    if (adsp_uevent_kset)
        goto done;

    /* Create a kset under /sys/kernel/ */
    adsp_uevent_kset = kset_create_and_add("adsp_ssr_event", NULL, kernel_kobj);
    if (!adsp_uevent_kset) {
        pr_err("%s: error creating uevent kernel set", __func__);
        ret = -EINVAL;
    }
done:
    return ret;
}

static void adsp_destroy_uevent_kset(void)
{
    if (adsp_uevent_kset) {
        kset_unregister(adsp_uevent_kset);
        adsp_uevent_kset = NULL;
    }
}

int adsp_init_uevent_data(struct adsp_reset_uevent_data *uevent_data, char *name)
{
    int ret = -EINVAL;

    if (!uevent_data || !name)
        return ret;

    ret = adsp_init_uevent_kset();
    if (ret)
        return ret;

    /* Set kset for kobject before initializing the kobject */
    uevent_data->kobj.kset = adsp_uevent_kset;

    /* Initialize kobject and add it to kernel */
    ret = kobject_init_and_add(&uevent_data->kobj, &uevent_data->ktype,
                               NULL, "%s", name);
    if (ret) {
        pr_err("%s: error initializing uevent kernel object: %d",
               __func__, ret);
        kobject_put(&uevent_data->kobj);
        return ret;
    }

    /* Send kobject add event to the system */
    kobject_uevent(&uevent_data->kobj, KOBJ_ADD);

    return ret;
}

void adsp_destroy_uevent_data(struct adsp_reset_uevent_data *uevent_data)
{
    if (uevent_data) {
        kobject_put(&uevent_data->kobj);
    }

    adsp_destroy_uevent_kset();
}

int adsp_send_uevent(struct adsp_reset_uevent_data *uevent_data, char *event)
{
    char *env[] = { event, NULL };

    if (!event || !uevent_data)
        return -EINVAL;

    return kobject_uevent_env(&uevent_data->kobj, KOBJ_CHANGE, env);
}

void adsp_release_uevent_data(struct kobject *kobj)
{
    struct adsp_reset_uevent_data *data = container_of(kobj,
                                          struct adsp_reset_uevent_data, kobj);
    kfree(data);
}
#endif
/*** adsp ssr uevent notify end***/


/**** adsp ssr api define *******/
static int adsp_load_notifier_service_cb(struct notifier_block *this,
        unsigned long opcode, void *data);
static ssize_t adsp_ssr_store(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              const char *buf, size_t count);

/**** adsp load api define *******/
static int adsp_load_ext(struct platform_device *pdev);

static struct notifier_block adsp_load_service_nb = {
    .notifier_call  = adsp_load_notifier_service_cb,
    .priority = 0,
};

static struct kobj_attribute adsp_ssr_attribute =
    __ATTR(ssr, S_IWUSR | S_IWGRP, NULL, adsp_ssr_store);

static ssize_t adsp_ssr_store(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              const char *buf,
                              size_t count)
{
    int ssr_command = 0;
    struct subsys_device *adsp_dev = NULL;
    struct platform_device *pdev = (struct platform_device *)drvdata->pdev;
    AdspLoadDrvData *privdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);
    int rc;

    if (kstrtoint(buf, 10, &ssr_command) < 0)
        return -EINVAL;

    if (!ssr_command) {
        pr_err("ssr_command(%d) is not reset.\n", ssr_command);
        return -EINVAL;
    }

    /*test uevent, /sys/kernel/adsp_load/ssr */
    {
#ifdef ADSP_SSR_UEVENT_NOTIFY
        if (ssr_command == 1) {
            PR_INFO("ssr_command(%d).\n", ssr_command);
            adsp_send_uevent(&adsp_reset_uevent, "ADSP_STAT_SHUTDOWN");
        } else if (ssr_command == 2) {
            PR_INFO("ssr_command(%d).\n", ssr_command);
            adsp_send_uevent(&adsp_reset_uevent, "ADSP_STAT_RUNNING");
        }

        return count;
#endif
    }

    if (!privdata) {
        pr_err("priv is null\n");
        return -EINVAL;
    }

    if (!privdata->pil_h) {
        /* adsp pil should get when adsp load. */
        PR_INFO("pil_h is null. prepare to get adsp pil.\n");
        privdata->pil_h = subsystem_get("adsp");
        if (!privdata->pil_h) {
            PR_INFO(MSG_TAG"get adsp subsystem fail.\n");
            return -EINVAL;
        }
    }

    adsp_dev = (struct subsys_device *)privdata->pil_h;
    if (!adsp_dev) {
        pr_err("adsp_dev is null\n");
        return -EINVAL;
    }

    PR_INFO("requesting for ADSP restart.\n");
    /* subsystem_restart_dev has worker queue to handle */
    rc = subsystem_restart_dev(adsp_dev);
    if (rc) {
        pr_err("subsystem_restart_dev failed\n");
        return rc;
    }

    PR_INFO("ADSP restarted completed.\n");
    return count;
}


static int adsp_load_notifier_service_cb(struct notifier_block *this,
        unsigned long opcode, void *data)
{
    //struct platform_device *pdev = (struct platform_device *)drvdata->pdev;
    static int adsp_restart_flag = 0;

    pr_crit(MSG_TAG"%s: opcode 0x%lx\n", __func__, opcode);

    switch (opcode) {
    case SUBSYS_BEFORE_SHUTDOWN:
        adsp_restart_flag = 1;
        break;
    case SUBSYS_AFTER_SHUTDOWN:
        adsp_restart_flag = 1;
        break;
    case SUBSYS_BEFORE_POWERUP:
        if(adsp_restart_flag == 1) {
            pr_crit(MSG_TAG"%s: adsp exc!\n", __func__);

            adsp_restart_flag = 0;

#ifdef ADSP_SSR_UEVENT_NOTIFY
            pr_crit(MSG_TAG"%s: SUBSYS_OFFLINE!\n", __func__);
            subsystem_state_set("adsp", 1);  //SUBSYS_OFFLINE
            jlq_apr_adsp_down();
            adsp_send_uevent(&adsp_reset_uevent, "ADSP_STAT_SHUTDOWN");
#endif

#ifdef ADSP_SUB_RESET
            if(adsp_reset_enable() == 1) {
                pr_crit(MSG_TAG"ADSP CRASH->adsp rereset!\n");
                adsp_gk_desc_clean(GK_DESC_ALL_CLEAN);
                adsp_subsys_reset();
            }
#endif

#ifdef ADSP_SSR_UEVENT_NOTIFY
            subsystem_state_set("adsp", 2);  //SUBSYS_ONLINE
            msleep(WAIT_ADSP_RERESET_TIME);
            jlq_apr_adsp_up();
            adsp_send_uevent(&adsp_reset_uevent, "ADSP_STAT_RUNNING");
            pr_crit(MSG_TAG"%s: SUBSYS_ONLINE!\n", __func__);
#endif
        }
        break;
    case SUBSYS_AFTER_POWERUP:
        break;
    case SUBSYS_SOC_RESET:  /*jr510 soc restart->sysdump, kernel will panic*/
        //adsp_wdt_clk_disable();
        pr_crit(MSG_TAG"ADSP->SUBSYS_SOC_RESET\n");
        break;

    default:
        break;
    }

    return NOTIFY_OK;
}


#endif
/****************************adsp ssr end*******************************************/

#ifdef ADSP_MDM_NOTIFI
/*mdm notify function*/
static void mdm_state_notify_adsp_irq(void)
{
    writel(mdm_state_notify_adsp_irq_index, adsp_top_mbox_base + TOP_MAILBOX_ADSP_NINTR_SET);
    /*#ifdef ADSP_SUB_RESET
        pr_crit(MSG_TAG"MODEM CRASH->adsp rereset!\n");
        adsp_subsys_reset();
    #endif*/
}

static int mdm_state_notifier_cb(struct notifier_block *this,
                                 unsigned long opcode, void *data)
{
    static int modem_restart_flag = 0;
    int delay_count = 20000 * 1000; //>5ms

    pr_crit(MSG_TAG"%s: opcode 0x%lx\n", __func__, opcode);

    switch (opcode) {
    case SUBSYS_BEFORE_SHUTDOWN:
        modem_restart_flag = 1;
        break;
    case SUBSYS_AFTER_SHUTDOWN:
        modem_restart_flag = 1;
        break;
    case SUBSYS_BEFORE_POWERUP:   /*case 1: modem crash->restart; case 2: mbn switch->pm service restart; */
        //PHASE3.
        if(modem_restart_flag == 1) {
#ifdef ADSP_SSR_UEVENT_NOTIFY
            pr_crit(MSG_TAG"%s: SUBSYS_OFFLINE!\n", __func__);
            subsystem_state_set("adsp", 1);  //SUBSYS_OFFLINE
            jlq_apr_adsp_down();
            adsp_send_uevent(&adsp_reset_uevent, "ADSP_STAT_SHUTDOWN");
#endif

#ifdef MDM_SSR_DUMP_DEV
            adsp_wdt_clk_disable();
            mdm_state_notify_adsp_irq();
            while(delay_count--);
            adsp_stall();
            adsp_ramdump();
#else
            adsp_wdt_clk_disable();
            adsp_stall();
#endif

            adsp_gk_desc_clean(GK_DESC_ALL_CLEAN);
        }
        break;
    case SUBSYS_AFTER_POWERUP:
        if(modem_restart_flag == 1) {
            pr_crit(MSG_TAG"%s: modem exc!\n", __func__);

            modem_restart_flag = 0;

#ifdef ADSP_SUB_RESET
            if(adsp_reset_enable() == 1) {
                pr_crit(MSG_TAG"MODEM CRASH->adsp rereset!\n");
                adsp_subsys_reset();
            }
#endif

#ifdef ADSP_SSR_UEVENT_NOTIFY
            subsystem_state_set("adsp", 2);  //SUBSYS_ONLINE
            msleep(WAIT_ADSP_RERESET_TIME);
            jlq_apr_adsp_up();
            adsp_send_uevent(&adsp_reset_uevent, "ADSP_STAT_RUNNING");
            pr_crit(MSG_TAG"%s: SUBSYS_ONLINE!\n", __func__);
#endif
        }
        break;
    case SUBSYS_RAMDUMP_NOTIFICATION:  /*case 1: modem crash->restart*/
        /*
        adsp_wdt_clk_disable();
        adsp_stall();
        adsp_ramdump();
        mdm_state_notify_adsp_irq();
        */

        break;
    case SUBSYS_SOC_RESET:   /*jr510 soc restart, kernel will panic*/
        pr_crit(MSG_TAG"MODEM->SUBSYS_SOC_RESET\n");
        adsp_wdt_clk_disable();
        mdm_state_notify_adsp_irq();
        while(delay_count--);
        adsp_stall();
        adsp_ramdump();
        break;
    default:
        break;
    }

    return NOTIFY_OK;
}

static struct notifier_block mdm_state_notifier_nb = {
    .notifier_call  = mdm_state_notifier_cb,
    .priority = 0,
};

static void mdm_state_notifi_register(void)
{
    subsys_notif_register_notifier(
        SUBSYS_MODEM_NAME, &mdm_state_notifier_nb);
    return;
}
#endif

#ifdef ADSP_PANIC_NOTIFI
static int panic_nb_call(struct notifier_block *this,
                         unsigned long event, void *ptr)
{
    pr_crit(MSG_TAG"ADSP->%s\n", __func__);
    if(adsp_state_get() == ADSP_STAT_RUNNING) {
        adsp_wdt_clk_disable();
        adsp_stall();
    }
    return NOTIFY_DONE;
}

static struct notifier_block panic_nb = {
    .notifier_call  = panic_nb_call,
};

static void panic_noti_register(void)
{
    atomic_notifier_chain_register(&panic_notifier_list,
                                   &panic_nb);
}

#endif


#ifdef LOAD_ADSP_BY_AP

/***loader attr***/
static ssize_t loader_show(struct kobject *kobj, struct kobj_attribute *attr,
                           char *buf);

static ssize_t loader_store(struct kobject *kobj, struct kobj_attribute *attr,
                            const char *buf, size_t count);

static struct kobj_attribute _attr_loader =
    __ATTR(loader, S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP, loader_show, loader_store);

static struct attribute *_attrs[] = {
    &_attr_loader.attr,
    &adsp_state_attribute.attr,
#ifdef ADSP_SSR
    &adsp_ssr_attribute.attr,
#endif
    NULL,
};

static struct attribute_group _attr_group = {
    .attrs = _attrs,
};

/***loader sysfs func***/
static ssize_t loader_show(struct kobject *kobj, struct kobj_attribute *attr,
                           char *buf)
{
    return sprintf(buf, "%d\n", loader_op);
}
static ssize_t loader_store(struct kobject *kobj, struct kobj_attribute *attr,
                            const char *buf, size_t count)
{
    int ret, loader = 0;

    if (strcmp(attr->attr.name, "loader") != 0)
        return 0;

    ret = kstrtoint(buf, 10, &loader);
    if (ret < 0)
        return ret;

    if(loader == LOADER_START) {
        PR_INFO(MSG_TAG"LOADER_START\n");
        schedule_work(&drvdata->loader_work);
        loader_op = LOADER_END;
    }

    return count;
}


/***load adsp bin func***/
static int fm_load_bin(const char *name, unsigned char **buf, size_t *sz, struct device *dev)
{
    int ret = 0;
    const struct firmware *fm = NULL;
    char name_str[64];

    memset(name_str, 0, sizeof(name_str));
    strncat(name_str, ADSP_BIN_PATH, sizeof(ADSP_BIN_PATH));
    strncat(name_str, name, 16);

    PR_INFO(MSG_TAG"fm will load %s\n", name_str);

    ret = request_firmware(&fm, name_str, dev);
    if(ret || !fm || !fm->size) {
        pr_err(MSG_TAG"fm load %s fail(%d)\n", name_str, ret);
        return ret;
    }

    PR_INFO(MSG_TAG"fm size:%d\n", (int)fm->size);

    *buf = vzalloc(fm->size);
    if(!(*buf)) {
        pr_err(MSG_TAG"malloc fail\n");
        release_firmware(fm);
        return -ENOMEM;
    }

    memcpy(*buf, fm->data, fm->size);

    *sz = fm->size;

    wmb();

    pr_info(MSG_TAG"fm load %s succ(size:%d)\n", name_str, (int)fm->size);

    release_firmware(fm);
    return ret;
}

#if LOAD_PATH_BACKDOOR
static int fm_load_bin_from_backdoor(unsigned char **adsp_bin_buf, unsigned char **lma_bin_buf)
{
    void __iomem *adsp_bin_base = NULL;
    void __iomem *lma_bin_base = NULL;

    /*ADSP BIN load*/
    adsp_bin_base = ioremap(ADSP_BIN_BACKDOOR_LOAD_ADDR, ADSP_BIN_BACKDOOR_LOAD_ADDR_SIZE);
    if(!adsp_bin_base) {
        pr_err(MSG_TAG"ioremap ADSP_BIN_BACKDOOR fail\n");
        return -EIO;
    }

    *adsp_bin_buf = vzalloc(ADSP_BIN_BACKDOOR_LOAD_ADDR_SIZE);
    if(!(*adsp_bin_buf)) {
        pr_err(MSG_TAG"malloc fail\n");
        return -ENOMEM;
    }

    memcpy_fromio(*adsp_bin_buf, adsp_bin_base, ADSP_BIN_BACKDOOR_LOAD_ADDR_SIZE);

    /*LMA BIN load*/
    lma_bin_base = ioremap(ADSP_LMABIN_BACKDOOR_LOAD_ADDR, ADSP_LMABIN_BACKDOOR_LOAD_ADDR_SIZE);
    if(!lma_bin_base) {
        pr_err(MSG_TAG"ioremap ADSP_LMABIN_BACKDOOR fail\n");
        return -EIO;
    }

    *lma_bin_buf = vzalloc(ADSP_LMABIN_BACKDOOR_LOAD_ADDR_SIZE);
    if(!(*lma_bin_buf)) {
        pr_err(MSG_TAG"malloc fail\n");
        return -ENOMEM;
    }

    memcpy_fromio(*lma_bin_buf, lma_bin_base, ADSP_LMABIN_BACKDOOR_LOAD_ADDR_SIZE);

    wmb();

    iounmap(adsp_bin_base);
    iounmap(lma_bin_base);

    PR_INFO(MSG_TAG"fm_load_bin_from_backdoor\n");

    return 0;
}
#endif

static int adsp_load_ext(struct platform_device *pdev)
{
    int ret = 0;

    const char *adspbin = HIFI3_ADSP_BIN;
    const char *adsplmabin = ADSP_LMA_BIN;

    unsigned char *adsp_bin_buf = NULL;
    unsigned char *adsp_lma_bin_buf = NULL;

    unsigned char *bin_base;
    unsigned char *lma_info_base;

    LMA_IDX_ST lma_info;
    void __iomem *lma_v;
    unsigned int size;
    unsigned char *offs;
    unsigned int flag;
    size_t bin_sz, lma_sz;

    struct resource *r;
    /*ddr*/
    phys_addr_t ddr_ram_phys;
    resource_size_t ddr_ram_size;
    void  *ddrmem_base = NULL;
    /*iram*/
    phys_addr_t iram_phys;
    resource_size_t iram_size;
    /*dram*/
    phys_addr_t dram_phys;
    resource_size_t dram_size;
    /*topram*/
    phys_addr_t topram_phys;
    resource_size_t topram_size;

#ifdef ADSP_IMG_AUTH
    const struct firmware *fw_sign = NULL;
    char *sig_header = NULL;
    struct verifier_image_info *arg_info;
    size_t arg_size;
    int session_id = -1;
    int shm_id = -1;
    int seg_count = 0;
    int seg_index = 0;
    bool tz_verify;
#endif

    int i, j, k, m;
    u32 v32;
    u8 v8;

    struct device *dev = &pdev->dev;

    struct fwnode_handle *fwh = of_fwnode_handle(dev->of_node);

    if (fwnode_property_read_string(fwh, "adspbin_name", &adspbin)) {
        dev_err(dev, MSG_TAG"unable to get property \"adspbin_name\"!");
        adspbin = HIFI3_ADSP_BIN;
    }

    if (fwnode_property_read_string(fwh, "lmabin_name", &adsplmabin)) {
        dev_err(dev, MSG_TAG"unable to get property \"lmabin_name\"!");
        adsplmabin = ADSP_LMA_BIN;
    }

    PR_INFO(MSG_TAG"load bin: %s %s\n", adspbin, adsplmabin);

#ifdef ADSP_IMG_AUTH
    tz_verify = of_property_read_bool(pdev->dev.of_node,
                                      "jlq,tz_verify");
    pr_info(MSG_TAG"tz_verify:[%d]\n", tz_verify);

    if (tz_verify) {
        char fw_name[64];

        snprintf(fw_name, sizeof(fw_name), "%s%s", ADSP_BIN_PATH, HIFI3_ADSP_SIG);
        ret = request_firmware(&fw_sign, fw_name, dev);
        if (ret) {
            pr_err(MSG_TAG"Failed to locate %s(rc:%d)\n", fw_name, ret);
            return -ENOENT;
        }
    }
#endif

#if LOAD_PATH_BACKDOOR
    PR_INFO(MSG_TAG"load bin by haps backdoor\n");

    ret = fm_load_bin_from_backdoor(&adsp_bin_buf, &adsp_lma_bin_buf);

    if(ret != 0 || !adsp_bin_buf || !adsp_lma_bin_buf)
        return ret;

#else
    ret = fm_load_bin(adspbin, &adsp_bin_buf, &bin_sz, dev);
    if(ret) {
        pr_err(MSG_TAG"fm_load_bin(adspbin) fail\n");
        goto err1;
    }

    PR_INFO(MSG_TAG"adspbin size: %ld\n", bin_sz);

    ret = fm_load_bin(adsplmabin, &adsp_lma_bin_buf, &lma_sz, dev);
    if(ret) {
        pr_err(MSG_TAG"fm_load_bin(adsplmabin) fail\n");
        goto err1;
    }

    PR_INFO(MSG_TAG"adsplmabin size: %ld, lma_num: %ld\n", lma_sz, lma_sz / sizeof(LMA_IDX_ST));
#endif

    /*ddr*/
    r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ddrmem");
    if (!r) {
        pr_err(MSG_TAG"platform_get_resource_byname(ddrmem) fail\n");
        ret = ENOMEM;
        goto err1;
    }

    ddr_ram_phys = r->start;
    ddr_ram_size = resource_size(r);

    PR_INFO(MSG_TAG"ddr mem_base, of addr: %x, size: %x\n", ddr_ram_phys, ddr_ram_size);

    ddrmem_base = ioremap_nocache(ddr_ram_phys, ddr_ram_size);

    if (!ddrmem_base) {
        pr_err(MSG_TAG"ioremap_nocache() faild, of addr:%pa size: %pa\n",
                &ddr_ram_phys, &ddr_ram_size);
        ret = -ENODEV;
        goto err2;
    }

    /*iram*/
    r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iram");
    if (!r) {
        pr_err(MSG_TAG"platform_get_resource_byname(iram) fail\n");
        ret = ENOMEM;
        goto err1;
    }

    iram_phys = r->start;
    iram_size = resource_size(r);

    PR_INFO(MSG_TAG"iram_base, of addr: %x, size: %x\n", iram_phys, iram_size);

    /*dram*/
    r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dram");
    if (!r) {
        pr_err(MSG_TAG"platform_get_resource_byname(dram) fail\n");
        ret = ENOMEM;
        goto err1;
    }

    dram_phys = r->start;
    dram_size = resource_size(r);

    PR_INFO(MSG_TAG"dram_base, of addr: %x, size: %x\n", dram_phys, dram_size);

    /*topram*/
    r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "topram");
    if (!r) {
        pr_err(MSG_TAG"platform_get_resource_byname(topram) fail\n");
        ret = ENOMEM;
        goto err1;
    }

    topram_phys = r->start;
    topram_size = resource_size(r);

    PR_INFO(MSG_TAG"topram_base, of addr: %x, size: %x\n", topram_phys, topram_size);

    /*section to ram*/
    bin_base = adsp_bin_buf;
    lma_info_base = adsp_lma_bin_buf;

#ifdef ADSP_IMG_AUTH
    if (tz_verify) {
        LMA_IDX_ST *plma_info = (LMA_IDX_ST *)lma_info_base;
        int end = 0;

        session_id = tz_verifier_init();
        if (session_id < 0) {
            pr_err(MSG_TAG" verify get session fail:%d\n", session_id);
            ret = -EBUSY;
            goto err2;
        }

        for (m = 0; m < LMA_IDX_MAX; m++) {
            if (!plma_info[m].lma || !plma_info[m].size) {
                if (!end)
                    end = m;

                continue;
            }
            seg_count++;
        }

        arg_size = TZ_SHM_SIZE(seg_count);
        shm_id = tz_verifier_alloc_shm(arg_size);
        if (shm_id < 0) {
            pr_err(MSG_TAG" verify alloc shm fail:%d\n", shm_id);
            ret = -ENOMEM;
            goto err2;
        }

        tz_verifier_get_shm_info(shm_id, (char **)(&arg_info), &arg_size);

        sig_header = kmalloc(fw_sign->size, GFP_KERNEL);
        if (!sig_header) {
            pr_err(MSG_TAG" %s%s verify alloc metadata mem %d fail\n",
                   ADSP_BIN_PATH, adspbin, fw_sign->size);
            ret = -ENOMEM;
            goto err_tz;
        }
        memcpy(sig_header, fw_sign->data, fw_sign->size);

        memset(arg_info, 0, arg_size);
        arg_info->ss_prop.ssid = SS_COMMON;
        arg_info->ss_prop.para_num = 0;
        arg_info->header_paddr = virt_to_phys(sig_header);
        arg_info->header_size = fw_sign->size;
        arg_info->image_seg_num = seg_count;

        seg_index = 0;
        for (m = 0; m < LMA_IDX_MAX; m++) {

            if (!plma_info[m].lma || !plma_info[m].size)
                continue;

            arg_info->ism[seg_index].paddr = plma_info[m].lma;
            arg_info->ism[seg_index].sz = plma_info[m].size;
            seg_index++;
        }
    }
#endif

    for(m = 0; m < LMA_IDX_MAX; m++) {
        memset(&lma_info, 0, sizeof(LMA_IDX_ST));
        memcpy(&lma_info, lma_info_base + m * sizeof(LMA_IDX_ST), sizeof(LMA_IDX_ST));
        if(lma_info.lma == 0 || lma_info.size == 0)
            continue;

        size = lma_info.size;
        offs = bin_base + lma_info.offs;
        flag = lma_info.flag;

        if((lma_info.lma >= ddr_ram_phys) && (lma_info.lma < (ddr_ram_phys + ddr_ram_size))) {
            if(flag != LM_FLAG_BSS)
                memcpy_toio(ddrmem_base + (lma_info.lma - ddr_ram_phys), offs, size);
            else
                memset_io(ddrmem_base + (lma_info.lma - ddr_ram_phys), 0, size);
        } else if((lma_info.lma >= topram_phys) && (lma_info.lma < (topram_phys + topram_size))) {
            lma_v = ioremap(lma_info.lma, size);
            if(lma_v) {
                if(flag != LM_FLAG_BSS) {
                    memcpy_toio(lma_v, offs, size);
                } else
                    memset_io(lma_v, 0, size);

                iounmap(lma_v);
            } else
                ret = -ENOMEM;
        } else if(((lma_info.lma >= iram_phys) && (lma_info.lma < (iram_phys + iram_size)))
                  || ((lma_info.lma >= dram_phys) && (lma_info.lma < (dram_phys + dram_size)))) {
            if (!request_mem_region(lma_info.lma, size, "adsptcm")) {
                pr_err(MSG_TAG"request_mem_region, fail\n");
                ret = -ENOMEM;
            } else {
                lma_v = ioremap(lma_info.lma, size);  //(size + (0x100-1)) & (~(0x100-1))
                if(lma_v) {
                    if(flag != LM_FLAG_BSS) {
                        for(i = 0; i < size; ) {
                            v32 = readl(offs + i);
                            writel(v32, lma_v + i);

                            i += sizeof(u32);
                        }

                        j = size % sizeof(u32);
                        k = size / sizeof(u32) * sizeof(u32);

                        for(i = 0; i < j; i++) {
                            v8 = readb(offs + k + i);
                            writeb(v8, lma_v + k + i);
                        }

                    } else
                        memset_io(lma_v, 0, size);

                    iounmap(lma_v);
                } else
                    ret = -ENOMEM;

                release_mem_region(lma_info.lma, size);
            }

        } else {
            //nothing.
        }
    }

    mb();

    pr_info(MSG_TAG"adsp load, succ!\n");
    adsp_state = ADSP_STAT_LOADED;

#ifdef ADSP_IMG_AUTH
    if (tz_verify) {
        /* wmb() ensures copy completes prior to starting authentication. */
        wmb();
        ret = tz_verifier_invode_command(session_id, shm_id, TZCMD_AUTH_BOOT, arg_size);
        if (ret) {
            pr_err(MSG_TAG" auth failed! ret:%d\n", ret);
            goto err_tz;
        }
        pr_info(MSG_TAG" auth success!\n");
    }
#endif

    /* load adsp calibration parameter before adsp startup */
    audio_cal_load(AUDIO_CAL_EFFECT_DONOT_SEND);

    adsp_run();
    adsp_state = ADSP_STAT_RUNNING;
    pr_info(MSG_TAG"adsp start to run!\n");

#ifdef ADSP_SSR
    {
        int ret1 = 0;
        PR_INFO(MSG_TAG"prepare to set adsp online.\n");
        ret1 = subsystem_state_set("adsp", 1);
        if (!ret1) {
            PR_INFO(MSG_TAG"set adsp online ok.\n");
        } else {
            PR_INFO(MSG_TAG"set adsp online fail.\n");
        }
    }
#endif

    //finish:
#ifdef ADSP_IMG_AUTH
err_tz:
    kfree(sig_header);

    if (shm_id >= 0)
        tz_verifier_free_shm(shm_id);

    if (session_id >= 0)
        tz_verifier_deinit(session_id, 0);
#endif

err2:
    if(ddrmem_base)
        iounmap(ddrmem_base);

err1:
#if LOAD_PATH_BACKDOOR
    //nothing.
#else
    if(adsp_bin_buf)
        vfree(adsp_bin_buf);
    if(adsp_lma_bin_buf)
        vfree(adsp_lma_bin_buf);
#endif
#ifdef ADSP_IMG_AUTH
    if (fw_sign)
        release_firmware(fw_sign);
#endif

    return ret;
}

#if !defined(LOAD_ADSP_BY_AP_DIRECT)
static void adsp_load_work(struct work_struct *work)
{
    AdspLoadDrvData *privdata = container_of(work, AdspLoadDrvData, loader_work);

    adsp_load_ext(privdata->pdev);
}
#endif

int adsp_top_mbox_init(void)
{
    struct device_node *np_mb;

    /*topmailbox regbase*/
    np_mb = of_find_compatible_node(NULL, NULL, "jlq,top-mailbox-base");
    if (!np_mb) {
        pr_err(MSG_TAG"jlq,top-mailbox-base No found\n");
        return -ENODEV;
    }

    adsp_top_mbox_base = of_iomap(np_mb, 0);
    if (!adsp_top_mbox_base) {
        pr_err(MSG_TAG"jlq,top_mailbox_base is NULL\n");
        return -ENOMEM;
    }

    of_node_put(np_mb);

    /*enable mbox int*/
    writel(0x1, adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_EN);

    /*enable mbox src int*/
    //writel(xxx, adsp_top_mbox_base + TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN);

    return 0;
}

static int adsp_load_init(struct platform_device *pdev)
{
    int ret;
    AdspLoadDrvData *privdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);

    PR_INFO(MSG_TAG"adsp_load_init\n");

    /*init sysfs*/
    privdata->loader_kobj = kobject_create_and_add(ADSP_LOAD_SYSDIR, kernel_kobj);
    if (!privdata->loader_kobj) {
        pr_err(MSG_TAG"create adsp loader kobj fail\n");
        return -ENOMEM;
    }

    ret = sysfs_create_group(privdata->loader_kobj, &_attr_group);
    if(ret != 0) {
        pr_err(MSG_TAG"create adsp loader sysfs fail\n");
        kobject_put(privdata->loader_kobj);
        privdata->loader_kobj = NULL;
    }

#ifdef LOAD_ADSP_BY_AP_DIRECT
    PR_INFO(MSG_TAG"LOAD_ADSP_BY_AP_DIRECT(ext,5.4)\n");
    adsp_load_ext(pdev);
#else
    /*init work*/
    INIT_WORK(&privdata->loader_work, adsp_load_work);
#endif
    return ret;
}

static void adsp_load_uninit(struct platform_device *pdev)
{
    AdspLoadDrvData *privdata = (AdspLoadDrvData *)platform_get_drvdata(pdev);

    cancel_work_sync(&privdata->loader_work);

    if(privdata->loader_kobj) {
        sysfs_remove_group(privdata->loader_kobj, &_attr_group);
        kobject_put(privdata->loader_kobj);
        privdata->loader_kobj = NULL;
    }
}

static int adsp_load_probe(struct platform_device *pdev)
{
    int ret = 0;

    AdspLoadDrvData *pdrvdata;
    unsigned long freq;
    //struct device_node *np;

    PR_INFO(MSG_TAG"adsp_load_probe\n");

    drvdata = devm_kzalloc(&pdev->dev, sizeof(AdspLoadDrvData), GFP_KERNEL);
    if (!drvdata) {
        return -ENOMEM;
    }

    pdrvdata = drvdata;

    /*init drvdata*/
    drvdata->pdev = pdev;
    platform_set_drvdata(pdev, drvdata);

    ret = adsp_ctrl_regs_base_init(pdev);
    if(ret < 0)
        return ret;

#ifdef ADSP_REGU
    ret = adsp_regu_get(pdev);
    if(ret < 0)
        return -EPROBE_DEFER;
#endif

    /*dereset adsp*/
    adsp_state = ADSP_STAT_SHUTDOWN;
    ret = adsp_dereset();
    if(ret < 0)
        return ret;
    adsp_state = ADSP_STAT_POWERUP;

    /*adsp clk*/
    /*clk*/
    PR_INFO(MSG_TAG"will enable adsp_clk\n");
    pdrvdata->clk = clk_get(&pdev->dev, "adsp_clk");
    if (IS_ERR(pdrvdata->clk))
        pr_err(MSG_TAG"get adsp_clk, error\n");
    else {
        freq = clk_get_rate(pdrvdata->clk);
        PR_INFO(MSG_TAG"check adsp_clk, %ld\n", freq);
    }
    clk_prepare_enable(pdrvdata->clk);
    PR_INFO(MSG_TAG"clk_prepare_enable(adsp_clk) ok\n");

    /*bclk*/
    PR_INFO(MSG_TAG"will enable adsp_bclk\n");
    pdrvdata->bclk = clk_get(&pdev->dev, "adsp_bclk");
    if (IS_ERR(pdrvdata->bclk))
        pr_err(MSG_TAG"get adsp_bclk, error\n");
    /*else {
        freq = clk_get_rate(pdrvdata->bclk);
        pr_info(MSG_TAG"check adsp_bclk, %ld\n", freq);
    }
    clk_prepare_enable(pdrvdata->bclk);
    pr_info(MSG_TAG"clk_prepare_enable(adsp_bclk) ok\n");
    */

    /*pbclk*/
    PR_INFO(MSG_TAG"will enable adsp_pbclk\n");
    pdrvdata->pbclk = clk_get(&pdev->dev, "adsp_pbclk");
    if (IS_ERR(pdrvdata->pbclk))
        pr_err(MSG_TAG"get adsp_pbclk, error\n");
    else {
        freq = clk_get_rate(pdrvdata->pbclk);
        PR_INFO(MSG_TAG"check adsp_pbclk, %ld\n", freq);
    }
    clk_prepare_enable(pdrvdata->pbclk);
    PR_INFO(MSG_TAG"clk_prepare_enable(adsp_pbclk) ok\n");

    /*core clk rate, set PLLCOMM rate*/
    ret = of_property_read_u32(pdev->dev.of_node, "pll-comm-rate", &pdrvdata->pll_comm_rate);
    if (ret) {
        pr_err(MSG_TAG"read pll-comm-rate error\n");
    } else {
        PR_INFO(MSG_TAG" pll-comm-rate is %ld\n", pdrvdata->pll_comm_rate);

        if(pdrvdata->clk) {
            ret = clk_set_rate(pdrvdata->clk, pdrvdata->pll_comm_rate);  //PLL_COMM_HZ
            if(ret) {
                pr_err(MSG_TAG"Failed to set adsp clk to PLLCOMM rate\n");
            }
            freq = clk_get_rate(pdrvdata->clk);
            pr_info(MSG_TAG"check adsp_clk(after set PLLCOMM), %ld\n", freq);
        }

        if(pdrvdata->bclk) {
            ret = clk_set_rate(pdrvdata->bclk, BCLK_FREQ_133330K_HZ);
            if(ret) {
                pr_err(MSG_TAG"Failed to set adsp bclk to PLLCOMM rate\n");
            }
            clk_prepare_enable(pdrvdata->bclk);
            freq = clk_get_rate(pdrvdata->bclk);
            pr_info(MSG_TAG"check adsp_bclk, %ld\n", freq);
        }

    }
#if FOR_PLLI2S_DEBUG
    ret = of_property_read_u32(pdev->dev.of_node, "pll-i2s-rate", &pdrvdata->pll_i2s_rate);
    if (ret) {
        pr_err(MSG_TAG"read pll-i2s-rate error\n");
    } else {
        PR_INFO(MSG_TAG" pll-i2s-rate is %ld\n", pdrvdata->pll_i2s_rate);

        /*only for adsp verification now.*/
        if(pdrvdata->clk) {
            ret = clk_set_rate(pdrvdata->clk, pdrvdata->pll_i2s_rate);  //PLL_I2S_HZ
            if(ret) {
                pr_err(MSG_TAG"Failed to set adsp clk to PLL_I2S_HZ rate\n");
            }
            freq = clk_get_rate(pdrvdata->clk);
            pr_info(MSG_TAG"check adsp_clk(after set PLL_I2S_HZ), %ld\n", freq);
        }
    }
#endif

    adsp_top_mbox_init();

    /*mdm-state-notify-adsp-irq index*/
    if(of_property_read_u32(pdev->dev.of_node, "mdm-state-notify-adsp-irq",
                            &mdm_state_notify_adsp_irq_index)) {
        pr_err(MSG_TAG"read node mdm-state-notify-adsp-irq fail\n");
        return -ENODEV;
    }
    PR_INFO(MSG_TAG"mdm-state-notify-adsp-irq: %d \n", mdm_state_notify_adsp_irq_index);

    /*reset enable*/
    of_property_read_u32(pdev->dev.of_node, "reset-enable", &reset_enable);
    pr_info(MSG_TAG"reset_enable: %d \n", reset_enable);

#ifdef ADSP_LPM
    /*icc*/
    adsp_lpm_init(pdev);
    adsp_icc_set_bw(pdev, 0, 0);
#endif

#ifdef ADSP_OPP
    adsp_opp_init(pdev);
#endif

#ifdef ADSP_WDT
    adsp_wdt_register();
#endif

    /*init loader*/
    ret = adsp_load_init(pdev);
    if (ret < 0) {
        pr_err(MSG_TAG"adsp_load_init fail(%d).\n", ret);
        return ret;
    }

#ifdef ADSP_SSR
    subsys_notif_register_notifier(
        SUBSYS_ADSP_NAME, &adsp_load_service_nb);

    adsp_subsystem_get();

#ifdef ADSP_SSR_UEVENT_NOTIFY
    adsp_reset_uevent.ktype.release = adsp_release_uevent_data;
    adsp_init_uevent_data(&adsp_reset_uevent, "adsp_uevent");
#endif

#endif

#ifdef ADSP_MDM_NOTIFI
    mdm_state_notifi_register();
#endif

#ifdef ADSP_PANIC_NOTIFI
    panic_noti_register();
#endif

#ifdef ADSP_DUMP_DEV
    adsp_dump_dev_init();
#endif

    return ret;
}

static int adsp_load_remove(struct platform_device *pdev)
{
    PR_INFO("adsp_load_remove\n");

    adsp_load_uninit(pdev);

#ifdef ADSP_LPM
    adsp_lpm_uninit(pdev);
#endif

#ifdef ADSP_REGU
    adsp_regu_put(pdev);
#endif

#ifdef ADSP_OPP
    adsp_opp_uninit(pdev);
#endif

    devm_kfree(&pdev->dev, drvdata);
    drvdata = NULL;

#ifdef ADSP_SSR_UEVENT_NOTIFY
    adsp_destroy_uevent_data(&adsp_reset_uevent);
#endif

#ifdef ADSP_DUMP_DEV
    adsp_dump_dev_uninit();
#endif

    iounmap(adsp_top_mbox_base);

    return 0;
}

static void adsp_load_shutdown(struct platform_device *pdev)
{
    pr_crit(MSG_TAG"%s\n", __func__);
    adsp_wdt_clk_disable();
    adsp_stall();
}


static const struct of_device_id adsp_load_ids[] = {
    { .compatible = ADSP_LOAD_COMPATIBLE },
    {},
};

MODULE_DEVICE_TABLE(of, adsp_load_ids);

static struct platform_driver adsp_load_drv = {
    .probe = adsp_load_probe,
    .remove = adsp_load_remove,
    .shutdown = adsp_load_shutdown,
    .driver =
    {
        .name = "adsp-loader",
        .of_match_table = adsp_load_ids,
        .owner = THIS_MODULE,
    }
};

static int __init adsp_driver_init(void)
{
    int r = platform_driver_register(&adsp_load_drv);

    if (r < 0)
        pr_err("adsp_driver register failed %d", r);

    return r;
}

static void __exit adsp_driver_exit(void)
{
    platform_driver_unregister(&adsp_load_drv);
}

void adsp_gk_desc_clean(int flag)
{
    void __iomem *loca_adsp_state_base;
    unsigned int loca_state_gk_tx_desc_offset = 0x20;
    unsigned int loca_state_gk_rx_desc_offset = 0x24;

    unsigned int gk_tx_desc_addr_phy, gk_rx_desc_addr_phy;
    unsigned int adsp_ddr_no_cache_offset = 0x20000000;
    void __iomem *gk_tx_desc_addr, *gk_rx_desc_addr;

    loca_adsp_state_base = ioremap(LOCAL_ADSP_STATE_BASE, 0x80);
    if (loca_adsp_state_base) {
        pr_crit(MSG_TAG"%s\n", __func__);

        gk_tx_desc_addr_phy = readl(loca_adsp_state_base + loca_state_gk_tx_desc_offset) + adsp_ddr_no_cache_offset;
        gk_rx_desc_addr_phy = readl(loca_adsp_state_base + loca_state_gk_rx_desc_offset) + adsp_ddr_no_cache_offset;

        if((gk_tx_desc_addr_phy > JR510_SMEM_BASE_ADDR) &&
           (gk_tx_desc_addr_phy < (JR510_SMEM_BASE_ADDR + JR510_SMEM_SIZE))) {

            gk_tx_desc_addr = ioremap(gk_tx_desc_addr_phy, 8);
            if(gk_tx_desc_addr) {
                pr_crit(MSG_TAG"gk tx desc, txr/txw: 0x%x/0x%x\n",
                        readl(gk_tx_desc_addr + 0),
                        readl(gk_tx_desc_addr + 4));
                if(flag == GK_DESC_TX_CLEAN || flag == GK_DESC_ALL_CLEAN) {
                    //writel(0, gk_tx_desc_addr + 0);  /*read_ind, clean txw and rxr*/
                    writel(0, gk_tx_desc_addr + 4);
                    pr_crit(MSG_TAG"gk tx desc clean, txr/txw: 0x%x/0x%x\n",
                            readl(gk_tx_desc_addr + 0),
                            readl(gk_tx_desc_addr + 4));
                }
                iounmap(gk_tx_desc_addr);
            }
        }

        if((gk_rx_desc_addr_phy > JR510_SMEM_BASE_ADDR) &&
           (gk_rx_desc_addr_phy < (JR510_SMEM_BASE_ADDR + JR510_SMEM_SIZE))) {

            gk_rx_desc_addr = ioremap(gk_rx_desc_addr_phy, 8);
            if(gk_rx_desc_addr) {
                pr_crit(MSG_TAG"gk rx desc, rxr/rxw: 0x%x/0x%x\n",
                        readl(gk_rx_desc_addr + 0),
                        readl(gk_rx_desc_addr + 4));
                if(flag == GK_DESC_RX_CLEAN || flag == GK_DESC_ALL_CLEAN) {
                    writel(0, gk_rx_desc_addr + 0);
                    //writel(0, gk_rx_desc_addr + 4);
                    pr_crit(MSG_TAG"gk rx desc clean, rxr/rxw: 0x%x/0x%x\n",
                            readl(gk_rx_desc_addr + 0),
                            readl(gk_rx_desc_addr + 4));
                }
                iounmap(gk_rx_desc_addr);
            }
        }

        iounmap(loca_adsp_state_base);
    }
}

/*adsp subsys reset*/
void adsp_subsys_reset(void)
{
    struct platform_device *pdev;
    AdspLoadDrvData *pdrvdata = drvdata;
    int ret;
    unsigned long freq;

    if(!pdrvdata)
        return;
    pdev = pdrvdata->pdev;
    if(!pdev)
        return;

    pr_crit(MSG_TAG"adsp_subsys_reset, begin!\n");

    pr_info(MSG_TAG"calling bolero_pow_dn_interface\n");
    if(bolero_pow_dw_cb_get())
        (*(bolero_pow_dw_cb_get()))();

    /*apr bridge clean, first*/
    apr_tal_pd_clean();

    /*dereset adsp*/
    adsp_state = ADSP_STAT_SHUTDOWN;
    ret = adsp_rereset();
    if(ret < 0)
        return;
    adsp_state = ADSP_STAT_POWERUP;

    /*adsp clk*/
    pr_info(MSG_TAG"will enable adsp_clk\n");
    pdrvdata->clk = clk_get(&pdev->dev, "adsp_clk");
    if (IS_ERR(pdrvdata->clk))
        pr_err(MSG_TAG"get adsp_clk, error\n");
    else {
        freq = clk_get_rate(pdrvdata->clk);
        pr_info(MSG_TAG"check adsp_clk, %ld\n", freq);
    }
    //clk_prepare_enable(pdrvdata->clk);
    //pr_info(MSG_TAG"clk_prepare_enable(adsp_clk) ok\n");

    /*core clk rate, set PLLCOMM rate*/
    ret = of_property_read_u32(pdev->dev.of_node, "pll-comm-rate", &pdrvdata->pll_comm_rate);
    if (ret) {
        pr_err(MSG_TAG"read pll-comm-rate error\n");
    } else {
        PR_INFO(MSG_TAG" pll-comm-rate is %ld\n", pdrvdata->pll_comm_rate);

        if(pdrvdata->clk) {
            ret = clk_set_rate(pdrvdata->clk, pdrvdata->pll_comm_rate);
            if(ret) {
                pr_err(MSG_TAG"Failed to set adsp clk to PLLCOMM rate\n");
            }
            freq = clk_get_rate(pdrvdata->clk);
            pr_info(MSG_TAG"check adsp_clk(after set PLLCOMM), %ld\n", freq);
        }
    }

    /*notify submod rereset*/
    adsp_submod_notify(ADSP_STAT_RERESET, NULL);

#ifdef ADSP_WDT
    /*rereset wdt*/
    //adsp_wdt_rereset();
#endif

    /*adsp load*/
    adsp_load_ext(pdev);

    /*apr bridge clean, second*/
    apr_tal_pd_clean();

    pr_info(MSG_TAG"callinig bolero_pow_up_interface\n");
    if(bolero_pow_up_cb_get())
        (*(bolero_pow_up_cb_get()))();


    pr_crit(MSG_TAG"adsp_subsys_reset, end!\n");
}

unsigned int adsp_reset_enable(void)
{
    return reset_enable;
}

static bolero_pow_updw_CB bolero_pow_up_cb = NULL;
static bolero_pow_updw_CB bolero_pow_dw_cb = NULL;

void adsp_register_boleno_updw(bolero_pow_updw_CB upcb, bolero_pow_updw_CB dwcb)
{
    PR_INFO(MSG_TAG"%s!\n", __func__);

    bolero_pow_up_cb = upcb;
    bolero_pow_dw_cb = dwcb;
}
EXPORT_SYMBOL(adsp_register_boleno_updw);

bolero_pow_updw_CB bolero_pow_up_cb_get(void)
{
    return bolero_pow_up_cb;
}

bolero_pow_updw_CB bolero_pow_dw_cb_get(void)
{
    return bolero_pow_dw_cb;
}


module_init(adsp_driver_init);
module_exit(adsp_driver_exit);

#endif //LOAD_ADSP_BY_AP


MODULE_LICENSE("GPL");
MODULE_AUTHOR("JLQ Inc.");
MODULE_DESCRIPTION("JLQ JR510 adsp kernel driver");


