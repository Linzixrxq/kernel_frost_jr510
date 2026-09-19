// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define DEBUG
#include <crypto/algapi.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/blkdev.h>
#include "ufshcd-crypto-jlq.h"

#define MINIMUM_DUN_SIZE 512
#define MAXIMUM_DUN_SIZE 65536

#define UFS_JLQ_CRYPTO_LABEL	"jlq-crypto"


#define NUM_KEYSLOTS(hba) (hba->crypto_capabilities.config_count + 1)

static struct ufs_hba_crypto_variant_ops ufshcd_crypto_jlq_variant_ops = {
	.hba_init_crypto = ufshcd_crypto_jlq_init_crypto,
	.prepare_lrbp_crypto = ufshcd_crypto_jlq_prep_lrbp_crypto,
	.complete_lrbp_crypto = ufshcd_crypto_jlq_complete_lrbp_crypto
};


static int ufshcd_program_key(struct sdhci_jlq_fde_data *fde,
			      const struct fde_crypto_config *cfg, int slot)
{
	int ret = -EINVAL;

	if (fde->vops->program_key)
		ret = fde->vops->program_key(fde->pdev, cfg, slot);
	return ret;

}

static void ufshcd_clear_keyslot(struct sdhci_jlq_fde_data *fde, int slot)
{
	struct fde_crypto_config cfg = { 0 };
	int err;

	cfg.key.key_slot_key_size = 64;
	err = ufshcd_program_key(fde, &cfg, slot);
	WARN_ON_ONCE(err);
}

static int ufshcd_fde_keyslots(struct sdhci_jlq_fde_data *fde)
{
	if (fde->vops && fde->vops->get_keyslot_num)
		return fde->vops->get_keyslot_num(fde->pdev);
	else
		return 0;
}

/* Clear all keyslots at driver init time */
static void ufshcd_clear_all_keyslots(struct sdhci_jlq_fde_data *fde)
{
	int slot;
	int total = ufshcd_fde_keyslots(fde);

	if (fde->vops->do_reset) {
		fde->vops->do_reset(fde->pdev, 1);
	} else {
		for (slot = 0; slot < total; slot++)
			ufshcd_clear_keyslot(fde, slot);
	}
}

#if 0
static u8 data_unit_size_valid(unsigned int data_unit_size)
{
	if (data_unit_size < 512 || data_unit_size > 65536 ||
	    !is_power_of_2(data_unit_size))
		return 0;

	return 1;
}
#endif

static int ufshcd_crypto_jlq_keyslot_program(struct keyslot_manager *ksm,
					     const struct blk_crypto_key *key,
					     unsigned int slot)
{
	struct ufs_hba *hba = keyslot_manager_private(ksm);
	int err = 0;
	struct fde_crypto_config cfg;

	pr_info("%s slot:%d, key->crypto_mode:%d, key.size:%d, key.data_unit_size:%d\n", __func__, slot, key->crypto_mode, key->size, key->data_unit_size);

	memset(&cfg, 0, sizeof(cfg));
	cfg.key_slot_sel = slot;
	memcpy((unsigned char *)cfg.key.key_slot_key, key->raw, key->size);
	cfg.key.key_slot_key_size = key->size;
	//cfg.key.key_slot_key_mode = key->crypto_mode;

	err = ufshcd_program_key(&hba->fde, &cfg, slot);

	memzero_explicit(&cfg, sizeof(cfg));

	return err;
}

static int ufshcd_crypto_jlq_keyslot_evict(struct keyslot_manager *ksm,
					   const struct blk_crypto_key *key,
					   unsigned int slot)
{
	int err = 0;
	struct ufs_hba *hba = keyslot_manager_private(ksm);

	pr_err("%s slot:%d, key->crypto_mode:%d, key.size:%d, key.data_unit_size:%d\n", __func__, slot, key->crypto_mode, key->size, key->data_unit_size);

	if (!ufshcd_is_crypto_enabled(hba) || !ufshcd_keyslot_valid(hba, slot))
		return -EINVAL;

	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	ufshcd_clear_keyslot(&hba->fde, slot);

	return err;
}

static const struct keyslot_mgmt_ll_ops ufshcd_crypto_jlq_ksm_ops = {
	.keyslot_program	= ufshcd_crypto_jlq_keyslot_program,
	.keyslot_evict		= ufshcd_crypto_jlq_keyslot_evict,
};

#if 0
static enum blk_crypto_mode_num ufshcd_blk_crypto_jlq_mode_num_for_alg_dusize(
					enum ufs_crypto_alg ufs_crypto_alg,
					enum ufs_crypto_key_size key_size)
{
	/*
	 * This is currently the only mode that UFS and blk-crypto both support.
	 */
	if (ufs_crypto_alg == UFS_CRYPTO_ALG_AES_XTS &&
		key_size == UFS_CRYPTO_KEY_SIZE_256)
		return BLK_ENCRYPTION_MODE_AES_256_XTS;

	return BLK_ENCRYPTION_MODE_INVALID;
}
#endif

static int ufshcd_hba_init_crypto_jlq_spec(struct ufs_hba *hba,
				    const struct keyslot_mgmt_ll_ops *ksm_ops)
{
	int err = 0;
	unsigned int crypto_modes_supported[BLK_ENCRYPTION_MODE_MAX];

	/* Default to disabling crypto */
	hba->caps &= ~UFSHCD_CAP_CRYPTO;

	/*
	 * Crypto Capabilities should never be 0, because the
	 * config_array_ptr > 04h. So we use a 0 value to indicate that
	 * crypto init failed, and can't be enabled.
	 */
	hba->crypto_capabilities.reg_val =
			  cpu_to_le32(ufshcd_readl(hba, REG_UFS_CCAP));
	hba->crypto_cfg_register =
		 (u32)hba->crypto_capabilities.config_array_ptr * 0x100;
	hba->crypto_cap_array =
		 devm_kcalloc(hba->dev,
				hba->crypto_capabilities.num_crypto_cap,
				sizeof(hba->crypto_cap_array[0]),
				GFP_KERNEL);

	pr_info("%s capabilities:0x%x, reg_val:0x%x, crypto_cfg_register:0x%x\n", __func__, hba->capabilities, hba->crypto_capabilities.reg_val, hba->crypto_cfg_register);
	if (hba->crypto_capabilities.config_count != (ufshcd_fde_keyslots(&hba->fde) - 1)) {
		hba->crypto_capabilities.config_count = ufshcd_fde_keyslots(&hba->fde) - 1;
		if (hba->crypto_capabilities.config_count >= 31) {
			hba->crypto_capabilities.config_count = 0;
		}
		pr_info("force config_count:%d, fde_slot:%d\n", hba->crypto_capabilities.config_count, ufshcd_fde_keyslots(&hba->fde));
	}

	if (hba->fde.vops->init)
		hba->fde.vops->init(UFS_FDE_TYPE_STR, hba->fde.pdev, hba);

	memset(crypto_modes_supported, 0, sizeof(crypto_modes_supported));
	crypto_modes_supported[BLK_ENCRYPTION_MODE_AES_256_XTS] = 4096;

	ufshcd_clear_all_keyslots(&hba->fde);

	hba->ksm = keyslot_manager_create(hba->dev, ufshcd_fde_keyslots(&hba->fde),
					ksm_ops,
					BLK_CRYPTO_FEATURE_STANDARD_KEYS,
					crypto_modes_supported, hba);

	if (!hba->ksm) {
		err = -ENOMEM;
		pr_err("%s: keyslot create fail!\n", __func__);
		goto out_err;
	}

	hba->caps |= UFSHCD_CAP_CRYPTO;
	keyslot_manager_set_max_dun_bytes(hba->ksm, sizeof(u64));
	pr_info("keyslot_manager_create ksm=%p, hba:%p, caps:0x%x\n", hba->ksm, hba, hba->caps);

	return 0;

out_err:
	hba->crypto_capabilities.reg_val = 0;
	return err;
}

int ufshcd_crypto_jlq_init_crypto(struct ufs_hba *hba,
				  const struct keyslot_mgmt_ll_ops *ksm_ops)
{
	int err = 0;
	struct platform_device *pdev = to_platform_device(hba->dev);
	struct device_node *node;
	struct device *dev = &pdev->dev;

	node = of_parse_phandle(pdev->dev.of_node, UFS_JLQ_CRYPTO_LABEL, 0);
	if (!node) {
		dev_info(dev, "%s: " UFS_JLQ_CRYPTO_LABEL " property not specified."
			"FDE is not enable\n",	__func__);
		err = 0;
		goto out_err;
	}

	hba->fde.vops = jlq_fde_get_ops();
	hba->fde.pdev = jlq_fde_get_pdevice(node);
	if (hba->fde.pdev == ERR_PTR(-EPROBE_DEFER)) {
		dev_err(dev, "%s: FDE device not probed yet\n", __func__);
		hba->fde.pdev = NULL;
		hba->fde.vops = NULL;
		err = -EPROBE_DEFER;
		goto out_err;
	}

	if (!hba->fde.pdev) {
		dev_err(dev, "%s: invalid platform device\n", __func__);
		hba->fde.vops = NULL;
		err = -ENODEV;
		goto out_err;
	}
	if (!hba->fde.vops) {
		dev_err(dev, "%s: invalid fde vops\n", __func__);
		hba->fde.vops = NULL;
		err = -ENODEV;
		goto out_err;
	}

	err = ufshcd_hba_init_crypto_jlq_spec(hba, &ufshcd_crypto_jlq_ksm_ops);
	if (err) {
		pr_err("%s: Error initiating crypto capabilities, err %d\n",
					__func__, err);
		return err;
	}

out_err:
	if (err) {
		pr_err("%s: Error initiating crypto, err %d\n",
					__func__, err);
	}
	return err;
}

int ufshcd_crypto_jlq_prep_lrbp_crypto(struct ufs_hba *hba,
				       struct scsi_cmnd *cmd,
				       struct ufshcd_lrb *lrbp)
{
	struct bio_crypt_ctx *bc;
	int ret = 0;
	struct request *req;
	struct fde_crypto_slot_config cfg;
	struct utp_upiu_req *ucd_req_ptr = lrbp->ucd_req_ptr;
	u32 lba = 0;
	u16 blocks = 0;
	int context;
	unsigned long flags;

	lrbp->crypto_enable = false;
	req = cmd->request;
	if (!req || !req->bio || !ucd_req_ptr)
		return ret;

	if (!bio_crypt_should_process(req)) {
		return ret;
	}
	bc = req->bio->bi_crypt_context;

	if (WARN_ON(!ufshcd_is_crypto_enabled(hba))) {
		/*
		 * Upper layer asked us to do inline encryption
		 * but that isn't enabled, so we fail this request.
		 */
		return -EINVAL;
	}
	if (!ufshcd_keyslot_valid(hba, bc->bc_keyslot))
		return -EINVAL;

	lrbp->crypto_enable = true;
	lrbp->crypto_key_slot = bc->bc_keyslot;

	pr_devel("@@data_unit_num:%d, sector:%d, bc_dun[0]:%d\n", lrbp->data_unit_num, (u32)cmd->request->bio->bi_iter.bi_sector, bc->bc_dun[0]);

	if (ucd_req_ptr) {
		lba = (ucd_req_ptr->sc.cdb[2] << 24) | (ucd_req_ptr->sc.cdb[3] << 16) |
							(ucd_req_ptr->sc.cdb[4] << 8) | ucd_req_ptr->sc.cdb[5];
		blocks = (ucd_req_ptr->sc.cdb[7] << 8) | ucd_req_ptr->sc.cdb[8];
	}

	memset(&cfg, 0, sizeof(cfg));

	pr_devel("%s crypto_enable:%d, crypto_key_slot:%d lba:%d(0x%x), blocks:%d}\n", __func__, lrbp->crypto_enable, lrbp->crypto_key_slot, lba, lba, blocks);

	cfg.slot_bypass = !lrbp->crypto_enable;
	cfg.data_unit_size = 4096;
	cfg.is_nonecq = 0;
	cfg.key_slot = lrbp->crypto_key_slot;
	if (rq_data_dir(req) == WRITE) {
		cfg.iswrite = 1;
	} else {
		cfg.iswrite = 0;
	}
	cfg.start_sector_number = lba;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->fde.vops->alloc_context)
		ret = hba->fde.vops->alloc_context(hba->fde.pdev, &context, cfg.iswrite);

	if (ret < 0) {
		pr_err("config keyslot fail!\n");
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		return -1;
	}

	cfg.context_sel = context;
	lrbp->crypto_context = context;

	pr_devel("cfg:context_sel:%d, data_unit_size:%d, key_slot:%d, iswrite:%d start_sector_number:%d\n", cfg.context_sel, cfg.data_unit_size, cfg.key_slot, cfg.iswrite, cfg.start_sector_number);
	if (hba->fde.vops->config_context)
		ret = hba->fde.vops->config_context(hba->fde.pdev, &cfg);


	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return 0;
}

int ufshcd_crypto_jlq_complete_lrbp_crypto(struct ufs_hba *hba,
				       struct scsi_cmnd *cmd,
				       struct ufshcd_lrb *lrbp)
{
	struct bio_crypt_ctx *bc;
	int ret = 0;
	struct request *req;
	struct fde_crypto_slot_config cfg;

	lrbp->crypto_enable = false;
	req = cmd->request;
	if (!req || !req->bio)
		return ret;

	if (!bio_crypt_should_process(req)) {
		return ret;
	}

	bc = req->bio->bi_crypt_context;

	if (WARN_ON(!ufshcd_is_crypto_enabled(hba))) {
		/*
		 * Upper layer asked us to do inline encryption
		 * but that isn't enabled, so we fail this request.
		 */
		return -EINVAL;
	}
	if (!ufshcd_keyslot_valid(hba, bc->bc_keyslot))
		return -EINVAL;

	pr_devel("crypto complete:data_unit_num:%d, sector:%d, bc_dun[0]:%d\n", lrbp->data_unit_num, (u32)cmd->request->bio->bi_iter.bi_sector, bc->bc_dun[0]);

	memset(&cfg, 0, sizeof(cfg));
	cfg.slot_bypass = 1;
	cfg.context_sel = lrbp->crypto_context;
	cfg.data_unit_size = 4096;
	cfg.is_nonecq = 0;

	cfg.key_slot = lrbp->crypto_key_slot;
	if (rq_data_dir(req) == WRITE) {
		cfg.iswrite = 1;
	} else {
		cfg.iswrite = 0;
	}
	cfg.start_sector_number = 0;
	//if (hba->fde.vops->config_context)
	//	ret = hba->fde.vops->config_context(hba->fde.pdev, &cfg);

	if (hba->fde.vops->free_context)
		ret = hba->fde.vops->free_context(hba->fde.pdev, cfg.context_sel, cfg.iswrite);

	return ret;
}

void ufshcd_crypto_jlq_set_vops(struct ufs_hba *hba)
{
	return ufshcd_crypto_set_vops(hba, &ufshcd_crypto_jlq_variant_ops);
}

