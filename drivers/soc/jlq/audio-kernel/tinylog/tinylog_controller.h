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

#ifndef _TINYLOG_CONTROLLER_H
#define _TINYLOG_CONTROLLER_H

#include <ipc/apr.h>
#include <ipc/apr_tal.h>

/******************************************************************************
 * tinylog and tiny dump command define
 *****************************************************************************/
#define ASM_LOG_APP_OPEN                      0x0000FF00
#define ASM_LOG_APP_OPEN_EVENT                0x0000FF01
#define ASM_LOG_APP_CLOSE                     0x0000FF02
#define ASM_LOG_APP_MODULE_MESSAGE_LEVEL_SET  0x0000FF03
#define ASM_LOG_APP_ALL_MESSAGE_LEVEL_SET     0x0000FF04
#define ASM_LOG_APP_BUFFER_LEVEL_SET          0x0000FF05

#define ASM_LOG_POINT_OPEN                    0x0000FF06
#define ASM_LOG_POINT_OPEN_EVENT              0x0000FF07
#define ASM_LOG_POINT_CLOSE                   0x0000FF08
#define ASM_LOG_POINT_BUFFER_LEVEL_SET        0x0000FF09

#define ASM_SYS_TIME_SYNC_SET                 0x0000FF11


/******************************************************************************
 * tinylog and tiny dump command payload define
 *****************************************************************************/
struct asm_log_app_open {
	struct apr_hdr hdr;
	uint8_t app_id;
	uint8_t in_buf_level;
	uint8_t ex_buf_level;
    uint8_t reserved;
} __packed;

struct log_app_open_event {
	uint8_t app_id;
	uint8_t status;
	uint16_t build_info_offset;
	uint32_t ex_buf_addr;
	uint32_t ex_buf_len;
	uint32_t string_img_addr;
	uint32_t string_img_len;
	uint8_t build_info[26];
	uint8_t reserved[2];
	uint32_t version_addr;
}__packed;

struct asm_log_app_close {
	struct apr_hdr hdr;
	uint8_t app_id;
	uint8_t reserved[3];
} __packed;

struct asm_log_app_module_message_level_set {
	struct apr_hdr hdr;
	uint8_t app_id;
    uint8_t module_id;
	uint8_t log_level;
	uint8_t reserved;
} __packed;

struct asm_log_app_all_message_level_set {
	struct apr_hdr hdr;
	uint8_t app_id;
	uint8_t log_level;
	uint8_t reserved[2];
} __packed;

struct asm_log_app_buffer_level_set {
	struct apr_hdr hdr;
	uint8_t in_buf_level;
	uint8_t ex_buf_level;
	uint16_t reserved;
} __packed;

struct asm_log_point_open {
	struct apr_hdr hdr;
	uint8_t dump_point;
	uint8_t in_buf_level;
	uint8_t ex_buf_level;
	uint8_t reserved;
} __packed;

struct log_point_open_event {
	uint32_t ex_buf_addr;
	uint32_t ex_buf_len;
	uint32_t status;
}__packed;

struct asm_log_point_close {
	struct apr_hdr hdr;
	uint8_t dump_point;
	uint8_t reserved[3];
} __packed;

struct asm_log_point_buffer_level_set {
	struct apr_hdr hdr;
	uint8_t in_buf_level;
	uint8_t ex_buf_level;
	uint8_t reserved[2];
} __packed;

struct asm_sys_time_sync {
	struct apr_hdr hdr;
	uint32_t rtc_cnt_h;
	uint32_t rtc_cnt_l;
	uint32_t sys_cnt_h;
	uint32_t sys_cnt_l;
} __packed;


/******************************************************************************
 * log controller interface
 *****************************************************************************/
struct jlq_log_private;
struct log_controller_operations {
	int (*log_app_open)(struct jlq_log_private *drvdata, unsigned int app_id,
	unsigned int in_buf_level, unsigned int ex_buf_level);
	int (*log_app_close)(struct jlq_log_private *drvdata, unsigned int app_id);
	int (*log_app_set_msg_level)(struct jlq_log_private *drvdata, unsigned int app_id,
		unsigned int module_id, unsigned int log_level);
	int (*log_app_set_all_msg_level)(struct jlq_log_private *drvdata, unsigned int app_id,
		unsigned int log_level);
	int (*log_app_set_buffer_level)(struct jlq_log_private *drvdata,
		unsigned int in_buf_level, unsigned int ex_buf_level);
	int (*log_point_open)(struct jlq_log_private *drvdata, unsigned int dump_point,
		unsigned int in_buf_level, unsigned int ex_buf_level);
	int (*log_point_close)(struct jlq_log_private *drvdata, unsigned int dump_point);
	int (*log_point_close_all)(struct jlq_log_private *drvdata);
	int (*log_point_set_buffer_level)(struct jlq_log_private *drvdata,
		unsigned int in_buf_level, unsigned int ex_buf_level);
	int (*sys_time_sync_put)(struct jlq_log_private *drvdata, struct asm_sys_time_sync *sync_time);
};

/******************************************************************************
 * external function protocol type
 *****************************************************************************/
extern const struct log_controller_operations *get_log_controller_ops(void);


#endif /* end of _TINYLOG_CONTROLLER_H */

