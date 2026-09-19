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
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/elf.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/kthread.h>
#include "adsp_local.h"


#define MSG_TAG	"[adspk][dump]"

#define RAMDUMP_NUM_DEVICES	256
#define RAMDUMP_NAME			"hfadsp_ramdump"
#define RAMDUMP_DEV_NAME_suffix	"hfadsp"

enum ramdump_status {
    RAMDUMP_NOMAL,
    RAMDUMP_DUMPING,
    RAMDUMP_DONE,
};

struct adsp_dump_dev_st {
    char name[256];
    enum ramdump_status status;
    void *dump_buff;
    size_t dump_size;
    struct completion ramdump_complete;
    struct mutex consumer_lock;
    struct cdev cdev;
    struct device *dev;
    wait_queue_head_t dump_wait_q;
};

static struct class *ramdump_class;
static dev_t ramdump_dev;
static DEFINE_MUTEX(rd_minor_mutex);
static DEFINE_IDA(rd_minor_id);
static bool ramdump_devnode_inited;
static struct adsp_dump_dev_st *g_rd_dev = NULL;

static int ramdump_open(struct inode *inode, struct file *filep)
{
    struct adsp_dump_dev_st *rd_dev = container_of(inode->i_cdev,
                                      struct adsp_dump_dev_st, cdev);

    pr_info(MSG_TAG"%s:\n", __func__);

    rd_dev->status = RAMDUMP_NOMAL;
    filep->private_data = rd_dev;
    return 0;
}

static int ramdump_release(struct inode *inode, struct file *filep)
{
    struct adsp_dump_dev_st *rd_dev = container_of(inode->i_cdev,
                                      struct adsp_dump_dev_st, cdev);

    rd_dev->status = RAMDUMP_DONE;
    return 0;
}

static unsigned int ramdump_poll(struct file *filep,
                                 struct poll_table_struct *wait)
{
    struct adsp_dump_dev_st *rd_dev = filep->private_data;
    unsigned int mask = 0;

    //pr_info(MSG_TAG"%s:\n", __func__);

    poll_wait(filep, &rd_dev->dump_wait_q, wait);
    if (rd_dev->status == RAMDUMP_DUMPING)
        mask |= (POLLIN | POLLRDNORM);

    return mask;
}

static ssize_t ramdump_read(struct file *filep, char __user *buf,
                            size_t count, loff_t *ppos)
{
    struct adsp_dump_dev_st *rd_dev = filep->private_data;

    void *dump_buff;
    size_t dump_size;
    size_t offset;
    size_t cnt;
    static int log_num = 0;
    int log_nums = 3;

    if(log_num < log_nums) {
        pr_crit(MSG_TAG"%s: count:%ul, pos: %ul\n", __func__, count, *ppos);
        log_num++;
    }

    dump_size = rd_dev->dump_size;
    dump_buff = rd_dev->dump_buff;

    if(dump_size == 0 || !dump_buff)
        return -1;

    if (*ppos >= dump_size) {
        rd_dev->status = RAMDUMP_DONE;
        *ppos = 0;
        log_num = 0;
        pr_crit(MSG_TAG"%s: RAMDUMP_DONE\n", __func__);
        return 0;
    }

    cnt = dump_size - *ppos;
    if (cnt > count)
        cnt = count;

    offset = *ppos;
    copy_to_user(buf, dump_buff + offset, cnt);
    *ppos = cnt + *ppos;

    //pr_crit(MSG_TAG"%s: cnt:%ul\n", __func__, cnt);

    return cnt;
}

static const struct file_operations ramdump_file_ops = {
    .open = ramdump_open,
    .release = ramdump_release,
    .read = ramdump_read,
    .poll = ramdump_poll
};

void adsp_dump_available(void *dump_buff, size_t dump_size)
{
    struct adsp_dump_dev_st *rd_dev = g_rd_dev;

    if (IS_ERR_OR_NULL(rd_dev))
        return;

    pr_crit(MSG_TAG"%s: size: 0x%x\n", __func__, dump_size);

    rd_dev->dump_buff = dump_buff;
    rd_dev->dump_size = dump_size;
    rd_dev->status = RAMDUMP_DUMPING;
    wake_up_interruptible(&rd_dev->dump_wait_q);
}
EXPORT_SYMBOL(adsp_dump_available);


static int ramdump_devnode_init(void)
{
    int ret;

    ramdump_class = class_create(THIS_MODULE, RAMDUMP_NAME);
    ret = alloc_chrdev_region(&ramdump_dev, 0, RAMDUMP_NUM_DEVICES,
                              RAMDUMP_NAME);
    if (ret < 0) {
        pr_warn("%s: unable to allocate major\n", __func__);
        return ret;
    }

    ramdump_devnode_inited = true;

    pr_info(MSG_TAG"%s: done\n", __func__);

    return 0;
}

static int ramdump_devnode_uninit(void)
{
    unregister_chrdev_region(ramdump_dev, 1);
    class_destroy(ramdump_class);
    ramdump_class = NULL;
    ramdump_devnode_inited = false;
    return 0;
}


void adsp_dump_dev_init(void)
{
    int ret, minor;
    struct adsp_dump_dev_st *rd_dev;

    PR_INFO(MSG_TAG"%s: entry\n", __func__);

    mutex_lock(&rd_minor_mutex);
    if (!ramdump_devnode_inited) {
        ret = ramdump_devnode_init();
        if (ret)
            return;
    }
    mutex_unlock(&rd_minor_mutex);

    rd_dev = kzalloc(sizeof(struct adsp_dump_dev_st), GFP_KERNEL);

    if (!rd_dev)
        return;

    /* get a minor number */
    minor = ida_simple_get(&rd_minor_id, 0, RAMDUMP_NUM_DEVICES,
                           GFP_KERNEL);
    if (minor < 0) {
        pr_err("No more minor numbers left! rc:%d\n", minor);
        ret = -ENODEV;
        goto fail_out_of_minors;
    }

    snprintf(rd_dev->name, ARRAY_SIZE(rd_dev->name), "ramdump_%s",
             RAMDUMP_DEV_NAME_suffix);

    init_waitqueue_head(&rd_dev->dump_wait_q);

    rd_dev->dev = device_create(ramdump_class, NULL,
                                MKDEV(MAJOR(ramdump_dev), minor),
                                rd_dev, rd_dev->name);
    if (IS_ERR(rd_dev->dev)) {
        ret = PTR_ERR(rd_dev->dev);
        pr_err("device_create failed for %s (%d)",
               RAMDUMP_DEV_NAME_suffix, ret);
        goto fail_return_minor;
    }

    cdev_init(&rd_dev->cdev, &ramdump_file_ops);

    ret = cdev_add(&rd_dev->cdev, MKDEV(MAJOR(ramdump_dev), minor), 1);
    if (ret < 0) {
        pr_err("cdev_add failed for %s (%d)",
               RAMDUMP_DEV_NAME_suffix, ret);
        goto fail_cdev_add;
    }

    g_rd_dev = rd_dev;

    PR_INFO(MSG_TAG"%s: done\n", __func__);

    return;

fail_cdev_add:
    device_unregister(rd_dev->dev);
fail_return_minor:
    ida_simple_remove(&rd_minor_id, minor);
fail_out_of_minors:
    kfree(rd_dev);
    return;
}

void adsp_dump_dev_uninit(void)
{
    struct adsp_dump_dev_st *rd_dev = g_rd_dev;
    int minor;

    if (IS_ERR_OR_NULL(rd_dev))
        return;

    minor = MINOR(rd_dev->cdev.dev);

    cdev_del(&rd_dev->cdev);
    device_del(rd_dev->dev);
    device_destroy(ramdump_class, MKDEV(MAJOR(ramdump_dev), minor));
    ida_simple_remove(&rd_minor_id, minor);
    ramdump_devnode_uninit();
    kfree(rd_dev);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JLQ Inc.");
MODULE_DESCRIPTION("JLQ JR510 adsp kernel driver");

