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
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/rtc.h>

#include "vdsp-ssr.h"
#include "vdsp-runtime-type.h"
#include "vdsp-driver.h"
#include "vdsp-wdt.h"

#define VDSP_TCM_SIZE (256 * 1024)
#define DUMP_FILE_NUM_MAX  20

struct sysdump_body_t {
	char name[20]; /* file name */
	uint32_t addr; /* file start load addr */
	uint32_t offset; /* offset of sysdump mem region */
	uint32_t size; /* file size */
};

struct sysdump_info_t {
	uint32_t file_num; /* file nums */
	struct sysdump_body_t body[DUMP_FILE_NUM_MAX]; /* file body */
};

#ifdef DUMP_IN_KERNEL
static void vdsp_sysdump(void *tcm_baseaddr, struct vdsp_mem_t mem_info, char *dump_path)
{
	struct sysdump_info_t sysdump_info;
	int i = 0;
	char file_name[128];
	loff_t pos = 0;
	struct file *file;
	struct timex  txc;
	struct rtc_time tm;

	if (mem_info.baseaddr == NULL) {
		pr_err("Baseaddr is NULL!\n");
		return;
	}

	//do_gettimeofday(&(txc.time));
	rtc_time_to_tm(txc.time.tv_sec, &tm);
	pr_info("VDSP SYSDUMP UTC time :%d-%d-%d %d:%d:%d\n", tm.tm_year+1900, tm.tm_mon,
		tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	memcpy_fromio(&sysdump_info, mem_info.baseaddr, sizeof(struct sysdump_info_t));

	if (sysdump_info.file_num <= 0 || sysdump_info.file_num > DUMP_FILE_NUM_MAX) {
		pr_err("nothing needs to dump file num=%d\n", sysdump_info.file_num);
		return;
	}

	for (i = 0; i < sysdump_info.file_num; i++) {
		pos = 0;
		file = NULL;
		memset(file_name, 0, sizeof(file_name));
		sprintf(file_name, "%s/%s.dump", dump_path,
		/*tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, */
			sysdump_info.body[i].name);
		pr_err("dump info to %s\n", file_name);

		file = filp_open(file_name, O_RDWR | O_CREAT | O_TRUNC, 0600);
		if (IS_ERR_OR_NULL(file)) {
			pr_err("filp open error.\n");
			file = NULL;
			break;
		}
		kernel_write(file, mem_info.baseaddr + sysdump_info.body[i].offset,
				sysdump_info.body[i].size, &pos);
		filp_close(file, NULL);
	}

#ifdef DUMP_VDSP_TCM
	if (!tcm_baseaddr) {
		pr_err("tcm_baseaddr is NULL!\n");
		return;
	}
	memcpy_toio(mem_info->baseaddr + mem_info->size - VDSP_TCM_SIZE,
					tcm_baseaddr, VDSP_TCM_SIZE);
	pos = 0;
	file = NULL;
	memset(file_name, 0, sizeof(file_name));
	sprintf(file_name, "%s/%s.dump", dump_path, "vdsp_tcm");
	file = filp_open(file_name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	kernel_write(file, mem_info->baseaddr + mem_info->size
					- VDSP_TCM_SIZE, VDSP_TCM_SIZE, &pos);
	filp_close(file, NULL);
#endif
}
#endif
static size_t vdsp_sysdump(void *tcm_baseaddr, struct vdsp_mem_t mem_info,
			char __user *buf, size_t count, loff_t *ppos)
{
	size_t cnt;
	size_t ramdump_size;
	size_t offset;
	struct sysdump_info_t sysdump_info;
	uint32_t file_num;

	if (mem_info.baseaddr == NULL) {
		pr_err("Baseaddr is NULL!\n");
		return 0;
	}

	memcpy_fromio(&sysdump_info, mem_info.baseaddr, sizeof(struct sysdump_info_t));

	if (sysdump_info.file_num < 1 || sysdump_info.file_num > DUMP_FILE_NUM_MAX) {
		pr_err("no file need to dump!\n");
		return 0;
	}

	file_num = sysdump_info.file_num;

	ramdump_size = sysdump_info.body[file_num - 1].offset
			+ sysdump_info.body[file_num - 1].size;

	if (*ppos >= ramdump_size)
		return 0;

	cnt = ramdump_size - *ppos;
	if (cnt > count)
		cnt = count;

	offset = *ppos;
	copy_to_user(buf, mem_info.baseaddr + offset, cnt);
	*ppos = cnt + *ppos;

	return cnt;
}

int vdsp_ssr_set_status(struct vdsp_ssr_info *ssr_info, enum subsys_state state)
{
	if (!ssr_info) {
		pr_err("ssr_info is NULL.\n");
		return -EINVAL;
	}

	mutex_lock(&ssr_info->mutex);
	vdsp_subsys_set_state(ssr_info->subsys_dev, state);
	mutex_unlock(&ssr_info->mutex);
	return 0;
}
EXPORT_SYMBOL(vdsp_ssr_set_status);

static int vdsp_ssr_powerup(const struct vdsp_subsys_desc *subsys_desc)
{
	int rc;

	if (!subsys_desc || !subsys_desc->dev) {
		pr_err("subsys_desc or dev are NULL.\n");
		return -EINVAL;
	}

	rc = raw_notifier_call_chain(&ssr_notifier_list, SSR_EVENT_RESUME, subsys_desc->dev);
	//if (vdsp_runtime_resume(subsys_desc->dev)) {
	if (rc == NOTIFY_BAD) {
		pr_err("vdsp restart failed.\n");
		return -ENODEV;
	}

	return 0;
}

static int vdsp_ssr_shutdown(const struct vdsp_subsys_desc *subsys_desc, bool force_stop)
{
	int rc;

	if (!subsys_desc || !subsys_desc->dev) {
		pr_err("subsys_desc or dev are NULL.\n");
		return -EINVAL;
	}
	rc = raw_notifier_call_chain(&ssr_notifier_list, SSR_EVENT_SUSPENED, subsys_desc->dev);
	//return vdsp_runtime_suspend(subsys_desc->dev);
	if (rc == NOTIFY_BAD)
		return -EINVAL;

	return 0;
}

static void vdsp_ssr_trigger_ramdump_by_irq(const struct vdsp_subsys_desc *subsys_desc)
{
	int rc;

	if (!subsys_desc) {
		pr_err("subsys_desc is NULL.\n");
		return;
	}

	if (subsys_desc->force_ramdump_irq_id <= 0) {
		pr_err("Invalid force_ramdump_irq_id:%d.\n", subsys_desc->force_ramdump_irq_id);
		return;
	}

	raw_notifier_call_chain(&ssr_notifier_list, SSR_EVENT_TX_IRQ, (void *)subsys_desc);
	//send_irq_to_vdsp(subsys_desc->dev, (int)subsys_desc->force_ramdump_irq_id);
	pr_info("!!!!!!!!!trigger vdsp to ramdump!!!!!!!!!!.\n");
	rc = vdsp_wdt_hw_deinit();
	if (rc)
		pr_err("deinit wdt failed!!\n");

}

static void vdsp_crash_shutdown(const struct vdsp_subsys_desc *subsys_desc)
{
	vdsp_ssr_trigger_ramdump_by_irq(subsys_desc);
}
/*
static void vdsp_ssr_trigger_ramdump_by_jrpc(const struct vdsp_subsys_desc *subsys_desc)
{
	struct vdsp_runtime_request_t runtime_rq;
	struct vdsp_request rq;
	int rc;

	if (sizeof(runtime_rq) > VDSP_CMD_INLINE_DATA_SIZE) {
		pr_err("parameter bytes are overflow.\n");
		return;
	}

	memset(&runtime_rq, 0, sizeof(runtime_rq));
	runtime_rq.type = VDSP_RUNTIME_SET_SYSDUMP;

	memset(&rq, 0, sizeof(rq));
	memcpy(rq.nsid, RUNTIME_NSID, VDSP_CMD_NAMESPACE_ID_SIZE);
	memcpy(rq.in_data, &runtime_rq, sizeof(runtime_rq));
	rq.ioctl_queue.in_data_size = sizeof(runtime_rq);
	rq.ioctl_queue.flags = VDSP_QUEUE_FLAG_NSID;

	pr_info("!!!!!!!!!force vdsp to ramdump!!!!!!!!!!.\n");
	vdsp_kernel_commit_sync(&rq);
	rc = vdsp_wdt_hw_deinit();
	if (rc)
		pr_err("deinit wdt failed!!\n");
}
*/
static size_t vdsp_ssr_do_ramdump(const struct vdsp_subsys_desc *subsys_desc,
				char __user *buf, size_t count, loff_t *ppos)
{
	size_t cnt = 0;

	if (!subsys_desc) {
		pr_err("subsys_desc is NULL.\n");
		return -EINVAL;
	}

	cnt = vdsp_sysdump(subsys_desc->tcm_baseaddr,
		subsys_desc->mem_info,
		buf, count, ppos);

	return cnt;
}

int vdsp_ssr_init(struct platform_device *pdev,
			struct vdsp_ssr_info **ssr_info,
			struct vdsp_extern_module *module, struct vdsp_mem_t *mem_info)
{
	struct vdsp_ssr_info *ssr;

	if (!mem_info || !module) {
		pr_err("mem_info or module is NULL.\n");
		return -EINVAL;
	}

	ssr = kzalloc(sizeof(struct vdsp_ssr_info), GFP_KERNEL);
	if (!ssr)
		return -ENOMEM;

	ssr->subsys_desc.name = "vdsp";
	ssr->subsys_desc.dev = &pdev->dev;
	ssr->subsys_desc.owner = THIS_MODULE;
	ssr->subsys_desc.shutdown = vdsp_ssr_shutdown;
	ssr->subsys_desc.powerup = vdsp_ssr_powerup;
	ssr->subsys_desc.crash_shutdown = vdsp_crash_shutdown;
	ssr->subsys_desc.is_support_ramdump = 1;
	// ssr->subsys_desc.trigger_ramdump = vdsp_ssr_trigger_ramdump_by_jrpc;
	ssr->subsys_desc.trigger_ramdump = vdsp_crash_shutdown;
	memset(ssr->subsys_desc.ramdump_path, 0,
		sizeof(ssr->subsys_desc.ramdump_path));
	memcpy(ssr->subsys_desc.ramdump_path, "/data", 5);
	ssr->subsys_desc.ramdump = vdsp_ssr_do_ramdump;
	ssr->subsys_desc.force_ramdump_irq_id = module->force_ramdump_irq_id;
	ssr->subsys_desc.panic_irq = module->panic_irq_id;
	ssr->subsys_desc.wdog_bite_irq = module->wdog_bite_irq_id;
	ssr->subsys_desc.tcm_baseaddr = module->tcm_baseaddr;
	memcpy(&ssr->subsys_desc.mem_info, mem_info, sizeof(struct vdsp_mem_t));

	ssr->subsys_dev = vdsp_subsys_register(&ssr->subsys_desc);
	if (IS_ERR(ssr->subsys_dev)) {
		pr_err("jlq subsys register failed.\n");
		goto err_subsys;
	}

	mutex_init(&ssr->mutex);
	*ssr_info = ssr;
	return 0;

err_subsys:
	kfree(ssr);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(vdsp_ssr_init);

int vdsp_ssr_uninit(struct vdsp_ssr_info *ssr_info)
{
	if (!ssr_info) {
		pr_err("ssr_info is NULL.\n");
		return -EINVAL;
	}

	vdsp_subsys_unregister(ssr_info->subsys_dev);
	mutex_destroy(&ssr_info->mutex);

	kfree(ssr_info);
	return 0;
}
EXPORT_SYMBOL_GPL(vdsp_ssr_uninit);
