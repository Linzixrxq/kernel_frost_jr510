// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019~2020 JLQ Technology Co., Ltd. or its affiliates.
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
#define pr_fmt(fmt) KBUILD_MODNAME "jlq_blklog:" fmt

#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/mfd/syscon.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/reset-controller.h>
#include <linux/bitops.h>
#include <linux/fs.h>
#include <linux/time64.h>
#include <linux/reboot.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include "jlq-blk-log.h"

#define MAX_ATTRS_SIZE (8)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
struct jlq_blklog_dev_s {
	struct device *dev;
	struct jlq_blklog_buffer_head_s *log_buffer;
	const char *blk_path;
	const char *logname;
	s32 blk_offset;
	unsigned int max_size;
	unsigned int log_size;
	unsigned int last_reboot_time;
	struct block_device *bdev;
	struct kobject *node_kobj;
	struct attribute_group attr_group;
	struct attribute *attrs[MAX_ATTRS_SIZE];
	struct bin_attribute *bin_attrs[MAX_ATTRS_SIZE];
	struct bin_attribute data_bin_attr;
	struct kobj_attribute  clean_attr;
	struct kobj_attribute  update_attr;
	struct kobj_attribute  status_attr;
	struct notifier_block reboot_nb;
};

static int
_jlq_blklog_head_read_check(struct jlq_blklog_dev_s *blklog_dev,
		struct jlq_blklog_buffer_head_s *head)
{
	if (!head)
		return -ENODATA;

	if (head->Sig != JLQFILELOGSIG && head->Sig != ~JLQFILELOGSIG)
		return -EINVAL;

	if (!(head->Flag & JLQFLOGFLAG_VALID))
		return -EINVAL;

	if (head->BufferMaxSize < 0x400 ||
			head->BufferMaxSize >= blklog_dev->max_size ||
			head->Pos > head->BufferMaxSize) {
		head->Flag &= ~JLQFLOGFLAG_FULL;
		head->OutPos = 0;
		head->Pos = 0;
		return -EINVAL;
	}
	return 0;
}

static unsigned int
_jlq_blklog_get_size(struct jlq_blklog_dev_s *blklog_dev,
		struct jlq_blklog_buffer_head_s *head)
{
	if (_jlq_blklog_head_read_check(blklog_dev, head))
		return 0;

	if (head->Flag & JLQFLOGFLAG_FULL)
		return head->BufferMaxSize;
	return head->Pos;
}

static int jlq_blklog_buffer_out(struct jlq_blklog_dev_s *blklog_dev,
	void *phead, unsigned long offset, char *buffer, ssize_t *len
)
{
	int ret = 0;
	unsigned long out_len = 0;
	unsigned long remain_len = 0;
	int copy_off;
	int copy_len;
	unsigned long valid_len;
	struct jlq_blklog_buffer_head_s *head =
		(struct jlq_blklog_buffer_head_s *)phead;

	ret = _jlq_blklog_head_read_check(blklog_dev, head);
	if (ret < 0)
		return ret;
	valid_len = head->Flag & JLQFLOGFLAG_FULL ? head->BufferMaxSize : head->Pos;
	if (offset >= valid_len)
		return -EINVAL;
	remain_len = MIN(valid_len - offset, *len);
	copy_off = head->Flag & JLQFLOGFLAG_FULL ? head->Pos : 0;
	copy_off += offset;
	if (copy_off > head->BufferMaxSize)
		copy_off -= head->BufferMaxSize;

	if (remain_len && copy_off >= head->Pos) {
		copy_len = MIN(head->BufferMaxSize - copy_off, remain_len);
		memcpy(buffer + out_len, head->Buffer + copy_off, copy_len);
		out_len += copy_len;
		remain_len -= copy_len;
		copy_off = 0;
	}
	if (remain_len && copy_off < head->Pos) {
		memcpy(buffer + out_len, head->Buffer + copy_off, remain_len);
		out_len += remain_len;
		remain_len = 0;
	}
	*len = out_len;
	return 0;
}

static struct page *page_read(struct address_space *mapping, int index)
{
	return read_mapping_page(mapping, index, NULL);
}

static int jlq_blklog_read_blk_data(
		struct jlq_blklog_dev_s *blklog_dev, int from, void *buffer, int len)
{
	struct page *page;
	int start = from + blklog_dev->blk_offset;
	int index = start >> PAGE_SHIFT;
	int offset = start & (PAGE_SIZE-1);
	int retlen = 0;
	int cpylen;

	while (len) {
		if ((offset + len) > PAGE_SIZE)
			cpylen = PAGE_SIZE - offset;
		else
			cpylen = len;
		len = len - cpylen;

		page = page_read(blklog_dev->bdev->bd_inode->i_mapping, index);
		if (IS_ERR(page))
			return PTR_ERR(page);

		memcpy(buffer, page_address(page) + offset, cpylen);
		put_page(page);
		retlen += cpylen;
		buffer += cpylen;
		offset = 0;
		index++;
	}
	return retlen;
}


static int jlq_blklog_save_blk_data(
		struct jlq_blklog_dev_s *blklog_dev, int to, void *buffer, int len)
{
	struct page *page;
	struct address_space *mapping = blklog_dev->bdev->bd_inode->i_mapping;
	int start = to + blklog_dev->blk_offset;
	int index = start >> PAGE_SHIFT;
	int offset = start & ~PAGE_MASK;
	int retlen = 0;
	int cpylen;

	while (len) {
		if ((offset+len) > PAGE_SIZE)
			cpylen = PAGE_SIZE - offset;
		else
			cpylen = len;
		len = len - cpylen;
		page = page_read(mapping, index);
		if (IS_ERR(page))
			return PTR_ERR(page);

		if (memcmp(page_address(page)+offset, buffer, cpylen)) {
			lock_page(page);
			memcpy(page_address(page) + offset, buffer, cpylen);
			set_page_dirty(page);
			unlock_page(page);
			balance_dirty_pages_ratelimited(mapping);
		}
		put_page(page);
		retlen += cpylen;
		buffer += cpylen;
		offset = 0;
		index++;
	}
	return retlen;
}


static int jlq_blklog_update_head(struct jlq_blklog_dev_s *blklog_dev)
{
	ssize_t wsize;

	wsize = jlq_blklog_save_blk_data(blklog_dev, 0, blklog_dev->log_buffer,
				sizeof(struct jlq_blklog_buffer_head_s));
	if (wsize != sizeof(struct jlq_blklog_buffer_head_s))
		return -EIO;
	return 0;
}

static const char devname[] = "/dev/jlq-blklog";
static int jlq_blklog_blk_init(struct jlq_blklog_dev_s *blklog_dev)
{
#ifndef MODULE
	dev_t devt;
#endif
	const fmode_t mode = FMODE_READ | FMODE_WRITE | FMODE_EXCL;
	struct block_device *bdev;

	/* Get a handle on the device */
	bdev = blkdev_get_by_path(blklog_dev->blk_path, mode, blklog_dev->dev);
#ifndef MODULE
	if (IS_ERR(bdev)) {
		/*
		* We might not have the root device mounted at this point.
		* Try to resolve the device name by other means.
		*/
		devt = name_to_dev_t(blklog_dev->blk_path);
		if (!devt) {
//			dev_err(blklog_dev->dev,
//				"devt error: cannot find device %s\n", blklog_dev->blk_path);
			return -EPROBE_DEFER;
		}
		bdev = blkdev_get_by_dev(devt, mode, blklog_dev->dev);
	}
#endif
	if (IS_ERR(bdev)) {
		dev_err(blklog_dev->dev,
			"bdev error: cannot open device %s\n", blklog_dev->blk_path);
		return -ENODEV;
	}
	blklog_dev->bdev = bdev;
	return 0;
}

static int jlq_blklog_update_reboot_time(struct jlq_blklog_dev_s *blklog_dev)
{
	struct jlq_blklog_buffer_head_s *head = blklog_dev->log_buffer;
	unsigned int reboot_time;
	int ret;
	long tm_year = 0;
	time64_t second = ktime_get_real_seconds();
	struct tm tm;

	time64_to_tm(second, -sys_tz.tz_minuteswest * 60, &tm);
	ret = _jlq_blklog_head_read_check(blklog_dev, head);
	if (ret < 0)
		return ret;
	if (tm.tm_year + 1900 > 2000)
		tm_year = tm.tm_year + 1900 - 2000;

	reboot_time = JLQ_BLKLOG_TIME_SET(tm_year, tm.tm_mon + 1,
			tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	dev_info(blklog_dev->dev, "update reboot time:%04d-%d-%d %02d:%02d:%02d\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	head->LastRebootTime = reboot_time;
	return jlq_blklog_update_head(blklog_dev);
}

static int jlq_blklog_clean(struct jlq_blklog_dev_s *blklog_dev)
{
	struct jlq_blklog_buffer_head_s *head = blklog_dev->log_buffer;
	int ret;

	ret = _jlq_blklog_head_read_check(blklog_dev, head);
	if (ret < 0)
		return ret;
	head->Sig = JLQFILELOGSIG;
	head->Flag = JLQFLOGFLAG_VALID;
	head->OutPos = 0;
	head->Pos = 0;
	return jlq_blklog_update_head(blklog_dev);
}

static int jlq_blklog_reboot_notifier(struct notifier_block *this,
	unsigned long code, void *unused)
{
	struct jlq_blklog_dev_s *blklog_dev =
		container_of(this, struct jlq_blklog_dev_s, reboot_nb);

	jlq_blklog_update_reboot_time(blklog_dev);
	sync_blockdev(blklog_dev->bdev);

	blklog_dev->log_buffer->Sig = 0;
	dev_dbg(blklog_dev->dev, "update reboot time done.\n");
	return NOTIFY_DONE;
}

static int jlq_blklog_get_dt(struct jlq_blklog_dev_s *blklog_dev, struct device *dev)
{
	struct device_node *np = dev->of_node;

	if (!np) {
		dev_err(dev, "no of node\n");
		return -ENODEV;
	}
	if (of_property_read_string(np, "jlq,blk-dev", &blklog_dev->blk_path) < 0) {
		dev_info(dev, "no blk dev path\n");
		return -EINVAL;
	}
	blklog_dev->blk_offset = 0;
	of_property_read_s32(np, "jlq,blklog-offset", &blklog_dev->blk_offset);
	blklog_dev->max_size = JLQ_BLKLOG_MAXSIZE;
	of_property_read_s32(np, "jlq,blklog-maxsize", &blklog_dev->max_size);
	dev_info(dev, "Path:%s blk_offset:%x max_size:%x",
		blklog_dev->blk_path, blklog_dev->blk_offset, blklog_dev->max_size);
	return 0;
}

static int jlq_blklog_data_init(struct jlq_blklog_dev_s *blklog_dev)
{
	struct jlq_blklog_buffer_head_s tblklog_d;
	int ret;
	int buffer_size;

	ret = jlq_blklog_read_blk_data(blklog_dev, 0,
			&tblklog_d, sizeof(struct jlq_blklog_buffer_head_s));
	if (ret <= 0)
		return -EINVAL;
	ret = _jlq_blklog_head_read_check(blklog_dev, &tblklog_d);
	if (ret < 0)
		return ret;
	blklog_dev->log_size = _jlq_blklog_get_size(blklog_dev, &tblklog_d);
	blklog_dev->last_reboot_time = tblklog_d.LastRebootTime;
	buffer_size = tblklog_d.BufferMaxSize + sizeof(struct jlq_blklog_buffer_head_s);
	blklog_dev->log_buffer = devm_kzalloc(blklog_dev->dev, buffer_size, GFP_KERNEL);
	if (!blklog_dev->log_buffer)
		return -ENOMEM;
	return jlq_blklog_read_blk_data(blklog_dev, 0, blklog_dev->log_buffer, buffer_size);
}

static ssize_t jlq_blklog_logdump_read(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	struct jlq_blklog_dev_s *blklog_dev =
			container_of(bin_attr, struct jlq_blklog_dev_s, data_bin_attr);
	ssize_t len = count;
	int ret;

	ret = jlq_blklog_buffer_out(blklog_dev, blklog_dev->log_buffer, off, buf, &len);
	if (ret < 0)
		return ret;
	return len;
}

static long log_clean_store(struct kobject *kobj, struct kobj_attribute *attr,
		  const char *buf, unsigned long count)
{
	int ret;
	int enable;
	struct jlq_blklog_dev_s *blklog_dev =
			container_of(attr, struct jlq_blklog_dev_s, clean_attr);

	if (!blklog_dev)
		return 0;

	ret = kstrtoint(buf, 0, &enable);
	if (ret < 0) {
		ret = -EINVAL;
		return ret;
	}
	if (enable)
		jlq_blklog_clean(blklog_dev);
	return count;
}

static long time_update_store(struct kobject *kobj, struct kobj_attribute *attr,
		  const char *buf, unsigned long count)
{
	int ret;
	int enable;
	struct jlq_blklog_dev_s *blklog_dev =
			container_of(attr, struct jlq_blklog_dev_s, update_attr);

	if (!blklog_dev)
		return 0;

	ret = kstrtoint(buf, 0, &enable);
	if (ret < 0) {
		ret = -EINVAL;
		return ret;
	}
	if (enable)
		jlq_blklog_update_reboot_time(blklog_dev);
	return count;
}

static long status_show(struct kobject *kobj,
		 struct kobj_attribute *attr,
		 char *buf)
{
	int len = 0;
	struct jlq_blklog_dev_s *blklog_dev =
			container_of(attr, struct jlq_blklog_dev_s, status_attr);

	if (!blklog_dev)
		return 0;
	len = sprintf(buf + len, "blkpath:%s [0x%lx + 0x%lx]\r\n",
		blklog_dev->blk_path, blklog_dev->blk_offset, blklog_dev->max_size);
	len += sprintf(buf + len, "reboot Time: %04d-%d-%d %02d:%02d:%02d\r\n",
			JLQ_BLKLOG_YEAR(blklog_dev->log_buffer->LastRebootTime),
			JLQ_BLKLOG_MON(blklog_dev->log_buffer->LastRebootTime),
			JLQ_BLKLOG_DAY(blklog_dev->log_buffer->LastRebootTime),
			JLQ_BLKLOG_HOUR(blklog_dev->log_buffer->LastRebootTime),
			JLQ_BLKLOG_MIN(blklog_dev->log_buffer->LastRebootTime),
			JLQ_BLKLOG_SEC(blklog_dev->log_buffer->LastRebootTime));
	len += sprintf(buf + len, "last reboot Time: %04d-%d-%d %02d:%02d:%02d\r\n",
			JLQ_BLKLOG_YEAR(blklog_dev->last_reboot_time),
			JLQ_BLKLOG_MON(blklog_dev->last_reboot_time),
			JLQ_BLKLOG_DAY(blklog_dev->last_reboot_time),
			JLQ_BLKLOG_HOUR(blklog_dev->last_reboot_time),
			JLQ_BLKLOG_MIN(blklog_dev->last_reboot_time),
			JLQ_BLKLOG_SEC(blklog_dev->last_reboot_time));
	len += sprintf(buf + len, "pos:0x%x log_size:%x\r\n",
			blklog_dev->log_buffer->Pos, blklog_dev->log_size);
	return len;
}

static int jlq_blklog_binattr_init(struct jlq_blklog_dev_s *blklog_dev)
{
	struct bin_attribute *temp_binattr = NULL;
	int index = 0;

	temp_binattr = &blklog_dev->data_bin_attr;
	temp_binattr->attr.name = "logdump";
	temp_binattr->attr.mode = 0444;
	temp_binattr->read = jlq_blklog_logdump_read;
	temp_binattr->size =  blklog_dev->log_size;
	sysfs_bin_attr_init(temp_binattr);
	blklog_dev->bin_attrs[index++] = temp_binattr;

	blklog_dev->bin_attrs[index++] = NULL;
	return 0;
}

static int jlq_blklog_attr_init(struct jlq_blklog_dev_s *blklog_dev)
{
	struct kobj_attribute *temp_attr = NULL;
	int index = 0;

	temp_attr = &blklog_dev->clean_attr;
	temp_attr->attr.name = "clean";
	temp_attr->attr.mode = 0222;
	temp_attr->store = log_clean_store;
	sysfs_attr_init(&temp_attr->attr);
	blklog_dev->attrs[index++] = &temp_attr->attr;

	temp_attr = &blklog_dev->update_attr;
	temp_attr->attr.name = "update";
	temp_attr->attr.mode = 0222;
	temp_attr->store = time_update_store;
	sysfs_attr_init(&temp_attr->attr);
	blklog_dev->attrs[index++] = &temp_attr->attr;

	temp_attr = &blklog_dev->status_attr;
	temp_attr->attr.name = "status";
	temp_attr->attr.mode = 0444;
	temp_attr->show = status_show;
	sysfs_attr_init(&temp_attr->attr);
	blklog_dev->attrs[index++] = &temp_attr->attr;

	blklog_dev->attrs[index++] = NULL;
	return 0;
}

static int jlq_blklog_sysfs_init(struct jlq_blklog_dev_s *blklog_dev)
{
	struct bin_attribute *temp_binattr = NULL;
	int i;

	blklog_dev->node_kobj = kobject_create_and_add(blklog_dev->logname, kernel_kobj);
	if (!blklog_dev->node_kobj) {
		dev_err(blklog_dev->dev, "Creat kobj(%s) Failed.\n", blklog_dev->logname);
		return -ENODEV;
	}
	sysfs_create_files(blklog_dev->node_kobj,
			(const struct attribute *const *)blklog_dev->attrs);
	for (i = 0; i < ARRAY_SIZE(blklog_dev->bin_attrs); i++) {
		temp_binattr = blklog_dev->bin_attrs[i];
		if (temp_binattr)
			sysfs_create_bin_file(blklog_dev->node_kobj, temp_binattr);
		else
			break;
	}
	return 0;
}

static int jlq_blklog_probe(struct platform_device *pdev)
{
	struct jlq_blklog_dev_s *blklog_dev = NULL;
	int ret;

	blklog_dev = devm_kzalloc(&pdev->dev, sizeof(struct jlq_blklog_dev_s), GFP_KERNEL);
	if (!blklog_dev)
		return -ENOMEM;

	blklog_dev->dev = &pdev->dev;
	ret = jlq_blklog_get_dt(blklog_dev, &pdev->dev);
	if (ret < 0) {
		dev_err(blklog_dev->dev, "jlq_blklog_get_dt ret %d\n", ret);
		return -EINVAL;
	}
	blklog_dev->logname =
			(const char *)(uintptr_t)of_device_get_match_data(&pdev->dev);
	ret = jlq_blklog_blk_init(blklog_dev);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(blklog_dev->dev,
				"jlq_blklog_blk_init ret %d\n", ret);
		else
			dev_info(blklog_dev->dev, "Retry\n");
		return ret;
	}
	ret = jlq_blklog_data_init(blklog_dev);
	if (ret < 0) {
		dev_err(blklog_dev->dev, "jlq_blklog_data_init ret %d\n", ret);
		return ret;
	}
	dev_set_drvdata(&pdev->dev, blklog_dev);
	ret = jlq_blklog_binattr_init(blklog_dev);
	if (ret < 0) {
		dev_err(blklog_dev->dev, "jlq_blklog_binattr_init ret %d\n", ret);
		return ret;
	}
	ret = jlq_blklog_attr_init(blklog_dev);
	if (ret < 0) {
		dev_err(blklog_dev->dev, "jlq_blklog_attr_init ret %d\n", ret);
		return ret;
	}
	ret = jlq_blklog_sysfs_init(blklog_dev);
	if (ret < 0) {
		dev_err(blklog_dev->dev, "jlq_blklog_sysfs_init ret %d\n", ret);
		return ret;
	}
	blklog_dev->reboot_nb.notifier_call = jlq_blklog_reboot_notifier;
	blklog_dev->reboot_nb.priority = INT_MAX - 1;
	register_reboot_notifier(&blklog_dev->reboot_nb);
	return 0;
}


static const struct of_device_id jlq_blklog_match[] = {
	{ .compatible = "jlq,blk-log-loader", .data = (void *)"loader-log"},
	{},
};

MODULE_DEVICE_TABLE(of, jlq_blklog_match);

static struct platform_driver jlq_blklog_driver = {
	.probe = jlq_blklog_probe,
	.driver = {
		.name = "jlq-blk-log",
		.of_match_table = jlq_blklog_match,
	},
//	.shutdown = jlq_blklog_shutdown,
};

static int __init jlq_blklog_init(void)
{
	return platform_driver_register(&jlq_blklog_driver);
}
late_initcall(jlq_blklog_init);

MODULE_SOFTDEP("pre: block_dev sdhci_jlq");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jlq-blk-log");
MODULE_DESCRIPTION("JLQ Block Log Driver");
MODULE_AUTHOR("QiweiChen <QiweiChen@jlq.com>");
MODULE_VERSION("V0.1");

