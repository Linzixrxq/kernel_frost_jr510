// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/platform_device.h>
#include <linux/ktime.h>
#include <linux/bitops.h>

#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>

#include "cqhci.h"


#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
#define FORCE_QBR_MODE
#endif

#include "sdhci-crypto.h"


#define DCMD_SLOT 31
#define NUM_SLOTS 32

struct cqhci_slot {
	struct mmc_request *mrq;
	unsigned int flags;
#define CQHCI_EXTERNAL_TIMEOUT	BIT(0)
#define CQHCI_COMPLETED		BIT(1)
#define CQHCI_HOST_CRC		BIT(2)
#define CQHCI_HOST_TIMEOUT	BIT(3)
#define CQHCI_HOST_OTHER	BIT(4)
};

static inline u8 *get_desc(struct cqhci_host *cq_host, u8 tag)
{
	return cq_host->desc_base + (tag * cq_host->slot_sz);
}

static inline u8 *get_link_desc(struct cqhci_host *cq_host, u8 tag)
{
	u8 *desc = get_desc(cq_host, tag);

	return desc + cq_host->task_desc_len;
}

static inline dma_addr_t get_trans_desc_dma(struct cqhci_host *cq_host, u8 tag)
{
#if IS_ENABLED(CONFIG_SDC_JLQ)
	/* workaround of DMA 128MB boundary */
	return cq_host->trans_desc_dma_base +
		(cq_host->trans_desc_max_num * tag *
		 cq_host->trans_desc_len);
#else
	return cq_host->trans_desc_dma_base +
		(cq_host->mmc->max_segs * tag *
		 cq_host->trans_desc_len);
#endif
}

static inline u8 *get_trans_desc(struct cqhci_host *cq_host, u8 tag)
{
#if IS_ENABLED(CONFIG_SDC_JLQ)
	/* workaround of DMA 128MB boundary */
	return cq_host->trans_desc_base +
		(cq_host->trans_desc_len * cq_host->trans_desc_max_num * tag);
#else
	return cq_host->trans_desc_base +
		(cq_host->trans_desc_len * cq_host->mmc->max_segs * tag);
#endif
}

static void setup_trans_desc(struct cqhci_host *cq_host, u8 tag)
{
	u8 *link_temp;
	dma_addr_t trans_temp;

	link_temp = get_link_desc(cq_host, tag);
	trans_temp = get_trans_desc_dma(cq_host, tag);

	memset(link_temp, 0, cq_host->link_desc_len);
	if (cq_host->link_desc_len > 8)
		*(link_temp + 8) = 0;

	if (tag == DCMD_SLOT && (cq_host->mmc->caps2 & MMC_CAP2_CQE_DCMD)) {
		*link_temp = CQHCI_VALID(0) | CQHCI_ACT(0) | CQHCI_END(1);
		return;
	}

	*link_temp = CQHCI_VALID(1) | CQHCI_ACT(0x6) | CQHCI_END(0);

	if (cq_host->dma64) {
		__le64 *data_addr = (__le64 __force *)(link_temp + 4);

		data_addr[0] = cpu_to_le64(trans_temp);
	} else {
		__le32 *data_addr = (__le32 __force *)(link_temp + 4);

		data_addr[0] = cpu_to_le32(trans_temp);
	}
}

static void cqhci_set_irqs(struct cqhci_host *cq_host, u32 set)
{
	cqhci_writel(cq_host, set, CQHCI_ISTE);
	cqhci_writel(cq_host, set, CQHCI_ISGE);
}

#define DRV_NAME "cqhci"

#define CQHCI_DUMP(f, x...) \
	pr_err("%s: " DRV_NAME ": " f, mmc_hostname(mmc), ## x)

static void cqhci_dumpregs(struct cqhci_host *cq_host)
{
	struct mmc_host *mmc = cq_host->mmc;

	CQHCI_DUMP("============ CQHCI REGISTER DUMP ===========\n");

	CQHCI_DUMP("Caps:      0x%08x | Version:  0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_CAP),
		   cqhci_readl(cq_host, CQHCI_VER));
	CQHCI_DUMP("Config:    0x%08x | Control:  0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_CFG),
		   cqhci_readl(cq_host, CQHCI_CTL));
	CQHCI_DUMP("Int stat:  0x%08x | Int enab: 0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_IS),
		   cqhci_readl(cq_host, CQHCI_ISTE));
	CQHCI_DUMP("Int sig:   0x%08x | Int Coal: 0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_ISGE),
		   cqhci_readl(cq_host, CQHCI_IC));
	CQHCI_DUMP("TDL base:  0x%08x | TDL up32: 0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_TDLBA),
		   cqhci_readl(cq_host, CQHCI_TDLBAU));
	CQHCI_DUMP("Doorbell:  0x%08x | TCN:      0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_TDBR),
		   cqhci_readl(cq_host, CQHCI_TCN));
	CQHCI_DUMP("Dev queue: 0x%08x | Dev Pend: 0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_DQS),
		   cqhci_readl(cq_host, CQHCI_DPT));
	CQHCI_DUMP("Task clr:  0x%08x | SSC1:     0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_TCLR),
		   cqhci_readl(cq_host, CQHCI_SSC1));
	CQHCI_DUMP("SSC2:      0x%08x | DCMD rsp: 0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_SSC2),
		   cqhci_readl(cq_host, CQHCI_CRDCT));
	CQHCI_DUMP("RED mask:  0x%08x | TERRI:    0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_RMEM),
		   cqhci_readl(cq_host, CQHCI_TERRI));
	CQHCI_DUMP("Resp idx:  0x%08x | Resp arg: 0x%08x\n",
		   cqhci_readl(cq_host, CQHCI_CRI),
		   cqhci_readl(cq_host, CQHCI_CRA));

	if (cq_host->ops->dumpregs)
		cq_host->ops->dumpregs(mmc);
	else
		CQHCI_DUMP(": ===========================================\n");
}

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
void cqhci_dumpregs_ext(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;

	if (mmc && cq_host)
		cqhci_dumpregs(cq_host);
}
#endif

/**
 * The allocated descriptor table for task, link & transfer descritors
 * looks like:
 * |----------|
 * |task desc |  |->|----------|
 * |----------|  |  |trans desc|
 * |link desc-|->|  |----------|
 * |----------|          .
 *      .                .
 *  no. of slots      max-segs
 *      .           |----------|
 * |----------|
 * The idea here is to create the [task+trans] table and mark & point the
 * link desc to the transfer desc table on a per slot basis.
 */
static int cqhci_host_alloc_tdl(struct cqhci_host *cq_host)
{
	int i = 0;

	/* task descriptor can be 64/128 bit irrespective of arch */
	if (cq_host->caps & CQHCI_TASK_DESC_SZ_128) {
		cqhci_writel(cq_host, cqhci_readl(cq_host, CQHCI_CFG) |
			       CQHCI_TASK_DESC_SZ, CQHCI_CFG);
		cq_host->task_desc_len = 16;
	} else {
		cq_host->task_desc_len = 8;
	}

	/*
	 * 96 bits length of transfer desc instead of 128 bits which means
	 * ADMA would expect next valid descriptor at the 96th bit
	 * or 128th bit
	 */
	if (cq_host->dma64) {
		if (cq_host->quirks & CQHCI_QUIRK_SHORT_TXFR_DESC_SZ)
			cq_host->trans_desc_len = 12;
		else
			cq_host->trans_desc_len = 16;
		cq_host->link_desc_len = 16;
	} else {
		cq_host->trans_desc_len = 8;
		cq_host->link_desc_len = 8;
	}

	/* total size of a slot: 1 task & 1 transfer (link) */
	cq_host->slot_sz = cq_host->task_desc_len + cq_host->link_desc_len;

	cq_host->desc_size = cq_host->slot_sz * cq_host->num_slots;

#if IS_ENABLED(CONFIG_SDC_JLQ)
	/* workaround of DMA 128MB boundary */
	cq_host->trans_desc_max_num = cq_host->mmc->max_segs + EXTRA_SEGS;
	cq_host->data_size = cq_host->trans_desc_len * cq_host->trans_desc_max_num *
		cq_host->mmc->cqe_qdepth;

	pr_debug("%s: cqhci: task_desc_len: %d - link_desc_len: %d\n"
		"- trans_desc_len %d - trans_desc_max_num: %d - cqe_qdepth %d\n",
		 mmc_hostname(cq_host->mmc),
		 cq_host->task_desc_len,
		 cq_host->link_desc_len,
		 cq_host->trans_desc_len,
		 cq_host->trans_desc_max_num,
		 cq_host->mmc->cqe_qdepth);
#else
	cq_host->data_size = cq_host->trans_desc_len * cq_host->mmc->max_segs *
		cq_host->mmc->cqe_qdepth;
#endif

	pr_debug("%s: cqhci: desc_size: %zu data_sz: %zu slot-sz: %d\n",
		 mmc_hostname(cq_host->mmc), cq_host->desc_size, cq_host->data_size,
		 cq_host->slot_sz);

	/*
	 * allocate a dma-mapped chunk of memory for the descriptors
	 * allocate a dma-mapped chunk of memory for link descriptors
	 * setup each link-desc memory offset per slot-number to
	 * the descriptor table.
	 */
	cq_host->desc_base = dmam_alloc_coherent(mmc_dev(cq_host->mmc),
						 cq_host->desc_size,
						 &cq_host->desc_dma_base,
						 GFP_KERNEL);
	if (!cq_host->desc_base)
		return -ENOMEM;

	cq_host->trans_desc_base = dmam_alloc_coherent(mmc_dev(cq_host->mmc),
					      cq_host->data_size,
					      &cq_host->trans_desc_dma_base,
					      GFP_KERNEL);
	if (!cq_host->trans_desc_base) {
		dmam_free_coherent(mmc_dev(cq_host->mmc), cq_host->desc_size,
				   cq_host->desc_base,
				   cq_host->desc_dma_base);
		cq_host->desc_base = NULL;
		cq_host->desc_dma_base = 0;
		return -ENOMEM;
	}

	pr_debug("%s: cqhci: desc-base: 0x%p trans-base: 0x%p\n desc_dma 0x%llx trans_dma: 0x%llx\n",
		 mmc_hostname(cq_host->mmc), cq_host->desc_base, cq_host->trans_desc_base,
		(unsigned long long)cq_host->desc_dma_base,
		(unsigned long long)cq_host->trans_desc_dma_base);

	for (; i < (cq_host->num_slots); i++)
		setup_trans_desc(cq_host, i);

	return 0;
}

static void __cqhci_enable(struct cqhci_host *cq_host)
{
	struct mmc_host *mmc = cq_host->mmc;
	u32 cqcfg;

	cqcfg = cqhci_readl(cq_host, CQHCI_CFG);

	/* Configuration must not be changed while enabled */
	if (cqcfg & CQHCI_ENABLE) {
		cqcfg &= ~CQHCI_ENABLE;
		cqhci_writel(cq_host, cqcfg, CQHCI_CFG);
	}

	cqcfg &= ~(CQHCI_DCMD | CQHCI_TASK_DESC_SZ);

	if (mmc->caps2 & MMC_CAP2_CQE_DCMD)
		cqcfg |= CQHCI_DCMD;

	if (cq_host->caps & CQHCI_TASK_DESC_SZ_128)
		cqcfg |= CQHCI_TASK_DESC_SZ;

	cqhci_writel(cq_host, cqcfg, CQHCI_CFG);

	cqhci_writel(cq_host, lower_32_bits(cq_host->desc_dma_base),
		     CQHCI_TDLBA);
	cqhci_writel(cq_host, upper_32_bits(cq_host->desc_dma_base),
		     CQHCI_TDLBAU);

#if IS_ENABLED(CONFIG_SDC_JLQ)
	cqhci_writel(cq_host, CQHCI_SSC1_VAL, CQHCI_SSC1);
#endif
	cqhci_writel(cq_host, cq_host->rca, CQHCI_SSC2);

	cqhci_set_irqs(cq_host, 0);

	cqcfg |= CQHCI_ENABLE;

	cqhci_writel(cq_host, cqcfg, CQHCI_CFG);

	if (cqhci_readl(cq_host, CQHCI_CTL) & CQHCI_HALT)
		cqhci_writel(cq_host, 0, CQHCI_CTL);

#if IS_ENABLED(CONFIG_SDC_JLQ)
	pr_debug("%s: cqhci: CQE unhalt\n", mmc_hostname(mmc));
	if (cqhci_readl(cq_host, CQHCI_CTL) & CQHCI_HALT) {
		pr_err("%s: cqhci: CQE failed to exit halt state\n",
			   mmc_hostname(mmc));
	}
#endif

	mmc->cqe_on = true;

	if (cq_host->ops->enable)
		cq_host->ops->enable(mmc);

	/* Ensure all writes are done before interrupts are enabled */
	wmb();

	cqhci_set_irqs(cq_host, CQHCI_IS_MASK);

	cq_host->activated = true;
}

static void __cqhci_disable(struct cqhci_host *cq_host)
{
	u32 cqcfg;
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	int tag;
	struct cqhci_slot *slot;

	pr_debug("%s: %s\n",
		 mmc_hostname(cq_host->mmc), __func__);


	if (!cq_host->recovery_halt) {
		if (cq_host->qcnt)
			pr_notice("%s: %s: qcnt %d\n",
				mmc_hostname(cq_host->mmc),
				__func__, cq_host->qcnt);

		for (tag = 0; tag < cq_host->num_slots; tag++) {
			slot = &cq_host->slot[tag];
			if (slot->mrq)
				pr_notice("%s: cqhci: tag %d Pending\n",
					mmc_hostname(cq_host->mmc), tag);
		}
	}
#endif
	cqcfg = cqhci_readl(cq_host, CQHCI_CFG);
	cqcfg &= ~CQHCI_ENABLE;
	cqhci_writel(cq_host, cqcfg, CQHCI_CFG);

	cq_host->mmc->cqe_on = false;

	cq_host->activated = false;
}

int cqhci_deactivate(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;

	if (cq_host->enabled && cq_host->activated)
		__cqhci_disable(cq_host);

	return 0;
}
EXPORT_SYMBOL(cqhci_deactivate);

int cqhci_resume(struct mmc_host *mmc)
{
	/* Re-enable is done upon first request */
	return 0;
}
EXPORT_SYMBOL(cqhci_resume);

static int cqhci_enable(struct mmc_host *mmc, struct mmc_card *card)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	int err;

	if (!card->ext_csd.cmdq_en)
		return -EINVAL;

	if (cq_host->enabled)
		return 0;

	cq_host->rca = card->rca;

	err = cqhci_host_alloc_tdl(cq_host);
	if (err) {
		pr_err("%s: Failed to enable CQE, error %d\n",
		       mmc_hostname(mmc), err);
		return err;
	}

	__cqhci_enable(cq_host);

	cq_host->enabled = true;

#ifdef DEBUG
	cqhci_dumpregs(cq_host);
#endif
	return 0;
}

/* CQHCI is idle and should halt immediately, so set a small timeout */
#define CQHCI_OFF_TIMEOUT 100

static u32 cqhci_read_ctl(struct cqhci_host *cq_host)
{
	return cqhci_readl(cq_host, CQHCI_CTL);
}

static void cqhci_off(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 reg;
	int err;
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	int tag;
	struct cqhci_slot *slot;
#endif

	if (!cq_host->enabled || !mmc->cqe_on || cq_host->recovery_halt)
		return;

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
		if (cq_host->qcnt ||
			cqhci_readl(cq_host, CQHCI_TDBR) ||
			cqhci_readl(cq_host, CQHCI_DPT))
			pr_err("%s: %s: qcnt %d TDBR %08x DPT %08x\n",
				mmc_hostname(mmc),
				__func__,
				cq_host->qcnt,
				cqhci_readl(cq_host, CQHCI_TDBR),
				cqhci_readl(cq_host, CQHCI_DPT));
#endif

	if (cq_host->ops->disable)
		cq_host->ops->disable(mmc, false);

	cqhci_writel(cq_host, CQHCI_HALT, CQHCI_CTL);

	err = readx_poll_timeout(cqhci_read_ctl, cq_host, reg,
				 reg & CQHCI_HALT, 0, CQHCI_OFF_TIMEOUT);

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	if (cq_host->qcnt)
		pr_notice("%s: %s: qcnt %d\n",
			mmc_hostname(mmc),
			__func__,
			cq_host->qcnt);

	for (tag = 0; tag < cq_host->num_slots; tag++) {
		slot = &cq_host->slot[tag];
		if (slot->mrq)
			pr_notice("%s: cqhci: tag %d Pending\n",
				mmc_hostname(mmc), tag);
	}

	cqhci_check_pending_tasks(mmc, __func__, __LINE__);
#endif

	if (err < 0)
		pr_err("%s: cqhci: CQE stuck on\n", mmc_hostname(mmc));
	else
		pr_debug("%s: cqhci: CQE off\n", mmc_hostname(mmc));

	mmc->cqe_on = false;

#if IS_ENABLED(CONFIG_SDC_JLQ)
	/* workaround of halt issue */
	__cqhci_disable(cq_host);
#endif
}

static void cqhci_disable(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;

	if (!cq_host->enabled)
		return;

	cqhci_off(mmc);

	__cqhci_disable(cq_host);

	dmam_free_coherent(mmc_dev(mmc), cq_host->data_size,
			   cq_host->trans_desc_base,
			   cq_host->trans_desc_dma_base);

	dmam_free_coherent(mmc_dev(mmc), cq_host->desc_size,
			   cq_host->desc_base,
			   cq_host->desc_dma_base);

	cq_host->trans_desc_base = NULL;
	cq_host->desc_base = NULL;

	cq_host->enabled = false;
}

static void cqhci_prep_task_desc(struct mmc_request *mrq,
					u64 *data, bool intr)
{
	u32 req_flags = mrq->data->flags;
#ifdef FORCE_QBR_MODE
	u64 qbr;
#endif

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	pr_debug("%s: %s: data-tag: 0x%08x - dir: %d\n"
		"- prio: %d - blk cnt: 0x%08x - addr: 0x%llx\n",
		 mmc_hostname(mrq->host),
		 __func__,
		 !!(req_flags & MMC_DATA_DAT_TAG),
		 !!(req_flags & MMC_DATA_READ),
		 !!(req_flags & MMC_DATA_PRIO),
		 mrq->data->blocks,
		 (u64)mrq->data->blk_addr);
#endif

#ifdef FORCE_QBR_MODE
	if (!!(req_flags & MMC_DATA_READ))
		qbr = CQHCI_QBAR(!!(req_flags & MMC_DATA_QBR));
	else
		qbr = CQHCI_QBAR(1);
#endif

	*data = CQHCI_VALID(1) |
		CQHCI_END(1) |
		CQHCI_INT(intr) |
		CQHCI_ACT(0x5) |
		CQHCI_FORCED_PROG(!!(req_flags & MMC_DATA_FORCED_PRG)) |
		CQHCI_DATA_TAG(!!(req_flags & MMC_DATA_DAT_TAG)) |
		CQHCI_DATA_DIR(!!(req_flags & MMC_DATA_READ)) |
		CQHCI_PRIORITY(!!(req_flags & MMC_DATA_PRIO)) |
#ifdef FORCE_QBR_MODE
		qbr |
#else
		CQHCI_QBAR(!!(req_flags & MMC_DATA_QBR)) |
#endif
		CQHCI_REL_WRITE(!!(req_flags & MMC_DATA_REL_WR)) |
		CQHCI_BLK_COUNT(mrq->data->blocks) |
		CQHCI_BLK_ADDR((u64)mrq->data->blk_addr);

	pr_debug("%s: cqhci: tag %d task descriptor 0x%016llx\n",
		 mmc_hostname(mrq->host), mrq->tag, (unsigned long long)*data);

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	pr_debug("%s: Task: 0x%08x | Args: 0x%08x\n",
		mmc_hostname(mrq->host),
		lower_32_bits(*data),
		upper_32_bits(*data));
#endif
}

static int cqhci_dma_map(struct mmc_host *host, struct mmc_request *mrq)
{
	int sg_count;
	struct mmc_data *data = mrq->data;

	if (!data)
		return -EINVAL;

	sg_count = dma_map_sg(mmc_dev(host), data->sg,
			      data->sg_len,
			      (data->flags & MMC_DATA_WRITE) ?
			      DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (!sg_count) {
		pr_err("%s: sg-len: %d\n", __func__, data->sg_len);
		return -ENOMEM;
	}

	return sg_count;
}

#if IS_ENABLED(CONFIG_SDC_JLQ)
/* workaround of DMA 128MB boundary */
static void cqhci_set_tran_desc(u8 **desc, dma_addr_t addr, int len, bool end,
				bool dma64, struct cqhci_host *cq_host, int tag)
{
	__le32 *attr = (__le32 __force *)(*desc);

	/* check the desc address */
	BUG_ON(*desc >= get_trans_desc(cq_host, tag + 1));

	*attr = (CQHCI_VALID(1) |
		 CQHCI_END(end ? 1 : 0) |
		 CQHCI_INT(0) |
		 CQHCI_ACT(0x4) |
		 CQHCI_DAT_LENGTH(len));

	if (dma64) {
		__le64 *dataddr = (__le64 __force *)(*desc + 4);

		dataddr[0] = cpu_to_le64(addr);
	} else {
		__le32 *dataddr = (__le32 __force *)(*desc + 4);

		dataddr[0] = cpu_to_le32(addr);
	}

	pr_debug("tran_desc: Attr(0x%x) Len(0x%x) Addr(0x%x)\n",
		*(u16 *)(*desc),
		*(u16 *)(*desc + 2),
		*(u64 *)(*desc + 4));

	*desc += cq_host->trans_desc_len;
}

/*
 * If DMA addr spans 128MB boundary, we split the DMA transfer into two
 * so that each DMA transfer doesn't exceed the boundary.
 */
#define BOUNDARY_OK(addr, len) \
	((addr | (SZ_128M - 1)) == ((addr + len - 1) | (SZ_128M - 1)))
static void __cqhci_set_tran_desc(u8 **desc, dma_addr_t addr, int len, bool end,
				bool dma64, struct cqhci_host *cq_host, int tag)
{
	int tmplen, offset;

	if (likely(!len || BOUNDARY_OK(addr, len))) {
		cqhci_set_tran_desc(desc, addr, len, end, dma64, cq_host, tag);
		return;
	}

	offset = addr & (SZ_128M - 1);
	tmplen = SZ_128M - offset;

	pr_info("one desc split into TWOs: S 0x%x, L 0x%x; S 0x%x, L 0x%x\n",
		addr, tmplen,
		addr + tmplen, len - tmplen);

	cqhci_set_tran_desc(desc, addr, tmplen, false, dma64, cq_host, tag);

	addr += tmplen;
	len -= tmplen;
	cqhci_set_tran_desc(desc, addr, len, end, dma64, cq_host, tag);
}

#else
static void cqhci_set_tran_desc(u8 *desc, dma_addr_t addr, int len, bool end,
				bool dma64)
{
	__le32 *attr = (__le32 __force *)desc;

	*attr = (CQHCI_VALID(1) |
		 CQHCI_END(end ? 1 : 0) |
		 CQHCI_INT(0) |
		 CQHCI_ACT(0x4) |
		 CQHCI_DAT_LENGTH(len));

	if (dma64) {
		__le64 *dataddr = (__le64 __force *)(desc + 4);

		dataddr[0] = cpu_to_le64(addr);
	} else {
		__le32 *dataddr = (__le32 __force *)(desc + 4);

		dataddr[0] = cpu_to_le32(addr);
	}
}
#endif

static int cqhci_prep_tran_desc(struct mmc_request *mrq,
			       struct cqhci_host *cq_host, int tag)
{
	struct mmc_data *data = mrq->data;
	int i, sg_count, len;
	bool end = false;
	bool dma64 = cq_host->dma64;
	dma_addr_t addr;
	u8 *desc;
	struct scatterlist *sg;

	sg_count = cqhci_dma_map(mrq->host, mrq);
	if (sg_count < 0) {
		pr_err("%s: %s: unable to map sg lists, %d\n",
				mmc_hostname(mrq->host), __func__, sg_count);
		return sg_count;
	}

	desc = get_trans_desc(cq_host, tag);

	for_each_sg(data->sg, sg, sg_count, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);

		if ((i+1) == sg_count)
			end = true;
#if IS_ENABLED(CONFIG_SDC_JLQ)
		/* workaround of DMA 128MB boundary */
		pr_debug("%s: tag: %d trans_des(%d):\t",
			__func__, tag, i);
		__cqhci_set_tran_desc(&desc, addr, len, end, dma64, cq_host, tag);
#else
		cqhci_set_tran_desc(desc, addr, len, end, dma64);
		desc += cq_host->trans_desc_len;
#endif
	}

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	pr_debug("%s: tag: %d - trans_des:\t"
		"vir(0x%p) phy(0x%llx) - sg-cnt: %d\n",
		__func__, tag,
		get_trans_desc(cq_host, tag),
		get_trans_desc_dma(cq_host, tag),
		sg_count);
#endif

	return 0;
}

static void cqhci_prep_dcmd_desc(struct mmc_host *mmc,
				   struct mmc_request *mrq)
{
	u64 *task_desc = NULL;
	u64 data = 0;
	u8 resp_type;
	u8 *desc;
	__le64 *dataddr;
	struct cqhci_host *cq_host = mmc->cqe_private;
	u8 timing;

	if (!(mrq->cmd->flags & MMC_RSP_PRESENT)) {
		resp_type = 0x0;
		timing = 0x1;
	} else {
		if (mrq->cmd->flags & MMC_RSP_R1B) {
			resp_type = 0x3;
			timing = 0x0;
		} else {
			resp_type = 0x2;
			timing = 0x1;
		}
	}

	task_desc = (__le64 __force *)get_desc(cq_host, cq_host->dcmd_slot);
	memset(task_desc, 0, cq_host->task_desc_len);
	data |= (CQHCI_VALID(1) |
		 CQHCI_END(1) |
		 CQHCI_INT(1) |
		 CQHCI_QBAR(1) |
		 CQHCI_ACT(0x5) |
		 CQHCI_CMD_INDEX(mrq->cmd->opcode) |
		 CQHCI_CMD_TIMING(timing) | CQHCI_RESP_TYPE(resp_type));
	if (cq_host->ops->update_dcmd_desc)
		cq_host->ops->update_dcmd_desc(mmc, mrq, &data);
	*task_desc |= data;
	desc = (u8 *)task_desc;
	pr_debug("%s: cqhci: dcmd: cmd: %d timing: %d resp: %d\n",
		 mmc_hostname(mmc), mrq->cmd->opcode, timing, resp_type);
	dataddr = (__le64 __force *)(desc + 4);
	dataddr[0] = cpu_to_le64((u64)mrq->cmd->arg);

}

static void cqhci_post_req(struct mmc_host *host, struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;

	if (data) {
		dma_unmap_sg(mmc_dev(host), data->sg, data->sg_len,
			     (data->flags & MMC_DATA_READ) ?
			     DMA_FROM_DEVICE : DMA_TO_DEVICE);
	}
}

static inline int cqhci_tag(struct mmc_request *mrq)
{
	return mrq->cmd ? DCMD_SLOT : mrq->tag;
}


#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
static int cqhci_crypto_get_task_available(struct cqhci_host *cq_host, struct mmc_request *mrq)
{
	int tag = 0;
	int ret;

	ret = sdhci_crypto_context_alloc(cq_host->mmc, mrq, &tag);
	if (!ret) {
#if defined(CONFIG_MMC_SDHCI_CRYPTO_TEST)
		if (test_bit(tag, &cq_host->ongoing_tasks)) {
			pr_warn("%s try to set %d, but on going:%x", __func__, tag, cq_host->ongoing_tasks);
		}
		__set_bit(tag, &cq_host->ongoing_tasks);
		pr_debug("%s alloc tag:%d, on going:%lx", __func__, tag, cq_host->ongoing_tasks);
#endif
		return tag;
	}
	return -1;
}

static void cqhci_crypto_free_task(struct cqhci_host *cq_host, struct mmc_request *mrq, int task)
{
	if (task == DCMD_SLOT)
		return;
#if defined(CONFIG_MMC_SDHCI_CRYPTO_TEST)
	__clear_bit(task, &cq_host->ongoing_tasks);
	pr_debug("%s after free tag:%d, on going:%lx", __func__, task, cq_host->ongoing_tasks);
#endif
	sdhci_crypto_context_free(cq_host->mmc, mrq, task);
}

static int cqhci_get_task_available(struct cqhci_host *cq_host, struct mmc_request *mrq, int *task)
{
	unsigned long flags;
	int task_available;

	spin_lock_irqsave(&cq_host->lock, flags);

	task_available =
		cqhci_crypto_get_task_available(cq_host, mrq);

	spin_unlock_irqrestore(&cq_host->lock, flags);

	if (task_available < 0 || task_available >= cq_host->task_queue_depth) {
#if defined(CONFIG_MMC_SDHCI_CRYPTO_TEST)
		pr_debug("%s: no task available %d ongoing_tasks 0x%08x\n",
			mmc_hostname(cq_host->mmc),
			task_available,
			cq_host->ongoing_tasks);
#else
		//pr_debug("%s: no task available %d\n",
		//	mmc_hostname(cq_host->mmc),
		//	task_available);
#endif
		return -1;
	}
#if defined(CONFIG_MMC_SDHCI_CRYPTO_TEST)
	pr_debug("%s: task available %d ongoing_tasks 0x%08x\n",
		mmc_hostname(cq_host->mmc),
		task_available,
		cq_host->ongoing_tasks);
#endif
	*task = task_available;
	return 0;
}
#endif

static int cqhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	int err = 0;
	u64 data = 0;
	u64 *task_desc = NULL;
	int tag = cqhci_tag(mrq);
	struct cqhci_host *cq_host = mmc->cqe_private;
	unsigned long flags;

	if (!cq_host->enabled) {
		pr_err("%s: cqhci: not enabled\n", mmc_hostname(mmc));
		return -EINVAL;
	}

	/* First request after resume has to re-enable */
	if (!cq_host->activated)
		__cqhci_enable(cq_host);

	if (!mmc->cqe_on) {
		cqhci_writel(cq_host, 0, CQHCI_CTL);
		mmc->cqe_on = true;
		pr_debug("%s: cqhci: CQE on\n", mmc_hostname(mmc));
		if (cqhci_readl(cq_host, CQHCI_CTL) & CQHCI_HALT) {
			pr_err("%s: cqhci: CQE failed to exit halt state\n",
			       mmc_hostname(mmc));
		}
		if (cq_host->ops->enable)
			cq_host->ops->enable(mmc);
	}

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
		if (tag != DCMD_SLOT) {
			if (cq_host->task_queue_depth == 0) {
				cq_host->task_queue_depth =
					min_t(int, mmc->card->ext_csd.cmdq_depth, mmc->cqe_qdepth);

				sdhci_crypto_context_depth(mmc, cq_host->task_queue_depth);
				pr_debug("%s: cmdq_depth %d cqe_qdepth %d task_queue_depth %d\n",
					mmc_hostname(mmc),
					mmc->card->ext_csd.cmdq_depth,
					mmc->cqe_qdepth,
					cq_host->task_queue_depth);
			}

			err = cqhci_get_task_available(cq_host, mrq, &tag);

#if defined(CONFIG_MMC_SDHCI_CRYPTO_TEST)
			pr_debug("%s:CQHCI tag %d; mrq->tag %d; ongoing_tasks 0x%08x\n",
				 mmc_hostname(mmc),
				 tag,
				 cqhci_tag(mrq),
				 cq_host->ongoing_tasks);
			if (err) {
				pr_debug("%s: cqhci: no task available\n", mmc_hostname(mmc));
			}
#endif
			if (err) {
				return -EBUSY;
			}
		}
#endif

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	if ((cqhci_readl(cq_host, CQHCI_TDBR) & (1 << tag)))
		pr_warn("%s: cqhci: doorbell not clear for tag %d! error!\n",
			 mmc_hostname(mmc), tag);
#endif

	if (mrq->data) {
		task_desc = (__le64 __force *)get_desc(cq_host, tag);
		cqhci_prep_task_desc(mrq, &data, 1);
		*task_desc = cpu_to_le64(data);
		err = cqhci_prep_tran_desc(mrq, cq_host, tag);
		if (err) {
			pr_err("%s: cqhci: failed to setup tx desc: %d\n",
			       mmc_hostname(mmc), err);
			goto out_err;
		}
	} else {
		cqhci_prep_dcmd_desc(mmc, mrq);
	}

	spin_lock_irqsave(&cq_host->lock, flags);

	if (cq_host->recovery_halt) {
		err = -EBUSY;
		goto out_unlock;
	}

	cq_host->slot[tag].mrq = mrq;
	cq_host->slot[tag].flags = 0;

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
	if (sdhci_prepare_crypto(mmc, mrq, tag, 0)) {
		err = -EBUSY;
		goto out_unlock;
	}
#endif


	cq_host->qcnt += 1;
	/* Make sure descriptors are ready before ringing the doorbell */
	wmb();

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	if (mmc->card && cq_host->qcnt > mmc->card->ext_csd.cmdq_depth)
		pr_warn("%s: cqhci: ext_csd.cmdq_depth %d\t"
			"qcnt %d CQHCI_DPT %d\n",
			 mmc_hostname(mmc),
			 mmc->card->ext_csd.cmdq_depth,
			 cq_host->qcnt,
			 cqhci_readl(cq_host, CQHCI_DPT));

	cq_host->last_record.t_last_request = ktime_get() / 1000;
	cq_host->last_record.last_request_tag = tag;
	cq_host->last_record.last_request_tdbr = cqhci_readl(cq_host, CQHCI_TDBR);
#endif

	cqhci_writel(cq_host, 1 << tag, CQHCI_TDBR);

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	if (!(cqhci_readl(cq_host, CQHCI_TDBR) & (1 << tag))) {
		pr_warn("%s: cqhci: doorbell not set for tag %d\n",
			 mmc_hostname(mmc), tag);
		cqhci_dumpregs(cq_host);
	} else {
#if defined(CONFIG_MMC_SDHCI_CRYPTO_TEST)
		pr_debug("%s:CQHCI tag %d; mrq->tag %d; CQHCI_TDBR 0x%08x; ongoing_tasks 0x%08x\n",
			 mmc_hostname(mmc),
			 tag,
			 cqhci_tag(mrq),
			 cqhci_readl(cq_host, CQHCI_TDBR),
			 cq_host->ongoing_tasks);
#else
		pr_debug("%s:tag %d\n",
			 mmc_hostname(mmc), tag);
#endif
	}
#else
	if (!(cqhci_readl(cq_host, CQHCI_TDBR) & (1 << tag)))
		pr_debug("%s: cqhci: doorbell not set for tag %d\n",
			 mmc_hostname(mmc), tag);
#endif

out_unlock:
	spin_unlock_irqrestore(&cq_host->lock, flags);

	if (err)
		cqhci_post_req(mmc, mrq);

	return err;

out_err:
#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
	sdhci_complete_crypto(mmc, mrq, tag, 0);
	cqhci_crypto_free_task(cq_host, mrq, tag);
#endif
	return err;
}

static void cqhci_recovery_needed(struct mmc_host *mmc, struct mmc_request *mrq,
				  bool notify)
{
	struct cqhci_host *cq_host = mmc->cqe_private;

	if (!cq_host->recovery_halt) {
		cq_host->recovery_halt = true;
		pr_debug("%s: cqhci: recovery needed\n", mmc_hostname(mmc));
		wake_up(&cq_host->wait_queue);
		if (notify && mrq->recovery_notifier)
			mrq->recovery_notifier(mrq);
	}
}

static unsigned int cqhci_error_flags(int error1, int error2)
{
	int error = error1 ? error1 : error2;

	switch (error) {
	case -EILSEQ:
		return CQHCI_HOST_CRC;
	case -ETIMEDOUT:
		return CQHCI_HOST_TIMEOUT;
	default:
		return CQHCI_HOST_OTHER;
	}
}

static void cqhci_error_irq(struct mmc_host *mmc, u32 status, int cmd_error,
			    int data_error)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	struct cqhci_slot *slot;
	u32 terri;
	int tag;

	spin_lock(&cq_host->lock);

	terri = cqhci_readl(cq_host, CQHCI_TERRI);

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	pr_info("%s: cqhci: error IRQ status: 0x%08x cmd error %d data error %d TERRI: 0x%08x\n",
		 mmc_hostname(mmc), status, cmd_error, data_error, terri);

	/*
	 * dump regs.
	 * notice: some regs have been cleared before this step.
	 * such as: SDHCI_INT_STATUS, CQHCI_IS
	 */
	cqhci_dumpregs(cq_host);
#else
	pr_debug("%s: cqhci: error IRQ status: 0x%08x cmd error %d data error %d TERRI: 0x%08x\n",
		 mmc_hostname(mmc), status, cmd_error, data_error, terri);
#endif

	/* Forget about errors when recovery has already been triggered */
	if (cq_host->recovery_halt)
		goto out_unlock;

	if (!cq_host->qcnt) {
		WARN_ONCE(1, "%s: cqhci: error when idle. IRQ status: 0x%08x cmd error %d data error %d TERRI: 0x%08x\n",
			  mmc_hostname(mmc), status, cmd_error, data_error,
			  terri);
		goto out_unlock;
	}

	if (CQHCI_TERRI_C_VALID(terri)) {
		tag = CQHCI_TERRI_C_TASK(terri);
		slot = &cq_host->slot[tag];
		if (slot->mrq) {
			slot->flags = cqhci_error_flags(cmd_error, data_error);
			cqhci_recovery_needed(mmc, slot->mrq, true);
		}
	}

	if (CQHCI_TERRI_D_VALID(terri)) {
		tag = CQHCI_TERRI_D_TASK(terri);
		slot = &cq_host->slot[tag];
		if (slot->mrq) {
			slot->flags = cqhci_error_flags(data_error, cmd_error);
			cqhci_recovery_needed(mmc, slot->mrq, true);
		}
	}

	if (!cq_host->recovery_halt) {
		/*
		 * The only way to guarantee forward progress is to mark at
		 * least one task in error, so if none is indicated, pick one.
		 */
		for (tag = 0; tag < NUM_SLOTS; tag++) {
			slot = &cq_host->slot[tag];
			if (!slot->mrq)
				continue;
			slot->flags = cqhci_error_flags(data_error, cmd_error);
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
			pr_info("%s: cqhci: no task is indicated as err, pick task %d!\n",
				 mmc_hostname(mmc), tag);
#endif
			cqhci_recovery_needed(mmc, slot->mrq, true);
			break;
		}
	}

out_unlock:
	spin_unlock(&cq_host->lock);
}

static void cqhci_finish_mrq(struct mmc_host *mmc, unsigned int tag)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	struct cqhci_slot *slot = &cq_host->slot[tag];
	struct mmc_request *mrq = slot->mrq;
	struct mmc_data *data;

	if (!mrq) {
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
		pr_info("%s: %s: tag %d, qcnt=%d\n",
			  mmc_hostname(mmc),
			  __func__,
			  tag,
			  cq_host->qcnt);
#endif

		WARN_ONCE(1, "%s: cqhci: spurious TCN for tag %d\n",
			  mmc_hostname(mmc), tag);
		return;
	}

	/* No completions allowed during recovery */
	if (cq_host->recovery_halt) {
		slot->flags |= CQHCI_COMPLETED;
		return;
	}

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
	sdhci_complete_crypto(mmc, mrq, tag, 0);
	cqhci_crypto_free_task(cq_host, mrq, tag);
#endif

	slot->mrq = NULL;

	cq_host->qcnt -= 1;

	data = mrq->data;
	if (data) {
		if (data->error)
			data->bytes_xfered = 0;
		else
			data->bytes_xfered = data->blksz * data->blocks;
	}

	mmc_cqe_request_done(mmc, mrq);
}

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
bool cqhci_check_pending_tasks(struct mmc_host *mmc, const char *check_func, u32 line)
{
	struct cqhci_host *cq_host = mmc->cqe_private;

	if (!cq_host)
		return false;

	if (!cq_host->activated)
		return false;

	if (cq_host->recovery_halt)
		return false;

	if (cqhci_readl(cq_host, CQHCI_TDBR) ||
		cqhci_readl(cq_host, CQHCI_DPT)) {
		pr_info("%s: %s: qcnt %d TDBR %08x DPT %08x @ %s Line%d\n",
			mmc_hostname(mmc),
			__func__,
			cq_host->qcnt,
			cqhci_readl(cq_host, CQHCI_TDBR),
			cqhci_readl(cq_host, CQHCI_DPT),
			check_func,
			line);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(cqhci_check_pending_tasks);
#endif

irqreturn_t cqhci_irq(struct mmc_host *mmc, u32 intmask, int cmd_error,
		      int data_error)
{
	u32 status;
	unsigned long tag = 0, comp_status;
	struct cqhci_host *cq_host = mmc->cqe_private;

	status = cqhci_readl(cq_host, CQHCI_IS);
	cqhci_writel(cq_host, status, CQHCI_IS);

	pr_debug("%s: cqhci: IRQ status: 0x%08x\n", mmc_hostname(mmc), status);

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	cq_host->last_record.t_last_isr = ktime_get() / 1000;
	cq_host->last_record.last_isr_is = status;
#endif

	if ((status & CQHCI_IS_RED) || cmd_error || data_error)
		cqhci_error_irq(mmc, status, cmd_error, data_error);

	if (status & CQHCI_IS_TCC) {
		/* read TCN and complete the request */
		comp_status = cqhci_readl(cq_host, CQHCI_TCN);
		cqhci_writel(cq_host, comp_status, CQHCI_TCN);
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
		cq_host->last_record.last_isr_tcn = comp_status;
		cq_host->last_record.last_isr_tdbr = cqhci_readl(cq_host, CQHCI_TDBR);
		cq_host->last_record.last_isr_dpt = cqhci_readl(cq_host, CQHCI_DPT);
		if ((cq_host->last_record.last_isr_tcn & cq_host->last_record.last_isr_tdbr) ||
			(cq_host->last_record.last_isr_tcn & cq_host->last_record.last_isr_dpt)) {
			memcpy(&cq_host->last_record_back,
				&cq_host->last_record,
				sizeof(cq_host->last_record));

			pr_err("%s: %s: qcnt %d TCN %08x TDBR %08x DPT %08x\n",
				mmc_hostname(mmc),
				__func__,
				cq_host->qcnt,
				comp_status,
				cq_host->last_record.last_isr_tdbr,
				cq_host->last_record.last_isr_dpt);
		}
#endif
#if IS_ENABLED(CONFIG_SDC_JLQ)
		/*
		 * clear TCC interrupt:
		 * To clear the TCC(Task complete interrupt) in CQHCI_IS,
		 * need to clear the relevant task in CQHCI_TCN first.
		 */
		cqhci_writel(cq_host, CQHCI_IS_TCC, CQHCI_IS);
#endif

		pr_debug("%s: cqhci: TCN: 0x%08lx\n",
			 mmc_hostname(mmc), comp_status);

		spin_lock(&cq_host->lock);

		for_each_set_bit(tag, &comp_status, cq_host->num_slots) {
			/* complete the corresponding mrq */
			pr_debug("%s: cqhci: completing tag %lu\n",
				 mmc_hostname(mmc), tag);
			cqhci_finish_mrq(mmc, tag);
		}

		if (cq_host->waiting_for_idle && !cq_host->qcnt) {
			cq_host->waiting_for_idle = false;
			wake_up(&cq_host->wait_queue);
		}

		spin_unlock(&cq_host->lock);
	}

	if (status & CQHCI_IS_TCL)
		wake_up(&cq_host->wait_queue);

	if (status & CQHCI_IS_HAC)
		wake_up(&cq_host->wait_queue);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(cqhci_irq);

static bool cqhci_is_idle(struct cqhci_host *cq_host, int *ret)
{
	unsigned long flags;
	bool is_idle;

	spin_lock_irqsave(&cq_host->lock, flags);
	is_idle = !cq_host->qcnt || cq_host->recovery_halt;
	*ret = cq_host->recovery_halt ? -EBUSY : 0;
	cq_host->waiting_for_idle = !is_idle;
	spin_unlock_irqrestore(&cq_host->lock, flags);

	return is_idle;
}

static int cqhci_wait_for_idle(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	int ret;

	wait_event(cq_host->wait_queue, cqhci_is_idle(cq_host, &ret));

	return ret;
}

static bool cqhci_timeout(struct mmc_host *mmc, struct mmc_request *mrq,
			  bool *recovery_needed)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	int tag = cqhci_tag(mrq);
	struct cqhci_slot *slot = &cq_host->slot[tag];
	unsigned long flags;
	bool timed_out;

	spin_lock_irqsave(&cq_host->lock, flags);
	timed_out = slot->mrq == mrq;
	if (timed_out) {
		slot->flags |= CQHCI_EXTERNAL_TIMEOUT;
		cqhci_recovery_needed(mmc, mrq, false);
		*recovery_needed = cq_host->recovery_halt;
	}
	spin_unlock_irqrestore(&cq_host->lock, flags);

	if (timed_out) {
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
		pr_err("%s: cqhci: timeout for tag %d; qcnt = %d\n",
		       mmc_hostname(mmc), tag, cq_host->qcnt);
#else
		pr_err("%s: cqhci: timeout for tag %d\n",
		       mmc_hostname(mmc), tag);
#endif
		cqhci_dumpregs(cq_host);
	}

	return timed_out;
}

static bool cqhci_tasks_cleared(struct cqhci_host *cq_host)
{
	return !(cqhci_readl(cq_host, CQHCI_CTL) & CQHCI_CLEAR_ALL_TASKS);
}

static bool cqhci_clear_all_tasks(struct mmc_host *mmc, unsigned int timeout)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	bool ret;
	u32 ctl;

	cqhci_set_irqs(cq_host, CQHCI_IS_TCL);

	ctl = cqhci_readl(cq_host, CQHCI_CTL);
	ctl |= CQHCI_CLEAR_ALL_TASKS;
	cqhci_writel(cq_host, ctl, CQHCI_CTL);

	wait_event_timeout(cq_host->wait_queue, cqhci_tasks_cleared(cq_host),
			   msecs_to_jiffies(timeout) + 1);

	cqhci_set_irqs(cq_host, 0);

	ret = cqhci_tasks_cleared(cq_host);

	if (!ret)
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
		pr_info("%s: cqhci: Failed to clear tasks\n",
			 mmc_hostname(mmc));
#else
		pr_debug("%s: cqhci: Failed to clear tasks\n",
			 mmc_hostname(mmc));
#endif

	return ret;
}

static bool cqhci_halted(struct cqhci_host *cq_host)
{
	return cqhci_readl(cq_host, CQHCI_CTL) & CQHCI_HALT;
}

static bool cqhci_halt(struct mmc_host *mmc, unsigned int timeout)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	bool ret;
	u32 ctl;

	if (cqhci_halted(cq_host))
		return true;

	cqhci_set_irqs(cq_host, CQHCI_IS_HAC);

	ctl = cqhci_readl(cq_host, CQHCI_CTL);
	ctl |= CQHCI_HALT;
	cqhci_writel(cq_host, ctl, CQHCI_CTL);

	wait_event_timeout(cq_host->wait_queue, cqhci_halted(cq_host),
			   msecs_to_jiffies(timeout) + 1);

	cqhci_set_irqs(cq_host, 0);

	ret = cqhci_halted(cq_host);

	if (!ret)
		pr_debug("%s: cqhci: Failed to halt\n", mmc_hostname(mmc));
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	else
		pr_debug("%s: cqhci: halt\n", mmc_hostname(mmc));
#endif

	return ret;
}

/*
 * After halting we expect to be able to use the command line. We interpret the
 * failure to halt to mean the data lines might still be in use (and the upper
 * layers will need to send a STOP command), so we set the timeout based on a
 * generous command timeout.
 */
#define CQHCI_START_HALT_TIMEOUT	5

static void cqhci_recovery_start(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;

	pr_debug("%s: cqhci: %s\n", mmc_hostname(mmc), __func__);

	WARN_ON(!cq_host->recovery_halt);

	cqhci_halt(mmc, CQHCI_START_HALT_TIMEOUT);

	if (cq_host->ops->disable)
		cq_host->ops->disable(mmc, true);

	mmc->cqe_on = false;
}

static int cqhci_error_from_flags(unsigned int flags)
{
	if (!flags)
		return 0;

	/* CRC errors might indicate re-tuning so prefer to report that */
	if (flags & CQHCI_HOST_CRC)
		return -EILSEQ;

	if (flags & (CQHCI_EXTERNAL_TIMEOUT | CQHCI_HOST_TIMEOUT))
		return -ETIMEDOUT;

	return -EIO;
}

static void cqhci_recover_mrq(struct cqhci_host *cq_host, unsigned int tag)
{
	struct cqhci_slot *slot = &cq_host->slot[tag];
	struct mmc_request *mrq = slot->mrq;
	struct mmc_data *data;

	if (!mrq)
		return;

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
	sdhci_complete_crypto(cq_host->mmc, mrq, tag, 0);
	cqhci_crypto_free_task(cq_host, mrq, tag);
#endif

	slot->mrq = NULL;

	cq_host->qcnt -= 1;

	data = mrq->data;
	if (data) {
		data->bytes_xfered = 0;
		data->error = cqhci_error_from_flags(slot->flags);
	} else {
		mrq->cmd->error = cqhci_error_from_flags(slot->flags);
	}

	mmc_cqe_request_done(cq_host->mmc, mrq);
}

static void cqhci_recover_mrqs(struct cqhci_host *cq_host)
{
	int i;

	for (i = 0; i < cq_host->num_slots; i++)
		cqhci_recover_mrq(cq_host, i);
}

/*
 * By now the command and data lines should be unused so there is no reason for
 * CQHCI to take a long time to halt, but if it doesn't halt there could be
 * problems clearing tasks, so be generous.
 */
#define CQHCI_FINISH_HALT_TIMEOUT	20

/* CQHCI could be expected to clear it's internal state pretty quickly */
#define CQHCI_CLEAR_TIMEOUT		20

static void cqhci_recovery_finish(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	unsigned long flags;
	u32 cqcfg;
	bool ok;

	pr_debug("%s: cqhci: %s\n", mmc_hostname(mmc), __func__);

	WARN_ON(!cq_host->recovery_halt);

	ok = cqhci_halt(mmc, CQHCI_FINISH_HALT_TIMEOUT);

	if (!cqhci_clear_all_tasks(mmc, CQHCI_CLEAR_TIMEOUT))
		ok = false;

	/*
	 * The specification contradicts itself, by saying that tasks cannot be
	 * cleared if CQHCI does not halt, but if CQHCI does not halt, it should
	 * be disabled/re-enabled, but not to disable before clearing tasks.
	 * Have a go anyway.
	 */
	if (!ok) {
		pr_debug("%s: cqhci: disable / re-enable\n", mmc_hostname(mmc));
		cqcfg = cqhci_readl(cq_host, CQHCI_CFG);
		cqcfg &= ~CQHCI_ENABLE;
		cqhci_writel(cq_host, cqcfg, CQHCI_CFG);
		cqcfg |= CQHCI_ENABLE;
		cqhci_writel(cq_host, cqcfg, CQHCI_CFG);
		/* Be sure that there are no tasks */
		ok = cqhci_halt(mmc, CQHCI_FINISH_HALT_TIMEOUT);
		if (!cqhci_clear_all_tasks(mmc, CQHCI_CLEAR_TIMEOUT))
			ok = false;
		WARN_ON(!ok);
	}

	cqhci_recover_mrqs(cq_host);

	WARN_ON(cq_host->qcnt);

	spin_lock_irqsave(&cq_host->lock, flags);
	cq_host->qcnt = 0;
	cq_host->recovery_halt = false;
	mmc->cqe_on = false;
	spin_unlock_irqrestore(&cq_host->lock, flags);

	/* Ensure all writes are done before interrupts are re-enabled */
	wmb();

	cqhci_writel(cq_host, CQHCI_IS_HAC | CQHCI_IS_TCL, CQHCI_IS);

	cqhci_set_irqs(cq_host, CQHCI_IS_MASK);

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	pr_info("%s: cqhci: recovery done\n", mmc_hostname(mmc));
#else
	pr_debug("%s: cqhci: recovery done\n", mmc_hostname(mmc));
#endif
}

static const struct mmc_cqe_ops cqhci_cqe_ops = {
	.cqe_enable = cqhci_enable,
	.cqe_disable = cqhci_disable,
	.cqe_request = cqhci_request,
	.cqe_post_req = cqhci_post_req,
	.cqe_off = cqhci_off,
	.cqe_wait_for_idle = cqhci_wait_for_idle,
	.cqe_timeout = cqhci_timeout,
	.cqe_recovery_start = cqhci_recovery_start,
	.cqe_recovery_finish = cqhci_recovery_finish,
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	.cqe_dumpregs = cqhci_dumpregs_ext,
#endif
};

struct cqhci_host *cqhci_pltfm_init(struct platform_device *pdev)
{
	struct cqhci_host *cq_host;
	struct resource *cqhci_memres = NULL;

	/* check and setup CMDQ interface */
	cqhci_memres = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "cqhci_mem");
	if (!cqhci_memres) {
		dev_dbg(&pdev->dev, "CMDQ not supported\n");
		return ERR_PTR(-EINVAL);
	}

	cq_host = devm_kzalloc(&pdev->dev, sizeof(*cq_host), GFP_KERNEL);
	if (!cq_host)
		return ERR_PTR(-ENOMEM);
	cq_host->mmio = devm_ioremap(&pdev->dev,
				     cqhci_memres->start,
				     resource_size(cqhci_memres));
	if (!cq_host->mmio) {
		dev_err(&pdev->dev, "failed to remap cqhci regs\n");
		return ERR_PTR(-EBUSY);
	}
	dev_dbg(&pdev->dev, "CMDQ ioremap: done\n");

	return cq_host;
}
EXPORT_SYMBOL(cqhci_pltfm_init);

static unsigned int cqhci_ver_major(struct cqhci_host *cq_host)
{
	return CQHCI_VER_MAJOR(cqhci_readl(cq_host, CQHCI_VER));
}

static unsigned int cqhci_ver_minor(struct cqhci_host *cq_host)
{
	u32 ver = cqhci_readl(cq_host, CQHCI_VER);

	return CQHCI_VER_MINOR1(ver) * 10 + CQHCI_VER_MINOR2(ver);
}

int cqhci_init(struct cqhci_host *cq_host, struct mmc_host *mmc,
	      bool dma64)
{
	int err;

	cq_host->dma64 = dma64;
	cq_host->mmc = mmc;
	cq_host->mmc->cqe_private = cq_host;

	cq_host->num_slots = NUM_SLOTS;
	cq_host->dcmd_slot = DCMD_SLOT;

	mmc->cqe_ops = &cqhci_cqe_ops;

	mmc->cqe_qdepth = NUM_SLOTS;
	if (mmc->caps2 & MMC_CAP2_CQE_DCMD)
		mmc->cqe_qdepth -= 1;

	cq_host->slot = devm_kcalloc(mmc_dev(mmc), cq_host->num_slots,
				     sizeof(*cq_host->slot), GFP_KERNEL);
	if (!cq_host->slot) {
		err = -ENOMEM;
		goto out_err;
	}

	spin_lock_init(&cq_host->lock);

	init_completion(&cq_host->halt_comp);
	init_waitqueue_head(&cq_host->wait_queue);

	pr_info("%s: CQHCI version %u.%02u\n",
		mmc_hostname(mmc), cqhci_ver_major(cq_host),
		cqhci_ver_minor(cq_host));

	return 0;

out_err:
	pr_err("%s: CQHCI version %u.%02u failed to initialize, error %d\n",
	       mmc_hostname(mmc), cqhci_ver_major(cq_host),
	       cqhci_ver_minor(cq_host), err);
	return err;
}
EXPORT_SYMBOL(cqhci_init);

MODULE_AUTHOR("Venkat Gopalakrishnan <venkatg@codeaurora.org>");
MODULE_DESCRIPTION("Command Queue Host Controller Interface driver");
MODULE_LICENSE("GPL v2");
