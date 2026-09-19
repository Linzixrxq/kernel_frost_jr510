// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/vdsp-ioctl.h>

#include "vdsp-log.h"
#include "vdsp-log-intf.h"
#include "vdsp-runtime-type.h"

#undef VDBG
#define VDBG(fmt, args...) pr_debug(fmt, ##args)

//#define LOG_OUTPUT_TO_FILE
//#define LOG_OUTPUT_IN_KERNEL

BLOCKING_NOTIFIER_HEAD(vdsp_log_notify_list);
EXPORT_SYMBOL(vdsp_log_notify_list);

static int g_major;
static struct vdsp_log_drvdata *s_drvdata;

static inline u32 vdsp_read32(u64 addr)
{
	return readl((void *)addr);
}

static inline void vdsp_write32(u64 addr, u32 v)
{
	writel(v, (void *)addr);
}

static bool vdsp_get_status(void)
{
	struct vdsp_log_drvdata *drvdata = s_drvdata;

	if (drvdata == NULL || drvdata->irq_base == NULL)
		return false;

	if (vdsp_read32((u64)drvdata->irq_base + 4) == 1)
		return true;
	else
		return false;
}

static void vdsp_log_irq_enable(int enable)
{
	struct vdsp_log_drvdata *drvdata = s_drvdata;

	mutex_lock(&drvdata->mutex);
	if (enable == 1) {
		if (drvdata->is_irq_enable == false) {
			enable_irq(drvdata->log_irq_id);
			drvdata->is_irq_enable = true;
		}
	} else {
		if (drvdata->is_irq_enable == true) {
			disable_irq(drvdata->log_irq_id);
			drvdata->is_irq_enable = false;
		}
	}
	mutex_unlock(&drvdata->mutex);
}
#ifdef LOG_OUTPUT_IN_KERNEL
char tmp[VDSP_LOG_MAX_LEN];
static int jlq_log_default_process(void *data)
{
	char *log_string;
	struct vdsp_log_drvdata *drvdata = (struct vdsp_log_drvdata *)data;
	struct log_handle_t *log_handle = NULL;
	struct log_buf_head_info_t *buf_head;
#ifdef LOG_OUTPUT_TO_FILE
	char file_name[128];
#endif

	pr_info("vdsp log thread start run!.\n");
	if (!drvdata || !drvdata->log_handle) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}
	log_handle = drvdata->log_handle;

	buf_head = log_handle->buf_head;
	if (!buf_head) {
		pr_err("Buf_head is NULL.\n");
		return -EINVAL;
	}

	for (;;) {
		wait_event_interruptible(log_handle->log_irq_wait,
				(atomic_read(&log_handle->log_irq_count) ||
				kthread_should_stop()));
		if (kthread_should_stop()) {
			pr_info("exit =============.\n");
			break;
		}
		atomic_dec(&log_handle->log_irq_count);

		while (buf_head->read_log_id != buf_head->write_log_id) {
			log_string = (char *)(buf_head->log_pool_cpu_addr +
				(buf_head->read_log_id * log_handle->common_params.fifo_width));
#ifndef LOG_OUTPUT_TO_FILE
			memcpy(tmp, log_string, log_handle->common_params.fifo_width);
			//printk("%s", tmp);
			pr_err("%s", tmp);
#else
			/* output log to file*/
			if (IS_ERR_OR_NULL(log_handle->file)) {
				pr_err("file(%d) is null.\n", log_handle->file_index);
				log_handle->file_index++;
				memset(file_name, 0, 100);
				sprintf(file_name, "/data/log/%s_%d.log",
					log_handle->log_name, log_handle->file_index);
				log_handle->file = filp_open(file_name,
								O_RDWR | O_CREAT | O_TRUNC, 0600);
				if (IS_ERR_OR_NULL(log_handle->file)) {
					pr_err("filp open error.\n");
					log_handle->file = NULL;
					break;
				}
				log_handle->pos = 0;
			} else if (log_handle->pos > LOG_FILE_MAX_SIZE) {
				if (IS_ERR_OR_NULL(log_handle->file)) {
					pr_err("file ptr is error.\n");
					break;
				}
				filp_close(log_handle->file, NULL);
				log_handle->file_index++;
				memset(file_name, 0, 128);
				sprintf(file_name, "/data/log/%s_%d.log",
					log_handle->log_name, log_handle->file_index);
				log_handle->file = filp_open(file_name,
								O_RDWR | O_CREAT | O_TRUNC, 0600);
				if (IS_ERR_OR_NULL(log_handle->file)) {
					pr_err("%s: filp open error.\n", __func__);
					log_handle->file = NULL;
					break;
				}
				log_handle->pos = 0;
			}
			kernel_write(log_handle->file, (void *)tmp, strlen(tmp),
				&log_handle->pos);
#endif
			spin_lock(&log_handle->log_lock);
			buf_head->read_log_id++;

			if (buf_head->read_log_id >= log_handle->common_params.fifo_depth)
				buf_head->read_log_id = 0;

			spin_unlock(&log_handle->log_lock);
		}
	}

	pr_info("vdsp log thread exit.\n");
	return 0;
}

static void jlq_log_irq_default_handler(struct vdsp_log_drvdata *drvdata)
{
	struct log_handle_t *log_handle = NULL;

	if (!drvdata || !drvdata->log_handle) {
		pr_err("drvdata or log_handle are NULL.\n");
		return;
	}

	log_handle = drvdata->log_handle;
	atomic_inc(&log_handle->log_irq_count);
	wake_up(&log_handle->log_irq_wait);
}

static int jlq_log_default_init(struct vdsp_log_drvdata *drvdata)
{
	struct log_handle_t *log_handle = NULL;

	if (!drvdata || !drvdata->log_handle)
		return -EINVAL;

	log_handle = drvdata->log_handle;
	//init_waitqueue_head(&log_handle->log_irq_wait);
	atomic_set(&log_handle->log_irq_count, 0);
	if (log_handle->log_thread !=  NULL)
		return 0;

	log_handle->log_thread =
		kthread_run(jlq_log_default_process,
				drvdata, "%s", log_handle->log_name);

	if (IS_ERR(log_handle->log_thread)) {
		pr_err("Thread Creation failed\n");
		log_handle->log_thread = NULL;
		return 0;
	}
	return 0;
}

static int jlq_log_default_destroy(struct vdsp_log_drvdata *drvdata)
{
	struct log_handle_t *log_handle = NULL;

	VDBG("Enter.\n");
	if (!drvdata || !drvdata->log_handle)
		return -EINVAL;

	log_handle = drvdata->log_handle;
	if (log_handle->log_thread)
		kthread_stop(log_handle->log_thread);
	log_handle->log_thread = NULL;

	VDBG("Exit.\n");
	return 0;
}
#endif

static ssize_t vdsp_log_level_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct vdsp_log_drvdata *drvdata = s_drvdata;
	struct log_init_params *log_params;

	if (!drvdata || !drvdata->init_params)
		return -EINVAL;

	log_params = drvdata->init_params;

	return sprintf(buf, "vdsp log enable:%d level:%d mode:%d modules:%d\n",
			log_params->log_enable,
			log_params->log_level,
			log_params->log_output_mode,
			log_params->log_output_modules);
}

static ssize_t vdsp_log_level_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int val;
	int rc;
	struct vdsp_runtime_request_t runtime_rq;
	struct vdsp_runtime_response_t *response;
	struct vdsp_request rq;
	struct vdsp_log_drvdata *drvdata = s_drvdata;
	struct log_init_params *log_params;


	if (!drvdata || !drvdata->log_handle || !drvdata->init_params)
		return -EINVAL;

	rc = kstrtouint(buf, 10, &val);
	if (rc)
		return rc;

	if (val < OUTPUT_LEVEL_DISABLE || val >= OUTPUT_LEVEL_MAX) {
		pr_err("%d is a invalid value\n", val);
		return -EINVAL;
	}

	if (sizeof(runtime_rq) > VDSP_CMD_INLINE_DATA_SIZE) {
		pr_err("parameter bytes are overflow.\n");
		return -EINVAL;
	}

	log_params = drvdata->init_params;
	if (vdsp_get_status() != VDSP_POWER_ON) {
		log_params->log_level = val & 0xFF;
		return count;
	}

	memset(&runtime_rq, 0, sizeof(runtime_rq));
	runtime_rq.type = VDSP_RUNTIME_SET_LOG;
	runtime_rq.log_payload.set_log_enable =
				log_params->log_enable;
	runtime_rq.log_payload.output_mode =
				log_params->log_output_mode;
	runtime_rq.log_payload.output_modules =
				log_params->log_output_modules;
	runtime_rq.log_payload.output_levels = val & 0xFF;
	runtime_rq.log_payload.set_common_params = 0;

	memset(&rq, 0, sizeof(rq));
	memcpy(rq.nsid, RUNTIME_NSID, VDSP_CMD_NAMESPACE_ID_SIZE);
	memcpy(rq.in_data, &runtime_rq, sizeof(runtime_rq));
	rq.ioctl_queue.in_data_size = sizeof(runtime_rq);
	rq.ioctl_queue.out_data_size = sizeof(struct vdsp_runtime_response_t);
	rq.ioctl_queue.flags = VDSP_QUEUE_FLAG_NSID;
/*
	rc = vdsp_kernel_commit_sync(&rq);
*/
	rc = blocking_notifier_call_chain(&vdsp_log_notify_list, LOG_EVENT_SETTING, &rq);
	if (rc == NOTIFY_BAD) {
		pr_err("submit command to vdsp failed.\n");
		return -EINVAL;
	}

	response = (struct vdsp_runtime_response_t *)rq.out_data;
	if (response->result) {
		pr_err("set log parameters failed.\n");
		return -EINVAL;
	}

	log_params->log_level =
				runtime_rq.log_payload.output_levels;

	return count;
}

static struct kobj_attribute vdsp_log_level_attribute =
	__ATTR(vdsp_log_level, 0644, vdsp_log_level_show, vdsp_log_level_store);

static ssize_t vdsp_log_module_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct vdsp_log_drvdata *drvdata = s_drvdata;
	struct log_init_params *log_params;

	if (!drvdata || !drvdata->init_params)
		return -EINVAL;

	log_params = drvdata->init_params;

	return sprintf(buf, "log modules:%d\n",
			log_params->log_output_modules);
}

static ssize_t vdsp_log_module_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int val;
	int rc;
	struct vdsp_runtime_request_t runtime_rq;
	struct vdsp_runtime_response_t *response;
	struct vdsp_request rq;
	struct vdsp_log_drvdata *drvdata = s_drvdata;
	struct log_init_params *log_params;

	if (!drvdata || !drvdata->log_handle || !drvdata->init_params)
		return -EINVAL;

	log_params = drvdata->init_params;
	rc = kstrtouint(buf, 10, &val);
	if (rc)
		return rc;

	if (val <= OUTPUT_MODULE_INVALID || val >= OUTPUT_MODULE_MAX) {
		pr_err("%d is a invalid value\n", val);
		return -EINVAL;
	}

	if (sizeof(runtime_rq) > VDSP_CMD_INLINE_DATA_SIZE) {
		pr_err("parameter bytes are overflow.\n");
		return -EINVAL;
	}

	if (vdsp_get_status() != VDSP_POWER_ON) {
		log_params->log_output_modules = val & 0xFF;
		return count;
	}

	memset(&runtime_rq, 0, sizeof(runtime_rq));
	runtime_rq.type = VDSP_RUNTIME_SET_LOG;
	runtime_rq.log_payload.set_log_enable =
				log_params->log_enable;
	runtime_rq.log_payload.output_mode =
				log_params->log_output_mode;
	runtime_rq.log_payload.output_modules = val & 0xFF;
	runtime_rq.log_payload.output_levels =
				log_params->log_level;
	runtime_rq.log_payload.set_common_params = 0;

	memset(&rq, 0, sizeof(rq));
	memcpy(rq.nsid, RUNTIME_NSID, VDSP_CMD_NAMESPACE_ID_SIZE);
	memcpy(rq.in_data, &runtime_rq, sizeof(runtime_rq));
	rq.ioctl_queue.in_data_size = sizeof(runtime_rq);
	rq.ioctl_queue.out_data_size = sizeof(struct vdsp_runtime_response_t);
	rq.ioctl_queue.flags = VDSP_QUEUE_FLAG_NSID;
/*
	rc = vdsp_kernel_commit_sync(&rq);
*/
	rc = blocking_notifier_call_chain(&vdsp_log_notify_list, LOG_EVENT_SETTING, &rq);
	if (rc == NOTIFY_BAD) {
		pr_err("submit command to vdsp failed.\n");
		return -EINVAL;
	}

	response = (struct vdsp_runtime_response_t *)rq.out_data;
	if (response->result) {
		pr_err("set log parameters failed.\n");
		return -EINVAL;
	}

	log_params->log_output_modules =
				runtime_rq.log_payload.output_modules;

	return count;
}

static struct kobj_attribute vdsp_log_module_attribute =
	__ATTR(vdsp_log_module, 0644, vdsp_log_module_show, vdsp_log_module_store);

static struct attribute *log_attrs[] = {
	&vdsp_log_level_attribute.attr,
	&vdsp_log_module_attribute.attr,
	NULL,
};

static struct attribute_group log_attr_group = {
	.attrs = log_attrs,
};


int vdsp_log_start(void)
{
	struct log_address_info_t log_addr;
	struct vdsp_log_drvdata *drvdata = s_drvdata;
	struct log_handle_t *log_handle = NULL;

	VDBG("enter.\n");
	if (!drvdata || !drvdata->log_handle) {
		pr_err("drvdata or log_handle are NULL");
		return -EINVAL;
	}

	log_handle = drvdata->log_handle;
	if (!log_handle) {
		pr_err("log_handle is NULL.\n");
		return -EINVAL;
	}

	log_addr.phy_addr = drvdata->phys_addr;
	log_addr.virt_addr = (u64)drvdata->baseaddr;

	log_handle->buf_head = (struct log_buf_head_info_t *)(log_addr.virt_addr);
	memset(log_handle->buf_head, 0x0, sizeof(struct log_buf_head_info_t));
	log_handle->head_paddr = log_addr.phy_addr;
	log_handle->buf_head->log_pool_paddr = log_addr.phy_addr +
				VDSP_LOG_HEAD_SIZE + VDSP_LOG_PARAM_SIZE;
	log_handle->buf_head->log_pool_cpu_addr = log_addr.virt_addr +
				 VDSP_LOG_HEAD_SIZE + VDSP_LOG_PARAM_SIZE;
	VDBG("log_pool_cpu_addr:0x%lx, virt_addr:0x%lx.\n",
			log_handle->buf_head->log_pool_cpu_addr, log_addr.virt_addr);
	memcpy(log_handle->log_name, VDSP_LOG_NAME, VDSP_LOG_NAME_MAX_LEN);
	spin_lock_init(&log_handle->log_lock);

#ifdef LOG_OUTPUT_IN_KERNEL
	jlq_log_default_init(drvdata);
#endif
	drvdata->is_last_read = false;
	VDBG("exit.\n");

	return 0;
}
EXPORT_SYMBOL(vdsp_log_start);

void vdsp_log_stop(void)
{
	struct vdsp_log_drvdata *drvdata = s_drvdata;

	if (!drvdata || !drvdata->log_handle) {
		pr_err("drvdata or log_handle are NULL");
		return;
	}

#ifdef LOG_OUTPUT_IN_KERNEL
	jlq_log_default_destroy(drvdata);
#endif
}
EXPORT_SYMBOL_GPL(vdsp_log_stop);

int vdsp_log_init(struct vdsp_mem_t *mem_info, struct log_init_params *init_params)
{
	struct vdsp_log_drvdata *drvdata = s_drvdata;
	struct log_handle_t *log_handle = NULL;

	VDBG("enter.\n");
	if (!drvdata || !mem_info || !init_params) {
		pr_err("drvdata or mem_info or init_params are NULL.\n");
		return -EINVAL;
	}

	log_handle = drvdata->log_handle;
	if (!log_handle) {
		pr_err("log_handle is NULL.\n");
		return -EINVAL;
	}

	mutex_lock(&drvdata->mutex);
	drvdata->baseaddr = mem_info->baseaddr;
	drvdata->phys_addr = mem_info->phys_addr;

	drvdata->init_params = init_params;
	log_handle->common_params.fifo_depth = init_params->log_fifo_depth;
	log_handle->common_params.fifo_width = init_params->log_fifo_width;
	log_handle->common_params.fifo_watermark = init_params->log_fifo_watermark;
	mutex_unlock(&drvdata->mutex);
	VDBG("exit.\n");
	return 0;
}
EXPORT_SYMBOL(vdsp_log_init);

bool vdsp_log_check(struct vdsp_log_drvdata *drvdata)
{
	struct log_handle_t *log_handle = drvdata->log_handle;
	struct log_buf_head_info_t *buf_head = log_handle->buf_head;

	if (vdsp_get_status() == VDSP_POWER_ON
		&& buf_head->read_log_id != buf_head->write_log_id)
		return true;
	else
		return false;
}

void vdsp_log_read_last(void)
{
	struct vdsp_log_drvdata *drvdata = s_drvdata;
	struct log_buf_head_info_t *buf_head = NULL;

	if (drvdata == NULL || drvdata->log_handle == NULL)
		return;

	buf_head = drvdata->log_handle->buf_head;
	mutex_lock(&drvdata->mutex);
	if (/*vdsp_log_check(drvdata)*/buf_head != NULL
	&& buf_head->read_log_id != buf_head->write_log_id) {
		wake_up(&drvdata->waitq);
		drvdata->is_last_read = true;
	}
	mutex_unlock(&drvdata->mutex);
}
EXPORT_SYMBOL_GPL(vdsp_log_read_last);

static irqreturn_t vdsp_log_handler(int irq, void *dev_id)
{
	struct vdsp_log_drvdata *drvdata = dev_id;

	if (!drvdata)
		return IRQ_NONE;

#ifdef LOG_OUTPUT_IN_KERNEL
	jlq_log_irq_default_handler(drvdata);
#else
	wake_up_interruptible(&drvdata->waitq);
#endif
	return IRQ_HANDLED;
}

static int vdsp_read_log_from_buf(struct vdsp_log_drvdata *drvdata, char __user *buf, size_t count)
{
	struct log_handle_t *log_handle;
	struct log_buf_head_info_t *buf_head;
	char *log_string;
	uint32_t out_cnt = 0;
	uint32_t len = 0;

	if (!drvdata || !drvdata->log_handle || !drvdata->log_handle->buf_head)
		return -EINVAL;
	log_handle = drvdata->log_handle;
	buf_head = log_handle->buf_head;

	while (buf_head->read_log_id != buf_head->write_log_id) {
		log_string = (void *)(buf_head->log_pool_cpu_addr +
			(buf_head->read_log_id * log_handle->common_params.fifo_width));

		if (log_string == NULL || buf_head->log_pool_cpu_addr == 0) {
			pr_err("log addr is inval, log addr = %llx, log pool addr = %llx,"
				" read log id = %d, fifi width = %d\n",
				log_string, buf_head->log_pool_cpu_addr,
				buf_head->read_log_id, log_handle->common_params.fifo_width);
			return -EINVAL;
		}

		len = strlen(log_string);
		pr_debug("log len   %d ============\n", len);
		if (len >= log_handle->common_params.fifo_width)
			len  = log_handle->common_params.fifo_width;

		log_string[len - 1] = '\n';

		if (count >= len + out_cnt) {
			if (copy_to_user(buf + out_cnt, log_string, len)) {
				pr_err("Failed to copy data to user\n");
				return out_cnt;
			}
			out_cnt += len;
		} else {
			break;
		}

		spin_lock_irq(&log_handle->log_lock);
		buf_head->read_log_id++;

		if (buf_head->read_log_id >= log_handle->common_params.fifo_depth)
			buf_head->read_log_id = 0;

		spin_unlock_irq(&log_handle->log_lock);
	}

	return out_cnt;
}

static int vdsp_log_open(struct inode *inode, struct file *filp)
{
	struct vdsp_log_drvdata *drvdata;

	drvdata = container_of(inode->i_cdev, struct vdsp_log_drvdata, cdev);
	if (!drvdata || !drvdata->log_handle) {
		pr_err("drvdata or log_handle are NULL!!\n");
		return -EINVAL;
	}

	filp->private_data = drvdata;
	vdsp_log_irq_enable(1);
	return 0;
}

static int vdsp_log_release(struct inode *inode, struct file *filp)
{
	pr_info("close vdsp log.\n");
	vdsp_log_irq_enable(0);
	return 0;
}

static unsigned int vdsp_log_poll(struct file *filp,
		struct poll_table_struct *wait)
{
	struct vdsp_log_drvdata *drvdata = filp->private_data;

	if (!drvdata)
		return -EINVAL;

	poll_wait(filp, &drvdata->waitq, wait);
	if (vdsp_log_check(drvdata) || drvdata->is_last_read) {
		vdsp_log_irq_enable(0);
		return POLLIN | POLLRDNORM;
	}

	drvdata->is_last_read = false;
	vdsp_log_irq_enable(1);
	return 0;
}

static ssize_t vdsp_log_read(struct file *filp, char __user *buf, size_t count,
				loff_t *f_pos)
{
	int ret = 0;
	struct vdsp_log_drvdata *drvdata = filp->private_data;

	if (!drvdata)
		return -EINVAL;

	//if (vdsp_get_status() != VDSP_POWER_ON) {
	//	pr_err("vdsp device is not open\n");
	//	return -EINVAL;
	//}

	mutex_lock(&drvdata->mutex);
	ret = vdsp_read_log_from_buf(drvdata, buf, count);
	mutex_unlock(&drvdata->mutex);

	return ret;
}

static const struct file_operations vdsp_log_fops = {
	.owner =    THIS_MODULE,
	.read =     vdsp_log_read,
	.open =     vdsp_log_open,
	.release =  vdsp_log_release,
	.poll =     vdsp_log_poll,
};

static int vdsp_log_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct vdsp_log_drvdata *drvdata;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev;
	struct resource *irq_base_reg;
	dev_t devno;

	drvdata = devm_kzalloc(&pdev->dev,
			sizeof(struct vdsp_log_drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->log_handle = devm_kzalloc(&pdev->dev,
			sizeof(struct log_handle_t), GFP_KERNEL);
	if (!drvdata->log_handle)
		return -ENOMEM;

	memset(drvdata->log_handle, 0, sizeof(struct log_handle_t));
	drvdata->dev = &pdev->dev;
	platform_set_drvdata(pdev, drvdata);
	mutex_init(&drvdata->mutex);
	init_waitqueue_head(&drvdata->waitq);
#ifdef LOG_OUTPUT_IN_KERNEL
	init_waitqueue_head(&drvdata->log_handle->log_irq_wait);
#endif
	devno = MKDEV(g_major, 1);

	drvdata->vdsp_log_class = class_create(THIS_MODULE, "vdsp_log");
	if (IS_ERR(drvdata->vdsp_log_class)) {
		pr_err(" Create class failed.\n");
		rc = PTR_ERR(drvdata->vdsp_log_class);
		goto class_err;
	}
	dev = device_create(drvdata->vdsp_log_class, NULL, devno, NULL,
				"vdsp_log");
	if (IS_ERR(dev)) {
		pr_err("Create character device failed.\n");
		rc = PTR_ERR(drvdata->vdsp_log_class);
		goto dev_err;
	}

	cdev_init(&drvdata->cdev, &vdsp_log_fops);
	drvdata->cdev.owner = THIS_MODULE;
	rc = cdev_add(&drvdata->cdev, devno, 1);
	if (rc < 0) {
		pr_err("register char device failed.\n");
		goto cdev_err;
	}

	drvdata->log_irq_id = of_irq_get_byname(np, "log-irq-id");
	if (drvdata->log_irq_id < 0) {
		pr_err("get log_irq_id failed.\n");
		rc = -ENXIO;
		goto get_irq_err;
	}

	rc = devm_request_irq(drvdata->dev, drvdata->log_irq_id,
			vdsp_log_handler, 0,
			"vdsp_log_irq", drvdata);
	if (rc) {
		pr_err("request rxirq failed rc:%d\n", rc);
		rc = -ENXIO;
		goto get_irq_err;
	}

	drvdata->is_irq_enable = true;
	drvdata->is_last_read = false;

	irq_base_reg = platform_get_resource_byname(pdev,
						IORESOURCE_MEM, "log_irq_base");
	if (!irq_base_reg) {
		pr_err("Unable to get the irq base resources\n");
		rc = -ENOMEM;
		goto io_error;
	}

	drvdata->irq_base = ioremap(irq_base_reg->start,
						resource_size(irq_base_reg));
	if (!drvdata->irq_base) {
		pr_err("Unable to remap irq base resources\n");
		rc = -ENOMEM;
		goto io_error;
	}

	drvdata->vdsp_log_obj = NULL;

	drvdata->vdsp_log_obj = kobject_create_and_add("vdsp_log", kernel_kobj);
	if (!drvdata->vdsp_log_obj) {
		pr_err("sysfs create and add failed\n");
		rc = -ENOMEM;
		goto io_error;
	}

	rc = sysfs_create_group(drvdata->vdsp_log_obj, &log_attr_group);
	if (rc) {
		pr_err("sysfs_create_file failed %d\n", rc);
		goto err_kobj;
	}

	s_drvdata = drvdata;
	pr_info("vdsp log probe ok.\n");
	return rc;

err_kobj:
	kobject_put(drvdata->vdsp_log_obj);
io_error:
	if (drvdata->irq_base)
		iounmap(drvdata->irq_base);
get_irq_err:
	cdev_del(&drvdata->cdev);
cdev_err:
	device_destroy(drvdata->vdsp_log_class, devno);
dev_err:
	class_destroy(drvdata->vdsp_log_class);
class_err:
	mutex_destroy(&drvdata->mutex);
	devm_kfree(&pdev->dev, drvdata->log_handle);
	devm_kfree(&pdev->dev, drvdata);
	return rc;
}

static int vdsp_log_remove(struct platform_device *pdev)
{
	struct vdsp_log_drvdata *drvdata = platform_get_drvdata(pdev);
	dev_t devno;

	if (drvdata->irq_base)
		iounmap(drvdata->irq_base);
	kobject_put(drvdata->vdsp_log_obj);
	devno = MKDEV(g_major, 1);
	device_destroy(drvdata->vdsp_log_class, devno);
	class_destroy(drvdata->vdsp_log_class);
	cdev_del(&drvdata->cdev);
	mutex_destroy(&drvdata->mutex);
	devm_kfree(&pdev->dev, drvdata->log_handle);
	devm_kfree(&pdev->dev, drvdata);
	s_drvdata = NULL;

	return 0;
}

static const struct of_device_id vdsp_log_dt_match[] = {
	{.compatible = "jlq,vdsp-log"},
	{}
};

static struct platform_driver vdsp_log_driver = {
	.probe = vdsp_log_probe,
	.remove = vdsp_log_remove,
	.driver = {
		.name = "jlq,vdsp-log",
		.owner = THIS_MODULE,
		.of_match_table = vdsp_log_dt_match,
	},
};

static int __init vdsp_log_module_init(void)
{
	int rc;
	dev_t devno;

	rc = alloc_chrdev_region(&devno, 0, 1, "vdsp_log");
	if (rc < 0) {
		pr_err("get device number failed.\n");
		return rc;
	}
	g_major = MAJOR(devno);

	rc = platform_driver_register(&vdsp_log_driver);
	if (rc < 0) {
		pr_err("register platform driver failed.\n");
		goto out;
	}

	return rc;
out:
	unregister_chrdev_region(devno, 1);
	return rc;
}

static void __exit vdsp_log_module_exit(void)
{
	dev_t devno = MKDEV(g_major, 0);

	platform_driver_unregister(&vdsp_log_driver);
	unregister_chrdev_region(devno, 1);
}

module_init(vdsp_log_module_init);
module_exit(vdsp_log_module_exit);

MODULE_DESCRIPTION("log module platform driver");
MODULE_LICENSE("GPL v2");
