// SPDX-License-Identifier: GPL-2.0
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <uapi/linux/sched/types.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/syscalls.h>
#include <linux/of_irq.h>

#include "comms.h"
#include "nanohub.h"

#define READ_QUEUE_DEPTH       32//10
#define APP_FROM_HOST_EVENTID  0x000000F8
#define FIRST_SENSOR_EVENTID   0x00000200
#define LAST_SENSOR_EVENTID    0x000002FF
#define APP_TO_HOST_EVENTID    0x00000401
#define OS_LOG_EVENTID         0x3B474F4C
#define WAKEUP_INTERRUPT       1
#define WAKEUP_TIMEOUT_MS      5000
#define HAL_WAKEUP_TIMEOUT_MS  2000
#define SUSPEND_TIMEOUT_MS     5000
#define KTHREAD_ERR_TIME_NS    (60LL * NSEC_PER_SEC)
#define KTHREAD_ERR_CNT        70
#define KTHREAD_WARN_CNT       10
#define WAKEUP_ERR_TIME_NS     (60LL * NSEC_PER_SEC)
#define WAKEUP_ERR_CNT         4

#define SH_RST_FLAG1           0x1234
#define SH_RST_FLAG2           0xabcd
#define SSHUB_SIZE             (256 * 1024)

static struct class *sensor_class;
static int major;
static bool is_fw_load;

static int nanohub_open(struct inode *, struct file *);
static ssize_t nanohub_read(struct file *, char *, size_t, loff_t *);
static ssize_t nanohub_write(struct file *, const char *, size_t, loff_t *);
static unsigned int nanohub_poll(struct file *, poll_table *);
static int nanohub_release(struct inode *, struct file *);
static int nanohub_hw_reset(struct nanohub_data *data);

static const struct file_operations nanohub_fileops = {
	.owner = THIS_MODULE,
	.open = nanohub_open,
	.read = nanohub_read,
	.write = nanohub_write,
	.poll = nanohub_poll,
	.release = nanohub_release,
};

enum {
	ST_IDLE,
	ST_ERROR,
	ST_RUNNING
};

static inline void nanohub_notify_thread(struct nanohub_data *data)
{
	atomic_set(&data->kthread_run, 1);
	/* wake_up implementation works as memory barrier */
	wake_up_interruptible_sync(&data->kthread_wait);
}

static inline void nanohub_io_init(struct nanohub_io *io,
				   struct nanohub_data *data,
				   struct device *dev)
{
	init_waitqueue_head(&io->buf_wait);
	INIT_LIST_HEAD(&io->buf_list);
	io->data = data;
	io->dev = dev;
}

static inline bool nanohub_io_has_buf(struct nanohub_io *io)
{
	return !list_empty(&io->buf_list);
}

static struct nanohub_buf *nanohub_io_get_buf(struct nanohub_io *io,
					      bool wait)
{
	struct nanohub_buf *buf = NULL;
	int ret;

	spin_lock(&io->buf_wait.lock);
	if (wait) {
		ret = wait_event_interruptible_locked(io->buf_wait,
						      nanohub_io_has_buf(io));
		if (ret < 0) {
			spin_unlock(&io->buf_wait.lock);
			return ERR_PTR(ret);
		}
	}

	if (nanohub_io_has_buf(io)) {
		buf = list_first_entry(&io->buf_list, struct nanohub_buf, list);
		list_del(&buf->list);
	}
	spin_unlock(&io->buf_wait.lock);

	return buf;
}

static void nanohub_io_put_buf(struct nanohub_io *io,
			       struct nanohub_buf *buf)
{
	bool was_empty;

	spin_lock(&io->buf_wait.lock);
	was_empty = !nanohub_io_has_buf(io);
	list_add_tail(&buf->list, &io->buf_list);
	spin_unlock(&io->buf_wait.lock);

	if (was_empty) {
		if (&io->data->free_pool == io) {
			nanohub_notify_thread(io->data);
		} else {
			wake_up_interruptible(&io->buf_wait);
		}
	}
}

static ssize_t nanohub_app_info(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct nanohub_data *data = dev_get_nanohub_data(dev);
	struct {
		uint64_t appId;
		uint32_t appVer;
		uint32_t appSize;
	} __packed buffer;
	uint32_t i = 0;
	int ret;
	ssize_t len = 0;

	do {
		if (request_wakeup(data))
			return -ERESTARTSYS;

		if (nanohub_comms_tx_rx_retrans
		    (data, CMD_COMMS_QUERY_APP_INFO, (uint8_t *)&i,
		     sizeof(i), (u8 *)&buffer, sizeof(buffer),
		     false, 10, 10) == sizeof(buffer)) {
			ret =
			    scnprintf(buf + len, PAGE_SIZE - len,
				      "app: %d id: %016llx ver: %08x size: %08x\n",
				      i, buffer.appId, buffer.appVer,
				      buffer.appSize);
			if (ret > 0) {
				len += ret;
				i++;
			}
		} else {
			ret = -1;
		}
		release_wakeup(data);
	} while (ret > 0);

	return len;
}

static ssize_t nanohub_firmware_query(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct nanohub_data *data = dev_get_nanohub_data(dev);
	uint16_t buffer[6];

	if (request_wakeup(data))
		return -ERESTARTSYS;

	if (nanohub_comms_tx_rx_retrans
	    (data, CMD_COMMS_GET_OS_HW_VERSIONS, NULL, 0, (uint8_t *)&buffer,
	     sizeof(buffer), false, 10, 10) == sizeof(buffer)) {
		release_wakeup(data);
		return scnprintf(buf, PAGE_SIZE,
				 "hw type: %04x hw ver: %04x bl ver: %04x os ver: %04x variant ver: %08x\n",
				 buffer[0], buffer[1], buffer[2], buffer[3],
				 buffer[5] << 16 | buffer[4]);
	} else {
		release_wakeup(data);
		return 0;
	}
}

static ssize_t nanohub_download_app(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct nanohub_data *data = dev_get_nanohub_data(dev);
	const struct firmware *fw_entry;
	char buffer[70];
	int i, ret, ret1, ret2, file_len = 0, appid_len = 0, ver_len = 0;
	const char *appid = NULL, *ver = NULL;
	unsigned long version;
	uint64_t id;
	uint32_t cur_version;
	bool update = true;

	for (i = 0; i < count; i++) {
		if (buf[i] == ' ') {
			if (i + 1 != count) {
				if (appid == NULL)
					appid = buf + i + 1;
				else if (ver == NULL)
					ver = buf + i + 1;
				else
					break;
			} else
				break;
		} else if (buf[i] != '\n' && buf[i] != '\r') {
			if (ver)
				ver_len++;
			else if (appid)
				appid_len++;
			else
				file_len++;
		} else
			break;
	}

	if (file_len > 64 || appid_len > 16 || ver_len > 8 || file_len < 1)
		return -EIO;

	memcpy(buffer, buf, file_len);
	memcpy(buffer + file_len, ".napp", 5);
	buffer[file_len + 5] = '\0';

	ret = request_firmware(&fw_entry, buffer, dev);
	if (ret) {
		dev_err(dev, "%s(%s): err=%d\n", __func__, buffer, ret);
		return -EIO;
	}
	if (appid_len > 0 && ver_len > 0) {
		memcpy(buffer, appid, appid_len);
		buffer[appid_len] = '\0';

		ret1 = kstrtoull(buffer, 16, &id);

		memcpy(buffer, ver, ver_len);
		buffer[ver_len] = '\0';

		ret2 = kstrtoul(buffer, 16, &version);

		if (ret1 == 0 && ret2 == 0) {
			if (request_wakeup(data))
				return -ERESTARTSYS;
			if (nanohub_comms_tx_rx_retrans
			    (data, CMD_COMMS_GET_APP_VERSIONS,
			     (uint8_t *)&id, sizeof(id),
			     (uint8_t *)&cur_version,
			     sizeof(cur_version), false, 10,
			     10) == sizeof(cur_version)) {
				if (cur_version == version)
					update = false;
			}
			release_wakeup(data);
		}
	}

	if (update) {
		ret =
		    nanohub_comms_app_download(data, fw_entry->data,
					       fw_entry->size);
	}

	release_firmware(fw_entry);

	return count;
}

static inline bool nanohub_has_priority_lock_locked(struct nanohub_data *data)
{
	return  atomic_read(&data->wakeup_lock_cnt) >
		atomic_read(&data->wakeup_cnt);
}

static inline void mcu_wakeup_gpio_set_value(struct nanohub_data *data,
					     int val)
{
	const struct nanohub_platform_data *pdata = data->pdata;

	if (val != readl(pdata->top_mbox_base + MBOX_GEN_REG1)) {
		writel(val, pdata->top_mbox_base + MBOX_GEN_REG1);
		writel(pdata->wakelock_id, pdata->top_mbox_base + MBOX_CM4F_NINTR_SET);
	}
}

static inline void mcu_wakeup_gpio_get_locked(struct nanohub_data *data,
					      int priority_lock)
{
	atomic_inc(&data->wakeup_lock_cnt);
	if (!priority_lock && atomic_inc_return(&data->wakeup_cnt) == 1 &&
	    !nanohub_has_priority_lock_locked(data))
		mcu_wakeup_gpio_set_value(data, 0);
}

static inline bool mcu_wakeup_gpio_put_locked(struct nanohub_data *data,
					      int priority_lock)
{
	bool gpio_done = priority_lock ?
			 atomic_read(&data->wakeup_cnt) == 0 :
			 atomic_dec_and_test(&data->wakeup_cnt);
	bool done = atomic_dec_and_test(&data->wakeup_lock_cnt);

	if (!nanohub_has_priority_lock_locked(data))
		mcu_wakeup_gpio_set_value(data, gpio_done ? 1 : 0);

	return done;
}

static inline bool mcu_wakeup_gpio_is_locked(struct nanohub_data *data)
{
	return atomic_read(&data->wakeup_lock_cnt) != 0;
}

static inline bool mcu_wakeup_try_lock(struct nanohub_data *data, int key)
{
	int ret;
	/* implementation contains memory barrier */
	ret = atomic_cmpxchg(&data->wakeup_acquired, 0, key);

	return (ret == 0);
}

static inline void mcu_wakeup_unlock(struct nanohub_data *data, int key)
{
	WARN(atomic_cmpxchg(&data->wakeup_acquired, key, 0) != key,
	     "%s: failed to unlock with key %d; current state: %d",
	     __func__, key, atomic_read(&data->wakeup_acquired));
}

static inline void nanohub_set_state(struct nanohub_data *data, int state)
{
	atomic_set(&data->thread_state, state);
	smp_mb__after_atomic(); /* updated thread state is now visible */
}

static inline int nanohub_get_state(struct nanohub_data *data)
{
	smp_mb__before_atomic(); /* wait for all updates to finish */
	return atomic_read(&data->thread_state);
}

static inline void nanohub_clear_err_cnt(struct nanohub_data *data)
{
	data->kthread_err_cnt = data->wakeup_err_cnt = 0;
}

int request_wakeup_ex(struct nanohub_data *data, long timeout_ms,
		      int key, int lock_mode)
{
	long timeout;
	bool priority_lock = lock_mode > LOCK_MODE_NORMAL;
	struct device *sensor_dev = data->io[ID_NANOHUB_SENSOR].dev;
	int ret;
	ktime_t ktime_delta;
	ktime_t wakeup_ktime;

	spin_lock_irqsave(&data->wakeup_wait.lock, data->wakeup_flags);
	mcu_wakeup_gpio_get_locked(data, priority_lock);
	timeout = (timeout_ms != MAX_SCHEDULE_TIMEOUT) ?
		   msecs_to_jiffies(timeout_ms) :
		   MAX_SCHEDULE_TIMEOUT;

	if (!priority_lock && !data->wakeup_err_cnt)
		wakeup_ktime = ktime_get_boottime();

	timeout = wait_event_interruptible_timeout_locked(
			data->wakeup_wait,
			((priority_lock || nanohub_irq1_fired(data)) &&
			 mcu_wakeup_try_lock(data, key)),
			 timeout, data->wakeup_flags);
	if (timeout <= 0) {
		pr_info("nanohub: %s: dbg cnt:%d MBOX INTR:0x%x REG[0x%x 0x%x 0x%x] TO[%d] PRI[%d] WKCNT[%d %d]\n",
			__func__, atomic_read(&data->notify_cnt),
			readl(data->pdata->top_mbox_base + MBOX_CM42AP_NINTR_STA),
			readl(data->pdata->top_mbox_base + MBOX_GEN_REG1),
			readl(data->pdata->top_mbox_base + MBOX_GEN_REG2),
			readl(data->pdata->top_mbox_base + MBOX_GEN_REG3),
			timeout, priority_lock,
			atomic_read(&data->wakeup_cnt),
			atomic_read(&data->wakeup_lock_cnt));

		if (!timeout && !priority_lock) {
			if (!data->wakeup_err_cnt)
				data->wakeup_err_ktime = wakeup_ktime;
			ktime_delta = ktime_sub(ktime_get_boottime(),
						data->wakeup_err_ktime);
			data->wakeup_err_cnt++;
			if (ktime_to_ns(ktime_delta) > WAKEUP_ERR_TIME_NS
				&& data->wakeup_err_cnt > WAKEUP_ERR_CNT) {
				mcu_wakeup_gpio_put_locked(data, priority_lock);
				spin_unlock_irqrestore(&data->wakeup_wait.lock,
						       data->wakeup_flags);
				dev_info(sensor_dev,
					"wakeup: hard reset due to consistent error\n");
				ret = nanohub_hw_reset(data);
				if (ret) {
					dev_info(sensor_dev,
						"%s: failed to reset nanohub: ret=%d\n",
						__func__, ret);
				}
				return -ETIME;
			}
		}
		mcu_wakeup_gpio_put_locked(data, priority_lock);

		if (timeout == 0)
			timeout = -ETIME;
	} else {
		data->wakeup_err_cnt = 0;
		timeout = 0;
	}
	spin_unlock_irqrestore(&data->wakeup_wait.lock, data->wakeup_flags);

	return timeout;
}

void release_wakeup_ex(struct nanohub_data *data, int key, int lock_mode)
{
	bool done;
	bool priority_lock = lock_mode > LOCK_MODE_NORMAL;

	spin_lock_irqsave(&data->wakeup_wait.lock, data->wakeup_flags);
	done = mcu_wakeup_gpio_put_locked(data, priority_lock);
	mcu_wakeup_unlock(data, key);
	spin_unlock_irqrestore(&data->wakeup_wait.lock, data->wakeup_flags);

	if (!done)
		wake_up_interruptible_sync(&data->wakeup_wait);
	else if (nanohub_irq1_fired(data) || nanohub_irq2_fired(data))
		nanohub_notify_thread(data);
}

static int __nanohub_interrupt_cfg(struct nanohub_data *data,
				    u8 interrupt, bool mask)
{
	int ret;
	uint8_t mask_ret;
	int cnt = 10;
	struct device *dev = data->io[ID_NANOHUB_SENSOR].dev;
	int cmd = mask ? CMD_COMMS_MASK_INTR : CMD_COMMS_UNMASK_INTR;

	do {
		ret = request_wakeup_timeout(data, WAKEUP_TIMEOUT_MS);
		if (ret) {
			dev_err(dev,
				"%s: interrupt %d %s mask failed: ret=%d\n",
				__func__, interrupt, mask ? "" : "un", ret);
			return ret;
		}

		ret =
		    nanohub_comms_tx_rx_retrans(data, cmd,
						&interrupt, sizeof(interrupt),
						&mask_ret, sizeof(mask_ret),
						false, 10, 0);
		release_wakeup(data);
		dev_dbg(dev,
			"%smasking interrupt %d, ret=%d, mask_ret=%d\n",
			mask ? "" : "un",
			interrupt, ret, mask_ret);
	} while ((ret != 1 || mask_ret != 1) && --cnt > 0);

	return 0;
}

static inline int nanohub_mask_interrupt(struct nanohub_data *data,
					  u8 interrupt)
{
	return __nanohub_interrupt_cfg(data, interrupt, true);
}

static inline int nanohub_unmask_interrupt(struct nanohub_data *data,
					    u8 interrupt)
{
	return __nanohub_interrupt_cfg(data, interrupt, false);
}

static inline int nanohub_wakeup_lock(struct nanohub_data *data, int mode)
{
	int ret = 0;

	if (data->apnonwakeup)
		disable_irq(data->apnonwakeup);
	else
		ret = nanohub_mask_interrupt(data, 2);

	if (ret < 0)
		return ret;

	ret = request_wakeup_ex(data,
				mode == LOCK_MODE_SUSPEND_RESUME ?
				SUSPEND_TIMEOUT_MS : WAKEUP_TIMEOUT_MS,
				KEY_WAKEUP_LOCK, mode);
	if (ret < 0) {
		if (data->apnonwakeup)
			enable_irq(data->apnonwakeup);
		else
			nanohub_unmask_interrupt(data, 2);
		return ret;
	}
	if (mode != LOCK_MODE_SUSPEND_RESUME)
		disable_irq(data->apwakeup);

	atomic_set(&data->lock_mode, mode);
	mcu_wakeup_gpio_set_value(data, mode != LOCK_MODE_IO_BL);

	return 0;
}

/* returns lock mode used to perform this lock */
static inline int nanohub_wakeup_unlock(struct nanohub_data *data)
{
	int mode = atomic_read(&data->lock_mode);

	atomic_set(&data->lock_mode, LOCK_MODE_NONE);
	if (mode != LOCK_MODE_SUSPEND_RESUME)
		enable_irq(data->apwakeup);

	if (data->apnonwakeup)
		enable_irq(data->apnonwakeup);
	release_wakeup_ex(data, KEY_WAKEUP_LOCK, mode);
	if (!data->apnonwakeup)
		nanohub_unmask_interrupt(data, 2);
	nanohub_notify_thread(data);

	return mode;
}

static void sshub_hwcfg_reset(struct nanohub_data *data)
{
	/* cm4 gpios for sshub*/
	writel(data->pdata->cm4gpios | (data->pdata->cm4gpios << 16),
		data->pdata->cm4_gpio_base + CM4GPIO_INTR_MASK_CPU20);
	writel(data->pdata->cm4gpios,
		data->pdata->cm4_gpio_base + CM4GPIO_INTR_CLR0);
}

static void __nanohub_hw_reset(struct nanohub_data *data, int mode)
{
	if (mode == 1)
		writel(SH_RST_FLAG1,
		       data->pdata->top_mbox_base + MBOX_GEN_REG0);
	else if (mode == 2)
		writel(SH_RST_FLAG2,
		       data->pdata->top_mbox_base + MBOX_GEN_REG0);
}

static int nanohub_hw_reset(struct nanohub_data *data)
{
	int ret = 0;

	dev_info(data->dev, "%s: MBOX REG[0x%x 0x%x 0x%x]\n", __func__,
						readl(data->pdata->top_mbox_base + MBOX_GEN_REG1),
						readl(data->pdata->top_mbox_base + MBOX_GEN_REG2),
						readl(data->pdata->top_mbox_base + MBOX_GEN_REG3));

	dev_info(data->dev, "%s: wakeup_err_cnt=%d\n", __func__, data->wakeup_err_cnt);
	if (data->wakeup_err_cnt == 0)
		ret = nanohub_wakeup_lock(data, LOCK_MODE_RESET);
	sshub_hwcfg_reset(data);
	__nanohub_hw_reset(data, 1);
	memcpy_toio(data->dst_addr, data->fw_entry->data, data->fw_entry->size);
	__nanohub_hw_reset(data, 2);
	if (data->wakeup_err_cnt == 0)
		nanohub_wakeup_unlock(data);
	nanohub_clear_err_cnt(data);

	return ret;
}

static ssize_t nanohub_try_hw_reset(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct nanohub_data *data = dev_get_nanohub_data(dev);
	int ret = 0;

	dev_info(dev, "%s: load addr:0x%x\n", __func__, data->load_addr);
	if (!is_fw_load) {
		ret = request_firmware(&data->fw_entry, "sshub.bin", data->dev);
		if (ret)
			pr_err("nanohub: request_firmware failed; err=%d\n", ret);
		else
			is_fw_load = true;
	}

	if (is_fw_load)
		ret = nanohub_hw_reset(data);
	dev_info(dev, "%s: ret:%d out\n", __func__, ret);

	return ret < 0 ? ret : count;
}

static ssize_t nanohub_download_fw(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct nanohub_data *data = dev_get_nanohub_data(dev);
	int ret;

	dev_info(dev, "%s: in\n", __func__);
	ret = nanohub_hw_reset(data);
	dev_info(dev, "%s: ret:%d out\n", __func__, ret);

	return ret < 0 ? ret : count;
}

int nanohub_wait_for_interrupt(struct nanohub_data *data)
{
	int ret = -EFAULT;

	/* release the wakeup line, and wait for nanohub to send
	 * us an interrupt indicating the transaction completed.
	 */
	spin_lock_irqsave(&data->wakeup_wait.lock, data->wakeup_flags);
	if (mcu_wakeup_gpio_is_locked(data)) {
		mcu_wakeup_gpio_set_value(data, 1);
		ret = wait_event_interruptible_locked(data->wakeup_wait,
						      nanohub_irq1_fired(data));
		mcu_wakeup_gpio_set_value(data, 0);
	}
	spin_unlock_irqrestore(&data->wakeup_wait.lock, data->wakeup_flags);

	return ret;
}

int nanohub_wakeup_eom(struct nanohub_data *data, bool repeat)
{
	int ret = -EFAULT;

	spin_lock_irqsave(&data->wakeup_wait.lock, data->wakeup_flags);
	if (mcu_wakeup_gpio_is_locked(data)) {
		mcu_wakeup_gpio_set_value(data, 1);
		if (repeat)
			mcu_wakeup_gpio_set_value(data, 0);
		ret = 0;
	}
	spin_unlock_irqrestore(&data->wakeup_wait.lock, data->wakeup_flags);

	return ret;
}

static struct device_attribute attributes[] = {
	__ATTR(app_info, 0440, nanohub_app_info, NULL),
	__ATTR(firmware_version, 0440, nanohub_firmware_query, NULL),
	__ATTR(download_fw, 0220, NULL, nanohub_download_fw),
	__ATTR(download_app, 0220, NULL, nanohub_download_app),
	__ATTR(reset, 0220, NULL, nanohub_try_hw_reset),
};

static inline int nanohub_create_sensor(struct nanohub_data *data)
{
	int i, ret;
	struct device *sensor_dev = data->io[ID_NANOHUB_SENSOR].dev;

	for (i = 0, ret = 0; i < ARRAY_SIZE(attributes); i++) {
		ret = device_create_file(sensor_dev, &attributes[i]);
		if (ret) {
			dev_err(sensor_dev,
				"create sysfs attr %d [%s] failed; err=%d\n",
				i, attributes[i].attr.name, ret);
			goto fail_attr;
		}
	}

	goto done;

fail_attr:
	for (i--; i >= 0; i--)
		device_remove_file(sensor_dev, &attributes[i]);
done:
	return ret;
}

static int nanohub_create_devices(struct nanohub_data *data)
{
	int i, ret;
	static const char *names[ID_NANOHUB_MAX] = {
			"nanohub", "nanohub_comms"
	};

	for (i = 0; i < ID_NANOHUB_MAX; ++i) {
		struct nanohub_io *io = &data->io[i];

		nanohub_io_init(io, data, device_create(sensor_class, NULL,
							MKDEV(major, i),
							io, names[i]));
		if (IS_ERR(io->dev)) {
			ret = PTR_ERR(io->dev);
			pr_err("nanohub: device_create failed for %s; err=%d\n",
			       names[i], ret);
			goto fail_dev;
		}
	}
	ret = nanohub_create_sensor(data);
	if (!ret)
		goto done;

fail_dev:
	for (--i; i >= 0; --i)
		device_destroy(sensor_class, MKDEV(major, i));
done:
	return ret;
}

static int nanohub_match_devt(struct device *dev, const void *data)
{
	const dev_t *devt = data;

	return dev->devt == *devt;
}

static int nanohub_open(struct inode *inode, struct file *file)
{
	dev_t devt = inode->i_rdev;
	struct device *dev;

	dev = class_find_device(sensor_class, NULL, &devt, nanohub_match_devt);
	if (dev) {
		file->private_data = dev_get_drvdata(dev);
		nonseekable_open(inode, file);
		return 0;
	}

	return -ENODEV;
}

static ssize_t nanohub_read(struct file *file, char *buffer, size_t length,
			    loff_t *offset)
{
	struct nanohub_io *io = file->private_data;
	struct nanohub_data *data = io->data;
	struct nanohub_buf *buf;
	int ret;

	if (!nanohub_io_has_buf(io) && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	buf = nanohub_io_get_buf(io, true);
	if (IS_ERR_OR_NULL(buf))
		return PTR_ERR(buf);

	ret = copy_to_user(buffer, buf->buffer, buf->length);
	if (ret != 0)
		ret = -EFAULT;
	else
		ret = buf->length;

	nanohub_io_put_buf(&data->free_pool, buf);

	return ret;
}

static ssize_t nanohub_write(struct file *file, const char *buffer,
			     size_t length, loff_t *offset)
{
	struct nanohub_io *io = file->private_data;
	struct nanohub_data *data = io->data;
	int ret;

	ret = request_wakeup_timeout(data, HAL_WAKEUP_TIMEOUT_MS);
	if (ret < 0) {
		pr_info("%s: ret:%d\n", __func__, ret);
		return ret;
	}
	ret = nanohub_comms_write(data, buffer, length);

	release_wakeup(data);

	return ret;
}

static unsigned int nanohub_poll(struct file *file, poll_table *wait)
{
	struct nanohub_io *io = file->private_data;
	unsigned int mask = POLLOUT | POLLWRNORM;

	poll_wait(file, &io->buf_wait, wait);

	if (nanohub_io_has_buf(io))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static int nanohub_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static void nanohub_destroy_devices(struct nanohub_data *data)
{
	int i;
	struct device *sensor_dev = data->io[ID_NANOHUB_SENSOR].dev;

	sysfs_remove_link(&sensor_dev->kobj, "iio");
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(sensor_dev, &attributes[i]);
	for (i = 0; i < ID_NANOHUB_MAX; ++i)
		device_destroy(sensor_class, MKDEV(major, i));
}

static bool nanohub_os_log(char *buffer, int len)
{
	if (le32_to_cpu((((uint32_t *)buffer)[0]) & 0x7FFFFFFF) ==
	    OS_LOG_EVENTID) {
		char *mtype, *mdata = &buffer[5];

		buffer[len] = 0x00;

		switch (buffer[4]) {
		case 'E':
			mtype = KERN_ERR;
			break;
		case 'W':
			mtype = KERN_WARNING;
			break;
		case 'I':
			mtype = KERN_INFO;
			break;
		case 'D':
			mtype = KERN_DEBUG;
			break;
		default:
			mtype = KERN_DEFAULT;
			mdata--;
			break;
		}
		pr_info("%sNANOHUB-SH: %s", mtype, mdata);
		return true;
	} else {
		return false;
	}
}

static void nanohub_process_buffer(struct nanohub_data *data,
				   struct nanohub_buf **buf,
				   int ret)
{
	uint32_t event_id;
	uint8_t interrupt;
	struct nanohub_io *io = &data->io[ID_NANOHUB_SENSOR];

	data->kthread_err_cnt = 0;
	if (ret < 4 || nanohub_os_log((*buf)->buffer, ret)) {
		release_wakeup(data);
		return;
	}

	(*buf)->length = ret;

	event_id = le32_to_cpu((((uint32_t *)(*buf)->buffer)[0]) & 0x7FFFFFFF);
	if (ret >= sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t) &&
	    event_id > FIRST_SENSOR_EVENTID &&
	    event_id <= LAST_SENSOR_EVENTID) {
		interrupt = (*buf)->buffer[sizeof(uint32_t) +
					   sizeof(uint64_t) + 3];
	}

	if (event_id == APP_TO_HOST_EVENTID)
		io = &data->io[ID_NANOHUB_COMMS];

	nanohub_io_put_buf(io, *buf);

	*buf = NULL;

	release_wakeup(data);
}

static int nanohub_kthread(void *arg)
{
	struct nanohub_data *data = (struct nanohub_data *)arg;
	struct nanohub_buf *buf = NULL;
	int ret;
	ktime_t ktime_delta;
	uint32_t clear_interrupts[8] = { 0x00000006 };
	struct device *sensor_dev = data->io[ID_NANOHUB_SENSOR].dev;
	static const struct sched_param param = {
		.sched_priority = (MAX_USER_RT_PRIO/2)-1,
	};

	data->kthread_err_cnt = 0;
	sched_setscheduler(current, SCHED_FIFO, &param);
	nanohub_set_state(data, ST_IDLE);

	while (!kthread_should_stop()) {
		switch (nanohub_get_state(data)) {
		case ST_IDLE:
			wait_event_interruptible(data->kthread_wait,
						 atomic_read(&data->kthread_run)
						 );
			nanohub_set_state(data, ST_RUNNING);
			break;
		case ST_ERROR:
			ktime_delta = ktime_sub(ktime_get_boottime(),
						data->kthread_err_ktime);
			if (ktime_to_ns(ktime_delta) > KTHREAD_ERR_TIME_NS
				&& data->kthread_err_cnt > KTHREAD_ERR_CNT) {
				dev_info(sensor_dev,
					"kthread: hard reset due to consistent error\n");
				ret = nanohub_hw_reset(data);
				if (ret) {
					dev_info(sensor_dev,
						"%s: failed to reset nanohub: ret=%d\n",
						__func__, ret);
				}
			}
			msleep_interruptible(WAKEUP_TIMEOUT_MS);
			nanohub_set_state(data, ST_RUNNING);
			break;
		case ST_RUNNING:
			break;
		}
		atomic_set(&data->kthread_run, 0);
		if (!buf)
			buf = nanohub_io_get_buf(&data->free_pool, false);
		if (buf) {
			ret = request_wakeup_timeout(data, WAKEUP_TIMEOUT_MS);
			if (ret) {
				pr_info("%s: request_wakeup_timeout: ret=%d\n",
					 __func__, ret);
				continue;
			}

			ret = nanohub_comms_rx_retrans_boottime(
			    data, CMD_COMMS_READ, buf->buffer,
			    sizeof(buf->buffer), 10, 0);
			if (ret > 0) {
				nanohub_process_buffer(data, &buf, ret);
				if (!nanohub_irq1_fired(data) &&
				    !nanohub_irq2_fired(data)) {
					nanohub_set_state(data, ST_IDLE);
					continue;
				}
			} else if (ret == 0) {
				/* queue empty, go to sleep */
				data->kthread_err_cnt = 0;
				data->interrupts[0] &= ~0x00000006;

				release_wakeup(data);
				nanohub_set_state(data, ST_IDLE);
				continue;
			} else {
				release_wakeup(data);
				if (data->kthread_err_cnt == 0)
					data->kthread_err_ktime =
						ktime_get_boottime();

				data->kthread_err_cnt++;
				if (data->kthread_err_cnt >= KTHREAD_WARN_CNT) {
					dev_err(sensor_dev,
						"%s: kthread_err_cnt=%d\n",
						__func__,
						data->kthread_err_cnt);
					nanohub_set_state(data, ST_ERROR);
					continue;
				}
			}
		} else {
			if (!nanohub_irq1_fired(data) &&
			    !nanohub_irq2_fired(data)) {
				nanohub_set_state(data, ST_IDLE);
				continue;
			}
			if (request_wakeup(data))
				continue;

			nanohub_comms_tx_rx_retrans(data,
						    CMD_COMMS_CLR_GET_INTR,
						    (uint8_t *)
						    clear_interrupts,
						    sizeof(clear_interrupts),
						    (uint8_t *)data->interrupts,
						    sizeof(data->interrupts),
						    false, 10, 0);
			release_wakeup(data);
			nanohub_set_state(data, ST_IDLE);
		}
	}

	return 0;
}

static struct nanohub_platform_data *nanohub_parse_dt(struct device *dev)
{
	struct nanohub_platform_data *pdata;
	struct device_node *dt = dev->of_node;
	struct device_node *np;
	int ret;

	if (!dt)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	ret =
	of_property_read_u32(dt, "sensorhub,wakelock", &pdata->wakelock_id);
	if (ret < 0)
		pr_err("nanohub: missing sensorhub,wakelock in device tree\n");

	pdata->apwakeup_irq = of_irq_get_byname(dt, "apwakeup");
	if (pdata->apwakeup_irq < 0)
		pdata->apwakeup_irq = 0;
	pdata->apnonwakeup_irq = of_irq_get_byname(dt, "apnonwakeup");
	if (pdata->apnonwakeup_irq < 0)
		pdata->apnonwakeup_irq = 0;

	pdata->cm4_gpio_base = of_iomap(dt, 0);
	if (!pdata->cm4_gpio_base) {
		pr_err("%s: jlq,cm4_gpio_base is NULL\n", __func__);
		return ERR_PTR(-ENODEV);
	}
	ret =
	of_property_read_u32(dt, "sensorhub,cm4gpios", &pdata->cm4gpios);
	if (ret < 0)
		pr_err("nanohub: missing sensorhub,cm4gpios in device tree\n");

	np = of_find_compatible_node(NULL, NULL,
					"jlq,top-mailbox-base");
	if (!np) {
		pr_err("%s: No compatible node found\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	pdata->top_mbox_base = of_iomap(np, 0);
	if (!pdata->top_mbox_base) {
		pr_err("%s: jlq,top_mailbox_base is NULL\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	pdata->sh_power =  regulator_get(dev, "shpwr");
	if (IS_ERR(pdata->sh_power))
		dev_err(dev, "fail to get regulator\n");
	else {
		ret = regulator_enable(pdata->sh_power);
		if (ret)
			dev_err(dev, "fail to enable regulator\n");
	}

	of_node_put(np);

	return pdata;
}

static void nanohub_notify(void *priv, unsigned int flags)
{

	struct nanohub_data *data = (struct nanohub_data *)priv;

	spin_lock_irqsave(&data->notify_wait.lock, data->notify_flags);
	atomic_inc(&data->notify_cnt);
	spin_unlock_irqrestore(&data->notify_wait.lock, data->notify_flags);
	wake_up_interruptible_sync(&data->notify_wait);
}

static irqreturn_t nanohub_irq_wakeup(int irq, void *dev_id)
{
	struct nanohub_data *data = (struct nanohub_data *)dev_id;
	bool locked;

	spin_lock_irqsave(&data->wakeup_wait.lock, data->wakeup_flags);
	locked = mcu_wakeup_gpio_is_locked(data);
	spin_unlock_irqrestore(&data->wakeup_wait.lock, data->wakeup_flags);
	if (!locked)
		nanohub_notify_thread(data);
	else
		wake_up_interruptible_sync(&data->wakeup_wait);

	return IRQ_HANDLED;
}

static irqreturn_t nanohub_irq_nonwakeup(int irq, void *dev_id)
{
	pr_info("%s\n", __func__);

	return IRQ_HANDLED;
}

static int nanohub_request_irqs(struct nanohub_data *data)
{
	int ret;

	ret = bridge_name_open("SENSORHUB", 1, &data->ch, data,
				nanohub_notify);
	if (ret)
		pr_err("nanohub: SENSORHUB open bridge failed; err=%d\n", ret);

	ret = request_irq(data->apwakeup, nanohub_irq_wakeup,
					  0, "nanohub-apwakeup", data);
	if (ret)
		data->apwakeup = 0;
	else
		irq_set_irq_wake(data->apwakeup, 1);
	if (data->apnonwakeup <= 0 || ret < 0) {
		data->apnonwakeup = 0;
		return ret;
	}

	ret = request_irq(data->apnonwakeup, nanohub_irq_nonwakeup,
					  0, "nanohub-apnonwakeup", data);
	if (ret) {
		data->apnonwakeup = 0;
		WARN(1, "failed to request optional IRQ %d; err=%d",
		     data->apnonwakeup, ret);
	} else
		irq_set_irq_wake(data->apnonwakeup, 1);

	return 0;
}

static int nanohub_probe(struct platform_device *pdev)
{
	int ret, i;
	const struct nanohub_platform_data *pdata;
	struct nanohub_data *data;
	struct nanohub_buf *buf;

	data = kzalloc(sizeof(struct nanohub_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, (void *)data);
	data->dev = &pdev->dev;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		pdata = nanohub_parse_dt(&pdev->dev);
		if (!pdata) {
			ret = -ENOMEM;
			goto err1;
		}
	}

	data->pdata = pdata;
	init_waitqueue_head(&data->kthread_wait);
	init_waitqueue_head(&data->wakeup_wait);
	init_waitqueue_head(&data->notify_wait);

	nanohub_io_init(&data->free_pool, data, data->dev);

	buf = vmalloc(sizeof(*buf) * READ_QUEUE_DEPTH);
	data->vbuf = buf;
	if (!buf) {
		ret = -ENOMEM;
		goto err2;
	}

	for (i = 0; i < READ_QUEUE_DEPTH; i++)
		nanohub_io_put_buf(&data->free_pool, &buf[i]);

	atomic_set(&data->kthread_run, 0);
	atomic_set(&data->lock_mode, LOCK_MODE_NONE);
	atomic_set(&data->wakeup_cnt, 0);
	atomic_set(&data->wakeup_lock_cnt, 0);
	atomic_set(&data->wakeup_acquired, 0);
	atomic_set(&data->notify_cnt, 0);
	writel(1, data->pdata->top_mbox_base + MBOX_GEN_REG1);
	writel(1, data->pdata->top_mbox_base + MBOX_GEN_REG2);
	writel(1, data->pdata->top_mbox_base + MBOX_GEN_REG3);
	data->load_addr =
		(readl(data->pdata->top_mbox_base + MBOX_GEN_REG6) << 16) |
		readl(data->pdata->top_mbox_base + MBOX_GEN_REG7);
	data->dst_addr = ioremap_nocache(data->load_addr, SSHUB_SIZE);
	is_fw_load = false;
	data->seq = 1;
	data->wakeup_err_cnt = -1;

	data->apwakeup = data->pdata->apwakeup_irq;
	data->apnonwakeup = data->pdata->apnonwakeup_irq;
	if (data->apwakeup || data->apnonwakeup) {
		ret = nanohub_request_irqs(data);
		if (ret) {
			pr_err("nanohub:req irq  failed; err=%d\n", ret);
			goto err3;
		}
	}

	sensor_class = class_create(THIS_MODULE, "nanohub");
	if (IS_ERR(sensor_class)) {
		ret = PTR_ERR(sensor_class);
		pr_err("nanohub: class_create failed; err=%d\n", ret);
		goto err4;
	}
	major = __register_chrdev(0, 0, ID_NANOHUB_MAX, "nanohub",
				  &nanohub_fileops);
	if (major < 0) {
		ret = major;
		major = 0;
		pr_err("nanohub: can't register; err=%d\n", ret);
		goto err5;
	}

	ret = nanohub_create_devices(data);
	if (ret)
		goto err6;

	data->thread = kthread_run(nanohub_kthread, data, "nanohub");
	if (!data->thread) {
		ret = PTR_ERR(data->thread);
		goto err7;
	}

	//notify sensorhub that ap's nanohub is ok
	//writel(1, data->pdata->top_mbox_base + MBOX_GEN_REG7);

	pr_info("%s ok!\n", __func__);
	return 0;
err7:
	nanohub_destroy_devices(data);
err6:
	__unregister_chrdev(major, 0, ID_NANOHUB_MAX, "nanohub");
err5:
	class_destroy(sensor_class);
err4:
	bridge_release(data->ch);
err3:
	vfree(buf);
err2:
	kfree(pdata);
err1:
	kfree(data);

	return ret;
}

int nanohub_remove(struct platform_device *pdev)
{
	struct nanohub_data *data = dev_get_drvdata(&pdev->dev);

	kthread_stop(data->thread);
	nanohub_destroy_devices(data);
	__unregister_chrdev(major, 0, ID_NANOHUB_MAX, "nanohub");
	class_destroy(sensor_class);
	vfree(data->vbuf);
	bridge_release(data->ch);
	kfree(data->pdata);
	kfree(data);

	return 0;
}

static int nanohub_suspend(struct device *dev)
{
	struct nanohub_data *data = dev_get_drvdata(dev);

	pr_debug("%s!\n", __func__);
	nanohub_wakeup_lock(data, LOCK_MODE_SUSPEND_RESUME);
	return 0;
}

static int nanohub_resume(struct device *dev)
{
	struct nanohub_data *data = dev_get_drvdata(dev);

	pr_debug("%s!\n", __func__);
	nanohub_wakeup_unlock(data);
	return 0;
}

static const struct of_device_id nanohub_match_table[] = {
	{ .compatible = "jlq,nanohub", },
	{}
};

static SIMPLE_DEV_PM_OPS(nanohub_pm_ops, nanohub_suspend,
			 nanohub_resume);

static struct platform_driver nanohub_driver = {
	.probe = nanohub_probe,
	.remove = __exit_p(nanohub_remove),
	.driver = {
		.name = "nanohub",
		.owner = THIS_MODULE,
		.pm = &nanohub_pm_ops,
		.of_match_table = nanohub_match_table,
	},
};

static int __init nanohub_init(void)
{
	return platform_driver_register(&nanohub_driver);
}

static void __exit nanohub_exit(void)
{
	__unregister_chrdev(major, 0, ID_NANOHUB_MAX, "nanohub");
	class_destroy(sensor_class);
	major = 0;
	sensor_class = 0;

	platform_driver_unregister(&nanohub_driver);
}

module_init(nanohub_init);
module_exit(nanohub_exit);
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: qcom_pm8008-regulator jlq-regulator");
