/*
 * JLQ UREG user debug tool
 *
 * Copyright 2018~2019 JLQ Technology Co.,
 * Ltd. or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define UREG_HWREG_HEADER	"Demo:   cd /proc/dev_reg \r\n"
#define UREG_HWREG_READ		"echo \"r: 0x34510160 32\" > dev_reg \r\n"
#define UREG_HWREG_WRITE	"echo \"w: 0x34511300 0x12\" > dev_reg \r\n"
struct proc_dir_entry *ureg_dir;
struct devm_data {
	char *devm_ptr;
	char devm_buf_enable;
	struct mutex lock;
};
struct devm_data *dev_data;

enum REG_RW_TYPE
{
	REG_READ = 0,
	REG_WRITE = 1,
	REG_ERR	= 2,
};

static int ureg_hwreg_read(char *buf)
{
	unsigned long base, base_ptr;
	char *mem_base;
	char *mem_size = NULL;
	unsigned int intpart = 0;
	unsigned long size = 1024;
	void __iomem *vaddr, *vm_ptr;
	int offset = 0;
	int i = 0;

	mem_base = strim(buf);
	base = simple_strtoul(mem_base, &mem_size, 0);

	if (base > U32_MAX)
		return 0;

	mem_size = strim(mem_size);
	if (mem_size != NULL)
		size = simple_strtoul(mem_size, NULL, 0);

	if (base != 0 && size != 0)
		pr_err("read base:0x%lx size:0x%lx\n", 	base, size);
	else if (base != 0 && size == 0) {
		size = 4;
		pr_err("read base:0x%lx size:0x%lx\n",  base, size);
	} else {
		pr_err("set base error:0x%lx size:0x%lx\n",
					base, size);
		return 0;
	}

	if (size > 4096)
		size = 4096;

	size = ALIGN((size + 15), 16);

	vaddr = ioremap_nocache(base, size);
	if (!vaddr) {
		pr_err("ioremap for 0x%x with size 0x%x failed\n", base, size);
		return 0;
	}

	intpart = size / 16;
	i = 0;
	base_ptr = base;
	offset = 0;
	while (i < intpart) {
		vm_ptr = vaddr + offset;
		base_ptr = base + offset;
		pr_err("0x%08x:0x%08x 0x%08x 0x%08x 0x%08x\n", base_ptr,
			readl(vm_ptr + 0), readl(vm_ptr + 4),
			readl(vm_ptr + 8), readl(vm_ptr + 12));
		offset += 16;
		i++;
	}

	iounmap(vaddr);
	return 0;
}

static int ureg_hwreg_write(char *buf)
{
	char *mem_size = NULL;
	char *mem_base = buf;
	int addr = 0, value = 0;
	void *__iomem va;

	mem_base = strim(buf);
	addr = simple_strtoul(mem_base, &mem_size, 0);
	if (addr > U32_MAX)
		return 0;

	mem_size = strim(mem_size);
	if (mem_size != NULL)
		value = simple_strtoul(mem_size, NULL, 0);

	pr_err("write 0x%x with 0x%x\n", addr, value);

	va = ioremap(addr, PAGE_SIZE);
	if (!va) {
		pr_err("ioremap failed for 0x%x\n", addr);
		return 0;
	}

	writel(value, va);
	pr_err("write update 0x%x with 0x%x\n", addr, readl(va));

	iounmap(va);
	return 0;
}

enum REG_RW_TYPE ureg_rw(char *buf)
{
	if (buf == NULL)
		return -EINVAL;

	/* read or write ? */
	if (!strncmp(buf, "r:", 2))
		return REG_READ;
	else if (!strncmp(buf, "w:", 2))
		return REG_WRITE;
	else
		return -EINVAL;
}

static ssize_t ureg_hwreg_rw(const char __user *userbuf,
	size_t count, loff_t *ppos)
{
	char buf[128];
	char *str = NULL;
	int buf_size;

	if (count <= 3)
		return -EINVAL;

	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, userbuf, buf_size))
		return -EFAULT;

	buf[buf_size] = 0;
	str = buf + 2;

	switch (ureg_rw(buf)) {
	case REG_READ:
		ureg_hwreg_read(str);
		break;
	case REG_WRITE:
		ureg_hwreg_write(str);
		break;
	default:
		pr_err("input rw param error!\n");
		break;
	}

	return count;
}

static int ureg_proc_open_show(struct seq_file *m, void *v)
{
	seq_puts(m, UREG_HWREG_HEADER);
	seq_puts(m, UREG_HWREG_READ);
	seq_puts(m, UREG_HWREG_WRITE);
	return 0;
}

static int ureg_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ureg_proc_open_show, NULL);
}

static ssize_t ureg_proc_write(struct file *file,
        const char __user *buffer, size_t count, loff_t *pos)
{
	//pr_err("%s \n", __func__);
	ureg_hwreg_rw(buffer, count, pos);
	return count;
}


static const struct file_operations ureg_proc_fops = {
	.owner          = THIS_MODULE,
	.open           = ureg_proc_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
	.write          = ureg_proc_write,
};

static int devm_proc_open_show(struct seq_file *m, void *v)
{
	unsigned long base;
	char *mem_base;
	char *mem_size = NULL;
	unsigned long size = 1024;
	void __iomem *vaddr;
	int i = 0;

	mutex_lock(&dev_data->lock);
	if (dev_data->devm_buf_enable == 0) {
		seq_puts(m, "no addr input, demo: echo 34511300 32 > devm");
		goto out_no_buf;
	}
	//pr_err("devm config str :%s\n", dev_data->devm_ptr);
	mem_base = strim(dev_data->devm_ptr);
	if (mem_base != NULL) {
		base = simple_strtoul(mem_base, &mem_size, 0);
		if (base > U32_MAX) {
			seq_printf(m, "io excees 0x%x\n", U32_MAX);
			goto out;
		}
	} else {
		seq_puts(m, "no io base input\n");
		goto out;
	}

	mem_size = strim(mem_size);
	if (mem_size != NULL)
		size = simple_strtoul(mem_size, NULL, 0);
	else {
		seq_puts(m, "no size input\n");
		goto out;
	}

	if (size > 4096)
		size = 4096;
	size = ALIGN((size + 15), 16);
	vaddr = ioremap_nocache(base, size);
	if (!vaddr) {
		seq_printf(m, "ioremap failed to 0x%x\n", base);
		goto out;
	}

	for (i = 0; i < size; i += 4)
		seq_printf(m, "0x%x\n0x%x\n", base + i, readl(vaddr + i));

out:
	kfree(dev_data->devm_ptr);
out_no_buf:
	dev_data->devm_ptr = NULL;
	dev_data->devm_buf_enable = 0;
	mutex_unlock(&dev_data->lock);
	return 0;
}

static int devm_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, devm_proc_open_show, NULL);
}

static ssize_t devm_proc_write(struct file *file,
        const char __user *buffer, size_t count, loff_t *pos)
{
	mutex_lock(&dev_data->lock);
	if (!dev_data->devm_buf_enable)
		dev_data->devm_buf_enable = 1;
	else
		goto out;

	dev_data->devm_ptr = kzalloc(count, GFP_KERNEL);
	if (copy_from_user(dev_data->devm_ptr, buffer, count))
		goto out;

	//pr_err("devm write buffer %s\n", dev_data->devm_ptr);
out:
	mutex_unlock(&dev_data->lock);
	return count;
}
static const struct file_operations devm_proc_fops = {
	.owner          = THIS_MODULE,
	.open           = devm_proc_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
	.write          = devm_proc_write,
};

static int __init ureg_proc_init(void)
{
	ureg_dir = proc_mkdir("dev_reg", NULL);
	if (ureg_dir)
		proc_create("control", 0, ureg_dir, &ureg_proc_fops);
	if (ureg_dir)
		proc_create("devm", 0, ureg_dir, &devm_proc_fops);

	dev_data = kzalloc(sizeof(struct devm_data), GFP_KERNEL);
	mutex_init(&dev_data->lock);
	return 0;
}

static void __exit ureg_proc_exit(void)
{
	proc_remove(ureg_dir);
}

module_init(ureg_proc_init);
module_exit(ureg_proc_exit);
MODULE_LICENSE("GPL");


