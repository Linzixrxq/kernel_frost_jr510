// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/mmc/host/sdhci-jlq.c - JLQ SDHCI Platform driver
 *
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/mmc/mmc.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/sizes.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/pm_opp.h>
#include <opp.h>

#include "cqhci.h"
#include "sdhci-pltfm.h"

#include "sdhci-crypto.h"

#define DRIVER_NAME "sdhci-jlq"

#define SDHCI_JLQ_D(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

#if defined(CONFIG_JLQ_EMULATOR)
/* this macro is used only for JR510 HAPS, not for other EMULATORs including ZEBU */
//#define JR510_HAPS_NO_MMC_SD_CLKTX_CDIV
#endif

/* eMMC/SD command timeout value : in unit of us */
#define EMMC_CMD_TIMEOUT_VAL	50000
#define SD_CMD_TIMEOUT_VAL		500000

#define BOUNDARY_OK(addr, len) \
	((addr | (SZ_128M - 1)) == ((addr + len - 1) | (SZ_128M - 1)))


#define   SDHCI_JLQ_CTRL_HS400		0x0007

/*
 * JLQ SDHCI controller does not support INT_RETUNE,
 * but supports INT_TUNING_ERR while auto-tuning is enabled.
 */
#define  SDHCI_INT_TUNING_ERR   0x04000000


#define SDHCI_INT_TUNING_ERR_MASK (SDHCI_INT_TIMEOUT | SDHCI_INT_CRC | \
		SDHCI_INT_END_BIT | SDHCI_INT_INDEX | \
		SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC | \
		SDHCI_INT_DATA_END_BIT)
#define SDHCI_INT_TUNING_ALL_MASK (SDHCI_INT_TUNING_ERR_MASK | \
		SDHCI_INT_DATA_AVAIL)


/* maximum deley elements in DL(delay line) */
#define DL_ELEMENT_MASK				0x7F
#define DL_ELEMENT_MAX				(DL_ELEMENT_MASK + 1)

#define SMPL_DLY_SKIP 4

/*VENDOR_SPECIFIC_AREA*/
#define SDHCI_VENDOR_SPECIFIC_AREA			0xE8

/*VENDOR2_SPECIFIC_AREA(CDMQ)*/
#define SDHCI_CMDQ_AREA						0xEA

/*VENDOR1 SPECIFIC REGISTERS*/
#define MSHC_VER_ID		0x00	/*32bits*/

#define MSHC_VER_TYPE	0x04	/*32bits*/

#define MSHC_CTRL		0x08	/*8bits*/

#define CMD_CONFLICT_CHECK		BIT(0)

#define MBIU_CTRL		0x10	/*8bits*/

#define EMMC_CTRL		0x2C	/*16bits*/

#define CARD_IS_EMMC				BIT(0)
#define ENH_STROBE_ENABLE			BIT(8)

#define BOOT_CTRL		0x2E	/*16bits*/

#define AT_CTRL			0x40	/*32bits*/

#define SWIN_TH_VAL_MASK			DL_ELEMENT_MASK
#define SWIN_TH_VAL_SHIFT			24
#define POST_CHANGE_DLY_MASK		0x3
#define POST_CHANGE_DLY_SHIFT		19
#define PRE_CHANGE_DLY_MASK			0x3
#define PRE_CHANGE_DLY_SHIFT		17
#define TUNE_CLK_STOP_EN			BIT(16)
#define WIN_EDGE_SEL_MASK			0xF
#define WIN_EDGE_SEL_SHIFT			8
#define SW_TUNE_EN					BIT(4)
#define SWIN_TH_EN					BIT(2)
#define CI_SEL						BIT(1)
#define AT_EN						BIT(0)

#define AT_CTRL_DEFAULT			(AT_EN | \
	CI_SEL | \
	TUNE_CLK_STOP_EN | \
	0x3 << PRE_CHANGE_DLY_SHIFT | \
	0x1 << POST_CHANGE_DLY_SHIFT)

#define AT_STAT			0x44	/*32bits*/

#define CENTER_PH_CODE_SHIFT		0

/*PHY REGISTERS (32bits)*/
#define PHY_REGS_BASE	0x300

#define COMMDL_CNFG		(PHY_REGS_BASE + 0x00)

#define CLKDL_CNFG		(PHY_REGS_BASE + 0x04)

#define CLKDL_UPDATE_DC				BIT(4)
#define CLKDL_INPSEL_GATED_CCLK_TX	0
#define CLKDL_INPSEL_SHIFT			2
#define CLKDL_BYPASS				BIT(1)
#define CLKDL_EXTDLY				BIT(0)

#define CLKDL_DC		(PHY_REGS_BASE + 0x08)
#define CLKDL_DC_MASK				DL_ELEMENT_MASK

#define SMPLDL_CNFG		(PHY_REGS_BASE + 0x0C)

#define SMPLDL_INPSEL_CLK_DL		0
#define SMPLDL_INPSEL_GATED_CCLK_TX	3
#define SMPLDL_INPSEL_SHIFT			2
#define SMPLDL_BYPASS				BIT(1)
#define SMPLDL_EXTDLY				BIT(0)

#define ATDL_CNFG		(PHY_REGS_BASE + 0x10)
#define ATDL_INPSEL_CLK_DL			0
#define ATDL_INPSEL_GATED_CCLK_TX	3
#define ATDL_INPSEL_SHIFT			2
#define ATDL_BYPASS					BIT(1)
#define ATDL_EXTDLY					BIT(0)

#define DLLDL_CNFG		(PHY_REGS_BASE + 0x14)
#define DLLMST_TSTDC	(PHY_REGS_BASE + 0x18)
#define DLL_STATUS0		(PHY_REGS_BASE + 0x1C)
#define DLL_STATUS1		(PHY_REGS_BASE + 0x20)

#define DLL_CNFG0		(PHY_REGS_BASE + 0x24)

#define DLL_BYPASS_SHIFT			24
#define DLL_BYPASS					BIT(DLL_BYPASS_SHIFT)
#define SDN_NUM_SHIFT				16
#define SDB_NUM_SHIFT				0

#define DLL_CNFG1		(PHY_REGS_BASE + 0x28)

#define DLL_MODE					BIT(28)
#define MD_NUM_SHIFT				24
#define DLL_INCR_SHIFT				16
#define START_POINT_SHIFT			0

#define DLL_CNFG2		(PHY_REGS_BASE + 0x2C)
#define TIMEOUT_SHIFT				0

#define DLL_CTRL		(PHY_REGS_BASE + 0x30)

#define DLL_UPDATE					BIT(2)
#define DLL_SCLOSE					BIT(1)
#define DLL_ENABLE					BIT(0)

#define DLL_INTR_EN		(PHY_REGS_BASE + 0x34)

#define DLL_INTR_UNLOCK				BIT(1)
#define DLL_INTR_LOCK				BIT(0)

#define DLL_INTR_STATUS	(PHY_REGS_BASE + 0x38)

#define DLL_INTR_RAW	(PHY_REGS_BASE + 0x3C)

/* for eMMC controller */
#define PHY_MISC_EMMC		(PHY_REGS_BASE + 0x40)
/* for SD/SDIO controller */
#define PHY_MISC_SD			(PHY_REGS_BASE + 0x14)

/* default config for DLL non-bypass */

/* SDN_NUM = 0x20, delay 1/4 T of clktx*/
#define DLL_CNFG0_DEFAULT		(0 << DLL_BYPASS_SHIFT | \
	0x20 << SDN_NUM_SHIFT | \
	0 << SDB_NUM_SHIFT)

#define MD_NUM_DEFAULT		0x2
#define MD_NUM_MAX			0x5

#define START_POINT_200MHz	0x38
#define START_POINT_1XXMHz	0x50

#define DLL_INCR			0x10

/* MD_NUM + START_POINT > 1/2 T of clktx */
/* HS400@200MHz */
#define DLL_CNFG1_DEFAULT_200MHz	(DLL_MODE | \
	DLL_INCR << DLL_INCR_SHIFT | \
	START_POINT_200MHz << START_POINT_SHIFT)
/* HS400@133MHz or 100MHz*/
#define DLL_CNFG1_DEFAULT_1XXMHz	(DLL_MODE | \
	DLL_INCR << DLL_INCR_SHIFT | \
	START_POINT_1XXMHz << START_POINT_SHIFT)

/* about us at clktx 200MHz*/
#define DLL_CNFG2_DEFAULT		(0xFFFFF << TIMEOUT_SHIFT)

/* Timeout value for dll_irq */
#define DLL_IRQ_TIMEOUT_MS 100

/* software retune period in seconds*/
#define SW_RETUNE_PERIOD_SECS	300

#define INVALID_TUNING_PHASE	-1

#define TUNING_RETRY_MAX_TIMES	3

#define JLQ_MMC_AUTOSUSPEND_DELAY_MS	50

/* delay element period, in unit of ps */
#define DELAY_ELEMNT_PERIOD		55


#define MAX_TX_CLK_FREQ		200000000
#define MIN_TX_CLK_FREQ		100000

#define VCORE_SUPPLY		"vcore"
#define VCORE_SUPPLY_NAME	"vcore-supply"

struct sdhci_jlq_tuning_cfg {
	u32 clk_rate;		/* HZ */
	u8 sample_delay;	/* in unit of delay element */
	bool is_ddr;		/* DDR or SDR */
};

enum sdhc_device_type {
	DEVICE_UNKNOWN,
	DEVICE_EMMC,
	DEVICE_SD,
	DEVICE_SDIO,
	DEVICE_TYPE_MAX
};

struct sdhci_jlq_host {
	struct platform_device *pdev;
	u16 vendor_offset;
	enum sdhc_device_type device_type;
	int dll_irq;		/* dll irq (only for eMMC)*/
	u32 dll_intr_status;
	struct completion dll_irq_completion;
	struct clk *clktx;	/* card clock*/
	struct clk *bclk;	/* base clock*/
	u8 sample_delay;
	u8 drive_delay;
	u32 *sup_clk_table;
	unsigned char sup_clk_cnt;
	bool tuning_in_progress;
	bool tuning_failed;	/* tuning failed for one phase */
	bool tuning_done;
	u8 saved_tuning_phase;
	unsigned int force_drive_strength;
	unsigned long clk_rate;	/* actual clock rate of clktx */
	struct mmc_host *mmc;
	int cmd_gpio;		/* only for emmc */
	int ctrl_id;
	bool has_cqe;
	const char *hc_name;
	void __iomem *hblk_sctrl;
#ifdef JR510_HAPS_NO_MMC_SD_CLKTX_CDIV
	void __iomem *top_crg_clks;
#endif
	struct opp_table *opp_table;
};

/* sample delay with different clk rates */
static struct sdhci_jlq_tuning_cfg tuning_cfg_table[] = {
	/* clk_rate, sample_delay, is_ddr*/
	{400000,			0,			false},
	{25000000,			0,			false},
	{50000000,			0,			false},
	{50000000,			0,			true}
};

#if IS_ENABLED(CONFIG_JGKI)

#define sdhci_adma_write_desc_func	sdhci_adma_write_desc
#else
/*
 * This function is a copy from sdhci.c
 */
static void sdhci_adma_write_desc_copy(struct sdhci_host *host, void **desc,
			   dma_addr_t addr, int len, unsigned int cmd)
{
	struct sdhci_adma2_64_desc *dma_desc = *desc;

	/* 32-bit and 64-bit descriptors have these members in same position */
	dma_desc->cmd = cpu_to_le16(cmd);
	dma_desc->len = cpu_to_le16(len);
	dma_desc->addr_lo = cpu_to_le32(lower_32_bits(addr));

	if (host->flags & SDHCI_USE_64_BIT_DMA)
		dma_desc->addr_hi = cpu_to_le32(upper_32_bits(addr));

	*desc += host->desc_sz;
}

#define sdhci_adma_write_desc_func	sdhci_adma_write_desc_copy
#endif
/*
 * If DMA addr spans 128MB boundary, we split the DMA transfer into two
 * so that each DMA transfer doesn't exceed the boundary.
 */
static void sdhci_jlq_adma_write_desc(struct sdhci_host *host, void **desc,
				    dma_addr_t addr, int len, unsigned int cmd)
{
	int tmplen, offset;

	if (likely(!len || BOUNDARY_OK(addr, len))) {
		sdhci_adma_write_desc_func(host, desc, addr, len, cmd);
		return;
	}

	offset = addr & (SZ_128M - 1);
	tmplen = SZ_128M - offset;
	sdhci_adma_write_desc_func(host, desc, addr, tmplen, cmd);

	addr += tmplen;
	len -= tmplen;
	sdhci_adma_write_desc_func(host, desc, addr, len, cmd);
}
static unsigned int sdhci_jlq_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);

	return jlq_host->sup_clk_table[jlq_host->sup_clk_cnt - 1];
}

static unsigned int sdhci_jlq_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);

	return jlq_host->sup_clk_table[0];
}

static int sdhci_jlq_dt_get_array(struct device *dev, const char *prop_name,
				 u32 **out, int *len, u32 size)
{
	int ret = 0;
	struct device_node *np = dev->of_node;
	size_t sz;
	u32 *arr = NULL;

	if (!of_get_property(np, prop_name, len)) {
		ret = -EINVAL;
		goto out;
	}
	sz = *len = *len / sizeof(*arr);
	if (sz <= 0 || (size > 0 && (sz > size))) {
		dev_err(dev, "%s invalid size\n", prop_name);
		ret = -EINVAL;
		goto out;
	}

	arr = devm_kzalloc(dev, sz * sizeof(*arr), GFP_KERNEL);
	if (!arr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_property_read_u32_array(np, prop_name, arr, sz);
	if (ret < 0) {
		dev_err(dev, "%s failed reading array %d\n", prop_name, ret);
		goto out;
	}
	*out = arr;
out:
	if (ret)
		*len = 0;
	return ret;
}

static u32 sdhci_jlq_get_sup_clk_rate(struct sdhci_host *host,
						u32 req_clk)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	unsigned int sel_clk = -1;
	unsigned char cnt;

	if (req_clk < sdhci_jlq_get_min_clock(host)) {
		sel_clk = sdhci_jlq_get_min_clock(host);
		return sel_clk;
	}

	for (cnt = 0; cnt < jlq_host->sup_clk_cnt; cnt++) {
		if (jlq_host->sup_clk_table[cnt] > req_clk) {
			break;
		} else if (jlq_host->sup_clk_table[cnt] == req_clk) {
			sel_clk = jlq_host->sup_clk_table[cnt];
			break;
		}

		sel_clk = jlq_host->sup_clk_table[cnt];
	}
	return sel_clk;
}

static u8 sdhci_jlq_get_tuning_cfg(u32 clk, bool is_ddr)
{
	struct sdhci_jlq_tuning_cfg *ptr_cfg;
	u8 cfg;
	u8 sel_cfg = 0;

	for (cfg = 0; cfg < ARRAY_SIZE(tuning_cfg_table); cfg++) {
		ptr_cfg = &tuning_cfg_table[cfg];

		if (ptr_cfg->is_ddr != is_ddr)
			continue;

		if (ptr_cfg->clk_rate > clk) {
			break;
		} else if (ptr_cfg->clk_rate == clk) {
			sel_cfg = cfg;
			break;
		}

		sel_cfg = cfg;
	}
	return sel_cfg;
}

static int sdhci_jlq_cfg_drv_delay(struct sdhci_host *host,
						u8 drive_delay)
{
	u32 val;

	if (drive_delay == 0) {
		/*clk delay line bypass*/
		sdhci_writel(host, CLKDL_BYPASS, CLKDL_CNFG);
	} else {
		val = (drive_delay - 1) >= DL_ELEMENT_MAX ? CLKDL_EXTDLY : 0;

		/* input select : gated cclk_tx */
		val |= CLKDL_INPSEL_GATED_CCLK_TX << CLKDL_INPSEL_SHIFT;

		val |= CLKDL_UPDATE_DC;
		sdhci_writel(host, val, CLKDL_CNFG);

		sdhci_writel(host, (drive_delay - 1) & CLKDL_DC_MASK, CLKDL_DC);

		val &=	~CLKDL_UPDATE_DC;
		sdhci_writel(host, val, CLKDL_CNFG);
	}

	return 0;
}

static void sdhci_jlq_sw_tune(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u32 val;

	/*enable software tuning*/
	val = sdhci_readl(host, jlq_host->vendor_offset + AT_CTRL);
	val |= SW_TUNE_EN;
	sdhci_writel(host, val, jlq_host->vendor_offset + AT_CTRL);

}

static void sdhci_jlq_reset_tuning(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl &= ~SDHCI_CTRL_TUNED_CLK;
	ctrl &= ~SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
}

static int sdhci_jlq_cfg_sample_delay(struct sdhci_host *host,
						u8 sample_delay)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u32 val;

	if (sample_delay == 0) {
		/*sample delay line bypass*/
		val = SMPLDL_BYPASS;

		/* input select : clk_dl */
		val |= SMPLDL_INPSEL_CLK_DL << SMPLDL_INPSEL_SHIFT;

		sdhci_writel(host, val, SMPLDL_CNFG);
	} else {
		val = (sample_delay - 1) >= DL_ELEMENT_MAX ? SMPLDL_EXTDLY : 0;

		/* input select : clk_dl */
		val |= SMPLDL_INPSEL_CLK_DL << SMPLDL_INPSEL_SHIFT;

		sdhci_writel(host, val, SMPLDL_CNFG);

		/* set sample delay */
		val = ((sample_delay - 1) & DL_ELEMENT_MASK) <<
			CENTER_PH_CODE_SHIFT;
		sdhci_writel(host, val, jlq_host->vendor_offset + AT_STAT);
	}

	return 0;
}

static int sdhci_jlq_calc_drv_delay(struct sdhci_host *host,
						bool is_ddr)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u64 period;	/* period of clktx, in unit of ps */
	u16 drive_delay;

	period = 1000ULL * 1000 * 1000 * 1000 / jlq_host->clk_rate;

	if (is_ddr)
		drive_delay = (period / DELAY_ELEMNT_PERIOD + 3) / 4;
	else
		drive_delay = (period / DELAY_ELEMNT_PERIOD + 1) / 2;

	if (drive_delay >= DL_ELEMENT_MAX * 2)
		drive_delay = DL_ELEMENT_MAX * 2 - 1;

	jlq_host->drive_delay = (u8)drive_delay;

	return 0;
}

static int sdhci_jlq_tuning_cfg(struct sdhci_host *host,
						u32 clk)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios	curr_ios = host->mmc->ios;
	bool is_ddr;
	bool is_sd_clk_en;
	u16 clock_control;
	u8 tuning_cfg;

	if ((curr_ios.timing == MMC_TIMING_UHS_DDR50) ||
		(curr_ios.timing == MMC_TIMING_MMC_DDR52) ||
		(curr_ios.timing == MMC_TIMING_MMC_HS400))
		is_ddr = true;
	else
		is_ddr = false;

	sdhci_jlq_calc_drv_delay(host, is_ddr);

	if (jlq_host->clk_rate <= UHS_DDR50_MAX_DTR) {
		tuning_cfg = sdhci_jlq_get_tuning_cfg(clk, is_ddr);
		jlq_host->sample_delay =
			tuning_cfg_table[tuning_cfg].sample_delay;
	} else {
		jlq_host->sample_delay = 0;
	}

	/*use the saved_tuning_phase in HS200 mode when in HS400 mode*/
	if (curr_ios.timing == MMC_TIMING_MMC_HS400 &&
			jlq_host->clk_rate > MMC_HIGH_52_MAX_DTR)
		jlq_host->sample_delay = jlq_host->saved_tuning_phase;

	/*disable clk*/
	clock_control = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	is_sd_clk_en = clock_control & SDHCI_CLOCK_CARD_EN ? true : false;
	if (is_sd_clk_en) {
		clock_control &= ~SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clock_control, SDHCI_CLOCK_CONTROL);
	}

	/*config drive_delay*/
	sdhci_jlq_cfg_drv_delay(host, jlq_host->drive_delay);

	/*
	 * config sample_delay
	 * notice: the sample delay set here will be overridden in
	 * tuning executed later for timing HS200/SDR104/SDR50/DDR50, etc.
	 */
	sdhci_jlq_reset_tuning(host);
	sdhci_jlq_sw_tune(host);
	sdhci_jlq_cfg_sample_delay(host, jlq_host->sample_delay);

	/*enable clk*/
	if (is_sd_clk_en) {
		clock_control |= SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clock_control, SDHCI_CLOCK_CONTROL);
	}

	pr_debug("%s: drv_delay %d sample_delay %d\n",
		mmc_hostname(host->mmc),
		jlq_host->drive_delay, jlq_host->sample_delay);

	return 0;
}

/*
 * dll config for Data Strobe
 * notice: bus speed mode should be MMC_TIMING_MMC_HS400.
 */
static int sdhci_jlq_dll_config(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int retry_time = 0;
	u8 md_number = MD_NUM_DEFAULT;
	u32 dll_cnfg1;
	unsigned long ret_completion;

	init_completion(&jlq_host->dll_irq_completion);

	do {
		/*force to close DLL*/
		sdhci_writel(host, DLL_SCLOSE, DLL_CTRL);
		udelay(100);

		/*reset intr status before DLL config*/
		jlq_host->dll_intr_status = 0;

		sdhci_writel(host, DLL_CNFG0_DEFAULT, DLL_CNFG0);
		sdhci_writel(host, DLL_CNFG2_DEFAULT, DLL_CNFG2);

		if (jlq_host->clk_rate == MMC_HS200_MAX_DTR)
			dll_cnfg1 = DLL_CNFG1_DEFAULT_200MHz | md_number << MD_NUM_SHIFT;
		else
			dll_cnfg1 = DLL_CNFG1_DEFAULT_1XXMHz | md_number << MD_NUM_SHIFT;

		sdhci_writel(host, dll_cnfg1, DLL_CNFG1);

		/*enable DLL intr: lock & unlock*/
		sdhci_writel(host,
			DLL_INTR_UNLOCK | DLL_INTR_LOCK, DLL_INTR_EN);

		reinit_completion(&jlq_host->dll_irq_completion);

		/*enable DLL*/
		sdhci_writel(host, DLL_ENABLE, DLL_CTRL);

		/*wait till DLL intr lock or unlock is issued*/
		ret_completion = wait_for_completion_timeout(&jlq_host->dll_irq_completion,
			msecs_to_jiffies(DLL_IRQ_TIMEOUT_MS));

		if (jlq_host->dll_intr_status == DLL_INTR_LOCK) {
			break;
		} else if (jlq_host->dll_intr_status == DLL_INTR_UNLOCK) {
			if (md_number < MD_NUM_MAX)
				md_number++;

			pr_warn("%s: dll unlocked in 0x%x periods of clktx.\t"
				"Try md_num 0x%x next time\n",
				mmc_hostname(host->mmc),
				sdhci_readl(host, DLL_CNFG2),
				md_number);
		} else if (ret_completion == 0) {
			pr_debug("%s: dll intr lock or unlock not issued in %d ms\n",
				mmc_hostname(host->mmc),
				DLL_IRQ_TIMEOUT_MS);
		}

		retry_time++;

		pr_info("%s: retry DLL config time %d\n", mmc_hostname(host->mmc), retry_time);
	} while (retry_time < 10);

	if (jlq_host->dll_intr_status == DLL_INTR_LOCK)
		pr_info("%s: dll locked and updated\n",
			mmc_hostname(host->mmc));
	else
		panic("%s: dll can't lock! error!\n",
			mmc_hostname(host->mmc));

	return 0;
}

static void sdhci_jlq_disable_dll(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);

	if (jlq_host->dll_intr_status != 0) {
		pr_debug("%s: %s: timing %d: DO DISABLE DLL\n",
			mmc_hostname(host->mmc),
			__func__,
			host->mmc->ios.timing);

		sdhci_writel(host, DLL_SCLOSE, DLL_CTRL);

		udelay(100);

		/* reset intr status */
		jlq_host->dll_intr_status = 0;
	}
}
#if IS_ENABLED(CONFIG_SDC_JLQ)
static int sdhci_jlq_reconfig_dll(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios	curr_ios = host->mmc->ios;
	int ret = 0;

	if (jlq_host->dll_intr_status == DLL_INTR_UNLOCK &&
		curr_ios.timing == MMC_TIMING_MMC_HS400 &&
		jlq_host->clk_rate > MMC_HIGH_52_MAX_DTR)
		ret = sdhci_jlq_dll_config(host);

	return ret;
}
#endif
static irqreturn_t sdhci_jlq_dll_irq(int irq, void *dev_id)
{
	struct sdhci_host *host = dev_id;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u32 dll_intr_status;

	dll_intr_status = sdhci_readl(host, DLL_INTR_STATUS);

	if (dll_intr_status == DLL_INTR_LOCK)
		sdhci_writel(host, DLL_UPDATE, DLL_CTRL);

	sdhci_writel(host, dll_intr_status, DLL_INTR_STATUS);

	/*
	 * if the irq is issued during DLL config,
	 * produce a completion to continue the config scheme.
	 * if the irq is issued during auto re-lock of DLL,
	 * do nothing here (if DLL is locked again, do nothing.
	 * if DLL is unlocked, restart the config scheme before
	 * next command)
	 */
	if (jlq_host->dll_intr_status == 0)
		complete(&jlq_host->dll_irq_completion);
	else if (dll_intr_status == DLL_INTR_UNLOCK)
		pr_warn("%s: DLL_INTR_UNLOCK 0x%x during auto re-lock\n",
			mmc_hostname(host->mmc), dll_intr_status);
	else if (dll_intr_status == DLL_INTR_LOCK)
		pr_debug("%s: dll intr status 0x%x during auto re-lock\n",
			mmc_hostname(host->mmc), dll_intr_status);
	else
		panic("%s: dll intr status 0x%x error!\n",
			mmc_hostname(host->mmc), dll_intr_status);

	jlq_host->dll_intr_status = dll_intr_status;

	return IRQ_HANDLED;
}

static int sdhci_jlq_cfg_auto_tuning(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u32 val;

	/*AT_EN should be programmed only when CLK_CTRL_R.SD_CLK_EN is 0*/
	sdhci_writel(host, AT_CTRL_DEFAULT, jlq_host->vendor_offset + AT_CTRL);

	/* input select : clk_dl */
	val = SMPLDL_INPSEL_CLK_DL << SMPLDL_INPSEL_SHIFT;

	/* EXTDLY = 0, BYPASS = 0 */
	sdhci_writel(host, val, SMPLDL_CNFG);

	/* config ier to enable tuning error later */
	host->ier |= SDHCI_INT_TUNING_ERR;
	if (jlq_host->has_cqe)
		host->cqe_ier |= SDHCI_INT_TUNING_ERR;

	return 0;
}

#if IS_ENABLED(CONFIG_JGKI)
static s16 sdhci_jlq_get_best_smpl_dly(struct sdhci_host *host, u64 candiates)
{
	const u8 iter = 64;
	u64 __c;
	u8 i, j;
	s16 loc = -1;
	u8 consecutive_bits_set_num = 0;
	u8 start_bit = 0;
	u8 most_set_bit_temp;

	for (i = 0; i < iter; i++) {
		__c = ror64(candiates, i);

		most_set_bit_temp = 0;

		for (j = 0; j < iter; j++) {
			if (__c & (1ULL << j))
				most_set_bit_temp = j;
			else
				break;
		}

		if (consecutive_bits_set_num < most_set_bit_temp + 1) {
			consecutive_bits_set_num = most_set_bit_temp + 1;
			start_bit = i;
		}
	}

	if (consecutive_bits_set_num > iter / 8)
		loc = (start_bit + consecutive_bits_set_num / 2) % iter;

	pr_debug("%s: candiates 0x%016llx; consecutive_bits_set_num %d; start_bit %d; loc %d\n",
		mmc_hostname(host->mmc),
		candiates,
		consecutive_bits_set_num,
		start_bit,
		loc);

	return loc;
}

static int sdhci_jlq_execute_sw_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	bool is_sd_clk_en;
	u16 clock_control;
	int sample_delay;
	u64 candiates = 0;
	s16 sample_delay_found;

	/* the extra fixed delay of SAMPL could be used (EXTDLY = 1) */
	host->tuning_loop_count = DL_ELEMENT_MAX * 2;

	/*disable clk*/
	clock_control = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	is_sd_clk_en = clock_control & SDHCI_CLOCK_CARD_EN ? true : false;
	if (is_sd_clk_en) {
		clock_control &= ~SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clock_control, SDHCI_CLOCK_CONTROL);
	}

	sdhci_jlq_sw_tune(host);

	/*enable clk*/
	if (is_sd_clk_en) {
		clock_control |= SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clock_control, SDHCI_CLOCK_CONTROL);
	}

	jlq_host->tuning_in_progress = true;

	/* config INT for tuning */
	sdhci_writel(host, SDHCI_INT_TUNING_ALL_MASK, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_TUNING_ALL_MASK, SDHCI_SIGNAL_ENABLE);

	for (sample_delay = 0; sample_delay < host->tuning_loop_count;
		sample_delay += SMPL_DLY_SKIP) {

		sdhci_jlq_cfg_sample_delay(host, (u8)sample_delay);

		jlq_host->tuning_failed = false;

		sdhci_send_tuning(host, opcode);

		if (!host->tuning_done)
			pr_info("%s: Tuning timeout at sample delay = %d\n",
				mmc_hostname(host->mmc), sample_delay);
		else if (!jlq_host->tuning_failed)
			/* tuning pattern is sampled */
			candiates |= 1ULL << (sample_delay / SMPL_DLY_SKIP);

		/* clear buffered tuning block */
		sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
	}

	/* restore INT config after tuning */
	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);

	jlq_host->tuning_in_progress = false;

	sample_delay_found = sdhci_jlq_get_best_smpl_dly(host, candiates);

	if (sample_delay_found != -1) {
		jlq_host->saved_tuning_phase =
			sample_delay_found * SMPL_DLY_SKIP;

		sdhci_jlq_cfg_sample_delay(host,
			jlq_host->saved_tuning_phase);

		/* config sw retune period regardless of SDHCI_TUNING_MODE_3*/
		host->mmc->retune_period = SW_RETUNE_PERIOD_SECS;

		pr_info("%s: sw tuning done(%u)\n",
			mmc_hostname(host->mmc),
			jlq_host->saved_tuning_phase);

		return 0;
	}

	/* disable retune because of tuning failure*/
	host->mmc->retune_period = 0;

	return -EIO;
}
#endif

static int sdhci_jlq_execute_hw_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int ret;
	u16 ctrl;
	u32 val;

	/* the extra fixed delay of SAMPL will not be used (EXTDLY = 0) */
	host->tuning_loop_count = DL_ELEMENT_MAX;

	/* disable retune since hardware tuning & auto-tuning*/
	host->mmc->retune_period = 0;

	/* config auto-tuning before executing tuning */
	sdhci_jlq_cfg_auto_tuning(host);

	ret = sdhci_execute_tuning(mmc, opcode);

	if (ret == 0) {
		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		jlq_host->tuning_done = ctrl & SDHCI_CTRL_TUNED_CLK ?
			true : false;

		val = sdhci_readl(host, jlq_host->vendor_offset + AT_STAT);

		pr_info("%s: hw tuning done=%d(%d)\n",
			mmc_hostname(host->mmc),
			jlq_host->tuning_done,
			((val >> CENTER_PH_CODE_SHIFT) & DL_ELEMENT_MASK) + 1);

		if (!jlq_host->tuning_done)
			ret = -ETIMEDOUT;
		else
			jlq_host->saved_tuning_phase =
				((val >> CENTER_PH_CODE_SHIFT) & DL_ELEMENT_MASK) + 1;
	}

	return ret;
}

static int sdhci_jlq_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int retries = TUNING_RETRY_MAX_TIMES;
	int ret;

	do {
		sdhci_jlq_reset_tuning(host);

		sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

#if IS_ENABLED(CONFIG_JGKI)
		if (jlq_host->clk_rate <= UHS_SDR50_MAX_DTR)
			ret = sdhci_jlq_execute_sw_tuning(mmc, opcode);
		else
			ret = sdhci_jlq_execute_hw_tuning(mmc, opcode);
#else
		ret = sdhci_jlq_execute_hw_tuning(mmc, opcode);
#endif
	} while (ret && --retries);

	return ret;
}

static void sdhci_jlq_set_uhs_signaling(struct sdhci_host *host,
					unsigned int timing)
{
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u16 emmc_ctrl;
	u16 ctrl_2;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	if ((timing == MMC_TIMING_MMC_HS200) ||
	    (timing == MMC_TIMING_UHS_SDR104))
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
	else if (timing == MMC_TIMING_UHS_SDR12)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
	else if (timing == MMC_TIMING_UHS_SDR25)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
	else if (timing == MMC_TIMING_UHS_SDR50)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
	else if ((timing == MMC_TIMING_UHS_DDR50) ||
		 (timing == MMC_TIMING_MMC_DDR52))
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
	else if (timing == MMC_TIMING_MMC_HS400)
		ctrl_2 |= SDHCI_JLQ_CTRL_HS400;
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);

	if (jlq_host->device_type == DEVICE_EMMC) {
		/*config dll for HS400 (enhanced) data strobe*/
		if (timing == MMC_TIMING_MMC_HS400 &&
			jlq_host->clk_rate > MMC_HIGH_52_MAX_DTR)
			sdhci_jlq_dll_config(host);
		else
			sdhci_jlq_disable_dll(host);

		emmc_ctrl = sdhci_readw(host,
			jlq_host->vendor_offset + EMMC_CTRL);

		if (timing == MMC_TIMING_MMC_HS400 &&
			jlq_host->dll_intr_status == DLL_INTR_LOCK &&
			mmc->ios.enhanced_strobe)
			emmc_ctrl |= ENH_STROBE_ENABLE;
		else
			emmc_ctrl &= ~ENH_STROBE_ENABLE;

		sdhci_writew(host, emmc_ctrl,
			jlq_host->vendor_offset + EMMC_CTRL);

		if (timing == MMC_TIMING_MMC_HS400 &&
			mmc->ios.enhanced_strobe)
			pr_debug("%s: EMMC_CTRL 0x%08x\n",
				mmc_hostname(host->mmc),
				sdhci_readw(host,
					jlq_host->vendor_offset + EMMC_CTRL));
	}
}

static u32 sdhci_jlq_irq_for_tuning(struct sdhci_host *host, u32 intmask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u32 mask;
	u32 command;

	if (intmask & SDHCI_INT_TUNING_ERR) {
		pr_info("%s: auto tuning error triggers retuning. intmask 0x%08x.\n",
			   mmc_hostname(host->mmc), intmask);
		sdhci_writel(host, SDHCI_INT_TUNING_ERR, SDHCI_INT_STATUS);
		intmask &= ~SDHCI_INT_TUNING_ERR;

		/* use INT_RETUNE to trigger the retuning procedure later */
		intmask |= SDHCI_INT_RETUNE;
	}

	if (!jlq_host->tuning_in_progress)
		return intmask;

	command = SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND));
	if (command != MMC_SEND_TUNING_BLOCK &&
		command != MMC_SEND_TUNING_BLOCK_HS200) {
		pr_err("%s: tuning: Unexpected CMD %d with interrupt 0x%08x.\n",
			   mmc_hostname(host->mmc), command, intmask);
		sdhci_dumpregs(host);
		return intmask;
	}

	if (intmask & SDHCI_INT_TUNING_ERR_MASK) {

		mask = intmask & SDHCI_INT_TUNING_ALL_MASK;

		/* Clear all tuning interrupts. */
		sdhci_writel(host, mask, SDHCI_INT_STATUS);

		host->tuning_done = 1;

		jlq_host->tuning_failed = true;

		intmask &= ~(SDHCI_INT_TUNING_ALL_MASK | SDHCI_INT_ERROR);

		if (intmask) {
			sdhci_writel(host, intmask, SDHCI_INT_STATUS);
			pr_err("%s: tuning: Unexpected interrupt 0x%08x.\n",
				   mmc_hostname(host->mmc), intmask);
			sdhci_dumpregs(host);
		}

		wake_up(&host->buf_ready_int);
	} else	if (intmask != SDHCI_INT_DATA_AVAIL) {
		pr_err("%s: tuning: Unexpected interrupt 0x%08x.\n",
			   mmc_hostname(host->mmc), intmask);
	}

	/*
	 * let sdhci_irq to process other irqs,
	 * including SDHCI_INT_DATA_AVAIL when tuning success
	 */
	return intmask;
}

static void sdhci_jlq_cmd_conflict_check(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u8 val;

	/*
	 * disable CMD_CONFLICT_CHECK when cclk_tx is more than
	 * 100MHz to avoid CMD conflict ERROR
	 */
	val = sdhci_readb(host,
		jlq_host->vendor_offset + MSHC_CTRL);

	if (jlq_host->clk_rate > UHS_SDR50_MAX_DTR)
		val &= ~CMD_CONFLICT_CHECK;
	else
		val |= CMD_CONFLICT_CHECK;

	sdhci_writeb(host, val,
		jlq_host->vendor_offset + MSHC_CTRL);
}

/**
 * __sdhci_jlq_set_clock - sdhci_jlq clock control.
 *
 * Description:
 * JLQ controller does not use internal divider and
 * instead directly control the GCC clock as per
 * HW recommendation.
 **/
static void __sdhci_jlq_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;
	/*
	 * Keep actual_clock as zero -
	 * - since there is no divider used so no need of having actual_clock.
	 * - JLQ controller uses SDCLK for data timeout calculation. If
	 *   actual_clock is zero, host->clock is taken for calculation.
	 */
	host->mmc->actual_clock = 0;

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	/*
	 * JLQ controller do not use clock divider.
	 * Thus read SDHCI_CLOCK_CONTROL and only enable
	 * clock with no divider value programmed.
	 */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_enable_clk(host, clk);
}

#ifdef JR510_HAPS_NO_MMC_SD_CLKTX_CDIV
static void jlq_sdhci_set_clock_haps(struct sdhci_jlq_host *jlq_host,
	unsigned int clock)
{
#define EMMC_BCLK_SRC_MX_CTL	0x104
#define EMMC_CLKTX_CDIV_CTL		0x190
#define EMMC_CLKTX_CDIV_STAT	0x194
	unsigned int clk_ctl_reg_val;

	if (clock <= 400000)
		//cclktx = 200KHz
		clk_ctl_reg_val = 1<<0 | 0<<1 | 0x1FFF<<16;
	else
		//cclktx = 10MHz
		clk_ctl_reg_val = 1<<0 | 1<<1 | 0x1FFF<<16;

	writel(clk_ctl_reg_val, jlq_host->top_crg_clks + EMMC_CLKTX_CDIV_CTL);

	pr_info("%s: clock %d; EMMC_CLKTX_CDIV_CTL 0x%x 0x%x; EMMC_CLKTX_CDIV_STAT 0x%x\n",
		__func__,
		clock,
		clk_ctl_reg_val,
		readl(jlq_host->top_crg_clks + EMMC_CLKTX_CDIV_CTL),
		readl(jlq_host->top_crg_clks + EMMC_CLKTX_CDIV_STAT));
	pr_debug("%s: EMMC_BCLK_SRC_MX_CTL 0x%x\n",
		__func__,
		readl(jlq_host->top_crg_clks + EMMC_BCLK_SRC_MX_CTL));
}
#endif

/* sdhci_jlq_set_clock - Called with (host->lock) spinlock held. */
static void sdhci_jlq_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios curr_ios = host->mmc->ios;
	unsigned int sup_clk;
	int rc;

	if (!clock) {
		jlq_host->clk_rate = clock;
		goto out;
	}

	sup_clk = sdhci_jlq_get_sup_clk_rate(host, clock);

	rc = clk_set_rate(jlq_host->clktx, sup_clk);

	if (rc) {
		pr_err("%s: Failed to set clock at rate %u at timing %d\n",
		       mmc_hostname(host->mmc), clock,
		       curr_ios.timing);
		return;
	}

#ifdef JR510_HAPS_NO_MMC_SD_CLKTX_CDIV
	jlq_sdhci_set_clock_haps(jlq_host, sup_clk);
#endif

	jlq_host->clk_rate = clk_get_rate(jlq_host->clktx);

	if (sup_clk > 400000)
		pr_info("%s: Setting clock at rate %u (actual rate %lu) at timing %d\n",
			 mmc_hostname(host->mmc),
			 sup_clk,
			 jlq_host->clk_rate,
			 curr_ios.timing);
	else
		pr_debug("%s: Setting clock at rate %u (actual rate %lu) at timing %d\n",
			 mmc_hostname(host->mmc),
			 sup_clk,
			 jlq_host->clk_rate,
			 curr_ios.timing);

	sdhci_jlq_cmd_conflict_check(host);

	sdhci_jlq_tuning_cfg(host, sup_clk);

out:
	__sdhci_jlq_set_clock(host, clock);
}

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
static void sdhci_jlq_dump_vendor_regs(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);

	SDHCI_JLQ_D("----------- VENDOR REGISTER DUMP -----------\n");

	SDHCI_JLQ_D("VENDOR_SPECIFIC_AREA: 0x%08x | CMDQ_AREA: 0x%08x\n",
		sdhci_readw(host, SDHCI_VENDOR_SPECIFIC_AREA),
		sdhci_readw(host, SDHCI_CMDQ_AREA));

	SDHCI_JLQ_D("MSHC_VER_ID: 0x%08x | MSHC_VER_TYPE:  0x%08x\n",
		sdhci_readl(host, jlq_host->vendor_offset + MSHC_VER_ID),
		sdhci_readl(host, jlq_host->vendor_offset + MSHC_VER_TYPE));
	SDHCI_JLQ_D("MSHC_CTRL: 0x%08x | MBIU_CTRL:  0x%08x\n",
		sdhci_readb(host, jlq_host->vendor_offset + MSHC_CTRL),
		sdhci_readb(host, jlq_host->vendor_offset + MBIU_CTRL));
	SDHCI_JLQ_D("EMMC_CTRL: 0x%08x | BOOT_CTRL:  0x%08x\n",
		sdhci_readw(host, jlq_host->vendor_offset + EMMC_CTRL),
		sdhci_readw(host, jlq_host->vendor_offset + BOOT_CTRL));
	SDHCI_JLQ_D("AT_CTRL: 0x%08x | AT_STAT:  0x%08x\n",
		sdhci_readl(host, jlq_host->vendor_offset + AT_CTRL),
		sdhci_readl(host, jlq_host->vendor_offset + AT_STAT));

	SDHCI_JLQ_D("COMMDL_CNFG: 0x%08x | CLKDL_CNFG:  0x%08x\n",
		sdhci_readl(host, COMMDL_CNFG),
		sdhci_readl(host, CLKDL_CNFG));

	SDHCI_JLQ_D("CLKDL_DC: 0x%08x | SMPLDL_CNFG:  0x%08x\n",
		sdhci_readl(host, CLKDL_DC),
		sdhci_readl(host, SMPLDL_CNFG));

	SDHCI_JLQ_D("ATDL_CNFG: 0x%08x\n",
		sdhci_readl(host, ATDL_CNFG));

	if (jlq_host->device_type == DEVICE_EMMC) {
		SDHCI_JLQ_D("MISC: 0x%08x | DLLDL_CNFG:  0x%08x\n",
			sdhci_readl(host, PHY_MISC_EMMC),
			sdhci_readl(host, DLLDL_CNFG));

		SDHCI_JLQ_D("DLLMST_TSTDC: 0x%08x | DLL_STATUS0:  0x%08x\n",
			sdhci_readl(host, DLLMST_TSTDC),
			sdhci_readl(host, DLL_STATUS0));

		SDHCI_JLQ_D("DLL_STATUS1: 0x%08x | DLL_CNFG0:  0x%08x\n",
			sdhci_readl(host, DLL_STATUS1),
			sdhci_readl(host, DLL_CNFG0));

		SDHCI_JLQ_D("DLL_CNFG1: 0x%08x | DLL_CNFG2:  0x%08x\n",
			sdhci_readl(host, DLL_CNFG1),
			sdhci_readl(host, DLL_CNFG2));

		SDHCI_JLQ_D("DLL_CTRL: 0x%08x | DLL_INTR_EN:  0x%08x\n",
			sdhci_readl(host, DLL_CTRL),
			sdhci_readl(host, DLL_INTR_EN));

		SDHCI_JLQ_D("DLL_INTR_STATUS: 0x%08x | DLL_INTR_RAW: 0x%08x\n",
			sdhci_readl(host, DLL_INTR_STATUS),
			sdhci_readl(host, DLL_INTR_RAW));
	} else {
		SDHCI_JLQ_D("MISC: 0x%08x\n",
			sdhci_readl(host, PHY_MISC_SD));
	}
}
#endif
static void sdhci_jlq_reset_all_exit(struct sdhci_host *host,
			     u8 mask)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	u16 emmc_ctrl;

	/*init for JLQ sdhci controller*/
	if (jlq_host->device_type == DEVICE_EMMC) {

		emmc_ctrl = sdhci_readw(host,
			jlq_host->vendor_offset + EMMC_CTRL);

		emmc_ctrl |= CARD_IS_EMMC;
		sdhci_writew(host, emmc_ctrl,
			jlq_host->vendor_offset + EMMC_CTRL);
	}
}

static void sdhci_jlq_reset(struct sdhci_host *host, u8 mask)
{
	sdhci_reset(host, mask);

	if (mask & SDHCI_RESET_ALL)
		sdhci_jlq_reset_all_exit(host, mask);
}

static void sdhci_jlq_hw_reset(struct sdhci_host *host)
{
	sdhci_jlq_reset(host, SDHCI_RESET_ALL);
}

static int sdhci_jlq_select_drive_strength(struct mmc_card *card,
				       unsigned int max_dtr, int host_drv,
				       int card_drv, int *drv_type)
{
	struct sdhci_host *host = mmc_priv(card->host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	unsigned int drive_strength = jlq_host->force_drive_strength;

	pr_info("%s: card_drv 0x%x drive_strength %d\n",
		mmc_hostname(host->mmc),
		card_drv,
		drive_strength);

	if (((1 << drive_strength) & card_drv) ==
		(1 << drive_strength))
		return drive_strength;
	else
		return MMC_SET_DRIVER_TYPE_B;
}

static const struct of_device_id sdhci_jlq_dt_match[] = {
	{ .compatible = "jlq,sdhci-jlq" },
	{},
};

MODULE_DEVICE_TABLE(of, sdhci_jlq_dt_match);

#if IS_ENABLED(CONFIG_MMC_CQHCI)
static u32 sdhci_jlq_cqhci_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	intmask = sdhci_jlq_irq_for_tuning(host, intmask);

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);

	/*
	 * clear CQE interrupt:
	 * To clear the CQE interrupt in SDHCI_INT_STATUS,
	 * need to clear the relevant interrupt in CQHCI_IS first.
	 */
	sdhci_writel(host, intmask & host->cqe_ier, SDHCI_INT_STATUS);

	return 0;
}

static void sdhci_jlq_dumpregs(struct mmc_host *mmc)
{
	sdhci_dumpregs(mmc_priv(mmc));
}

static void sdhci_cqe_set_tx_mode(struct sdhci_host *host)
{
	u16 mode;

	mode = sdhci_readw(host, SDHCI_TRANSFER_MODE);

	mode |= SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_MULTI | SDHCI_TRNS_DMA;

	sdhci_writew(host, mode, SDHCI_TRANSFER_MODE);
}

static void sdhci_jlq_cqe_enable(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 reg;

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	pr_debug("%s: %s\n",
		mmc_hostname(mmc),
		__func__);
#endif

	reg = sdhci_readl(host, SDHCI_PRESENT_STATE);
	while (reg & SDHCI_DATA_AVAILABLE) {
		sdhci_readl(host, SDHCI_BUFFER);
		reg = sdhci_readl(host, SDHCI_PRESENT_STATE);
	}

	/*
	 * reset data line
	 * according to Synopsys's user guide
	 */
	sdhci_reset(host, SDHCI_RESET_DATA);

	sdhci_cqe_set_tx_mode(host);

	sdhci_cqe_enable(mmc);
}

static const struct cqhci_host_ops sdhci_jlq_cqhci_ops = {
	.enable         = sdhci_jlq_cqe_enable,
	.disable        = sdhci_cqe_disable,
	.dumpregs       = sdhci_jlq_dumpregs,
};
#else
static u32 sdhci_jlq_irq(struct sdhci_host *host, u32 intmask)
{
	return sdhci_jlq_irq_for_tuning(host, intmask);
}
#endif

static const struct sdhci_ops sdhci_jlq_ops = {
	.reset = sdhci_jlq_reset,
	.hw_reset = sdhci_jlq_hw_reset,
	.set_clock = sdhci_jlq_set_clock,
#if IS_ENABLED(CONFIG_MMC_CQHCI)
	.irq = sdhci_jlq_cqhci_irq,
#else
	.irq = sdhci_jlq_irq,
#endif
	.get_min_clock = sdhci_jlq_get_min_clock,
	.get_max_clock = sdhci_jlq_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.adma_write_desc	= sdhci_jlq_adma_write_desc,
	.set_uhs_signaling = sdhci_jlq_set_uhs_signaling,
#if IS_ENABLED(CONFIG_SDC_JLQ)
	.reconfig_dll = sdhci_jlq_reconfig_dll,
#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	.dump_vendor_regs = sdhci_jlq_dump_vendor_regs,
#endif
#endif
};

static const struct sdhci_pltfm_data sdhci_jlq_pdata = {
	.quirks =	SDHCI_QUIRK_BROKEN_CARD_DETECTION |
			SDHCI_QUIRK_NO_CARD_NO_RESET |
			SDHCI_QUIRK_SINGLE_POWER_WRITE |
			SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
			SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 =	SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_ACMD23_BROKEN,
	.ops = &sdhci_jlq_ops,
};

static int sdhci_jlq_add_host(struct sdhci_host *host)
{
#if IS_ENABLED(CONFIG_MMC_CQHCI)
	struct cqhci_host *cq_host;
	u16 cmdq_offset;
	bool dma64;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
#endif
	int ret;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	/* remove the bus speed modes caps from host controller */
	host->mmc->caps &= ~(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
		MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_DDR50 | MMC_CAP_UHS_SDR104);
	host->mmc->caps2 &= ~(MMC_CAP2_HS200 | MMC_CAP2_HS400);

	/*
	 * for MMC_SDHCI_JLQ, use the property of device node
	 * to config bus speed modes of host caps.
	 */
	ret = mmc_of_parse(host->mmc);
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_MMC_CQHCI)
	if (jlq_host->has_cqe) {
		cq_host = devm_kzalloc(host->mmc->parent,
				       sizeof(*cq_host), GFP_KERNEL);
		if (!cq_host) {
			ret = -ENOMEM;
			goto cleanup;
		}

		cmdq_offset = readw_relaxed((host->ioaddr + SDHCI_CMDQ_AREA));

		cq_host->mmio = host->ioaddr + cmdq_offset;
		cq_host->ops = &sdhci_jlq_cqhci_ops;

		dma64 = host->flags & SDHCI_USE_64_BIT_DMA;
		if (dma64)
			cq_host->caps |= CQHCI_TASK_DESC_SZ_128;

		ret = cqhci_init(cq_host, host->mmc, dma64);
		if (ret)
			goto cleanup;
	}
#endif

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	sdhci_cleanup_host(host);
	return ret;
}

static struct sdhci_host *sdhci_host_sdio;
void sdhci_bus_scan(void)
{
	pr_info("%s, entry\n", __func__);
	if (sdhci_host_sdio && (sdhci_host_sdio->mmc)) {
		pr_info("%s, sdhci_host_sdio enter\n", __func__);
		sdhci_host_sdio->mmc->ios.vdd = 21;
		if (sdhci_host_sdio->ops->set_clock)
			sdhci_host_sdio->ops->set_clock(sdhci_host_sdio, 1);
		sdhci_host_sdio->mmc->rescan_entered = 0;
		mmc_detect_change(sdhci_host_sdio->mmc, 0);
		pr_info("%s,exit\n", __func__);
	}
}

static int sdhci_jlq_hblk_un_reset(struct device *dev,
	struct sdhci_jlq_host *jlq_host)
{
#define HBLK_SW_RESET_REQ		0x10
#define EMMC_RST_REQ	(0x7F << 13)
#define SDIO0_RST_REQ	(0x3F << 6)
#define SDIO1_RST_REQ	(0x3F << 0)

	uint32_t val;
	struct device_node *dev_node;
	uint32_t hc_rst_mask = 0;

	dev_node = of_find_compatible_node(NULL, NULL,
	"jlq,hblk_sctrl");
	if (!dev_node) {
		dev_err(dev,
		"jlq,hblk_sctrl No compatible node found\n");
		return -ENODEV;
	}

	jlq_host->hblk_sctrl = of_iomap(dev_node, 0);
	if (IS_ERR(jlq_host->hblk_sctrl)) {
		dev_err(dev, "jlq,hblk_sctrl reg ioremap failed\n");
		return -ENOMEM;
	}
	of_node_put(dev_node);

	val = readl(jlq_host->hblk_sctrl + HBLK_SW_RESET_REQ);

	dev_dbg(dev, "HBLK_SW_RESET_REQ 0x%08x\n", val);

	if (strcmp(jlq_host->hc_name, "emmc_hc_mem") == 0)
		hc_rst_mask = EMMC_RST_REQ;
	else if (strcmp(jlq_host->hc_name, "sd0_hc_mem") == 0)
		hc_rst_mask = SDIO0_RST_REQ;
	else if (strcmp(jlq_host->hc_name, "sd1_hc_mem") == 0)
		hc_rst_mask = SDIO1_RST_REQ;

	if (hc_rst_mask != 0 &&
		(val & hc_rst_mask) != hc_rst_mask) {
		val |= hc_rst_mask;

		writel(val, jlq_host->hblk_sctrl + HBLK_SW_RESET_REQ);

		dev_dbg(dev, "%s: HBLK_SW_RESET_REQ 0x%08x\n",
		jlq_host->hc_name,
		readl(jlq_host->hblk_sctrl + HBLK_SW_RESET_REQ));
	}

	return 0;
}

#ifdef JR510_HAPS_NO_MMC_SD_CLKTX_CDIV
static int sdhci_jlq_clk_iomap(struct device *dev,
	struct sdhci_jlq_host *jlq_host)
{
	struct device_node *dev_node;

	dev_node = of_find_compatible_node(NULL, NULL, "jlq,top_crg_clks");
	if (!dev_node) {
		dev_err(dev, "jlq,top_crg_clks No compatible node found\n");
		return -ENODEV;
	}
	jlq_host->top_crg_clks = of_iomap(dev_node, 0);
	if (IS_ERR(jlq_host->top_crg_clks)) {
		dev_err(dev, "top_crg_clks reg ioremap failed\n");
		return -ENOMEM;
	}
	of_node_put(dev_node);
	return 0;
}
#endif

int sdhci_crypto_is_enabled(struct mmc_host *mmc, struct platform_device *pdev)
{
	struct device_node *node;

	node = of_parse_phandle(pdev->dev.of_node, SDHC_JLQ_CRYPTO_LABEL, 0);

	return node ? 1 : 0;
}

static ssize_t
card_slot_status_show(struct device *dev, struct device_attribute * attr, char *buf)
{
    struct mmc_host *mmc = container_of(dev, struct mmc_host, class_dev);
    return sprintf (buf, "%d\n", mmc_gpio_get_cd(mmc));
}

static DEVICE_ATTR_RO(card_slot_status);

static int sdhci_jlq_opp_init(struct device *dev, struct sdhci_host *host)
{
	int ret;
	int len;
	unsigned long freq;
	struct dev_pm_opp *opp;
	const char *vcore_name = VCORE_SUPPLY;
	struct device_node *np;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);

	np = of_node_get(dev->of_node);
	if (!of_find_property(np, VCORE_SUPPLY_NAME, NULL)) {
		dev_dbg(dev, "No vcore regulator found\n");
		return 0;
	}

	dev_info(dev, "vcore regulator found\n");

	jlq_host->opp_table = dev_pm_opp_set_regulators(dev, &vcore_name, 1);
	if (IS_ERR(jlq_host->opp_table)) {
		ret = PTR_ERR(jlq_host->opp_table);
		dev_err(dev, "Failed to set regulator: %d\n", ret);
		return ret;
	}

	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "failed to init OPP table\n");
		return ret;
	}

	len = dev_pm_opp_get_opp_count(dev);
	if (len <= 0) {
		dev_err(dev, "OPP count error: %d\n", len);
		return len;
	}

	freq = MAX_TX_CLK_FREQ;
	opp = dev_pm_opp_find_freq_ceil(dev, &freq);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		return ret;
	}

	dev_dbg(dev, "opp :freq %ul:  %u %u %u %u\n",
		freq,
		opp->supplies[0].u_volt,
		opp->supplies[0].u_volt_min,
		opp->supplies[0].u_volt_max,
		opp->supplies[0].u_amp);

	ret = regulator_set_voltage(jlq_host->opp_table->regulators[0],
		opp->supplies[0].u_volt,
		opp->supplies[0].u_volt_max);
	if (ret)
		pr_warn("%s: %s: vcore regulator_set_voltage err %d\n",
			mmc_hostname(host->mmc),
			__func__,
			ret);
	dev_pm_opp_put(opp);

	return ret;
}

int sdhci_jlq_emmc_cmd_gpio_cfg(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = devm_gpio_request(dev, jlq_host->cmd_gpio, "cmd-gpio");
	if (ret < 0) {
		pr_err("%s: %s: devm_gpio_request cmd_gpio ret = %d\n",
			mmc_hostname(host->mmc),
			__func__,
			ret);
		return -1;
	}

	/* for power saving */
	ret = gpio_direction_input(jlq_host->cmd_gpio);
	if (ret) {
		pr_err("%s: %s: gpio_direction_input cmd_gpio %d\n",
			mmc_hostname(host->mmc),
			__func__,
			ret);
	}

	devm_gpio_free(dev, jlq_host->cmd_gpio);

	return 0;
}

static int sdhci_jlq_probe(struct platform_device *pdev)
{
	struct resource *iomem;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_jlq_host *jlq_host;
	struct device_node *np = pdev->dev.of_node;
	int ret;
	u16 host_version;
	u32 extra;
	int clk_table_len;
	u32 *clk_table = NULL;

	host = sdhci_pltfm_init(pdev, &sdhci_jlq_pdata, sizeof(*jlq_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	/*
	 * extra adma table cnt for cross 128M boundary handling.
	 */
#if IS_ENABLED(CONFIG_JGKI)
	extra = DIV_ROUND_UP_ULL(dma_get_required_mask(&pdev->dev), SZ_128M);
	if (extra > SDHCI_MAX_SEGS)
		extra = SDHCI_MAX_SEGS;
#else
	extra = SDHCI_MAX_SEGS;
#endif
	host->adma_table_cnt += extra;

	/* don't use sdma */
	host->sdma_boundary = 0;
	pltfm_host = sdhci_priv(host);
	jlq_host = sdhci_pltfm_priv(pltfm_host);
	jlq_host->mmc = host->mmc;
	jlq_host->pdev = pdev;

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
	ret = sdhci_jlq_init_crypto(host->mmc, pdev);
	if (ret) {
		dev_err(&pdev->dev, "init crypto fail! ret=%d\n", ret);
		goto pltfm_free;
	}
#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO_PERSIST)
	host->sdhci_prepare_crypto = sdhci_prepare_crypto;
	host->sdhci_complete_crypto = sdhci_complete_crypto;
#endif

#else
	if (sdhci_crypto_is_enabled(host->mmc, pdev))
		host->mmc->caps2 |= MMC_CAP2_CRYPTO;

#endif

	if (np) {
		jlq_host->ctrl_id = of_alias_get_id(np, "sdhc");
		if (jlq_host->ctrl_id < 0) {
			ret = jlq_host->ctrl_id;
			dev_err(&pdev->dev,
				"ctrl id (%d)out of range error\n",
				jlq_host->ctrl_id);
			goto pltfm_free;
		}
	} else {
		ret = -1;
		dev_err(&pdev->dev, "device node null error\n");
		goto pltfm_free;
	}

	ret = device_property_read_u32(&pdev->dev,
		"device-type", &jlq_host->device_type);
	if (ret || jlq_host->device_type >= DEVICE_TYPE_MAX) {
		jlq_host->device_type = DEVICE_UNKNOWN;
		dev_warn(&pdev->dev, "unknown device type\n");
	}

	if (of_get_property(np, "drv_ste_type_A", NULL)) {
		jlq_host->force_drive_strength = MMC_SET_DRIVER_TYPE_A;
		host->mmc_host_ops.select_drive_strength =
			sdhci_jlq_select_drive_strength;
	}

	ret = sdhci_jlq_opp_init(&pdev->dev, host);
	if (ret)
		goto pltfm_free;

	sdhci_get_of_property(pdev);

	if (sdhci_jlq_dt_get_array(&pdev->dev, "jlq,clk-rates",
			&clk_table, &clk_table_len, 0)) {
		dev_err(&pdev->dev, "failed parsing supported clock rates\n");
		goto pltfm_free;
	}
	if (!clk_table || !clk_table_len) {
		dev_err(&pdev->dev, "Invalid clock table\n");
		goto pltfm_free;
	}

	jlq_host->sup_clk_table = clk_table;
	jlq_host->sup_clk_cnt = clk_table_len;

	jlq_host->saved_tuning_phase = INVALID_TUNING_PHASE;

	/* Setup base clock */
	jlq_host->bclk = devm_clk_get(&pdev->dev, "bclk");
	if (IS_ERR(jlq_host->bclk)) {
		ret = PTR_ERR(jlq_host->bclk);
		dev_err(&pdev->dev, "base clk setup failed (%d)\n", ret);
		goto pltfm_free;
	}

	ret = clk_prepare_enable(jlq_host->bclk);
	if (ret)
		goto pltfm_free;

	/* Setup card clock */
	jlq_host->clktx = devm_clk_get(&pdev->dev, "clktx");
	if (IS_ERR(jlq_host->clktx)) {
		ret = PTR_ERR(jlq_host->clktx);
		dev_err(&pdev->dev, "card clk setup failed (%d)\n", ret);
		goto bclk_disable;
	}

	ret = clk_prepare_enable(jlq_host->clktx);
	if (ret)
		goto bclk_disable;

	if (jlq_host->device_type == DEVICE_EMMC) {
		jlq_host->cmd_gpio = of_get_named_gpio_flags(pdev->dev.of_node,
					"cmd-gpio", 0, NULL);
		if (jlq_host->cmd_gpio < 0) {
			dev_err(&pdev->dev, "no emmc cmd gpio specifiled %d\n", jlq_host->cmd_gpio);
			goto clktx_disable;
		}

		if (!gpio_is_valid(jlq_host->cmd_gpio)) {
			dev_err(&pdev->dev, "emmc cmd gpio invalid\n");
			goto clktx_disable;
		}

		/* Setup eMMC DLL irq */
		jlq_host->dll_irq = platform_get_irq_byname(pdev, "dll_irq");
		if (jlq_host->dll_irq < 0) {
			dev_err(&pdev->dev, "Failed to get dll_irq by name (%d)\n",
					jlq_host->dll_irq);
			goto clktx_disable;
		}
		ret = devm_request_irq(&pdev->dev, jlq_host->dll_irq,
						sdhci_jlq_dll_irq, 0,
						dev_name(&pdev->dev), host);
		if (ret) {
			dev_err(&pdev->dev, "Request irq(%d) failed (%d)\n",
					jlq_host->dll_irq, ret);
			goto clktx_disable;
		}
	}

	host->mmc_host_ops.execute_tuning = sdhci_jlq_execute_tuning;

#ifdef JR510_HAPS_NO_MMC_SD_CLKTX_CDIV
	sdhci_jlq_clk_iomap(&pdev->dev, jlq_host);
	if (ret)
		goto clktx_disable;
#endif


	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (iomem && iomem->name)
		jlq_host->hc_name = iomem->name;

	ret = sdhci_jlq_hblk_un_reset(&pdev->dev, jlq_host);
	if (ret)
		goto clktx_disable;

	sdhci_enable_v4_mode(host);

	host_version = readw_relaxed((host->ioaddr + SDHCI_HOST_VERSION));
	dev_dbg(&pdev->dev, "Host Version: 0x%x Vendor Version 0x%x\n",
		host_version, ((host_version & SDHCI_VENDOR_VER_MASK) >>
			       SDHCI_VENDOR_VER_SHIFT));

	jlq_host->vendor_offset =
		readw_relaxed((host->ioaddr + SDHCI_VENDOR_SPECIFIC_AREA));

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev,
					 JLQ_MMC_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);


#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
		if (jlq_host->device_type == DEVICE_EMMC)
			host->mmc->hc_cmd_timeout_val = EMMC_CMD_TIMEOUT_VAL;
		else
			host->mmc->hc_cmd_timeout_val = SD_CMD_TIMEOUT_VAL;
#endif

	if (jlq_host->device_type == DEVICE_SDIO) {
		sdhci_host_sdio = host;
		pr_info("%s,sdio type!\n", __func__);
		sdhci_host_sdio->mmc->rescan_entered = 1;
	}

#if IS_ENABLED(CONFIG_MMC_CQHCI)
	if (jlq_host->device_type == DEVICE_EMMC
		&& of_property_read_bool(np, "supports-cqe")) {
		jlq_host->has_cqe = true;
		host->mmc->caps2 |= MMC_CAP2_CQE;
		if (!of_property_read_bool(np, "disable-cqe-dcmd"))
			host->mmc->caps2 |= MMC_CAP2_CQE_DCMD;
	}
#endif
	ret = sdhci_jlq_add_host(host);
	if (ret)
		goto pm_runtime_disable;

	if (mmc_can_gpio_cd(host->mmc))
		device_create_file(&(host->mmc->class_dev), &dev_attr_card_slot_status);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

pm_runtime_disable:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
clktx_disable:
	clk_disable_unprepare(jlq_host->clktx);
bclk_disable:
	clk_disable_unprepare(jlq_host->bclk);
pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_jlq_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int dead = (readl_relaxed(host->ioaddr + SDHCI_INT_STATUS) ==
		    0xffffffff);

	sdhci_remove_host(host, dead);

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	clk_disable_unprepare(jlq_host->clktx);
	clk_disable_unprepare(jlq_host->bclk);

	sdhci_pltfm_free(pdev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int sdhci_jlq_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int ret = 0;

	ret = pm_runtime_force_suspend(dev);

	if (!ret && jlq_host->device_type == DEVICE_EMMC) {
		ret = pinctrl_pm_select_sleep_state(dev);
		sdhci_jlq_emmc_cmd_gpio_cfg(dev);
	}
	else if (!ret && jlq_host->device_type == DEVICE_SD)
		ret = pinctrl_pm_select_sleep_state(dev);


	return ret;
}

static int sdhci_jlq_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int ret = 0;

	if (jlq_host->device_type == DEVICE_EMMC)
		ret = pinctrl_pm_select_default_state(dev);
	else if (jlq_host->device_type == DEVICE_SD)
		ret = pinctrl_pm_select_default_state(dev);

	if (!ret)
		ret = pm_runtime_force_resume(dev);

	return ret;
}
#endif

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
void sdhci_jlq_print_vcore_voltage(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);

	host->vcore_voltage = regulator_get_voltage(jlq_host->opp_table->regulators[0]);
	pr_err("%s: Vcore voltage %d uV\n",
		mmc_hostname(host->mmc),
		host->vcore_voltage);
}
#endif

#ifdef CONFIG_PM
static int sdhci_jlq_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int ret;
	unsigned long freq;
	struct dev_pm_opp *opp;

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	if (jlq_host->device_type == DEVICE_EMMC)
		cqhci_check_pending_tasks(host->mmc, __func__, __LINE__);

	if (host->mmc) {
		if (host->mmc->runtime_suspend_skip_state == STATE_PRE_SKIP)
			host->mmc->runtime_suspend_skip_state = STATE_SKIP;
		if (host->mmc->runtime_suspend_skip_state == STATE_SKIP)
			return 0;
		if (host->mmc->runtime_suspend_skip_state == STATE_PRE_DEFAULT)
			host->mmc->runtime_suspend_skip_state = STATE_DEFAULT;
	}
#endif
	clk_disable_unprepare(jlq_host->clktx);
	clk_disable_unprepare(jlq_host->bclk);

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
	ret = sdhci_crypto_runtime_level(host->mmc, FDE_RUNTIME_POWEROFF);
	if (ret)
		pr_warn("%s: fde suspend set clk error: %d\n",
				mmc_hostname(host->mmc), ret);
#endif

	if (jlq_host->opp_table) {
		freq = MIN_TX_CLK_FREQ;
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			pr_err("%s: dev_pm_opp_find_freq_ceil err %d\n",
				mmc_hostname(host->mmc), __func__, ret);
			return ret;
		}

		pr_debug("%s: %s: opp :freq %u:  %u %u %u %u\n",
			mmc_hostname(host->mmc),
			__func__,
			freq,
			opp->supplies[0].u_volt,
			opp->supplies[0].u_volt_min,
			opp->supplies[0].u_volt_max,
			opp->supplies[0].u_amp);

		ret = regulator_set_voltage(jlq_host->opp_table->regulators[0],
			opp->supplies[0].u_volt,
			opp->supplies[0].u_volt_max);
		if (ret)
			pr_debug("%s: suspend vcore regulator_set_voltage err %d\n",
				mmc_hostname(host->mmc), ret);

		dev_pm_opp_put(opp);
	}

	return 0;
}

static int sdhci_jlq_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_jlq_host *jlq_host = sdhci_pltfm_priv(pltfm_host);
	int ret;
	unsigned long freq;
	struct dev_pm_opp *opp;

#ifdef CONFIG_MMC_SDHCI_JLQ_DBG
	if (host->mmc &&
		(host->mmc->runtime_suspend_skip_state == STATE_SKIP ||
		host->mmc->runtime_suspend_skip_state == STATE_PRE_DEFAULT)) {
		return 0;
	}
#endif

	if (jlq_host->opp_table) {
		freq = clk_get_rate(jlq_host->clktx);
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			pr_err("%s: dev_pm_opp_find_freq_ceil err %d\n",
				mmc_hostname(host->mmc), __func__, ret);
			return ret;
		}

		pr_debug("%s: %s: opp :freq %u:  %u %u %u %u\n",
			mmc_hostname(host->mmc),
			__func__,
			freq,
			opp->supplies[0].u_volt,
			opp->supplies[0].u_volt_min,
			opp->supplies[0].u_volt_max,
			opp->supplies[0].u_amp);

		ret = regulator_set_voltage(jlq_host->opp_table->regulators[0],
			opp->supplies[0].u_volt,
			opp->supplies[0].u_volt_max);
		if (ret)
			pr_warn("%s: resume vcore regulator_set_voltage err %d\n",
				mmc_hostname(host->mmc), ret);

		dev_pm_opp_put(opp);
	}

#if IS_ENABLED(CONFIG_MMC_SDHCI_CRYPTO)
	ret = sdhci_crypto_runtime_level(host->mmc, FDE_RUNTIME_PERFORMANCE);
	if (ret)
		pr_warn("%s: fde resume set clk error: %d\n",
				mmc_hostname(host->mmc), ret);
#endif
	ret = clk_prepare_enable(jlq_host->bclk);
	if (ret)
		dev_err(dev, "clk_enable failed for base clock: %d\n", ret);

	ret = clk_prepare_enable(jlq_host->clktx);
	if (ret) {
		dev_err(dev, "clk_enable failed for card clock: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops sdhci_jlq_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_jlq_suspend,
				sdhci_jlq_resume)
	SET_RUNTIME_PM_OPS(sdhci_jlq_runtime_suspend,
			   sdhci_jlq_runtime_resume,
			   NULL)
};

static struct platform_driver sdhci_jlq_driver = {
	.probe = sdhci_jlq_probe,
	.remove = sdhci_jlq_remove,
	.driver = {
		   .name = "sdhci_jlq",
		   .of_match_table = sdhci_jlq_dt_match,
		   .pm = &sdhci_jlq_pm_ops,
	},
};

module_platform_driver(sdhci_jlq_driver);

MODULE_DESCRIPTION("JLQ Secure Digital Host Controller Interface driver");
MODULE_LICENSE("GPL v2");
