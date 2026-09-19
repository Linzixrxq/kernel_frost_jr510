// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2020, The Linux Foundation. All rights reserved.
 */
#define pr_fmt(fmt) "subsys-restart:%s:%d " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/suspend.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include "vdsp_subsystem_restart.h"
#include "vdsp-driver.h"

#include <linux/of_irq.h>
#include <linux/of.h>
#include <asm/current.h>
#include <linux/timer.h>
#include <linux/iopoll.h>

#define DISABLE_SSR 0x9889deed
/* If set to 0x9889deed, call to subsystem_restart_dev() returns immediately */
static uint disable_restart_work;
module_param(disable_restart_work, uint, 0644);

static int enable_debug;
module_param(enable_debug, int, 0644);

RAW_NOTIFIER_HEAD(ssr_notifier_list);
EXPORT_SYMBOL(ssr_notifier_list);

static const char * const subsys_states[] = {
	[SUBSYS_RUNNING] = "RUNNING",
	[SUBSYS_CLOSED] = "CLOSED",
	[SUBSYS_CRASHED] = "CRASHED",
	[SUBSYS_RESTARTING] = "RESTARTING",
};

static const char * const restart_levels[] = {
	[RESET_SOC] = "SYSTEM",
	[RESET_SUBSYS_COUPLED] = "RELATED",
};



static struct vdsp_subsys_device *to_subsys(struct device *d)
{
	return container_of(d, struct vdsp_subsys_device, dev);
}

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", to_subsys(dev)->desc->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	enum subsys_state state = to_subsys(dev)->track.state;

	return snprintf(buf, PAGE_SIZE, "%s\n", subsys_states[state]);
}
static DEVICE_ATTR_RO(state);

static ssize_t crash_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", to_subsys(dev)->crash_count);
}
static DEVICE_ATTR_RO(crash_count);

static ssize_t
restart_level_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int level = to_subsys(dev)->restart_level;

	return snprintf(buf, PAGE_SIZE, "%s\n", restart_levels[level]);
}

static ssize_t restart_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);
	const char *p;
	int i, orig_count = count;

	p = memchr(buf, '\n', count);
	if (p)
		count = p - buf;

	for (i = 0; i < ARRAY_SIZE(restart_levels); i++)
		if (!strncasecmp(buf, restart_levels[i], count)) {
			subsys->restart_level = i;
			return orig_count;
		}
	return -EPERM;
}
static DEVICE_ATTR_RW(restart_level);

static ssize_t force_ramdump_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);
	unsigned int val;
	int error;

	if (subsys->track.state != SUBSYS_RUNNING) {
		pr_err("device is not running\n");
		return count;
	}

	pr_err("!!! force ramdump\n");
	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;
	if (val > 0 && subsys != NULL && subsys->desc != NULL
		&& subsys->desc->trigger_ramdump != NULL)
		subsys->desc->trigger_ramdump(subsys->desc);
	else
		pr_err("don't support force ramdump\n");
	return count;
}
static DEVICE_ATTR_WO(force_ramdump);

static ssize_t system_debug_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);
	char p[6] = "set";

	if (!subsys->desc->system_debug)
		strlcpy(p, "reset", sizeof(p));

	return snprintf(buf, PAGE_SIZE, "%s\n", p);
}

static ssize_t system_debug_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);
	const char *p;
	int orig_count = count;

	p = memchr(buf, '\n', count);
	if (p)
		count = p - buf;

	if (!strncasecmp(buf, "set", count))
		subsys->desc->system_debug = true;
	else
		return -EPERM;
	return orig_count;
}
static DEVICE_ATTR_RW(system_debug);

static ssize_t ramdump_path_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", subsys->desc->ramdump_path);
}

static ssize_t ramdump_path_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);
	int orig_count = count;

	if (sizeof(subsys->desc->ramdump_path) > count) {
		memset(subsys->desc->ramdump_path, 0, sizeof(subsys->desc->ramdump_path));
		memcpy(subsys->desc->ramdump_path, buf, count);
	} else {
		return -EPERM;
	}
	return orig_count;
}
static DEVICE_ATTR_RW(ramdump_path);

static ssize_t reset_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);
	unsigned int val;
	int error;

	if (subsys->track.state == SUBSYS_CLOSED
		|| subsys->track.state == SUBSYS_RESTARTING) {
		pr_err("device is closed or restarting !\n");
		return count;
	}

	error = kstrtouint(buf, 10, &val);

	if (error)
		return error;

	if (val > 0) {
		subsys->track.state = SUBSYS_RESTARTING;
		subsys->desc->shutdown(subsys->desc, false);
		subsys->desc->powerup(subsys->desc);
		subsys->track.state = SUBSYS_RUNNING;
	}

	return count;
}
static DEVICE_ATTR_WO(reset);

static struct attribute *subsys_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_crash_count.attr,
	&dev_attr_restart_level.attr,
	&dev_attr_ramdump_path.attr,
	&dev_attr_system_debug.attr,
	&dev_attr_force_ramdump.attr,
	&dev_attr_reset.attr,
	NULL,
};
ATTRIBUTE_GROUPS(subsys);

struct bus_type vdsp_subsys_bus_type = {
	.name		= "vdsp_subsys",
	.dev_groups	= subsys_groups,
};

static DEFINE_IDA(subsys_ida);

static int enable_ramdumps;
module_param(enable_ramdumps, int, 0644);

struct workqueue_struct *vdsp_ssr_wq;
static struct class *char_class;

static LIST_HEAD(subsys_list);
static LIST_HEAD(ssr_order_list);
static DEFINE_MUTEX(soc_order_reg_lock);
static DEFINE_MUTEX(subsys_list_lock);
static DEFINE_MUTEX(char_device_lock);
static DEFINE_MUTEX(ssr_order_mutex);

static int max_restarts;
module_param(max_restarts, int, 0644);

static long max_history_time = 3600;
module_param(max_history_time, long, 0644);

static int subsys_device_open(struct inode *inode, struct file *file)
{
	struct vdsp_subsys_device *subsys_dev = 0;

	mutex_lock(&subsys_list_lock);
	mutex_unlock(&subsys_list_lock);

	if (!subsys_dev) {
		pr_err("subsys_dev is null\n");
		return -EINVAL;
	}

	return 0;
}

static int subsys_device_close(struct inode *inode, struct file *file)
{
	struct vdsp_subsys_device *device, *subsys_dev = 0;

	mutex_lock(&subsys_list_lock);
	list_for_each_entry(device, &subsys_list, list)
		if (MINOR(device->dev_no) == iminor(inode))
			subsys_dev = device;
	mutex_unlock(&subsys_list_lock);

	if (!subsys_dev)
		return -EINVAL;

	//subsystem_put(subsys_dev);
	return 0;
}

static const struct file_operations subsys_device_fops = {
		.owner = THIS_MODULE,
		.open = subsys_device_open,
		.release = subsys_device_close,
};

static void subsys_device_release(struct device *dev)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);

	wakeup_source_unregister(subsys->ssr_wlock);
	mutex_destroy(&subsys->track.lock);
	ida_simple_remove(&subsys_ida, subsys->id);
	kfree(subsys);
}

static int subsys_char_device_add(struct vdsp_subsys_device *subsys_dev)
{
	int ret = 0;
	static int major, minor;
	dev_t dev_no;

	mutex_lock(&char_device_lock);
	if (!major) {
		ret = alloc_chrdev_region(&dev_no, 0, 4, "subsys");
		if (ret < 0) {
			pr_err("Failed to alloc subsys_dev region, err %d\n",
					ret);
			goto fail;
		}
		major = MAJOR(dev_no);
		minor = MINOR(dev_no);
	} else
		dev_no = MKDEV(major, minor);

	if (!device_create(char_class, subsys_dev->desc->dev, dev_no,
			NULL, "subsys_%s", subsys_dev->desc->name)) {
		pr_err("Failed to create subsys_%s device\n",
				subsys_dev->desc->name);
		goto fail_unregister_cdev_region;
	}

	cdev_init(&subsys_dev->char_dev, &subsys_device_fops);
	subsys_dev->char_dev.owner = THIS_MODULE;
	ret = cdev_add(&subsys_dev->char_dev, dev_no, 1);
	if (ret < 0)
		goto fail_destroy_device;

	subsys_dev->dev_no = dev_no;
	minor++;
	mutex_unlock(&char_device_lock);

	return 0;

fail_destroy_device:
	device_destroy(char_class, dev_no);
fail_unregister_cdev_region:
	unregister_chrdev_region(dev_no, 1);
fail:
	mutex_unlock(&char_device_lock);
	return ret;
}

static void subsys_char_device_remove(struct vdsp_subsys_device *subsys_dev)
{
	cdev_del(&subsys_dev->char_dev);
	device_destroy(char_class, subsys_dev->dev_no);
	unregister_chrdev_region(subsys_dev->dev_no, 1);
}

size_t vdsp_subsys_do_ramdump(struct vdsp_ramdump_device *dev,
			char __user *buf, size_t count, loff_t *ppos)
{
	struct vdsp_subsys_desc *desc = dev->desc;
	struct vdsp_ssr_info *ssr_info = container_of(desc, struct vdsp_ssr_info, subsys_desc);
	struct vdsp_subsys_device *subsys = ssr_info->subsys_dev;

	if (subsys->crash_count < 1)
		return 0;

	return desc->ramdump(desc, buf, count, ppos);
}

static irqreturn_t subsys_wdog_irq_handler(int irq, void *dev_id)
{
	struct vdsp_subsys_device *subsys = (struct vdsp_subsys_device *)dev_id;

	if (subsys->track.state == SUBSYS_CLOSED
		|| subsys->track.state == SUBSYS_CRASHED)
		return IRQ_HANDLED;

	subsys->track.state = SUBSYS_CRASHED;
	subsys->crash_count++;

	if (subsys->desc->is_support_ramdump == 1)
		ramdump_happened(subsys->desc->dump_dev);

	raw_notifier_call_chain(&ssr_notifier_list, SSR_EVENT_PANIC, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t subsys_panic_irq_handler(int irq, void *dev_id)
{
	struct vdsp_subsys_device *subsys = (struct vdsp_subsys_device *)dev_id;

	if (subsys->track.state == SUBSYS_CLOSED
		|| subsys->track.state == SUBSYS_CRASHED)
		return IRQ_HANDLED;

	subsys->track.state = SUBSYS_CRASHED;
	subsys->crash_count++;

	if (subsys->desc->is_support_ramdump == 1)
		ramdump_happened(subsys->desc->dump_dev);

	raw_notifier_call_chain(&ssr_notifier_list, SSR_EVENT_PANIC, NULL);

	return IRQ_HANDLED;
}

void vdsp_subsys_set_state(struct vdsp_subsys_device *subsys,
							enum subsys_state state)
{
	if (IS_ERR_OR_NULL(subsys))
		return;

	subsys->track.state = state;
}

struct vdsp_subsys_device *vdsp_subsys_register(struct vdsp_subsys_desc *desc)
{
	struct vdsp_subsys_device *subsys;
	int ret;

	subsys = kzalloc(sizeof(*subsys), GFP_KERNEL);
	if (!subsys)
		return ERR_PTR(-ENOMEM);

	subsys->desc = desc;
	subsys->owner = desc->owner;
	subsys->dev.parent = desc->dev;
	subsys->dev.bus = &vdsp_subsys_bus_type;
	subsys->dev.release = subsys_device_release;

	snprintf(subsys->wlname, sizeof(subsys->wlname), "ssr(%s)", desc->name);

	subsys->ssr_wlock =
		wakeup_source_register(subsys->dev.parent, subsys->wlname);
	if (!subsys->ssr_wlock) {
		kfree(subsys);
		return ERR_PTR(-ENOMEM);
	}

	subsys->id = ida_simple_get(&subsys_ida, 0, 0, GFP_KERNEL);
	if (subsys->id < 0) {
		wakeup_source_unregister(subsys->ssr_wlock);
		ret = subsys->id;
		kfree(subsys);
		return ERR_PTR(ret);
	}

	dev_set_name(&subsys->dev, "%s", subsys->desc->name/*subsys->id*/);

	ret = devm_request_irq(desc->dev, desc->panic_irq,
							subsys_panic_irq_handler,
					IRQF_TRIGGER_HIGH, desc->name, subsys);
	if (ret) {
		pr_err("request panic_irq failed ret:%d\n", ret);
		ret = -ENXIO;
		goto err_request_irq;
	}

	ret = devm_request_irq(desc->dev, desc->wdog_bite_irq,
							subsys_wdog_irq_handler,
					IRQF_TRIGGER_HIGH, desc->name, subsys);

	if (ret) {
		pr_err("request wdog_bite_irq failed ret:%d\n", ret);
		ret = -ENXIO;
		goto err_request_irq;
	}

	mutex_init(&subsys->track.lock);

	ret = device_register(&subsys->dev);
	if (ret) {
		put_device(&subsys->dev);
		return ERR_PTR(ret);
	}

	ret = subsys_char_device_add(subsys);
	if (ret)
		goto err_register;

	if (desc->is_support_ramdump > 0) {
		desc->dump_dev = vdsp_create_ramdump_device(desc->name,
						desc->dev);
		if (!desc->dump_dev)
			goto err_register;

		desc->dump_dev->desc = desc;
	}

	vdsp_subsys_set_state(subsys, SUBSYS_CLOSED);
	mutex_lock(&subsys_list_lock);
	INIT_LIST_HEAD(&subsys->list);
	list_add_tail(&subsys->list, &subsys_list);
	mutex_unlock(&subsys_list_lock);

	return subsys;

err_register:
	subsys_char_device_remove(subsys);
	device_unregister(&subsys->dev);
err_request_irq:
	return ERR_PTR(ret);
}

void vdsp_subsys_unregister(struct vdsp_subsys_device *subsys)
{
	struct vdsp_subsys_device *subsys_dev, *tmp;

	if (IS_ERR_OR_NULL(subsys))
		return;

	if (get_device(&subsys->dev)) {
		if (subsys->desc->is_support_ramdump > 0)
			vdsp_destroy_ramdump_device(subsys->desc->dump_dev);

		mutex_lock(&subsys_list_lock);
		list_for_each_entry_safe(subsys_dev, tmp, &subsys_list, list)
			if (subsys_dev == subsys)
				list_del(&subsys->list);
		mutex_unlock(&subsys_list_lock);

		mutex_lock(&subsys->track.lock);
		WARN_ON(subsys->count);
		device_unregister(&subsys->dev);
		mutex_unlock(&subsys->track.lock);
		subsys_char_device_remove(subsys);
		put_device(&subsys->dev);
	}
}

static int subsys_panic(struct device *dev, void *data)
{
	struct vdsp_subsys_device *subsys = to_subsys(dev);

	if (subsys->desc->crash_shutdown && subsys->track.state == SUBSYS_RUNNING)
		subsys->desc->crash_shutdown(subsys->desc);
	return 0;
}

static int ssr_panic_handler(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	bus_for_each_dev(&vdsp_subsys_bus_type, NULL, NULL, subsys_panic);
	return NOTIFY_DONE;
}

static struct notifier_block panic_nb = {
	.notifier_call  = ssr_panic_handler,
};

static int __init vdsp_subsys_restart_init(void)
{
	int ret;

	vdsp_ssr_wq = alloc_workqueue("vdsp_ssr_wq",
		WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 0);
	WARN_ON(!vdsp_ssr_wq);

	ret = bus_register(&vdsp_subsys_bus_type);
	if (ret)
		goto err_bus;

	char_class = class_create(THIS_MODULE, "vdsp_subsys");
	if (IS_ERR(char_class)) {
		ret = -ENOMEM;
		pr_err("Failed to create subsys_dev class\n");
		goto err_class;
	}

	ret = atomic_notifier_chain_register(&panic_notifier_list,
			&panic_nb);
	if (ret)
		goto err_soc;
	return 0;

err_soc:
	class_destroy(char_class);
err_class:
	bus_unregister(&vdsp_subsys_bus_type);
err_bus:
	destroy_workqueue(vdsp_ssr_wq);
	return ret;
}
module_init(vdsp_subsys_restart_init);

MODULE_DESCRIPTION("vdsp Subsystem Restart Driver");
MODULE_LICENSE("GPL v2");
