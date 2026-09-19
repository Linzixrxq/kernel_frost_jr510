/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/soc/jlq/ja310/jlq-bridge.h
 *
 * Copyright (c) 2019-2021   Lite-On Technology Co., Ltd
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
 */

#ifndef __JLQ_VERIFIER_H__
#define __JLQ_VERIFIER_H__

#include <linux/types.h>

#if IS_ENABLED(CONFIG_MICROTRUST_VERIFIER_DRIVER)
#include "../../drivers/teei/300/common/include/tee_client_api.h"
#endif

#define TZV_LOG(lvl, fmt, ...)				printk(lvl fmt, ##__VA_ARGS__)
#define TZV_LOG_VERBOSE(fmt, ...)				//printk(KERN_DEBUG fmt, ##__VA_ARGS__)
#define TZV_LOG_DBG(fmt, ...)				printk(KERN_DEBUG fmt, ##__VA_ARGS__)
#define TZV_LOG_INFO(fmt, ...)				printk(KERN_INFO fmt, ##__VA_ARGS__)
#define TZV_LOG_ERR(fmt, ...)				printk(KERN_ERR fmt, ##__VA_ARGS__)

#define TZ_SHM_SIZE(seg_num)			max(sizeof(struct verifier_reg_op),	\
	(sizeof(struct verifier_image_info) + (seg_num) * (sizeof(struct image_seg_info))))

#define VERIFIER_HEADER_SIZE	4096

#define MAX_SSR_PARA_NUM	8

enum {
	SS_MODEM,
	SS_QBL,
	SS_MSADP,
	SS_COMMON = 0xff
};

enum {
	TZCMD_INVALID = 0,
	TZCMD_AUTH_BOOT = 1,
	TZCMD_OP_REG = 2,
	TZCMD_MAX
};

enum {
	TZCMD_REG_READ = 0,
	TZCMD_REG_WRITE,
	TZCMD_REG_INVALID
};

#define DRV_SEC_IMGLDR_SUCCESS				0
#define DRV_SEC_IMGLDR_FAIL					1
#define DRV_ERROR_UNKNOWN_COMMAND			2

struct ss_reg {
	uint32_t offset;
	uint32_t value;
};

struct ss_rst_para {
	uint32_t ssid;
	uint32_t para_num;
	struct ss_reg paras[MAX_SSR_PARA_NUM];
};

struct image_seg_info {
	uint64_t paddr;	//u64
	uint32_t sz;	//u32
};

struct verifier_image_info {
	struct ss_rst_para ss_prop;
	uint64_t header_paddr;	//sign header paddr
	uint32_t header_size;
	uint32_t image_seg_num;	//num of seg
	struct image_seg_info ism[1];//segs info
};

struct verifier_reg_op {
	uint32_t ssid;
	uint32_t op_type; // 0 read, 1 write
	uint32_t offset;
	uint32_t value;
};

struct tz_verifier_info {
	int32_t session_id;
	int32_t shm_id;
	int32_t argsize;
	struct verifier_image_info *image_info;
};

#if IS_ENABLED(CONFIG_MICROTRUST_VERIFIER_DRIVER) || IS_ENABLED(CONFIG_JLQ_TZ_VERIFIER)
int tz_verifier_init(void);
void tz_verifier_deinit(int session_id, int destroy);
const unsigned char *tz_verifier_get_raw_data(const unsigned char *image, size_t size);
int tz_verifier_alloc_shm(unsigned long size);
int tz_verifier_get_shm_info(int shm_id, char **buffer, size_t *size);
void tz_verifier_free_shm(int shm_id);
int tz_verifier_invode_command(int session_id, int shm_id, unsigned int command, int cmdsize);
int tz_verifier_reg_command(int session_id, int shm_id, struct verifier_reg_op *verifier_reg);
#else
static inline int tz_verifier_init(void)
{
	return -1;
}
static inline void tz_verifier_deinit(int session_id, int destroy)
{
}
static const unsigned char *tz_verifier_get_raw_data(const unsigned char *image, size_t size)
{
	return image;
}
static inline int tz_verifier_alloc_shm(unsigned long size)
{
	return -1;
}
static inline int tz_verifier_get_shm_info(int shm_id, char **buffer, size_t *size)
{
	return -1;
}

static inline void tz_verifier_free_shm(int shm_id)
{
}

static inline int tz_verifier_invode_command(int session_id, int shm_id, unsigned int command, int cmdsize)
{
	return -1;
}

static inline int tz_verifier_reg_command(int session_id, int shm_id, struct verifier_reg_op *verifier_reg)
{
	return -1;
}

#endif

#endif

