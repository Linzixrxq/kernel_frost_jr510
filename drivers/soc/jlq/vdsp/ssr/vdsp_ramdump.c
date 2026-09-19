// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2019, The Linux Foundation. All rights reserved.
 */
#define pr_fmt(fmt) "subsys-restart:%s:%d " fmt, __func__, __LINE__

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
#include "vdsp_ramdump.h"
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include "vdsp_subsystem_restart.h"
#include <linux/kthread.h>

#define RAMDUMP_NUM_DEVICES	256
#define RAMDUMP_NAME		"vdsp_ramdump"

static struct class *ramdump_class;
static dev_t ramdump_dev;
static DEFINE_MUTEX(rd_minor_mutex);
static DEFINE_IDA(rd_minor_id);
static bool ramdump_devnode_inited;

static int ramdump_open(struct inode *inode, struct file *filep)
{
	struct vdsp_ramdump_device *rd_dev = container_of(inode->i_cdev,
					struct vdsp_ramdump_device, cdev);

	rd_dev->status = RAMDUMP_NOMAL;
	filep->private_data = rd_dev;
	return 0;
}

static int ramdump_release(struct inode *inode, struct file *filep)
{
	return 0;
}

void ramdump_happened(struct vdsp_ramdump_device *dev)
{
	wake_up_interruptible(&dev->dump_wait_q);
	dev->status = RAMDUMP_DUMPING;
}

static unsigned int ramdump_poll(struct file *filep,
					struct poll_table_struct *wait)
{
	struct vdsp_ramdump_device *rd_dev = filep->private_data;
	unsigned int mask = 0;

	poll_wait(filep, &rd_dev->dump_wait_q, wait);
	if (rd_dev->status == RAMDUMP_DUMPING)
		mask |= (POLLIN | POLLRDNORM);

	return mask;
}

static ssize_t ramdump_read(struct file *filep, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct vdsp_ramdump_device *rd_dev = filep->private_data;
	ssize_t cnt = 0;

	cnt = vdsp_subsys_do_ramdump(rd_dev, buf, count, ppos);
	rd_dev->status = RAMDUMP_DONE;

	return cnt;
}

static const struct file_operations ramdump_file_ops = {
	.open = ramdump_open,
	.release = ramdump_release,
	.read = ramdump_read,
	.poll = ramdump_poll
};

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

#ifdef DUMP_IN_KERNEL
static int ramdump_process_function(void *data)
{
	struct vdsp_ramdump_device *rd_dev = (struct vdsp_ramdump_device *)data;

	while (1) {
		wait_event_interruptible(rd_dev->ramdump_irq_wait,
			(atomic_read(&rd_dev->ramdump_irq_count)));
		atomic_dec(&rd_dev->ramdump_irq_count);
		if (rd_dev != NULL)
			vdsp_subsys_do_ramdump(rd_dev);
	}
	return 0;
}
#endif

void *vdsp_create_ramdump_device(const char *dev_name, struct device *parent)
{
	int ret, minor;
	struct vdsp_ramdump_device *rd_dev;

	if (!dev_name) {
		pr_err("%s: Invalid device name.\n", __func__);
		return NULL;
	}

	mutex_lock(&rd_minor_mutex);
	if (!ramdump_devnode_inited) {
		ret = ramdump_devnode_init();
		if (ret)
			return ERR_PTR(ret);
	}
	mutex_unlock(&rd_minor_mutex);

	rd_dev = kzalloc(sizeof(struct vdsp_ramdump_device), GFP_KERNEL);

	if (!rd_dev)
		return NULL;

	/* get a minor number */
	minor = ida_simple_get(&rd_minor_id, 0, RAMDUMP_NUM_DEVICES,
			GFP_KERNEL);
	if (minor < 0) {
		pr_err("No more minor numbers left! rc:%d\n", minor);
		ret = -ENODEV;
		goto fail_out_of_minors;
	}

	snprintf(rd_dev->name, ARRAY_SIZE(rd_dev->name), "ramdump_%s",
		 dev_name);

	init_waitqueue_head(&rd_dev->dump_wait_q);

	rd_dev->dev = device_create(ramdump_class, parent,
				    MKDEV(MAJOR(ramdump_dev), minor),
				   rd_dev, rd_dev->name);
	if (IS_ERR(rd_dev->dev)) {
		ret = PTR_ERR(rd_dev->dev);
		pr_err("device_create failed for %s (%d)",
				dev_name, ret);
		goto fail_return_minor;
	}

	cdev_init(&rd_dev->cdev, &ramdump_file_ops);

	ret = cdev_add(&rd_dev->cdev, MKDEV(MAJOR(ramdump_dev), minor), 1);
	if (ret < 0) {
		pr_err("cdev_add failed for %s (%d)",
				dev_name, ret);
		goto fail_cdev_add;
	}

	return (void *)rd_dev;

fail_cdev_add:
	device_unregister(rd_dev->dev);
fail_return_minor:
	ida_simple_remove(&rd_minor_id, minor);
fail_out_of_minors:
	kfree(rd_dev);
	return ERR_PTR(ret);
}

void vdsp_destroy_ramdump_device(void *dev)
{
	struct vdsp_ramdump_device *rd_dev = dev;
	int minor = MINOR(rd_dev->cdev.dev);

	if (IS_ERR_OR_NULL(rd_dev))
		return;

	cdev_del(&rd_dev->cdev);
	device_del(rd_dev->dev);
	device_destroy(ramdump_class, MKDEV(MAJOR(ramdump_dev), minor));
	ida_simple_remove(&rd_minor_id, minor);
	ramdump_devnode_uninit();
	kfree(rd_dev);
}

