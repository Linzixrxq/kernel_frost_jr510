/*
 * JLQ Inline Crypto Module
 *
 * Copyright 2018~2021 JLQ Technology Co.,
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

#ifndef __FDE_H__
#define __FDE_H__

#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/tee_drv.h>

#define FDE_DBG_CFG			(1 << 0)
#define FDE_DBG_CONTEXT		(1 << 1)
#define FDE_DBG_KEYINFO		(1 << 2)
#define FDE_DBG_SLOTCFG		(1 << 3)
#define FDE_DBG_DVFS		(1 << 4)

#define FDE_TYPE_NAME_LEN		8

#define SDHC_FDE_TYPE_STR		"sdhc"
#define UFS_FDE_TYPE_STR		"ufs"

#define FDE_SLOT_BM_LEN		32

#define FDE_CONTEXT_MSK_MAX		((1ul << (FDE_SLOT_BM_LEN)) - 1)

#define FDE_DEFAULT_BYPASSED_SLOT	31
#define FDE_DEFAULT_BYPASSED_DATA_UNIT_SIZE		512

#define FDE_DBG_CONTEXT_NOTUSED		'-'
#define FDE_DBG_CONTEXT_W			'w'
#define FDE_DBG_CONTEXT_R			'r'
#define FDE_DBG_CONTEXT_RESV		'*'
#define FDE_DBG_CONTEXT_ERR			'#'

enum {
	FDE_RUNTIME_POWEROFF,
	FDE_RUNTIME_POWERSAVE,
	FDE_RUNTIME_PERFORMANCE
};

#define FDE_SHM_SIZE		roundup(((sizeof(struct fde_crypto_config) * FDE_SLOT_BM_LEN) + 8), 4)	//512 //128
#define FDE_SHM_MAGIC		(0x133A1290ul | 0)

struct fde_device {
//	struct list_head	list;
	struct device		*dev;
	struct kobject		*kobj;
	struct cdev			cdev;
	char				fde_instance_type[FDE_TYPE_NAME_LEN];
	dev_t				device_no;
	struct class		*driver_class;
	void __iomem		*base;
	u32					paddr;
	struct clk			*fde_clk;
	struct clk			*fde_bus_clk;
	void __iomem		*hblk_sctrl;
	u32					*clk_table;
	u32					*bus_clk_table;
	int					clk_table_len;
	int					bus_clk_table_len;
	u32					clk_rate;
	u32					bus_clk_rate;
	int					runtime_level;

	struct tee_context *ctx;
	u32 session_id;
	struct tee_shm *shm;
	void				*sharememory;


	struct mutex		mutex_shm;

	void 				*host;
	int					need_reset;

	spinlock_t			lock_context;
	spinlock_t			lock_fde;
	int					context_num;
	unsigned long	context_reserve_bits;
	int				context_bypassed_context;
	unsigned long	context_reserve_bits2;
	//DECLARE_BITMAP(slot_used, 32);
	unsigned long slot_used[BITS_TO_LONGS(FDE_SLOT_BM_LEN)];
	int				context_last_read;
	int				context_last_write;

	char			context_info[FDE_SLOT_BM_LEN + 1];	// only for debug
};

#define FDE_CRYPTO_KEY_MAX_SIZE			64 // 32


struct fde_crypto_key_config {
	unsigned int key_slot_key[FDE_CRYPTO_KEY_MAX_SIZE/4];
	unsigned char key_slot_key_size;
	unsigned char key_slot_key_mode;
	unsigned char resevered;
};

struct fde_crypto_config {
	//unsigned char contex_sel;	// 0~31
	char bypass;
	char reset;
	char key_slot_sel;
	char resevred;
	//unsigned short data_unit_size;
	//unsigned int start_sector_number;
	struct fde_crypto_key_config key;
};

// 0~31, -1 not config
struct fde_crypto_slot_config {
	bool slot_bypass;
	char context_sel;
	char is_nonecq;
	char key_slot;
	char iswrite;
	//char encrypt_slot;
	//char decrypt_slot;
	unsigned short data_unit_size;
	unsigned int start_sector_number;
};

struct jlq_fde_variant_ops {
	const char *name;
	int (*init)(const char *type, struct platform_device *pdev, void *pdata);
	int (*get_keyslot_num)(struct platform_device *pdev);
	int (*set_context_reserved)(struct platform_device *pdev, unsigned long reserved_bits);
	int (*program_key)(struct platform_device *pdev, const struct fde_crypto_config *cfg, int slot);
	int (*config_context)(struct platform_device *pdev, const struct fde_crypto_slot_config *cfg);
	int (*config_default_bypass)(struct platform_device *pdev, struct fde_crypto_slot_config *cfg);
	int (*do_reset)(struct platform_device *pdev, int reset);
	int (*set_bypass)(struct platform_device *pdev, int bypass);
	int (*alloc_context)(struct platform_device *pdev, int *slot, int w1r0);
	int (*free_context)(struct platform_device *pdev, int slot, int w1r0);
	int (*runtime_level)(struct platform_device *pdev, int level);
};

struct sdhci_jlq_fde_data {
	struct jlq_fde_variant_ops	*vops;
	struct platform_device		*pdev;
};


struct jlq_fde_variant_ops *jlq_fde_get_ops(void);
struct platform_device *jlq_fde_get_pdevice(struct device_node *node);


#endif

