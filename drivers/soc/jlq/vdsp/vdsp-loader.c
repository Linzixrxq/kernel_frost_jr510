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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include "vdsp-loader.h"
#include <linux/kthread.h>

#define VDSP_IMG_AUTH		1
#define VDSPTAG				"[VDSPV]"

#if VDSP_IMG_AUTH
#include "soc/jlq/jlq_verifier.h"
#endif


#undef VDBG
#define VDBG(fmt, args...) pr_debug(fmt, ##args)

#define BOOT_CMD 1
#define IMAGE_UNLOAD_CMD 0
#define VDSPBIN_FILE		"vdsp.bin"
#define WAITE_TIMEOUT  50000

struct vdsp_loader {
	struct platform_device *pdev;
	struct kobject *kobj;
	struct vdsp_mem_t mem_info;
	struct work_struct vdsp_ldr_work;
	bool is_load_done;
};

static ssize_t vdsp_load_fw_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count);

static struct kobj_attribute vdsp_load_fw_attribute =
	__ATTR(load_fw, 0220, NULL, vdsp_load_fw_store);

static struct vdsp_loader *g_vdsp_loader;

static int vdsp_load_prepare(void)
{
	struct vdsp_loader *vdsp_loader = g_vdsp_loader;
	struct vdsp_mem_t *mem_info;
	int ret = 0;
	const struct firmware *firmware = NULL;
#if VDSP_IMG_AUTH
	char *sig_header = NULL;
	struct verifier_image_info *arg_info;
	size_t arg_size;
	int session_id = -1;
	int shm_id = -1;
	bool tz_verify;
#endif

	VDBG("in\n");
	if (!vdsp_loader || !vdsp_loader->pdev) {
		pr_err(VDSPTAG"vdsp_loader:%p or pdev are NULL.\n", vdsp_loader);
		return -EINVAL;
	}

	ret = request_firmware(&firmware, VDSPBIN_FILE, &(vdsp_loader->pdev->dev));
	if (ret || !firmware) {
		pr_err(VDSPTAG"request_firmware failed for %s (ret = %d)\n",
				VDSPBIN_FILE, ret);
		return -ENOMEM;
	}

	pr_info(VDSPTAG"bin file size: %d\n", (int)firmware->size);

	mem_info = &vdsp_loader->mem_info;
	if (mem_info->baseaddr == NULL
		|| mem_info->size < firmware->size) {
		pr_err(VDSPTAG"vdsp get image buffer failed.\n");
		ret = -ENOMEM;
		goto err_firmware;
	}

#if VDSP_IMG_AUTH
	tz_verify = of_property_read_bool(vdsp_loader->pdev->dev.of_node,
									"jlq,tz_verify");
	pr_info(VDSPTAG"tz_verify:[%d]\n", tz_verify);

	if (tz_verify) {
		int header_size = VERIFIER_HEADER_SIZE;
		const u8 *raw_data;
		int raw_size;

		session_id = tz_verifier_init();
		if (session_id < 0) {
			pr_err(VDSPTAG"verify get session fail:%d\n", session_id);
			ret = -EBUSY;
			goto err_tz;
		}

		arg_size = TZ_SHM_SIZE(1);
		shm_id = tz_verifier_alloc_shm(arg_size);
		if (shm_id < 0) {
			pr_err(VDSPTAG"verify alloc shm fail:%d\n", shm_id);
			ret = -ENOMEM;
			goto err_tz;
		}

		tz_verifier_get_shm_info(shm_id, (char **)(&arg_info), &arg_size);

		sig_header = kmalloc(header_size, GFP_KERNEL);
		if (!sig_header) {
			pr_err(VDSPTAG"%s verify alloc metadata mem %d fail\n",
				VDSPBIN_FILE, header_size);
			ret = -ENOMEM;
			goto err_tz;
		}
		memcpy(sig_header, firmware->data, header_size);
		raw_data = firmware->data + header_size;
		raw_size = firmware->size - header_size;

		memset(arg_info, 0, arg_size);
		arg_info->ss_prop.ssid = SS_COMMON;
		arg_info->ss_prop.para_num = 0;
		arg_info->header_paddr = virt_to_phys(sig_header);
		arg_info->header_size = header_size;
		arg_info->image_seg_num = 1;

		arg_info->ism[0].paddr = mem_info->phys_addr;
		arg_info->ism[0].sz = raw_size;

		memcpy(mem_info->baseaddr, raw_data, raw_size);

		/* wmb() ensures copy completes prior to starting authentication. */
		wmb();
		ret = tz_verifier_invode_command(session_id, shm_id, TZCMD_AUTH_BOOT, arg_size);
		if (ret) {
			pr_err(VDSPTAG" auth failed! ret:%d\n", ret);
			goto err_tz;
		}
		pr_info(VDSPTAG" auth success!\n");
	} else {
		const u8 *raw_data;
		size_t raw_size;

		raw_data = tz_verifier_get_raw_data(firmware->data, firmware->size);
		if (raw_data == firmware->data) {
			raw_size = firmware->size;
		} else {
			raw_size = firmware->size - VERIFIER_HEADER_SIZE;
			pr_err(VDSPTAG"image has signed, skip header\n");
		}
		memcpy(mem_info->baseaddr, raw_data, raw_size);
	}
#else
	memcpy(mem_info->baseaddr, firmware->data, firmware->size);
#endif
	vdsp_loader->is_load_done = true;
	VDBG("end\n");
	ret = 0;

#if VDSP_IMG_AUTH
err_tz:
	kfree(sig_header);

	if (shm_id >= 0)
		tz_verifier_free_shm(shm_id);

	if (session_id >= 0)
		tz_verifier_deinit(session_id, 0);
#endif

err_firmware:
	release_firmware(firmware);

	return ret;
}

static void vdsp_loader_fw(struct work_struct *work)
{
	if (vdsp_load_prepare() < 0) {
		pr_err("image loading failed\n");
	}
}

void vdsp_loader_do(void)
{
	struct vdsp_loader *vdsp_loader = g_vdsp_loader;

	VDBG("in.\n");
	if (!vdsp_loader) {
		pr_err("vdsp_loader is NULL.\n");
		return;
	}
	if (vdsp_loader->is_load_done)
		return;

	schedule_work(&vdsp_loader->vdsp_ldr_work);
	VDBG("out.\n");
}
EXPORT_SYMBOL(vdsp_loader_do);

int vdsp_loader_wait_done(void)
{
	struct vdsp_loader *vdsp_loader = g_vdsp_loader;
	uint32_t retry = 0;

	VDBG("in.\n");
	do {
		if (vdsp_loader->is_load_done == true)
			break;

		udelay(20);
		retry++;

	} while (retry < WAITE_TIMEOUT);

	if (retry >= WAITE_TIMEOUT)
		return -1;

	VDBG("exit.\n");
	return 0;
}
EXPORT_SYMBOL(vdsp_loader_wait_done);

void vdsp_loader_unload(void)
{
	struct vdsp_loader *vdsp_loader = g_vdsp_loader;

	if (!vdsp_loader || !(vdsp_loader->mem_info.baseaddr)) {
		pr_err("vdsp_loader or baseaddr are NULL.\n");
		return;
	}

	memset_io(vdsp_loader->mem_info.baseaddr, 0, vdsp_loader->mem_info.size);
	vdsp_loader->is_load_done = false;
}
EXPORT_SYMBOL(vdsp_loader_unload);

static ssize_t vdsp_load_fw_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf,
	size_t count)
{
	int boot = 0;

	if (sscanf(buf, "%du", &boot) != 1) {
		pr_err("failed to read boot info from string\n");
		return -EINVAL;
	}

	if (boot == BOOT_CMD) {
		pr_debug("going to call vdsp_loader_do\n");
		vdsp_loader_do();
	} else if (boot == IMAGE_UNLOAD_CMD) {
		pr_debug("going to call vdsp_unloader\n");
		vdsp_loader_unload();
	}
	return count;
}

int vdsp_loader_init(struct platform_device *pdev, struct vdsp_mem_t *mem_info)
{
	int ret = 0;
	struct vdsp_loader *vdsp_loader = NULL;

	if (!pdev || !mem_info) {
		pr_err("pdev or mem_info are NULL.\n");
		return -EINVAL;
	}

	vdsp_loader = kzalloc(sizeof(struct vdsp_loader), GFP_KERNEL);
	if (!vdsp_loader)
		return -ENOMEM;

	/*init sysfs*/
	vdsp_loader->kobj = kobject_create_and_add("vdsp_loader", kernel_kobj);
	if (!vdsp_loader->kobj) {
		pr_err("create vdsp loader kobj fail.\n");
		ret = -ENOMEM;
		goto free_mem;
	}

	ret = sysfs_create_file(vdsp_loader->kobj,
				&vdsp_load_fw_attribute.attr);
	if (ret) {
		pr_err("sysfs vdsp loader create file failed %d.\n", ret);
		goto free_obj;
	}

	vdsp_loader->pdev = pdev;
	vdsp_loader->is_load_done = false;
	memcpy(&vdsp_loader->mem_info, mem_info, sizeof(struct vdsp_mem_t));
	g_vdsp_loader = vdsp_loader;

	INIT_WORK(&vdsp_loader->vdsp_ldr_work, vdsp_loader_fw);
	return 0;

free_obj:
	kobject_del(vdsp_loader->kobj);
free_mem:
	kfree(vdsp_loader);

	return ret;
}

void vdsp_loader_uninit(void)
{
	struct vdsp_loader *vdsp_loader = g_vdsp_loader;

	if (!vdsp_loader) {
		pr_err("vdsp_loader is NULL.\n");
		return;
	}

	if (vdsp_loader->kobj) {
		sysfs_remove_file(vdsp_loader->kobj, &vdsp_load_fw_attribute.attr);
		kobject_del(vdsp_loader->kobj);
		vdsp_loader->kobj = NULL;
	}

	cancel_work_sync(&vdsp_loader->vdsp_ldr_work);
	kfree(vdsp_loader);
}
EXPORT_SYMBOL(vdsp_loader_uninit);

MODULE_DESCRIPTION("vdsp Loader module");
MODULE_LICENSE("GPL v2");
