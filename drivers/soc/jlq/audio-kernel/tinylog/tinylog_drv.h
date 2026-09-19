/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _TINYLOG_DRV_H
#define _TINYLOG_DRV_H

#include <linux/rtc.h>
#include "tinylog_controller.h"

#define TINYLOG_CACHE_ENABLE  (1)

#if (TINYLOG_CACHE_ENABLE == 1)
#define CACHE_LINE_SIZE 128
#endif

enum app_id {
	RTOS_PLATFORM = 0,
	AUDIO_FW_ALGORITHM,
	VIDEO_FW_ALGORITHM,
	SENSOR_HUB,
	AI_FW_ALGORITHM,
	APP_ID_MAX,
};

enum point_id {
	POINT_1 = 0,
	POINT_2,
	POINT_3,
	POINT_4,
	POINT_5,
	POINT_6,
	POINT_7,
	POINT_8,
	POINT_9,
	POINT_10,
	POINT_11,
	POINT_12,
	POINT_13,
	POINT_14,
	POINT_15,
	POINT_16,
	POINT_17,
	POINT_18,
	POINT_19,
	POINT_20,
	POINT_21,
	POINT_22,
	POINT_23,
	POINT_24,
	POINT_MAX = 50,
};

enum log_type {
	ADSP_LOG,
	ADSP_DUMP,
};

enum module_id {
	MID_COMM = 0,
	MID_PLATFORM,
	MID_VOC_SVC,
	MID_VOC_TX_PROC,
	MID_VOC_RX_PROC,
	MID_VOC_ENC,
	MID_VOC_DEC,
	MID_AVCORE,
	MID_MSG_ADAPTER,
	MID_VT,
	MID_AUD_PB,
	MID_AUD_REC,
	MID_FM,
	MID_AFE,
	MID_RESERVED,
	MID_MAX,
};

enum log_state {
	STOPPED = 0,
	RUNNING,
};

enum log_level {
	LEVEL_DISABLE = 0,
	LEVEL_ERROR,
	LEVEL_INFO,
	LEVEL_DEBUG,
	LEVEL_MAX,
};

enum point_save_mode {
	SAVE_MODE_1 = 1,  // header + param + stream, cfg + wav
	SAVE_MODE_2,      // header + param + stream, bin
	SAVE_MODE_3,      // stream, wav
	SAVE_MODE_4,      // header + param + stream, cfg + bin
	SAVE_MODE_5,      // stream, bin
};

enum water_level {
	WATER_LEVEL_ONE_MESSAGE = 1,
	WATER_LEVEL_20_PRECENT_POOL,
	WATER_LEVEL_60_PRECENT_POOL,
	WATER_LEVEL_80_PRECENT_POOL,
};

struct adsp_sync_time {
	uint32_t time_differ_l;
	uint32_t time_differ_h;
	uint32_t rtc_cnt_h;
	uint32_t rtc_cnt_l;
	uint32_t sys_cnt_h;
	uint32_t sys_cnt_l;
} __packed;

struct app_info {
	uint32_t app_state[APP_ID_MAX];
	uint32_t log_level[APP_ID_MAX][MID_MAX];
	uint32_t build_info_offset[APP_ID_MAX];
	uint32_t in_buf_water_level;
	uint32_t ex_buf_water_level;
	uint32_t ex_buf_addr;
	uint32_t ex_buf_len;
	void __iomem *ex_virt_buf;
	uint32_t string_img_addr[APP_ID_MAX];
	uint32_t string_img_len[APP_ID_MAX];
	void __iomem *string_img_virt_buf[APP_ID_MAX];
	uint32_t version_addr[APP_ID_MAX];
};

struct current_file_info {
	uint32_t service_id;
	uint32_t stream_id;
	uint32_t channels;
	uint32_t sample_rate;
	uint32_t service_counter;
	struct file *cfg_file;
	loff_t cfg_pos;
	struct file *data_file;
	loff_t data_pos;
};

struct point_info {
	uint32_t point_state[POINT_MAX];
	uint32_t in_buf_water_level;
	uint32_t ex_buf_water_level;
	uint32_t ex_buf_addr;
	uint32_t ex_buf_len;
	void __iomem *ex_virt_buf;
};

struct jlq_log_private {
	struct device *dev;
	uint32_t log_state;
	void *apr;
	struct mutex cmd_lock;
	struct kobject *jlq_log_obj;
	struct attribute_group *jlq_log_attr_group;
	struct app_info jlq_app_info;
	struct point_info jlq_point_info;
	uint32_t log_app_irq_id;
	uint32_t log_point_irq_id;
	wait_queue_head_t log_cmd_wait;
	wait_queue_head_t log_app_irq_wait;
	wait_queue_head_t log_point_irq_wait;
	atomic_t log_cmd_state;
	atomic_t log_app_irq_count;
	atomic_t log_point_irq_count;
	struct task_struct* log_app_thread;
	struct task_struct* log_point_thread;
	uint32_t message_lost;
	uint32_t point_lost;
	uint32_t point_lost_count;
	uint16_t message_lost_pre;
	uint16_t point_lost_pre;
	uint8_t *adsp_string;
	uint8_t *log_string;
	uint8_t *message_string;
	const struct log_controller_operations *log_ctrl_ops;
};

struct log_app_message {
	uint8_t options;
	uint8_t app_id:3;
	uint8_t module_id:5;
	uint16_t log_id;
	uint32_t timestamp_s;
	uint32_t timestamp_us;
	union {
		int16_t value16[4];
		int32_t value32[2];
	}value;
};

struct log_app_ring_buf {
	uint16_t version;
	uint16_t lost;
	uint32_t deepth;
	int32_t write_index;
	#if (TINYLOG_CACHE_ENABLE == 1)
	uint8_t cache_line_reserved1[CACHE_LINE_SIZE - 12];
	#endif
	int32_t read_index;
	#if (TINYLOG_CACHE_ENABLE == 1)
	uint8_t cache_line_reserved2[CACHE_LINE_SIZE - 4];
	#endif
	struct log_app_message log_message;
};

struct log_point_message {
	uint32_t head;
	uint32_t timestamp_s;
	uint32_t timestamp_us;
	uint8_t dump_point;
	uint8_t service_id;
	uint8_t stream_id;
	uint8_t session_id;
	uint8_t channels;
	uint8_t save_mode;
	uint16_t service_counter;
	uint32_t sample_rate;
	uint32_t param_len;
	uint32_t stream_len;
};

struct log_point_ring_buf {
	uint16_t version;
	uint16_t lost;
	uint32_t base_addr;
	uint32_t end_addr;
	uint32_t total_bytes;
	uint32_t write_addr;
	#if (TINYLOG_CACHE_ENABLE == 1)
	uint8_t cache_line_reserved1[CACHE_LINE_SIZE - 20];
	#endif
	uint32_t read_addr;
	#if (TINYLOG_CACHE_ENABLE == 1)
	uint8_t cache_line_reserved2[CACHE_LINE_SIZE - 4];
	#endif
	char pData[0];
};

#endif /* end of _TINYLOG_DRV_H */
