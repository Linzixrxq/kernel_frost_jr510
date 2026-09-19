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


#ifndef __SDHCI_CRYPTO_H__
#define __SDHCI_CRYPTO_H__

#define SDHC_JLQ_CRYPTO_LABEL	"jlq-crypto"

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
#include <soc/jlq/jr510/fde.h>
#include <linux/keyslot-manager.h>

struct sdhci_host;



static inline int sdhci_num_keyslots(struct mmc_host *mmc)
{
	if (mmc->fde.vops && mmc->fde.vops->get_keyslot_num)
		return mmc->fde.vops->get_keyslot_num(mmc->fde.pdev);
	else
		return 0;
}

static inline bool sdhci_is_crypto_enabled(struct mmc_host *mmc)
{
	return mmc->caps2 & MMC_CAP2_CRYPTO;
}

static inline bool sdhci_keyslot_valid(struct mmc_host *mmc, unsigned int slot)
{
	return slot < sdhci_num_keyslots(mmc);
}


int sdhci_jlq_init_crypto(struct mmc_host *mmc, struct platform_device *pdev);

int sdhci_prepare_crypto(struct mmc_host *mmc, struct mmc_request *mrq, int tag, char is_nonecq);

int sdhci_complete_crypto(struct mmc_host *mmc, struct mmc_request *mrq, int tag, char is_nonecq);

int sdhci_crypto_context_alloc(struct mmc_host *mmc, struct mmc_request *mrq, int *tag);
int sdhci_crypto_context_free(struct mmc_host *mmc, struct mmc_request *mrq, int tag);
int sdhci_crypto_context_depth(struct mmc_host *mmc, int depth);

int sdhci_jlq_crypto_config_keyslot(struct mmc_host *mmc, struct mmc_request *mrq, char is_nonecq);

int sdhci_jlq_crypto_config_keyslot_bypass(struct mmc_host *mmc, struct mmc_request *mrq, char is_nonecq);
int sdhci_crypto_runtime_level(struct mmc_host *mmc, int level);


#else
static inline int sdhci_num_keyslots(struct mmc_host *mmc)
{
	return 0;
}

static inline int sdhci_jlq_init_crypto(struct mmc_host *mmc, struct platform_device *pdev)
{
	return 0;
}

static inline int sdhci_prepare_crypto(struct mmc_host *mmc, struct mmc_request *mrq, int tag, char is_nonecq)
{
	return 0;
}

static inline int sdhci_complete_crypto(struct mmc_host *mmc, struct mmc_request *mrq, int tag, char is_nonecq)
{
	return 0;
}

static inline int sdhci_crypto_context_alloc(struct mmc_host *mmc, struct mmc_request *mrq, int *tag)
{
	return -1;
}
static inline int sdhci_crypto_context_free(struct mmc_host *mmc, struct mmc_request *mrq, int tag)
{
	return -1;
}

static inline int sdhci_jlq_crypto_config_keyslot(struct mmc_host *mmc, struct mmc_request *mrq, char is_nonecq)
{
	return 0;
}

static inline int sdhci_jlq_crypto_config_keyslot_bypass(struct mmc_host *mmc, struct mmc_request *mrq, char is_nonecq)
{
	return 0;
}
static inline int sdhci_crypto_runtime_level(struct mmc_host *mmc, int level)
{
	return 0;
}
static inline int sdhci_crypto_context_depth(struct mmc_host *mmc, int depth)
{
	return 0;
}
#endif


#endif /* _UFSHCD_CRYPTO_H */
