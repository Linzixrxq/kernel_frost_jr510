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
#include <linux/notifier.h>

#include "adsp_subsys.h"
#include "adsp_local.h"


/***debug info***/
#define MSG_TAG	"[adspk][noti]"


struct submod_notif_info {
    char name[64];
    struct list_head list;
};

static LIST_HEAD(submod_list);
static DEFINE_MUTEX(notif_lock);
static RAW_NOTIFIER_HEAD (submod_chain);


static struct submod_notif_info *_notif_find_submod(const char *submod_name)
{
    struct submod_notif_info *submod;

    //mutex_lock(&notif_lock);
    list_for_each_entry(submod, &submod_list, list)
    if (!strcmp(submod->name, submod_name)) {
        mutex_unlock(&notif_lock);
        return submod;
    }
    //mutex_unlock(&notif_lock);

    return NULL;
}

static void *_notif_add_submod(const char *submod_name)
{
    struct submod_notif_info *submod = NULL;

    if (!submod_name) {
        goto done;
    }

    mutex_lock(&notif_lock);

    submod = _notif_find_submod(submod_name);
    if (submod) {
        mutex_unlock(&notif_lock);
        goto done;
    }

    submod = kmalloc(sizeof(struct submod_notif_info), GFP_KERNEL);
    if (!submod) {
        mutex_unlock(&notif_lock);
        return NULL;
    }

    strlcpy(submod->name, submod_name, ARRAY_SIZE(submod->name));

    INIT_LIST_HEAD(&submod->list);

    list_add_tail(&submod->list, &submod_list);

    mutex_unlock(&notif_lock);

done:
    return submod;
}

static void *_notif_del_submod(const char *submod_name)
{
    struct submod_notif_info *submod = NULL;

    if (!submod_name)
        goto done;

    mutex_lock(&notif_lock);

    submod = _notif_find_submod(submod_name);
    if (!submod) {
        mutex_unlock(&notif_lock);
        goto done;
    }

    list_del(&submod->list);
    kfree(submod);

    mutex_unlock(&notif_lock);

done:
    return submod;
}


void *adsp_submod_notif_register(
    const char *submod_name, struct notifier_block *nb)
{
    int ret;
    struct submod_notif_info *submod = _notif_find_submod(submod_name);

    if (!submod) {
        submod = (struct submod_notif_info *)
                 _notif_add_submod(submod_name);

        if (!submod)
            return ERR_PTR(-EINVAL);
    } else {
        pr_err(MSG_TAG"%s, (%s) has been registered!\n", __func__, submod_name);
        return submod;
    }

    ret = raw_notifier_chain_register(&submod_chain, nb);

    if (ret < 0) {
        pr_err(MSG_TAG"%s, (%s)\n", __func__, submod_name);
        return ERR_PTR(ret);
    }

    pr_info(MSG_TAG"%s, (%s)\n", __func__, submod_name);
    return submod;
}
EXPORT_SYMBOL(adsp_submod_notif_register);


int adsp_submod_notif_unregister(
    const char *submod_name, struct notifier_block *nb)
{
    int ret;

    if (!submod_name || !nb) {
        return -EINVAL;
    }

    ret = raw_notifier_chain_unregister(&submod_chain, nb);

    _notif_del_submod(submod_name);

    return ret;
}
EXPORT_SYMBOL(adsp_submod_notif_unregister);


int adsp_submod_notify(unsigned long val, void *v)
{
    return raw_notifier_call_chain(&submod_chain, val, v);
}
EXPORT_SYMBOL(adsp_submod_notify);


static int t_submod1_cb(struct notifier_block *this, unsigned long event, void *ptr)
{
    PR_INFO(MSG_TAG"%s, event: %u\n", __func__, event);
    return NOTIFY_OK;
}

static int t_submod2_cb(struct notifier_block *this, unsigned long event, void *ptr)
{
    PR_INFO(MSG_TAG"%s, event: %u\n", __func__, event);
    return NOTIFY_OK;
}

static struct notifier_block t_submod1_notifier = {
    .notifier_call = t_submod1_cb,
};

static struct notifier_block t_submod2_notifier = {
    .notifier_call = t_submod2_cb,
};

void t_submod_noti(void)
{
    PR_INFO(MSG_TAG"%s\n", __func__);

    adsp_submod_notif_register("t_submod1", &t_submod1_notifier);
    adsp_submod_notif_register("t_submod2", &t_submod2_notifier);

    adsp_submod_notify(123, NULL);

    adsp_submod_notif_register("t_submod1", &t_submod1_notifier);
    adsp_submod_notif_register("t_submod2", &t_submod2_notifier);

    adsp_submod_notify(100, NULL);

    adsp_submod_notif_unregister("t_submod1", &t_submod1_notifier);
    adsp_submod_notif_unregister("t_submod2", &t_submod2_notifier);

    adsp_submod_notif_register("t_submod1", &t_submod1_notifier);
    adsp_submod_notif_register("t_submod2", &t_submod2_notifier);

    adsp_submod_notify(102, NULL);

    adsp_submod_notif_unregister("t_submod1", &t_submod1_notifier);
    adsp_submod_notif_unregister("t_submod2", &t_submod2_notifier);
}


