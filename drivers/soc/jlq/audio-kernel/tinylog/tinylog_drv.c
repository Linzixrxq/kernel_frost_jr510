// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 JLQ Corporation
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
#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/timer.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>
#include <linux/firmware.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/rtc.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <ipc/apr_tal.h>
#include <ipc/apr.h>
#include "tinylog_controller.h"
#include "tinylog_drv.h"
#include "tinylog_psy.h"

#define TL_BIT_FIELD(len)                   ((1 << (len)) - 1)
#define TL_GET_BIT_FIELD(val, bit, len)     (((val) >> (bit)) & (TL_BIT_FIELD(len)))
#define TL_SET_BIT_FIELD(val, op, bit, len) \
		((val) & (~(TL_BIT_FIELD(len) << (bit))) | ((unsigned int)(op) << (bit)))

#define ADSP_DUMP_POINT_RB_DEPTH  1*1024*1024

struct jlq_log_private *log_priv = NULL;

#define MAX_NUM_DEVICE   2

#define LOG_APP_DEVICE_NAME   "adsp_log/log_app"
#define LOG_POINT_DEVICE_NAME "adsp_log/log_point"
#define LOG_APP_DEVICE_TYPE         0
#define LOG_POINT_DEVICE_TYPE       1

#define LOG_LEAST_CPY_SIZE          16 // this is limitation of copy_to_user

#define LOG_STRING_MAX_SIZE         512

#define SYSCNT_SNAP1_LO             0x438
#define SYSCNT_SNAP1_HI             0x43C
#define SYSCNT_SNAP_CTRL            0x42C
#define SYS_COUNTER_CLOCK_KHZ       (19200)

#define LOG_TIME_BJ_ZONE_OFFSET     (28800) // 8 Hours

#ifndef MAX
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif

enum {
	SNAPSHOT_0 = 0,
	SNAPSHOT_1 = 1,
	SNAPSHOT_2 = 2,
};

/* 2021-10-19 17:23:22.192855, strlen is 26, 1 is the string end char */
#define LOG_TIME_STRING_SIZE         (26 + 1)

/*strlen of module name is 12, 1 is the string end char '\0'*/
#define LOG_MODULE_NAME_STRING_SIZE  (12 + 1)

/* time buffer */
static char log_time_buf[LOG_TIME_STRING_SIZE];

/* log driver name, strlen must be LOG_MODULE_NAME_STRING_SIZE*/
static char *ap_log_module_name = "tl_drv      ";

/* log level char */
static char log_level_buf[LEVEL_MAX] = {'N', 'E', 'I', 'D'};

struct tinylog_module_mapping
{
	uint32_t module_id;
	char *p_module_name;
};

static struct tinylog_module_mapping adsp_log_module_name_table[] =
{
	{0,  "comm        "},
	{1,  "platform    "},
	{2,  "voice_svc   "},
	{3,  "voice_tx_prc"},
	{4,  "voice_rx_prc"},
	{5,  "voice_enc   "},
	{6,  "voice_dec   "},
	{7,  "avcore      "},
	{8,  "msg_adapter "},
	{9,  "VT          "},
	{10, "playback    "},
	{11, "record      "},
	{12, "FM          "},
	{13, "AFE         "},
	{14, "RESERVED    "},
};

struct tinylog_drv_dev
{
	int is_open;
	dev_t devno;
	char *name;
	int device_type;
	int is_version_print;
	struct class *my_class;
	struct device *dev;
	struct cdev cdev;
	struct jlq_log_private *p_log_priv;
	struct mutex mutex;
	wait_queue_head_t waitq;
};

static struct tinylog_drv_dev tinylog_dev[MAX_NUM_DEVICE];

static inline u32 tinylog_read32(u64 addr)
{
	return readl((void *)addr);
}

static inline void tinylog_write32(u64 addr, u32 v)
{
	writel(v, (void *)addr);
}

static int tinylog_drv_open(struct inode *inode, struct file *filp)
{
	struct tinylog_drv_dev *dev;

	dev = container_of(inode->i_cdev, struct tinylog_drv_dev, cdev);
	if (dev->is_open)
		return -EBUSY;

	mutex_lock(&dev->mutex);

	filp->private_data = dev;
	dev->is_open = 1;
	dev->is_version_print = 0;

	mutex_unlock(&dev->mutex);

	pr_err("%s: device %s is open\n", __func__, dev->name);
	return 0;
}

static int tinylog_drv_release(struct inode *inode, struct file *filp)
{
	int ret;
	int i;
	struct tinylog_drv_dev *dev;
	struct app_info *app;
	struct point_info *point_info;

	dev = filp->private_data;
	app = &dev->p_log_priv->jlq_app_info;
	point_info = &dev->p_log_priv->jlq_point_info;

	mutex_lock(&dev->mutex);

	if (dev->device_type == LOG_APP_DEVICE_TYPE) {
		ret = dev->p_log_priv->log_ctrl_ops->log_app_close(dev->p_log_priv, AUDIO_FW_ALGORITHM);
		if (ret < 0) {
			pr_err("%s: jlq log app close error, ret = %d.\n", __func__, ret);
			mutex_unlock(&dev->mutex);
			return -EBUSY;
		}
		if (app->ex_virt_buf != NULL) {
			iounmap(app->ex_virt_buf);
			app->ex_virt_buf = NULL;
		}
		if (app->string_img_virt_buf[AUDIO_FW_ALGORITHM] != NULL) {
			iounmap(app->string_img_virt_buf[AUDIO_FW_ALGORITHM]);
			app->string_img_virt_buf[AUDIO_FW_ALGORITHM] = NULL;
		}
		app->app_state[AUDIO_FW_ALGORITHM] = STOPPED;
	} else {
		ret = dev->p_log_priv->log_ctrl_ops->log_point_close_all(dev->p_log_priv);
		if (ret < 0) {
			pr_err("%s: jlq log point close error, ret = %d.\n", __func__, ret);
			mutex_unlock(&dev->mutex);
			return -EBUSY;
		}
		if (point_info->ex_virt_buf != NULL) {
			iounmap(point_info->ex_virt_buf);
			point_info->ex_virt_buf = NULL;
		}
		for (i = 0; i < POINT_MAX; i++) {
			point_info->point_state[i] = STOPPED;
		}
	}

	dev->is_open = 0;

	mutex_unlock(&dev->mutex);

	pr_err("%s: device %s is closed\n", __func__, dev->name);
	return 0;
}

static int tinylog_drv_app_read(struct tinylog_drv_dev *dev, char __user *buf, size_t count)
{
	struct log_app_ring_buf *log_app_rb = NULL;
	uint32_t real_read_index = 0;
	struct log_app_message *curr_log_message = NULL;
	uint32_t num_values = 0;
	uint32_t module_id = 0;
	uint32_t log_level = 0;
	uint32_t value1 = 0;
	uint32_t value2 = 0;
	uint32_t value3 = 0;
	uint32_t value4 = 0;
	uint8_t *curr_log_string = NULL;
	uint32_t out_cnt = 0;
	uint32_t log_offset = 0;
	uint32_t i = 0;
	struct rtc_time tm;
	struct timespec ts;
	uint8_t *adsp_string = dev->p_log_priv->adsp_string;
	uint8_t *log_string = dev->p_log_priv->log_string;
	uint8_t *message_string = dev->p_log_priv->message_string;
	struct app_info *app = &dev->p_log_priv->jlq_app_info;
	uint32_t temp32;
	uint32_t proto_error = 0;
	
	log_app_rb = (struct log_app_ring_buf *)app->ex_virt_buf;
	if (!log_app_rb) {
		pr_err("%s: ring buffer is null.\n", __func__);
		return 0;
	}

	if ((adsp_string == NULL) || (log_string == NULL) || (message_string == NULL)) {
		pr_err("%s: string mem is NULL.\n", __func__);
		return 0;
	}

	if (dev->is_version_print == 0) {
		dev->is_version_print = 1;
		for (i = 0; i < APP_ID_MAX; i++) {
			if (dev->p_log_priv->jlq_app_info.version_addr[i]) {
				curr_log_string = (uint8_t *)app->string_img_virt_buf[i] + (dev->p_log_priv->jlq_app_info.version_addr[i] & 0x0000FFFFUL);
				if (curr_log_string != NULL) {
					pr_info("%s: %s\n", __func__, curr_log_string);
					getnstimeofday(&ts);
					rtc_time64_to_tm(LOG_TIME_BJ_ZONE_OFFSET + ts.tv_sec + ts.tv_nsec / 1000000000, &tm);
					memset(log_time_buf, 0, sizeof(log_time_buf));
					memset(log_string, 0, LOG_STRING_MAX_SIZE);
					temp32 = ts.tv_nsec % 1000000000 / 1000;
					snprintf(log_time_buf, sizeof(log_time_buf),"%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.6d",
						(tm.tm_year + 1900),
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						temp32);
					snprintf(log_string, LOG_STRING_MAX_SIZE, "%s %s %c [tl_drv]: %s\n",
						log_time_buf,
						ap_log_module_name,
						log_level_buf[LEVEL_INFO],
						curr_log_string);
					log_string[LOG_STRING_MAX_SIZE - 1] = '\0';
					log_string[LOG_STRING_MAX_SIZE - 2] = '\n';
					if (count >= strlen(log_string) + out_cnt) {
						if (copy_to_user(buf + out_cnt, log_string, strlen(log_string))) {
							pr_err("%s: Failed to copy data to user for log app lost\n", __func__);
							return 0;
						}
						out_cnt += strlen(log_string);
					} else {
						pr_err("%s: provided buf size(%d) is too small for log app version\n", __func__, count);;
						return out_cnt;
					}
				}
			}
		}
	}

	if (log_app_rb->lost != dev->p_log_priv->message_lost_pre) {
		dev->p_log_priv->message_lost += (uint32_t)((uint16_t)(((int16_t)log_app_rb->lost) - ((int16_t)dev->p_log_priv->message_lost_pre)));
		dev->p_log_priv->message_lost_pre = log_app_rb->lost;
		pr_info("%s: log message lost = %d.\n", __func__, dev->p_log_priv->message_lost);
		getnstimeofday(&ts);
		rtc_time64_to_tm(LOG_TIME_BJ_ZONE_OFFSET + ts.tv_sec + ts.tv_nsec / 1000000000, &tm);
		memset(log_time_buf, 0, sizeof(log_time_buf));
		memset(log_string, 0, LOG_STRING_MAX_SIZE);
		temp32 = ts.tv_nsec % 1000000000 / 1000;
		snprintf(log_time_buf, sizeof(log_time_buf),"%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.6d",
			(tm.tm_year + 1900),
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
			temp32);
		snprintf(log_string, LOG_STRING_MAX_SIZE, "%s %s %c [tl_drv]:adsp log lost %d\n",
			log_time_buf,
			ap_log_module_name,
			log_level_buf[LEVEL_ERROR],
			dev->p_log_priv->message_lost);
		log_string[LOG_STRING_MAX_SIZE - 1] = '\0';
		log_string[LOG_STRING_MAX_SIZE - 2] = '\n';

		if (count >= strlen(log_string) + out_cnt) {
			if (copy_to_user(buf + out_cnt, log_string, strlen(log_string))) {
				pr_err("%s: Failed to copy data to user for log app lost\n", __func__);
				return 0;
			}
			out_cnt += strlen(log_string);
		} else {
			pr_err("%s: provided buf size(%d) is too small for log app lost\n", __func__, count);;
			return out_cnt;
		}
	}

	if (dev->p_log_priv->point_lost_count != dev->p_log_priv->point_lost) {
		dev->p_log_priv->point_lost_count = dev->p_log_priv->point_lost;
		getnstimeofday(&ts);
		rtc_time64_to_tm(LOG_TIME_BJ_ZONE_OFFSET + ts.tv_sec + ts.tv_nsec / 1000000000, &tm);
		memset(log_time_buf, 0, sizeof(log_time_buf));
		memset(log_string, 0, LOG_STRING_MAX_SIZE);
		temp32 = ts.tv_nsec % 1000000000 / 1000;
		snprintf(log_time_buf, sizeof(log_time_buf),"%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.6d",
			(tm.tm_year + 1900),
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
			temp32);
		snprintf(log_string, LOG_STRING_MAX_SIZE, "%s %s %c [tl_drv]:adsp point lost %d\n",
			log_time_buf,
			ap_log_module_name,
			log_level_buf[LEVEL_ERROR],
			dev->p_log_priv->point_lost);
		log_string[LOG_STRING_MAX_SIZE - 1] = '\0';
		log_string[LOG_STRING_MAX_SIZE - 2] = '\n';
		if (count >= strlen(log_string) + out_cnt) {
			if (copy_to_user(buf + out_cnt, log_string, strlen(log_string))) {
				pr_err("%s: Failed to copy data to user for log point lost\n", __func__);
				return 0;
			}
			out_cnt += strlen(log_string);
		} else {
			pr_err("%s: provided buf size(%d) is too small for log point lost\n", __func__, count);
			return out_cnt;
		}
	}

	while (((uint32_t)(log_app_rb->write_index - log_app_rb->read_index) > 0) &&
			dev->p_log_priv->log_state == RUNNING) {
		memset(adsp_string, 0, LOG_STRING_MAX_SIZE);
		memset(log_string, 0, LOG_STRING_MAX_SIZE);
		memset(message_string, 0, LOG_STRING_MAX_SIZE);
		memset(log_time_buf, 0, sizeof(log_time_buf));
		real_read_index = log_app_rb->read_index % log_app_rb->deepth;
		curr_log_message = &log_app_rb->log_message + real_read_index;
		if (curr_log_message == NULL) {
			pr_err("%s: cur log msg is NULL\n", __func__);
			log_app_rb->read_index++;
			continue;
		}
		num_values = curr_log_message->options & 0x7;
		log_level = (curr_log_message->options >> 3) & 0x3;
		module_id = curr_log_message->module_id;
		if (num_values > 0 && num_values <= 2) {
			value1 = curr_log_message->value.value32[0];
			value2 = curr_log_message->value.value32[1];
		} else if (num_values >= 3 && num_values <= 4) {
			value1 = (uint32_t)curr_log_message->value.value16[0];
			value2 = (uint32_t)curr_log_message->value.value16[1];
			value3 = (uint32_t)curr_log_message->value.value16[2];
			value4 = (uint32_t)curr_log_message->value.value16[3];
		}
		if (num_values > 4)
		{
			proto_error++;
		}
		if (log_level >= LEVEL_MAX) {
			proto_error++;
		}
		if (module_id >= MID_MAX) {
			proto_error++;
		}

		if (proto_error) {
			pr_err("%s: proto_error(%d), num_values(%d), log_level(%d), module_id(%d) is not support.\n",
				__func__, proto_error, num_values, log_level, module_id);
			proto_error = 0;
			getnstimeofday(&ts);
			rtc_time64_to_tm(LOG_TIME_BJ_ZONE_OFFSET + ts.tv_sec + ts.tv_nsec / 1000000000, &tm);
			memset(log_string, 0, LOG_STRING_MAX_SIZE);
			temp32 = ts.tv_nsec % 1000000000 / 1000;
			snprintf(log_time_buf, sizeof(log_time_buf),"%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.6d",
				(tm.tm_year + 1900),
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec,
				temp32);
			snprintf(log_string, LOG_STRING_MAX_SIZE, "%s %s %c [tl_drv]:proto_error(%d), num_values(%d), log_level(%d), module_id(%d) is not support\n",
				log_time_buf,
				ap_log_module_name,
				log_level_buf[LEVEL_ERROR],
				proto_error, num_values, log_level, module_id);
			log_string[LOG_STRING_MAX_SIZE - 1] = '\0';
			log_string[LOG_STRING_MAX_SIZE - 2] = '\n';

			if (count >= strlen(log_string) + out_cnt) {
				if (copy_to_user(buf+out_cnt, log_string, strlen(log_string))) {
					pr_err("%s: Failed to copy data to user\n", __func__);
					return out_cnt;
				}
				out_cnt += strlen(log_string);
			} else {
				return out_cnt;
			}
			
			log_app_rb->read_index++;
			continue;
		}
		
		log_offset = TL_GET_BIT_FIELD(curr_log_message->options, 5, 3);
		log_offset = (log_offset << 16) | curr_log_message->log_id;
		curr_log_string = (uint8_t *)app->string_img_virt_buf[curr_log_message->app_id] + log_offset;
		if (curr_log_string == NULL) {
			pr_err("%s: cur log string is NULL\n", __func__);
			log_app_rb->read_index++;
			continue;
		}
		if (LOG_STRING_MAX_SIZE >= (strlen(curr_log_string) + 1)) {
			memcpy_fromio(message_string, curr_log_string, strlen(curr_log_string));
			message_string[strlen(curr_log_string) + 1] = '\0';
		} else {
			memcpy_fromio(message_string, curr_log_string, LOG_STRING_MAX_SIZE);
			message_string[LOG_STRING_MAX_SIZE - 1] = '\0';
		}

		switch (num_values) {
		case 0: {
			snprintf(adsp_string, LOG_STRING_MAX_SIZE, "%s", (char *)message_string);
			break;
		}
		case 1: {
			snprintf(adsp_string, LOG_STRING_MAX_SIZE, (char *)message_string, value1);
			break;
		}
		case 2: {
			snprintf(adsp_string, LOG_STRING_MAX_SIZE, (char *)message_string, value1, value2);
			break;
		}
		case 3: {
			snprintf(adsp_string, LOG_STRING_MAX_SIZE, (char *)message_string, value1, value2, value3);
			break;
		}
		case 4: {
			snprintf(adsp_string, LOG_STRING_MAX_SIZE, (char *)message_string, value1, value2, value3, value4);
			break;
		}
		default: {
			pr_err("%s: num_values(%d) is not support.\n", __func__, num_values);
			log_app_rb->read_index++;
			continue;
		}
		}

		rtc_time64_to_tm(LOG_TIME_BJ_ZONE_OFFSET + curr_log_message->timestamp_s + curr_log_message->timestamp_us / 1000000, &tm);
		snprintf(log_time_buf, sizeof(log_time_buf),"%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.6d",
			(tm.tm_year + 1900),
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
			curr_log_message->timestamp_us);
		snprintf(log_string, LOG_STRING_MAX_SIZE, "%s %s %c %s\n",
			log_time_buf,
			adsp_log_module_name_table[module_id].p_module_name,
			log_level_buf[log_level],
			adsp_string);
		log_string[LOG_STRING_MAX_SIZE - 1] = '\0';
		log_string[LOG_STRING_MAX_SIZE - 2] = '\n';

		if (count >= strlen(log_string) + out_cnt) {
			if (copy_to_user(buf + out_cnt, log_string, strlen(log_string))) {
				pr_err("%s: Failed to copy data to user\n", __func__);
				return out_cnt;
			}
			out_cnt += strlen(log_string);
		} else {
			return out_cnt;
		}

		log_app_rb->read_index++;
	}
	return out_cnt;
}

static int tinylog_drv_point_read(struct tinylog_drv_dev *dev, char __user *buf, size_t count)
{
	struct log_point_ring_buf *log_point_rb = NULL;
	struct point_info *point_info = &dev->p_log_priv->jlq_point_info;
	uint8_t *virt_read_addr = NULL;
	uint8_t *virt_base_addr = NULL;
	uint32_t avail_size = 0;
	uint32_t write_addr = 0;
	uint32_t read_addr = 0;
	uint8_t __attribute__ ((aligned (8))) tmp_buf[LOG_LEAST_CPY_SIZE];
	uint32_t left_size = 0;
	uint32_t wrap_size = 0;

	log_point_rb = (struct log_point_ring_buf *)point_info->ex_virt_buf;
	if (!log_point_rb) {
		pr_err("%s: ring buffer is null.\n", __func__);
		return 0;
	}

	if (log_point_rb->lost != dev->p_log_priv->point_lost_pre) {
		dev->p_log_priv->point_lost += (uint32_t)((uint16_t)(((int16_t)log_point_rb->lost) - ((int16_t)dev->p_log_priv->point_lost_pre)));
		dev->p_log_priv->point_lost_pre = log_point_rb->lost;
		pr_err("%s: log point lost = %d.\n", __func__, dev->p_log_priv->point_lost);
	}

	read_addr = log_point_rb->read_addr;
	write_addr = log_point_rb->write_addr;

	if (read_addr > log_point_rb->end_addr ||
		read_addr < log_point_rb->base_addr ||
		write_addr > log_point_rb->end_addr ||
		write_addr < log_point_rb->base_addr) {
		pr_err("%s: addr error, read_addr = 0x%x, write_addr = 0x%x, expected range[0x%x-0x%x].\n",
			__func__,
			read_addr,
			write_addr,
			log_point_rb->base_addr,
			log_point_rb->end_addr);
		return 0;
	}

	virt_base_addr = (uint8_t *)point_info->ex_virt_buf + sizeof(*log_point_rb);
	virt_read_addr = (uint8_t *)virt_base_addr +
				(read_addr - log_point_rb->base_addr);

	if (read_addr > write_addr) {
		avail_size = log_point_rb->end_addr - read_addr +
			write_addr - log_point_rb->base_addr;

		if (avail_size > count) {
			pr_err("%s: received total size %d is bigger than the given size %d.\n",
				__func__, avail_size, count);
			return 0;
		}

		left_size = log_point_rb->end_addr - read_addr;
		wrap_size = write_addr - log_point_rb->base_addr;

		if (left_size <= LOG_LEAST_CPY_SIZE) {
			memcpy_fromio(tmp_buf, virt_read_addr, left_size);
			copy_to_user(buf, tmp_buf, left_size);
		} else {
			copy_to_user(buf,
					virt_read_addr,
					left_size);
		}

		if (wrap_size <= LOG_LEAST_CPY_SIZE) {
			memcpy_fromio(tmp_buf, virt_base_addr, wrap_size);
			copy_to_user(buf + left_size, tmp_buf, wrap_size);
		} else {
			copy_to_user(buf + left_size,
					virt_base_addr,
					wrap_size);
		}

	} else if (read_addr < write_addr) {
		avail_size = write_addr - read_addr;

		if (avail_size > count) {
			pr_err("%s: received total size %d is bigger than the given size %d.\n",
				__func__, avail_size, count);
			return 0;
		}

		if (avail_size <= LOG_LEAST_CPY_SIZE) {
			memcpy_fromio(tmp_buf, virt_read_addr, avail_size);
			copy_to_user(buf, tmp_buf, avail_size);
		} else {
			copy_to_user(buf, virt_read_addr, avail_size);
		}
	} else {
		return 0;
	}

	log_point_rb->read_addr = write_addr;

	return avail_size;
}

static ssize_t tinylog_drv_read(struct file *filp, char __user *buf, size_t count,
				loff_t *f_pos)
{
	int ret = 0;
	struct tinylog_drv_dev *dev;

	dev = filp->private_data;

	mutex_lock(&dev->mutex);

	if (!dev->is_open) {
		pr_err("%s: device %s is not open\n", __func__, dev->name);
		mutex_unlock(&dev->mutex);
		return -EINVAL;
	}

	if (dev->p_log_priv->log_state != RUNNING) {
		pr_err("%s: device %s log state is not running\n", __func__, dev->name);
		mutex_unlock(&dev->mutex);
		return 0;
	}

	if (dev->device_type == LOG_APP_DEVICE_TYPE)
		ret = tinylog_drv_app_read(dev, buf, count);
	else
		ret = tinylog_drv_point_read(dev, buf, count);

	mutex_unlock(&dev->mutex);

	return ret;
}

static __poll_t tinylog_drv_poll(struct file *filp, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
	struct tinylog_drv_dev *dev;
	struct log_point_ring_buf *log_point_rb = NULL;
	struct log_app_ring_buf *log_app_rb = NULL;

	dev = filp->private_data;

	mutex_lock(&dev->mutex);

	poll_wait(filp, &(dev->waitq), wait);

	if (dev->device_type == LOG_APP_DEVICE_TYPE) {
		log_app_rb = (struct log_app_ring_buf *)dev->p_log_priv->jlq_app_info.ex_virt_buf;
		if (log_app_rb) {
			if (((uint32_t)(log_app_rb->write_index - log_app_rb->read_index) > 0) &&
				dev->p_log_priv->log_state == RUNNING) {
				mask = POLLIN | POLLRDNORM;
			}
		} else {
			pr_err("%s: tinylog rb is NULL\n", __func__);
		}
	} else {
		log_point_rb =
			(struct log_point_ring_buf *)dev->p_log_priv->jlq_point_info.ex_virt_buf;
		if (log_point_rb) {
			if (log_point_rb->read_addr != log_point_rb->write_addr) {
				mask = POLLIN | POLLRDNORM;
			}
		} else {
			pr_err("%s: tinydump rb is NULL\n", __func__);
		}
	}

	mutex_unlock(&dev->mutex);

	return mask;
}

static struct file_operations tinylog_drv_fops = {
	.owner = THIS_MODULE,
	.read = tinylog_drv_read,
	.open = tinylog_drv_open,
	.release = tinylog_drv_release,
	.poll = tinylog_drv_poll,
};

static int setup_cdev (struct cdev *cdev, dev_t devno)
{
	int error;

	cdev_init(cdev, &tinylog_drv_fops);
	cdev->owner = THIS_MODULE;
	error = cdev_add(cdev, devno, 1);

	return error;
}

static void destroy_cdev (struct cdev *cdev)
{
	cdev_del(cdev);
}

static char *get_log_app_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, LOG_APP_DEVICE_NAME);
}

static char *get_log_point_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, LOG_POINT_DEVICE_NAME);
}

static void tinylog_drv_log_init(void)
{
	int i = 0;
	int j = 0;
	for (i = 0; i < APP_ID_MAX; i++) {
		log_priv->jlq_app_info.app_state[i] = STOPPED;
		log_priv->jlq_app_info.build_info_offset[i] = 0;
		log_priv->jlq_app_info.string_img_addr[i] = 0;
		log_priv->jlq_app_info.string_img_len[i] = 0;
		log_priv->jlq_app_info.string_img_virt_buf[i] = NULL;
		log_priv->jlq_app_info.version_addr[i] = 0;
		for (j = 0; j < MID_MAX; j++)
			log_priv->jlq_app_info.log_level[i][j] = 0;
	}
	log_priv->jlq_app_info.in_buf_water_level = 0;
	log_priv->jlq_app_info.ex_buf_water_level = 0;
	log_priv->jlq_app_info.ex_buf_addr = 0;
	log_priv->jlq_app_info.ex_buf_len = 0;
	log_priv->jlq_app_info.ex_virt_buf = NULL;
	log_priv->message_lost = 0;
	log_priv->message_lost_pre = 0;

	for (i = 0; i < POINT_MAX; i++) {
		log_priv->jlq_point_info.point_state[i] = STOPPED;
	}
	log_priv->jlq_point_info.in_buf_water_level = 0;
	log_priv->jlq_point_info.ex_buf_water_level = 0;
	log_priv->jlq_point_info.ex_buf_addr = 0;
	log_priv->jlq_point_info.ex_buf_len = 0;
	log_priv->jlq_point_info.ex_virt_buf = NULL;
	log_priv->point_lost = 0;
	log_priv->point_lost_pre = 0;
	log_priv->point_lost_count = 0;

	log_priv->adsp_string = NULL;
	log_priv->log_string = NULL;
	log_priv->message_string = NULL;

	init_waitqueue_head(&log_priv->log_cmd_wait);
	atomic_set(&log_priv->log_cmd_state, 0);
	mutex_init(&log_priv->cmd_lock);
}

static int32_t tinylog_drv_log_callback (struct apr_client_data *data, void *priv)
{
	pr_debug("%s: log_callback is called\n", __func__);
	if (!data) {
		pr_debug("%s: Invalid param data\n", __func__);
		return -EINVAL;
	}

	if (data->opcode == RESET_EVENTS) {
		pr_debug("%s: reset event = %d %d apr[%pK]\n",
			__func__,
			data->reset_event, data->reset_proc, log_priv->apr);
		return 0;
	}
	pr_debug("%s: opcode:0x%x, payload size:%d\n", __func__, data->opcode, data->payload_size);

	if (data->payload_size) {
		uint32_t *payload;

		payload = data->payload;
		if (data->opcode == APR_BASIC_RSP_RESULT) {
			if (data->payload_size < (2 * sizeof(uint32_t))) {
				pr_debug("%s: Error: size %d is less than expected\n",
					__func__, data->payload_size);
				return -EINVAL;
			}
			pr_debug("%s:opcode = 0x%X cmd = 0x%X status = 0x%X token=%d\n",
				__func__, data->opcode,
				payload[0], payload[1], data->token);
			/* payload[1] contains the error status for response */
			if (payload[1] != 0) {
				atomic_set(&log_priv->log_cmd_state, payload[1]);
				wake_up(&log_priv->log_cmd_wait);
				pr_debug("%s: cmd = 0x%x returned error = 0x%x\n",
					__func__, payload[0], payload[1]);
				return 0;
			}
			pr_debug("%s: payload[0]:0x%x, payload[1]:%d\n",
				__func__, payload[0], payload[1]);
			switch (payload[0]) {
			case ASM_LOG_APP_CLOSE:
			case ASM_LOG_APP_MODULE_MESSAGE_LEVEL_SET:
			case ASM_LOG_APP_ALL_MESSAGE_LEVEL_SET:
			case ASM_LOG_APP_BUFFER_LEVEL_SET:
			case ASM_LOG_POINT_CLOSE:
			case ASM_LOG_POINT_BUFFER_LEVEL_SET:
				pr_debug("%s: set log_cmd_state %d and wait up log_cmd_wait",
						__func__, payload[1]);
				atomic_set(&log_priv->log_cmd_state, payload[1]);
				wake_up(&log_priv->log_cmd_wait);
				break;
			default:
				pr_debug("%s: Unknown cmd 0x%X\n", __func__,
						payload[0]);
				break;
			}
		} else {
			switch (data->opcode) {
			case ASM_LOG_APP_OPEN_EVENT: {
				struct log_app_open_event *open_event =
					(struct log_app_open_event *)payload;
				if (open_event->status == 0) {
					pr_debug("%s: APP: app_id = %d.\n",
							__func__, open_event->app_id);
					pr_debug("%s: APP: ex_buf_addr = 0x%x.\n",
							__func__, open_event->ex_buf_addr);
					pr_debug("%s: APP: ex_buf_len = %d.\n",
							__func__, open_event->ex_buf_len);
					pr_debug("%s: APP: string_img_addr = 0x%x.\n",
							__func__, open_event->string_img_addr);
					pr_debug("%s: APP: string_img_len = %d.\n",
							__func__, open_event->string_img_len);
					pr_debug("%s: APP: build_info_offset = %d.\n",
							__func__, open_event->build_info_offset);
					pr_debug("%s: APP: status = %d.\n",
							__func__, open_event->status);
					log_priv->jlq_app_info.ex_buf_addr = open_event->ex_buf_addr;
					log_priv->jlq_app_info.ex_buf_len = open_event->ex_buf_len;
					log_priv->jlq_app_info.build_info_offset[open_event->app_id] =
							open_event->build_info_offset;
					log_priv->jlq_app_info.string_img_addr[open_event->app_id] =
							open_event->string_img_addr;
					log_priv->jlq_app_info.string_img_len[open_event->app_id] =
							open_event->string_img_len;
					log_priv->jlq_app_info.version_addr[open_event->app_id] =
							open_event->version_addr;
					pr_debug("%s: set log_cmd_state %d and wait up log_cmd_wait",
							__func__, 0);
				}else {
					pr_debug("%s: app %d log open failure\n", __func__,
						open_event->app_id);
				}
				atomic_set(&log_priv->log_cmd_state, 0);
				wake_up(&log_priv->log_cmd_wait);
				break;
			}
			case ASM_LOG_POINT_OPEN_EVENT: {
				struct log_point_open_event *open_event =
						(struct log_point_open_event *)payload;
				if (open_event->status == 0) {
					log_priv->jlq_point_info.ex_buf_addr = open_event->ex_buf_addr;
					log_priv->jlq_point_info.ex_buf_len = open_event->ex_buf_len;
					pr_debug("%s: POINT: ex_buf_addr = 0x%x, ex_buf_len = %d.\n",
						__func__,
						log_priv->jlq_point_info.ex_buf_addr,
						log_priv->jlq_point_info.ex_buf_len);
					pr_debug("%s: set log_cmd_state %d and wait up log_cmd_wait",
							__func__,
							0);
				} else {
					pr_debug("%s: point open failure\n", __func__);
				}
				atomic_set(&log_priv->log_cmd_state, 0);
				wake_up(&log_priv->log_cmd_wait);
				break;
			}
			default:
				pr_debug("Not Supported Event opcode[0x%x]\n", data->opcode);
				break;
			}
		}
	}
	return 0; 
}

static irqreturn_t tinylog_drv_app_message_irq_handler(int irq, void *dev_id)
{
	wake_up_interruptible(&(tinylog_dev[0].waitq));
	return IRQ_HANDLED;
}

static irqreturn_t tinylog_drv_point_irq_handler(int irq, void *dev_id)
{
	wake_up_interruptible(&(tinylog_dev[1].waitq));
	return IRQ_HANDLED;
}

static int tinylog_drv_log_open(void)
{
	int ret = 0;

	log_priv->apr  = apr_register("ADSP", "TinyLog", tinylog_drv_log_callback,
			0xFFFFFFFF, log_priv);
	if (log_priv->apr == NULL) {
		pr_err("%s: Unable to register tinylog\n", __func__);
		return -ENETRESET;
	}

	ret = request_irq(log_priv->log_app_irq_id, tinylog_drv_app_message_irq_handler, 0,
					 "log app irq", log_priv->dev);
	if (ret) {
		pr_err("%s: Could not request log app irq.\n", __func__);
		ret = -ENODEV;
		goto fail_open;
	}
	irq_set_irq_wake(log_priv->log_app_irq_id, 1);

	ret = request_irq(log_priv->log_point_irq_id, tinylog_drv_point_irq_handler, 0,
					 "log point irq", log_priv->dev);
	if (ret) {
		pr_err("%s: Could not request log point irq.\n", __func__);
		ret = -ENODEV;
		goto fail_open;
	}
	irq_set_irq_wake(log_priv->log_point_irq_id, 1);

	log_priv->log_state = RUNNING;

	return 0;
fail_open:
	log_priv->log_state = STOPPED;
	irq_set_irq_wake(log_priv->log_app_irq_id, 0);
	free_irq(log_priv->log_app_irq_id, log_priv->dev);
	irq_set_irq_wake(log_priv->log_point_irq_id, 0);
	free_irq(log_priv->log_point_irq_id, log_priv->dev);
	if (log_priv->log_app_thread) {
		kthread_stop(log_priv->log_app_thread);
	}
	if (log_priv->log_point_thread) {
		kthread_stop(log_priv->log_point_thread);
	}
	return ret;
}

static int tinylog_drv_log_close(void)
{
	log_priv->log_state = STOPPED;

	irq_set_irq_wake(log_priv->log_app_irq_id, 0);
	free_irq(log_priv->log_app_irq_id, log_priv->dev);
	irq_set_irq_wake(log_priv->log_point_irq_id, 0);
	free_irq(log_priv->log_point_irq_id, log_priv->dev);

	if (log_priv->log_app_thread)
		kthread_stop(log_priv->log_app_thread);

	if (log_priv->log_point_thread)
		kthread_stop(log_priv->log_point_thread);

	return 0;
}

static int tinylog_drv_log_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;

	log_priv = devm_kzalloc(&pdev->dev, sizeof(*log_priv), GFP_KERNEL);
	if (!log_priv) {
		dev_err(&pdev->dev, "%s: memory alloc failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}

	tinylog_drv_log_init();

	log_priv->jlq_log_obj = NULL;
	log_priv->jlq_log_attr_group = devm_kzalloc(&pdev->dev,
				sizeof(*(log_priv->jlq_log_attr_group)),
				GFP_KERNEL);
	if (!log_priv->jlq_log_attr_group) {
		dev_err(&pdev->dev, "%s: malloc jlq_log_set_attr_group failed\n",
						__func__);
		ret = -ENOMEM;
		devm_kfree(&pdev->dev, log_priv);
		return ret;
	}

	ret = create_adsp_log_psy(log_priv);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to create log psy %d\n",
							__func__, ret);
		goto error_return;
	}

	log_priv->log_state = STOPPED;
	log_priv->dev = &pdev->dev;

	log_priv->adsp_string = kmalloc(LOG_STRING_MAX_SIZE, GFP_KERNEL);
	if (log_priv->adsp_string == NULL) {
		pr_err("%s: adsp_string kmalloc fail.\n", __func__);
		goto error_return;
	}

	log_priv->message_string = kmalloc(LOG_STRING_MAX_SIZE, GFP_KERNEL);
	if (log_priv->message_string == NULL) {
		pr_err("%s: message_string kmalloc fail.\n", __func__);
		goto error_return;
	}
	log_priv->log_string = kmalloc(LOG_STRING_MAX_SIZE, GFP_KERNEL);
	if (log_priv->log_string == NULL) {
		pr_err("%s: log_string kmalloc fail.\n", __func__);
		goto error_return;
	}

	log_priv->log_app_irq_id = of_irq_get_byname(np, "log_app_interrupt");
	log_priv->log_point_irq_id = of_irq_get_byname(np, "log_point_interrupt");

	pr_debug("%s: log_app_irq_id:%d, log_point_irq_id:%d.\n",
		__func__, log_priv->log_app_irq_id, log_priv->log_point_irq_id);

	ret = tinylog_drv_log_open();
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to open tiny log %d\n",
							__func__, ret);
		goto error_return;
	}

	log_priv->log_ctrl_ops = get_log_controller_ops();

	pr_debug("%s: success probe adsp tiny log.\n", __func__);
	return 0;
error_return:
	pr_err("%s: failed probe adsp tiny log.\n", __func__);
	destroy_adsp_log_psy(log_priv);
	if (log_priv->adsp_string != NULL) {
		kfree(log_priv->adsp_string);
		log_priv->adsp_string = NULL;
	}
	if (log_priv->log_string != NULL) {
		kfree(log_priv->log_string);
		log_priv->log_string = NULL;
	}
	if (log_priv->message_string != NULL) {
		kfree(log_priv->message_string);
		log_priv->message_string = NULL;
	}
	devm_kfree(&pdev->dev, log_priv);
	return 0;
}

static int tinylog_drv_log_remove(struct platform_device *pdev)
{
	if (!log_priv)
		return 0;

	destroy_adsp_log_psy(log_priv);
	tinylog_drv_log_close();
	if (log_priv->adsp_string != NULL) {
		kfree(log_priv->adsp_string);
		log_priv->adsp_string = NULL;
	}
	if (log_priv->log_string != NULL) {
		kfree(log_priv->log_string);
		log_priv->log_string = NULL;
	}
	if (log_priv->message_string != NULL) {
		kfree(log_priv->message_string);
		log_priv->message_string = NULL;
	}
	devm_kfree(&pdev->dev, log_priv);
	log_priv = NULL;

	return 0;
}

static const struct of_device_id tinylog_drv_log_dt_match[] = {
	{.compatible = "jlq,jlq-adsp-log"},
	{}
};
MODULE_DEVICE_TABLE(of, tinylog_drv_log_dt_match);

static struct platform_driver tinylog_drv_log_driver = {
	.driver = {
		.name = "jlq-adsp-log",
		.owner = THIS_MODULE,
		.of_match_table = tinylog_drv_log_dt_match,
	},
	.probe = tinylog_drv_log_probe,
	.remove = tinylog_drv_log_remove,
};

static int __init tinylog_drv_platform_init(void)
{
	int ret = 0;
	dev_t devno = 0;
	int tinylog_minor = 0;

	ret = platform_driver_register(&tinylog_drv_log_driver);
	if (ret < 0) {
		pr_err("%s: Failed to register tinylog platform driver\n", __func__);
		return ret;
	}

	memset((void *)tinylog_dev, 0, sizeof(tinylog_dev));
	tinylog_dev[0].p_log_priv = log_priv;
	tinylog_dev[1].p_log_priv = log_priv;
	tinylog_dev[0].name = "adsp_log_app";
	tinylog_dev[1].name = "adsp_log_point";
	tinylog_dev[0].device_type = LOG_APP_DEVICE_TYPE;
	tinylog_dev[1].device_type = LOG_POINT_DEVICE_TYPE;
	mutex_init(&tinylog_dev[0].mutex);
	mutex_init(&tinylog_dev[1].mutex);
	init_waitqueue_head(&tinylog_dev[0].waitq);
	init_waitqueue_head(&tinylog_dev[1].waitq);

	/* alloc char device number */
	ret = alloc_chrdev_region(&devno, tinylog_minor, MAX_NUM_DEVICE, "adsp_log");
	if (ret < 0) {
		pr_err("%s: Failed to alloc device number\n", __func__);
		goto err1;
	}
	tinylog_dev[0].devno = devno;
	tinylog_dev[1].devno = devno+1;

	/* create char device */
	ret = setup_cdev(&tinylog_dev[0].cdev, tinylog_dev[0].devno);
	if (ret < 0) {
		pr_err("%s: Failed to setup log app char device\n", __func__);
		goto err2;
	}
	ret = setup_cdev(&tinylog_dev[1].cdev, tinylog_dev[1].devno);
	if (ret < 0) {
		pr_err("%s: Failed to setup log point char device\n", __func__);
		goto err3;
	}

	/* create class */
	tinylog_dev[0].my_class = class_create(THIS_MODULE, "adsp_log_app");
	if (IS_ERR(tinylog_dev[0].my_class)) {
		pr_err("%s: Failed to create log app class\n", __func__);
		ret = -1;
		goto err4;
	}
	tinylog_dev[0].my_class->devnode = get_log_app_devnode;

	tinylog_dev[1].my_class = class_create(THIS_MODULE, "adsp_log_point");
	if (IS_ERR(tinylog_dev[1].my_class)) {
		pr_err("%s: Failed to create log point class\n", __func__);
		ret = -1;
		goto err5;
	}
	tinylog_dev[1].my_class->devnode = get_log_point_devnode;

	/* create linux device */
	tinylog_dev[0].dev =
		device_create(tinylog_dev[0].my_class,
						NULL,
						tinylog_dev[0].devno,
						NULL,
						"adsp_log_app");
	if (IS_ERR(tinylog_dev[0].dev)) {
		pr_err("%s: Failed to create log app device\n", __func__);
		ret = -1;
		goto err6;
	}
	tinylog_dev[1].dev =
		device_create(tinylog_dev[1].my_class,
						NULL,
						tinylog_dev[1].devno,
						NULL,
						"adsp_log_point");
	if (IS_ERR(tinylog_dev[1].dev)) {
		pr_err("%s: Failed to create log point device\n", __func__);
		ret = -1;
		goto err7;
	}

	pr_debug("%s: adsp log module init successful\n", __func__);
	return 0;

err7:
	device_destroy(tinylog_dev[0].my_class, tinylog_dev[0].devno);
err6:
	class_destroy(tinylog_dev[1].my_class);
err5:
	class_destroy(tinylog_dev[0].my_class);
err4:
	destroy_cdev(&tinylog_dev[1].cdev);
err3:
	destroy_cdev(&tinylog_dev[0].cdev);
err2:
	unregister_chrdev_region(devno, MAX_NUM_DEVICE);
err1:
	platform_driver_unregister(&tinylog_drv_log_driver);

	return ret;
}

static void __exit tinylog_drv_platform_exit(void)
{
	mutex_destroy(&tinylog_dev[1].mutex);
	mutex_destroy(&tinylog_dev[0].mutex);

	device_destroy(tinylog_dev[1].my_class, tinylog_dev[1].devno);
	device_destroy(tinylog_dev[0].my_class, tinylog_dev[0].devno);
	class_destroy(tinylog_dev[1].my_class);
	class_destroy(tinylog_dev[0].my_class);
	destroy_cdev(&tinylog_dev[1].cdev);
	destroy_cdev(&tinylog_dev[0].cdev);
	unregister_chrdev_region(tinylog_dev[0].devno, MAX_NUM_DEVICE);
	platform_driver_unregister(&tinylog_drv_log_driver);

	pr_err("%s: adsp log module exit successful\n", __func__);
}

module_init(tinylog_drv_platform_init);
module_exit(tinylog_drv_platform_exit);
MODULE_DESCRIPTION("log module platform driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cooper Zheng");

