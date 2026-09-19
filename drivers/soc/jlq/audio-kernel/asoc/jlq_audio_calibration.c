// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 JLQ Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org.
 */
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kobject.h>
#include <linux/of_device.h>
#include "msm-pcm-routing-v2.h"
#include "jlq_audio_calibration.h"

enum {
	AUDIO_CAL_MODE_NORMAL = 0,
	AUDIO_CAL_MODE_TEST   = 1,
};

#define ADSP_CALIBRATION_DEVICE_NAME  "adsp_cal"

#define ADSP_CALIBRATION_FILE         "audio.cal"
#define ADSP_CALIBRATION_TEST_FILE    "audiotest.cal"
#define PA_CALIBRATION_FILE           "sixth_param.txt"

static ssize_t audio_cal_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf,
	size_t count);
static ssize_t audio_cal_show(struct kobject *kobj,
	struct kobj_attribute *attr,
	char *buf);

struct audio_cal_drvdata {
	dev_t devno;
	struct class *my_class;
	struct device *dev;
	struct cdev cdev;
	struct platform_device *pdev;
	int cal_mode;
	phys_addr_t cal_mem_addr;
	unsigned int cal_mem_size;
	void __iomem *cal_virt_addr;
	phys_addr_t pa_cal_mem_addr;
	unsigned int pa_cal_mem_size;
	void __iomem *pa_cal_virt_addr;
};

struct audio_cal_private {
	struct kobject *audio_cal_obj;
	struct attribute_group *attr_group;
};

static struct kobj_attribute audio_cal_attribute =
	__ATTR(audiocal, S_IRUGO | S_IWUSR, audio_cal_show, audio_cal_store);

static struct attribute *attrs[] = {
	&audio_cal_attribute.attr,
	NULL,
};

struct audio_cal_drvdata *gdrvdata = NULL;
static struct mutex audiocal_lock;

static int cal_drv_open(struct inode *inode, struct file *filp)
{
	if (gdrvdata  == NULL)
		return -ENOMEM;

	return 0;
}

static int cal_drv_release(struct inode *inode, struct file *filp)
{
	if (gdrvdata  == NULL)
		return -ENOMEM;

	return 0;
}

static int cal_drv_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int result;

	if (gdrvdata  == NULL)
		return -ENOMEM;

	if (gdrvdata->cal_virt_addr == NULL)
		return -ENOMEM;

	if (vma->vm_end - vma->vm_start > gdrvdata->cal_mem_size) {
		pr_err("%s:vm_end[%lu] - vm_start[%lu] [%lu] > mem->size[%lu]\n",
			__func__,
			vma->vm_end, vma->vm_start,
			(vma->vm_end - vma->vm_start), gdrvdata->cal_mem_size);
		return -EINVAL;
	}

	vma->vm_flags |= (VM_SHARED | VM_IO);

	result=remap_pfn_range(vma,
		vma->vm_start,
		gdrvdata->cal_mem_addr >> PAGE_SHIFT,
		vma->vm_end - vma->vm_start,
		vma->vm_page_prot);

	if(result){
		return -EAGAIN;
	}

	return 0;
}

static struct file_operations cal_drv_fops = {
	.owner = THIS_MODULE,
	.open = cal_drv_open,
	.release = cal_drv_release,
	.mmap = cal_drv_mmap,
};

static int cal_drv_setup_cdev(struct cdev *cdev, dev_t devno)
{
	int error;

	cdev_init(cdev, &cal_drv_fops);
	cdev->owner = THIS_MODULE;
	error = cdev_add(cdev, devno, 1);

	return error;
}

static void cal_drv_destroy_cdev(struct cdev *cdev)
{
	cdev_del(cdev);
}

static char *get_cal_drv_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, ADSP_CALIBRATION_DEVICE_NAME);
}

static int inform_cal_data_update(int flag){
	if (flag == AUDIO_CAL_EFFECT_DELAY) {
		pr_info("%s: effect calibration DELAYED\n", __func__);
		return 0;
	} else if (flag == AUDIO_CAL_EFFECT_DONOT_SEND) {
		pr_info("%s: do not send calibration updating cmd to adsp\n", __func__);
		return 0;
	}

	jlq_adsp_caldata_update();

	return 0;
}

static uint32_t sixth_core_load_param(
	char *param_str, uint32_t *param, uint32_t max_len)
{
	char* cur = NULL;
	int line = 0;

	if (NULL == param_str || NULL == param || 0 == max_len) {
		pr_err("%s : sixth_core_load_param: "
			"NULL == param_str || NULL == param || 0 == max_len\n",
			__FUNCTION__);
		return 0;
	}

	for (line = 0;	NULL != strsep(&param_str, " ") && NULL != (cur = strsep(&param_str, "\n"))
		&& line * sizeof(uint32_t) < max_len; ++line) {
		param[line] = (uint32_t)simple_strtoul(cur, NULL, 16);
	}

	if (line * sizeof(uint32_t) >= max_len) {
		pr_err("%s : param file too long (line > %u)\n",
			__FUNCTION__, max_len / sizeof(uint32_t));
		return 0;
	} else {
		return line * sizeof(uint32_t);
	}
}

int audio_cal_load(int flag)
{
	int ret = 0;
	const struct firmware *firmware = NULL;
	char *calfile;

	if (gdrvdata->cal_virt_addr == NULL) {
		pr_err("%s: adsp calibration address is not mapped yet\n", __func__);
		ret = -1;
		return ret;
	}

	if (gdrvdata->pa_cal_virt_addr == NULL) {
		pr_err("%s: smartPA calibration address is not mapped yet\n", __func__);
		ret = -1;
		return ret;
	}

	if (gdrvdata->cal_mode == AUDIO_CAL_MODE_NORMAL)
		calfile = ADSP_CALIBRATION_FILE;
	else
		calfile = ADSP_CALIBRATION_TEST_FILE;

	/* laod adsp calibration data */
	ret = request_firmware(&firmware, calfile, &(gdrvdata->pdev->dev));
	if (ret || !firmware) {
		pr_err("%s: request_firmware failed for %s (ret = %d)\n",
				__func__, calfile, ret);
		return -ENOMEM;
	}

	pr_debug("%s: cal bin file %s, size: %d\n", __func__, calfile, (int)firmware->size);

	memcpy_toio(gdrvdata->cal_virt_addr, firmware->data, firmware->size);

	pr_info("%s: cal file %s with size %d has been loaded to 0x%x %d\n",
		__func__,
		calfile,
		(int)firmware->size,
		gdrvdata->cal_mem_addr,
		gdrvdata->cal_mem_size);

	release_firmware(firmware);

	/* load smartPA calibration data */
	ret = request_firmware(&firmware, PA_CALIBRATION_FILE, &(gdrvdata->pdev->dev));
	if (ret || !firmware) {
		pr_err("%s: request_firmware failed for %s (ret = %d)\n",
				__func__, PA_CALIBRATION_FILE, ret);
		return -ENOMEM;
	}

	pr_debug("%s: cal bin file %s, size: %d\n", __func__, PA_CALIBRATION_FILE, (int)firmware->size);

	ret = sixth_core_load_param((char*)firmware->data, gdrvdata->pa_cal_virt_addr, gdrvdata->pa_cal_mem_size);

	pr_info("%s: cal bin file %s with size %d has been loaded to 0x%x %d\n",
		__func__,
		PA_CALIBRATION_FILE,
		(int)firmware->size,
		gdrvdata->pa_cal_mem_addr,
		ret);

	release_firmware(firmware);

	inform_cal_data_update(flag);

	return 0;
}
EXPORT_SYMBOL(audio_cal_load);

static ssize_t audio_cal_show(struct kobject *kobj,
							struct kobj_attribute *attr,
							char *buf)
{
	size_t count = 0;

	if (gdrvdata->cal_mode == AUDIO_CAL_MODE_NORMAL)
		count += sprintf(&buf[count], "calibration mode: NORMAL\n");
	else if (gdrvdata->cal_mode == AUDIO_CAL_MODE_TEST)
		count += sprintf(&buf[count], "calibration mode: TEST\n");

	pr_info("%s: mode = %d\n", __func__, gdrvdata->cal_mode);

	return count;
}

static ssize_t audio_cal_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret = 0;
	char opt_code[16];
	int opt_val = 0;

	if (!gdrvdata) {
		pr_err("%s: audio calibration driver not inited yet", __func__);
		pr_info("calibration return: failed\n");
		return 0;
	}

	sscanf(buf, "%s %d", &opt_code, &opt_val);

	if (strcmp("mode", opt_code) == 0) {
		if (opt_val == AUDIO_CAL_MODE_NORMAL) {
			pr_info("%s: set calibration mode to NORMAL\n", __func__);
			pr_info("calibration return: success\n");
		} else if (opt_val == AUDIO_CAL_MODE_TEST) {
			pr_info("%s: set calibration mode to TEST\n", __func__);
			pr_info("calibration return: success\n");
		} else {
			pr_info("%s: unsupported cal mode, fource to set to NORMAL\n", __func__);
			pr_info("calibration return: failed\n");
			opt_val = AUDIO_CAL_MODE_NORMAL;
		}

		gdrvdata->cal_mode  = opt_val;

	} else if (strcmp("update", opt_code) == 0) {
		if (opt_val == AUDIO_CAL_EFFECT_IMMEDIATE) {
			pr_info("%s: immediate to update the calibration data\n", __func__);
			mutex_lock(&audiocal_lock);
			ret = audio_cal_load(opt_val);
			if (ret != 0)
			{
				pr_err("%s: failed to load calibration data\n", __func__);
				pr_info("calibration return: failed\n");
			}
			else
			{
				pr_info("%s: success to update the calibration data\n", __func__);
				pr_info("calibration return: success\n");
			}

			mutex_unlock(&audiocal_lock);

		} else if (opt_val == AUDIO_CAL_EFFECT_DELAY) {
			pr_info("%s: delay to update the cal data in next power on\n", __func__);
			pr_info("calibration return: success\n");
		} else {
			pr_info("%s: unsupported action mode,set to IMMEDIATE mode\n", __func__);
			pr_info("calibration return: failed\n");

			mutex_lock(&audiocal_lock);
			ret = audio_cal_load(opt_val);
			if (ret != 0)
				pr_err("%s: failed to load calibration data\n", __func__);
			else
				pr_info("%s: success to update the calibration data\n", __func__);

			mutex_unlock(&audiocal_lock);
		}

	} else if (strcmp("read", opt_code) == 0) {
		pr_err("calibration return: audio cal read is not supported now\n");
		pr_err("calibration return: failed\n");
	} else {
		pr_err("%s: unsupported option code %s\n", __func__, opt_code);
		pr_err("calibration return: failed\n");
	}

	return count;
}

static int audio_cal_init_sysfs(struct audio_cal_drvdata *pAudioCalDrv)
{
	int ret = -EINVAL;
	struct audio_cal_private *priv = NULL;
	struct platform_device *pdev = NULL;

	pdev = (struct platform_device *)pAudioCalDrv->pdev;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "%s: memory alloc failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	priv->audio_cal_obj = NULL;
	priv->attr_group = devm_kzalloc(&pdev->dev,
				sizeof(*(priv->attr_group)),
				GFP_KERNEL);
	if (!priv->attr_group) {
		dev_err(&pdev->dev, "%s: malloc attr_group failed\n", __func__);
		ret = -ENOMEM;
		goto error1;
	}

	priv->attr_group->attrs = attrs;

	priv->audio_cal_obj = kobject_create_and_add("adsp_cal", kernel_kobj);
	if (!priv->audio_cal_obj) {
		dev_err(&pdev->dev, "%s: sysfs create and add failed\n", __func__);
		ret = -ENOMEM;
		goto error2;
	}

	ret = sysfs_create_group(priv->audio_cal_obj, priv->attr_group);
	if (ret) {
		dev_err(&pdev->dev, "%s: sysfs create group failed %d\n", __func__, ret);
		goto error3;
	}

	return 0;

error3:
	if (priv->audio_cal_obj) {
		sysfs_remove_group(priv->audio_cal_obj, priv->attr_group);
		kobject_del(priv->audio_cal_obj);
		priv->audio_cal_obj = NULL;
	}
error2:
	devm_kfree(&pdev->dev, priv->attr_group);
error1:
	devm_kfree(&pdev->dev, priv);

	return ret;
}

static int audio_cal_exit_sysfs(struct audio_cal_drvdata *pAudioCalDrv)
{
	struct audio_cal_private *priv = NULL;
	struct platform_device *pdev = NULL;

	pdev = (struct platform_device *)pAudioCalDrv->pdev;

	priv = platform_get_drvdata(pdev);
	if (!priv)
		return 0;

	if (priv->audio_cal_obj) {
		sysfs_remove_group(priv->audio_cal_obj, priv->attr_group);
		kobject_del(priv->audio_cal_obj);
		priv->audio_cal_obj = NULL;
	}
	devm_kfree(&pdev->dev, priv->attr_group);
	devm_kfree(&pdev->dev, priv);

	return 0;
}

static int audio_cal_remove(struct platform_device *pdev)
{
	audio_cal_exit_sysfs(gdrvdata);
	iounmap(gdrvdata->cal_virt_addr);
	iounmap(gdrvdata->pa_cal_virt_addr);
	devm_kfree(&pdev->dev, gdrvdata);
	gdrvdata = NULL;
	mutex_destroy(&audiocal_lock);

	return 0;
}

static int audio_cal_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *r;

	mutex_init(&audiocal_lock);

	mutex_lock(&audiocal_lock);

	/* create an environment to track the device */
	gdrvdata = devm_kzalloc(&pdev->dev, sizeof(struct audio_cal_drvdata),
							GFP_KERNEL);
	if (!gdrvdata) {
		ret = -ENOMEM;
		goto error1;
	}

	memset((void*)gdrvdata, 0, sizeof(struct audio_cal_drvdata));
	gdrvdata->cal_mode = AUDIO_CAL_MODE_NORMAL;
	gdrvdata->pdev = pdev;

	/* get calibration address */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "calibration_mem");
	if (!r) {
		pr_err("%s:platform_get_resource_byname(calibration_mem) fail\n", __func__);
		ret = ENOMEM;
		goto error1;
	}
	gdrvdata->cal_mem_addr = r->start;
	gdrvdata->cal_mem_size = resource_size(r);
	pr_debug("%s:calibration mem addr:0x%x, size:%d\n",
			__func__,
			gdrvdata->cal_mem_addr,
			gdrvdata->cal_mem_size);

	if ((gdrvdata->cal_mem_addr % (1UL << PAGE_SHIFT)) != 0) {
		pr_err("%s:calibration address must be %d bytes alignment\n",
			__func__, (1UL << PAGE_SHIFT));
		ret = ENOMEM;
		goto error1;
	}

	/* get smartPA calibration address*/
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pa_calibration_mem");
	if (!r) {
		pr_err("%s:platform_get_resource_byname(pa_calibration_mem) fail\n", __func__);
		ret = ENOMEM;
		goto error1;
	}
	gdrvdata->pa_cal_mem_addr = r->start;
	gdrvdata->pa_cal_mem_size = resource_size(r);
	pr_debug("%s:smaprtPA calibration mem addr:0x%x, size:%d\n",
			__func__,
			gdrvdata->pa_cal_mem_addr,
			gdrvdata->pa_cal_mem_size);

	/* init sys interface */
	ret = audio_cal_init_sysfs(gdrvdata);
	if (ret != 0) {
		pr_err("%s:failed to init audio calibration sysfs\n", __func__);
		goto error1;
	}

	/* map calibration memory to kernel space */
	gdrvdata->cal_virt_addr = ioremap_nocache(gdrvdata->cal_mem_addr, gdrvdata->cal_mem_size);
	if (gdrvdata->cal_virt_addr == NULL) {
		pr_err("%s:failed to ioremap adsp calibration address\n", __func__);
		ret = -1;
		goto error2;
	}
	memset_io((void *)gdrvdata->cal_virt_addr, 0x0, gdrvdata->cal_mem_size);

	/* map smartPA calibration memory to kernel space */
	gdrvdata->pa_cal_virt_addr = ioremap_nocache(gdrvdata->pa_cal_mem_addr, gdrvdata->pa_cal_mem_size);
	if (gdrvdata->pa_cal_virt_addr == NULL) {
		pr_err("%s:failed to ioremap smartPA calibration address\n", __func__);
		ret = -1;
		goto error2;
	}
	memset_io((void *)gdrvdata->pa_cal_virt_addr, 0x0, gdrvdata->pa_cal_mem_size);

	mutex_unlock(&audiocal_lock);

	pr_debug("%s:success\n", __func__);
	return 0;

error2:
	audio_cal_exit_sysfs(gdrvdata);
	if (gdrvdata->cal_virt_addr)
		iounmap(gdrvdata->cal_virt_addr);
	if (gdrvdata->pa_cal_virt_addr)
		iounmap(gdrvdata->pa_cal_virt_addr);

error1:
	devm_kfree(&pdev->dev, gdrvdata);
	mutex_unlock(&audiocal_lock);
	mutex_destroy(&audiocal_lock);
	gdrvdata = NULL;
	pr_err("%s:failed\n", __func__);
	return ret;
}

static const struct of_device_id audio_calibration_dt_match[] = {
	{ .compatible = "jlq,audio-calibration" },
	{ }
};
MODULE_DEVICE_TABLE(of, audio_calibration_dt_match);

static struct platform_driver audio_calibration_driver = {
	.driver = {
		.name = "jlq_audio_cal",
		.owner = THIS_MODULE,
		.of_match_table = audio_calibration_dt_match,
	},
	.probe = audio_cal_probe,
	.remove = audio_cal_remove,
};

static int __init audio_calibration_init(void)
{
	int ret;
	int cal_minor = 0;

	/* regsiter plaform driver */
	ret = platform_driver_register(&audio_calibration_driver);
	if (ret < 0)
		pr_err("register failed %d", ret);

	/* alloc char device number */
	ret = alloc_chrdev_region(&gdrvdata->devno, cal_minor, 1, "adsp_cal");
	if (ret < 0) {
		pr_err("%s: Failed to alloc device number\n", __func__);
		goto err1;
	}

	/* create char device */
	ret = cal_drv_setup_cdev(&gdrvdata->cdev, gdrvdata->devno);
	if (ret < 0) {
		pr_err("%s: Failed to setup adsp calibration char device\n", __func__);
		goto err2;
	}

	/* create class */
	gdrvdata->my_class = class_create(THIS_MODULE, "adsp_cal");
	if (IS_ERR(gdrvdata->my_class)) {
		pr_err("%s: Failed to create adsp_cal class\n", __func__);
		goto err3;
	}
	gdrvdata->my_class->devnode = get_cal_drv_devnode;

	/* create linux device */
	gdrvdata->dev =
		device_create(gdrvdata->my_class,
						NULL,
						gdrvdata->devno,
						NULL,
						"adsp_cal");
	if (IS_ERR(gdrvdata->dev)) {
		pr_err("%s: Failed to create adsp_cal device\n", __func__);
		goto err4;
	}

	return ret;

err4:
	class_destroy(gdrvdata->my_class);
err3:
	cal_drv_destroy_cdev(&gdrvdata->cdev);
err2:
	unregister_chrdev_region(gdrvdata->devno, 1);
err1:
	platform_driver_unregister(&audio_calibration_driver);
	return -1;
}

static void __exit audio_calibration_exit(void)
{
	platform_driver_unregister(&audio_calibration_driver);
	unregister_chrdev_region(gdrvdata->devno, 1);
	cal_drv_destroy_cdev(&gdrvdata->cdev);
	class_destroy(gdrvdata->my_class);
	device_destroy(gdrvdata->my_class, gdrvdata->devno);
}

module_init(audio_calibration_init);
module_exit(audio_calibration_exit);


MODULE_DESCRIPTION("JLQ audio calibration driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cooper Zheng");

