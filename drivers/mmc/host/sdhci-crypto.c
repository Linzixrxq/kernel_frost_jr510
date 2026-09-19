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

#define DEBUG

#include <linux/printk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/mmc/mmc.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/blkdev.h>
#include "../core/queue.h"


#include "sdhci.h"
#include "sdhci-crypto.h"
#include <linux/keyslot-manager.h>
#define DATA_UNIT_NUM(x)		(((u64)(x) & 0xFFFFFFFF) << 0)


extern int fde_debug_level;

static int sdhci_program_key(struct mmc_host *mmc,
			      const struct fde_crypto_config *cfg, int slot)
{
	int ret = -EINVAL;

	if (mmc->fde.vops->program_key)
		ret = mmc->fde.vops->program_key(mmc->fde.pdev, cfg, slot);
	return ret;

}

static void sdhci_clear_keyslot(struct mmc_host *mmc, int slot)
{
	struct fde_crypto_config cfg = { 0 };
	int err;

	cfg.key.key_slot_key_size = 64;
	err = sdhci_program_key(mmc, &cfg, slot);
	WARN_ON_ONCE(err);
}

/* Clear all keyslots at driver init time */
static void sdhci_clear_all_keyslots(struct mmc_host *mmc)
{
	int slot;

	if (mmc->fde.vops->do_reset) {
		mmc->fde.vops->do_reset(mmc->fde.pdev, 1);
	} else {
		for (slot = 0; slot < sdhci_num_keyslots(mmc); slot++)
			sdhci_clear_keyslot(mmc, slot);
	}
}

static u8 data_unit_size_valid(unsigned int data_unit_size)
{
	if (data_unit_size < 512 || data_unit_size > 65536 ||
	    !is_power_of_2(data_unit_size))
		return 0;

	return 1;
}


static int sdhci_crypto_keyslot_program(struct keyslot_manager *ksm,
					 const struct blk_crypto_key *key,
					 unsigned int slot)
{
	struct mmc_host *mmc = keyslot_manager_private(ksm);
	int err = 0;
	struct fde_crypto_config cfg;

	if (fde_debug_level & FDE_DBG_KEYINFO) {
		pr_devel("%s slot:%d, key->crypto_mode:%d, key.size:%d, key.data_unit_size:%d\n", __func__, slot, key->crypto_mode, key->size, key->data_unit_size);
	}

	//print_hex_dump(KERN_INFO, "key:", DUMP_PREFIX_ADDRESS, 16, 1, key->raw,
    //                  key->size, false);

	if (!sdhci_is_crypto_enabled(mmc) || !sdhci_keyslot_valid(mmc, slot)
		|| !data_unit_size_valid(key->data_unit_size)
		|| key->crypto_mode != BLK_ENCRYPTION_MODE_AES_256_XTS) {
		pr_err("%s not support or invalid parameter, crypto_mode:%d\n", __func__, key->crypto_mode);
		return -EINVAL;
	}

	if (key->size != 128/8 && key->size != 192/8 && key->size != 256/8 && key->size != 512/8) {
		pr_err("%s key size %d not support!\n", __func__, key->size);
		return -EINVAL;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.key_slot_sel = slot;
	memcpy((unsigned char *)cfg.key.key_slot_key, key->raw, key->size);
	cfg.key.key_slot_key_size = key->size;
	//cfg.key.key_slot_key_mode = key->crypto_mode;

	err = sdhci_program_key(mmc, &cfg, slot);

	memzero_explicit(&cfg, sizeof(cfg));

	return err;
}

static int sdhci_crypto_keyslot_evict(struct keyslot_manager *ksm,
				       const struct blk_crypto_key *key,
				       unsigned int slot)
{
	struct mmc_host *mmc = keyslot_manager_private(ksm);

	if (fde_debug_level & FDE_DBG_KEYINFO) {
		pr_err("%s slot:%d, key->crypto_mode:%d, key.size:%d, key.data_unit_size:%d\n", __func__, slot, key->crypto_mode, key->size, key->data_unit_size);
	}

	if (!sdhci_is_crypto_enabled(mmc) || !sdhci_keyslot_valid(mmc, slot))
		return -EINVAL;

	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	sdhci_clear_keyslot(mmc, slot);

	return 0;
}



static const struct keyslot_mgmt_ll_ops sdhci_ksm_ops = {
	.keyslot_program	= sdhci_crypto_keyslot_program,
	.keyslot_evict		= sdhci_crypto_keyslot_evict,
};

int sdhci_jlq_init_crypto(struct mmc_host *mmc, struct platform_device *pdev)
{
	struct device_node *node;
	struct device *dev = &pdev->dev;
	int ret = 0;
	unsigned int crypto_modes_supported[BLK_ENCRYPTION_MODE_MAX];

	memset(crypto_modes_supported, 0, sizeof(crypto_modes_supported));

	node = of_parse_phandle(pdev->dev.of_node, SDHC_JLQ_CRYPTO_LABEL, 0);
	if (!node) {
		dev_info(dev, "%s: " SDHC_JLQ_CRYPTO_LABEL " property not specified." \
			"FDE is not enable\n",	__func__);
		ret = 0;
		goto out_err;
	}

	mmc->fde.vops = jlq_fde_get_ops();
	mmc->fde.pdev = jlq_fde_get_pdevice(node);
	if (mmc->fde.pdev == ERR_PTR(-EPROBE_DEFER)) {
		dev_err(dev, "%s: FDE device not probed yet\n", __func__);
		mmc->fde.pdev = NULL;
		mmc->fde.vops = NULL;
		ret = -EPROBE_DEFER;
		goto out_err;
	}

	if (!mmc->fde.pdev) {
		dev_err(dev, "%s: invalid platform device\n", __func__);
		mmc->fde.vops = NULL;
		ret = -ENODEV;
		goto out_err;
	}
	if (!mmc->fde.vops) {
		dev_err(dev, "%s: invalid fde vops\n", __func__);
		mmc->fde.vops = NULL;
		ret = -ENODEV;
		goto out_err;
	}

	if (mmc->fde.vops->init)
		mmc->fde.vops->init(SDHC_FDE_TYPE_STR, mmc->fde.pdev, mmc);

	memset(crypto_modes_supported, 0, sizeof(crypto_modes_supported));
	crypto_modes_supported[BLK_ENCRYPTION_MODE_AES_256_XTS] = 4096; //0xFFFFFFFF; //todo????

	sdhci_clear_all_keyslots(mmc);

#define KSM_BLK_CRYPTO_FEATURE		(BLK_CRYPTO_FEATURE_STANDARD_KEYS		\
									/*| BLK_CRYPTO_FEATURE_WRAPPED_KEYS*/)

	mmc->ksm = keyslot_manager_create(dev, sdhci_num_keyslots(mmc),
					&sdhci_ksm_ops,
					KSM_BLK_CRYPTO_FEATURE,
					crypto_modes_supported, mmc);

	if (!mmc->ksm) {
		ret = -ENOMEM;
		dev_err(dev, "%s: keyslot create fail!\n", __func__);
		goto out_err;
	}

	mmc->caps2 |= MMC_CAP2_CRYPTO;
	keyslot_manager_set_max_dun_bytes(mmc->ksm, sizeof(u32));

	dev_info(dev, "%s: feature: 0x%x\n", __func__, KSM_BLK_CRYPTO_FEATURE);

	return 0;

out_err:
	return ret;
}
EXPORT_SYMBOL(sdhci_jlq_init_crypto);

int sdhci_check_mmc_rw(struct mmc_request *mrq)
{
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
						  brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	int w1r0 = -1;

	if (req) {
		struct mmc_data *data = mrq->data;

		if (!data)
			return 1;

		if (mmc_get_dma_dir(data) == DMA_FROM_DEVICE) {
			w1r0 = 0;
		} else if (mmc_get_dma_dir(data) == DMA_TO_DEVICE) {
			w1r0 = 1;
		} else {
			w1r0 = -1;
		}
		//printk("%s mrq->req:0x%llx, req:0x%llx", __func__, mrq->req, req);
		//WARN_ONCE((mrq->req != req), "%s mrq->req:0x%llx, req:0x%llx", __func__, mrq->req, req);
	}

	return w1r0;
}

int sdhci_crypto_context_alloc(struct mmc_host *mmc, struct mmc_request *mrq, int *tag)
{
	int ret = -EINVAL;
	int w1r0 = 1;

	if (mrq) {
		w1r0 = sdhci_check_mmc_rw(mrq);
	}
	if (mmc->fde.vops->alloc_context) {
		ret = mmc->fde.vops->alloc_context(mmc->fde.pdev, tag, w1r0);
	}

	if (ret < 0) {
		if (fde_debug_level & FDE_DBG_CONTEXT)
			pr_devel("%s fail!\n", __func__);

		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(sdhci_crypto_context_alloc);

int sdhci_crypto_context_free(struct mmc_host *mmc, struct mmc_request *mrq, int tag)
{
	int ret = -EINVAL;
	//int context;
	int w1r0 = 1;

	if (mrq) {
		w1r0 = sdhci_check_mmc_rw(mrq);
	}
	if (mmc->fde.vops->free_context) {
		ret = mmc->fde.vops->free_context(mmc->fde.pdev, tag, w1r0);
	}

	if (ret < 0) {
		if (fde_debug_level & FDE_DBG_CONTEXT)
			pr_devel("%s fail!\n", __func__);

		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(sdhci_crypto_context_free);

int sdhci_crypto_context_depth(struct mmc_host *mmc, int depth)
{
	unsigned long long bitreserved;
	int ret = -EINVAL;

	if (depth < 0 || depth >= 32) {
		pr_err("invalid depth:%d\n", depth);
		return -EINVAL;
	}

	bitreserved = (1 << depth) - 1;
	bitreserved = ~bitreserved;

	if (fde_debug_level & FDE_DBG_CONTEXT)
		pr_devel("%s depth:%d, bit:%llx\n", __func__, depth, bitreserved);

	if (mmc->fde.vops->set_context_reserved)
		ret = mmc->fde.vops->set_context_reserved(mmc->fde.pdev, bitreserved);

	return ret;
}
EXPORT_SYMBOL(sdhci_crypto_context_depth);

int sdhci_prepare_crypto(struct mmc_host *mmc, struct mmc_request *mrq, int tag, char is_nonecq)
{
	int ret = -EINVAL;
	struct fde_crypto_slot_config cfg;
	int context;
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
						  brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);

	if (!mrq->crypto_key) {
		return 0;
	}

	if (!sdhci_is_crypto_enabled(mmc) || !sdhci_keyslot_valid(mmc, mrq->crypto_key_slot)) {
		pr_err("%s not support or invalid parameter! enable:%d, valid:%d data_unit_num:%d\n", __func__, sdhci_is_crypto_enabled(mmc), sdhci_keyslot_valid(mmc, mrq->crypto_key_slot), mrq->data_unit_num);

		return -EINVAL;
	}

	memset(&cfg, 0, sizeof(cfg));

	cfg.iswrite = sdhci_check_mmc_rw(mrq);

	if (fde_debug_level & FDE_DBG_SLOTCFG) {
		pr_devel("%s mrq:0x%llx req:0x%llx, crypto_key_slot:%d crypto_key{ crypto_mode:%d data_unit_size:%d}\n", __func__, mrq, req, mrq->crypto_key_slot, mrq->crypto_key->crypto_mode, mrq->crypto_key->data_unit_size);

		pr_devel("$$mmc io tag:%d w:%d addr:0x%x, len:%d, nr_hw_queues:%d\n", req->tag, cfg.iswrite, req->__sector, req->__data_len, req->q->nr_hw_queues);
	}

	if (is_nonecq) {
		if (mmc->fde.vops->alloc_context)
			ret = mmc->fde.vops->alloc_context(mmc->fde.pdev, &context, cfg.iswrite);

		if (ret < 0) {
			pr_err("config keyslot fail!\n");
			return -1;
		}
	} else {
		if (fde_debug_level & FDE_DBG_SLOTCFG)
			pr_devel("use prealloced tag:%d\n", tag);

		context = tag;
	}

	mrq->crypto_context = context;

	cfg.slot_bypass = 0;
	cfg.context_sel = context;
	cfg.data_unit_size = mrq->crypto_key->data_unit_size;
	cfg.is_nonecq = is_nonecq;
	cfg.key_slot = mrq->crypto_key_slot;

	if (req)
		cfg.start_sector_number = DATA_UNIT_NUM(req->bio->bi_crypt_context->bc_dun[0]);

	if (fde_debug_level & FDE_DBG_SLOTCFG) {
		pr_devel("cfg:context_sel:%d, data_unit_size:%d, key_slot:%d, iswrite:%d, start_sector_number:%d, nonecqe:%d\n", cfg.context_sel, cfg.data_unit_size, cfg.key_slot, cfg.iswrite, cfg.start_sector_number, cfg.is_nonecq);
	}

	if (mmc->fde.vops->config_context)
		ret = mmc->fde.vops->config_context(mmc->fde.pdev, &cfg);
	return ret;
}
EXPORT_SYMBOL(sdhci_prepare_crypto);

int sdhci_complete_crypto(struct mmc_host *mmc, struct mmc_request *mrq, int tag, char is_nonecq)
{
	int ret = -EINVAL;
	struct fde_crypto_slot_config cfg;
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
						  brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);

	if (!mrq) {
		//printk(KERN_DEBUG "%s mrq:0x%llx\n", __func__, mrq);
		return 0;
	}

	if (!mrq->crypto_key) {
		//printk(KERN_DEBUG "%s mrq:0x%llx, req:0x%llx mrq->crypto_key:%llx\n", __func__, mrq, req, mrq->crypto_key);
		return 0;
	}

	if (!sdhci_is_crypto_enabled(mmc) || !sdhci_keyslot_valid(mmc, mrq->crypto_key_slot)
		/*|| !data_unit_size_valid(mrq->data_unit_num)*/) {
		pr_err("%s not support or invalid parameter! enable:%d, valid:%d data_unit_num:%d\n", __func__, sdhci_is_crypto_enabled(mmc), sdhci_keyslot_valid(mmc, mrq->crypto_key_slot), mrq->data_unit_num);

		return -EINVAL;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.iswrite = sdhci_check_mmc_rw(mrq);

	if (fde_debug_level & FDE_DBG_SLOTCFG) {
		pr_devel("%s mrq:%x, req:%x, tag:%d, crypto_key_slot:%d data_unit_num:%d crypto_key{ crypto_mode:%d data_unit_size:%d size:%d}\n", __func__, mrq, req, tag, mrq->crypto_key_slot, mrq->data_unit_num, mrq->crypto_key->crypto_mode, mrq->crypto_key->data_unit_size, mrq->crypto_key->size);
		//WARN_ON(tag != mrq->crypto_context);
	}

	cfg.slot_bypass = 1;
	cfg.context_sel = mrq->crypto_context;
	cfg.data_unit_size = mrq->crypto_key->data_unit_size;
	cfg.is_nonecq = is_nonecq;
	cfg.key_slot = mrq->crypto_key_slot;

	if (req)
		cfg.start_sector_number = 0;

	if (fde_debug_level & FDE_DBG_SLOTCFG) {
		pr_devel("$$mmc2 io tag:%d w:%d addr:0x%x, len:%d\n", req->tag, cfg.iswrite, req->__sector, req->__data_len);
		pr_devel("cmp cfg:context_sel:%d, data_unit_size:%d, key_slot:%d start_sector_number:%d, isnonecq:%d\n", cfg.context_sel, cfg.data_unit_size, cfg.key_slot, cfg.start_sector_number, cfg.is_nonecq);
	}


	if (is_nonecq) {
		if (mmc->fde.vops->free_context)
			ret = mmc->fde.vops->free_context(mmc->fde.pdev, mrq->crypto_context, cfg.iswrite);
		if (mmc->fde.vops->config_default_bypass)
			ret = mmc->fde.vops->config_default_bypass(mmc->fde.pdev, &cfg);
	} else {
		if (mmc->fde.vops->config_context)
			ret = mmc->fde.vops->config_context(mmc->fde.pdev, &cfg);
	}


	return ret;
}
EXPORT_SYMBOL(sdhci_complete_crypto);

int sdhci_crypto_runtime_level(struct mmc_host *mmc, int level)
{
	int ret = 0;

	if (mmc->fde.vops && mmc->fde.vops->runtime_level)
		ret = mmc->fde.vops->runtime_level(mmc->fde.pdev, level);

	return ret;
}
EXPORT_SYMBOL(sdhci_crypto_runtime_level);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("JLQ Inline Crypto Engine driver");

