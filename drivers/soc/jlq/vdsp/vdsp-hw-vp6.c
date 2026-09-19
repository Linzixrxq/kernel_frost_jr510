// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include "vdsp-hw-vp6.h"
#include "vdsp-mem.h"
#include "vdsp-loader.h"
#include <linux/pm_opp.h>
#include "vdsp-driver.h"

#undef VDBG
#define VDBG(fmt, args...) pr_debug(fmt, ##args)

#define DRIVER_NAME "vdsp-hw-vp6"

#define MAILBOX_TO_VDSP             3

#undef BIT
#define BIT(nr)                     (1UL << (nr))

#define RESET_VEC_ADDR              0x80000000

#define VDSP_STATUS_2               0x08
#define FATAL_ERROR                 BIT(5)
#define FAULT_INFO_VALID            BIT(4)
#define WAIT_MODE                   BIT(3)
#define DOUBLE_EXCEPTION_ERROR      BIT(2)
#define VDSP_CTRL_0                 0x20
#define VDSP_CRTL_1                 0x24
#define VDSP_CRTL_3                 0x2C
#define STATVECTOR_SEL              BIT(17)
#define RUN_STALL                   BIT(18)
#define VDSP_CRTL_4                 0x30
#define PRESET_REQ                  BIT(2)
#define DRESET_REQ                  BIT(3)
#define BRESET_REQ                  BIT(4)

//#define BOOT_COMPLETE             0x01

#define PMU_VDSP_PD_CTL             0x220
#define PMU_VDSP_PD_CNT1            0x224
#define PMU_VDSP_PD_CNT2            0x228
#define PMU_VDSP_PD_CNT3            0x22C

#define PMU_AP_FLAG                 0x2F0

#define BUS_LP_EN_CTL1              0x408
#define TOP_VDSP_RST_CTL            0x20C
#define vdsp_sys_rst_req_n          (1 << 0)
#define vdsp_rst_bus_idle_mask      (1 << 1)
#define vdsp_rst_st                 (3 << 2)
#define vdsp_sysctrl_perset_req_n   (1 << 4)


#define PMU_PD_WEN(x)               BIT(16+x)

#define PMU_VDSP_PD_CTL_POWER_DOWN  BIT(0)
#define PMU_VDSP_PD_CTL_POWER_ON    BIT(1)
#define PMU_VDSP_PD_CTL_PD_MK       BIT(4)

#define PMU_AP_FLAG_VDSP_PU_FLAG    BIT(12)
#define PMU_AP_FLAG_VDSP_PD_FLAG    BIT(13)

#define SYSCNT_SNAP2_LO             0x440
#define SYSCNT_SNAP2_HI             0x444
#define SYSCNT_SNAP_CTRL            0x42C

#define VDSP_SRAM_FIRMWARE_SIZE      (1024 * 1024)
#define VDSP_DRAM_FIRMWARE_SIZE      (256 * 1024)

#define MAX_VOLTAGE  1000000//1.0v
#define RETRY_TIMES  1000000
#define FREQ_FLAG_LEAST_UPPER_BOUND		0x1
#define CORE_CLK_MAX   800000000
#define CORE_CLK_MIN   100000000

enum {
	SNAPSHOT_0 = 0,
	SNAPSHOT_1 = 1,
	SNAPSHOT_2 = 2,
};

static int dvfs_vote(void *hw_arg, unsigned long coreclk,
		unsigned long mips, unsigned long *target_clk, int *is_update);
static int dvfs_set(void *hw_arg, unsigned long clk);

static inline u32 vdsp_read32(u64 addr)
{
	return readl((void *)addr);
}

static inline void vdsp_write32(u64 addr, u32 v)
{
	writel(v, (void *)addr);
}

static inline void vdsp_write32_mask(u64 addr, u32 m, u32 v)
{
	u32 tmp = vdsp_read32(addr);

	tmp &= (~m);
	vdsp_write32(addr, v | tmp);
}

static void dereset(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;

	VDBG("enter.\n");
	if (!hw->sysctl_regs)
		return;

	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_4,
			PRESET_REQ, PRESET_REQ);
	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_4,
			DRESET_REQ, 0);
	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_4,
			BRESET_REQ, 0);
	VDBG("exit.\n");
}

static void reset(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;

	VDBG("enter.\n");
	if (!hw->sysctl_regs)
		return;

	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_4,
			PRESET_REQ, 0);
	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_4,
			DRESET_REQ, DRESET_REQ);
	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_4,
			BRESET_REQ, BRESET_REQ);
	VDBG("exit.\n");
}

static int is_active(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	int state;
	int retry = 10;
	int status_2;
	int flags = (FATAL_ERROR | FAULT_INFO_VALID | WAIT_MODE | DOUBLE_EXCEPTION_ERROR);

	VDBG("enter.\n");
	if (!hw->sysctl_regs)
		return -1;

	do {
		status_2 = vdsp_read32((u64)(hw->sysctl_regs) + VDSP_STATUS_2);
		VDBG("%s:%d vdsp status_2:0x%x", __func__, __LINE__, status_2);
		if ((status_2 & flags) != 0) {//1:idle or error; 0:normal
			state = 0;
			break;
		}
		mdelay(5);
		state = 1;
	} while ((retry--) > 0);

	VDBG("exit.\n");
	return state;
}

static void halt(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;

	VDBG("enter.\n");
	if (!hw->sysctl_regs)
		return;

	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_1,
			RUN_STALL, RUN_STALL);
	VDBG("exit.\n");
}

static void release(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;

	VDBG("enter.\n");
	if (!hw->sysctl_regs)
		return;

	vdsp_write32_mask((u64)(hw->sysctl_regs) + VDSP_CRTL_1,
			RUN_STALL, 0);

	VDBG("exit.\n");
}

static int poweron(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	int retry = 0;
	int rc;

	VDBG("enter.\n");
	if (hw->is_support_smmu) {
		if (!hw->tbu_regulator)
			return -EINVAL;

		rc = regulator_is_enabled(hw->tbu_regulator);
		if (rc > 0) {
			pr_info("smmu tbu regulator has beed enabled.\n");
		} else {
			rc = regulator_enable(hw->tbu_regulator);
			if (rc) {
				pr_err("smmu tbu regulator enable failure.\n");
				return -1;
			}
		}
	}

	regulator_enable(hw->regulator);
	vdsp_write32((u64)hw->lpm_regs + PMU_VDSP_PD_CNT1, 0xffff);
	vdsp_write32((u64)hw->lpm_regs + PMU_VDSP_PD_CNT2, 0xffff);
	vdsp_write32((u64)hw->lpm_regs + PMU_VDSP_PD_CNT3, 0xffff);

	vdsp_write32((u64)hw->lpm_regs + PMU_VDSP_PD_CTL,
		PMU_VDSP_PD_CTL_POWER_ON | PMU_PD_WEN(1));
	for (retry = 0; retry < RETRY_TIMES; retry++) {
		if (vdsp_read32((u64)hw->lpm_regs + PMU_AP_FLAG) & PMU_AP_FLAG_VDSP_PU_FLAG)
			break;
		udelay(2);
	}

	if (retry >= RETRY_TIMES) {
		pr_err("%s:%d timeout.\n", __func__, __LINE__);
		regulator_disable(hw->regulator);
		return -1;
	}
	VDBG("exit.\n");
	return 0;
}

static int poweroff(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	int retry = 0;
	u32 temp;
	int rc;

	if (0 == (vdsp_read32((u64)hw->top_crg_regs + TOP_VDSP_RST_CTL) & vdsp_rst_bus_idle_mask)) {
		temp = vdsp_read32((u64)hw->top_crg_regs + BUS_LP_EN_CTL1);
		vdsp_write32((u64)hw->top_crg_regs + BUS_LP_EN_CTL1, 1 << 22 | temp);
	}
	vdsp_write32((u64)hw->lpm_regs + PMU_VDSP_PD_CTL,
			PMU_VDSP_PD_CTL_POWER_DOWN | PMU_PD_WEN(0) | PMU_PD_WEN(4));
	for (retry = 0; retry < RETRY_TIMES; retry++) {
		if (vdsp_read32((u64)hw->lpm_regs + PMU_AP_FLAG) & PMU_AP_FLAG_VDSP_PD_FLAG)
			break;
		udelay(2);
	}
	if (retry >= RETRY_TIMES) {
		pr_err("%s:%d timeout.\n", __func__, __LINE__);
		return -1;
	}

	regulator_disable(hw->regulator);

	if (hw->is_support_smmu) {
		if (!hw->tbu_regulator)
			return -EINVAL;
		rc = regulator_is_enabled(hw->tbu_regulator);
		if (rc <= 0) {
			pr_err("smmu tbu regulator has beed disabled.\n");
		} else {
			rc = regulator_disable(hw->tbu_regulator);
			if (rc) {
				pr_err("smmu tbu regulator disable failure.\n");
				return -1;
			}
		}
	}

	return 0;

}

static int enable(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	int rc;

	VDBG("enter.\n");

	if (poweron(hw) < 0)
		return -1;

	rc = clk_prepare_enable(hw->coreclk);
	if (rc) {
		pr_err("coreclk failed.\n");
		return -1;
	}
	rc = clk_prepare_enable(hw->bus_clk);
	if (rc) {
		clk_disable_unprepare(hw->coreclk);
		pr_err("bus_clk failed.\n");
		return -1;
	}

	/* enable mailbox irq*/
	vdsp_write32((u64)hw->irqtrig_regs + 4, 1);
	vdsp_write32((u64)hw->irqtrig_regs + 8, 0x1f);
	VDBG("exit.\n");
	return 0;
}

static void disable(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	int rc;
	unsigned long target_clk;
	int is_update;

	VDBG("enter.\n");

	rc = dvfs_vote(hw, CORE_CLK_MIN, 0, &target_clk, &is_update);
	if (rc)
		pr_err("Error: failed setting (%s) dvfs get clk\n", rc);

	rc = dvfs_set(hw, target_clk);
	if (rc)
		pr_err("Error: failed setting (%s) dvfs set\n", rc);

	rc = icc_set_bw(hw->vdsp_ddr, 0, 0);
	if (rc)
		pr_err("Error: failed setting (%s) bw vote\n", rc);

	clk_disable_unprepare(hw->coreclk);
	clk_disable_unprepare(hw->bus_clk);

	/* disable mailbox irq*/
	vdsp_write32((u64)hw->irqtrig_regs + 4, 0);
	vdsp_write32((u64)hw->irqtrig_regs + 8, 0);

	if (poweroff(hw) < 0)
		pr_err("%s:%d disable failed!!.\n", __func__, __LINE__);

	VDBG("exit.\n");
}

static void boot_config(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;

	VDBG("enter.\n");
	if (!hw->sysctl_regs)
		return;

	vdsp_write32((u64)hw->sysctl_regs + VDSP_CRTL_3,
			0xFF);
	vdsp_write32((u64)hw->sysctl_regs + VDSP_CTRL_0,
			RESET_VEC_ADDR);
	vdsp_write32_mask((u64)hw->sysctl_regs + VDSP_CRTL_1,
			STATVECTOR_SEL, STATVECTOR_SEL);
	VDBG("exit.\n");
}

static int load_firmware(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	void __iomem *baseaddr = hw->firmware_addr;
	int rc = 0;

	VDBG("enter.\n");
	if (!hw->tcm_regs || !baseaddr)
		return -EINVAL;

	rc = vdsp_loader_wait_done();
	if (rc) {
		pr_err("vdsp image prepare timeout!\n");
		return -EINVAL;
	}

	memcpy(baseaddr, hw->firmware_backup_addr, VDSP_SRAM_FIRMWARE_SIZE);
	memcpy(hw->tcm_regs, hw->firmware_backup_addr + VDSP_SRAM_FIRMWARE_SIZE,
		VDSP_DRAM_FIRMWARE_SIZE);
	VDBG("exit.\n");

	return 0;
}

static void *get_hw_sync_data(void *hw_arg, size_t *sz)
{
	static const u32 irq_mode[] = {
		[VDSP_IRQ_NONE] = VDSP_SYNC_IRQ_MODE_NONE,
		[VDSP_IRQ_LEVEL] = VDSP_SYNC_IRQ_MODE_LEVEL,
		[VDSP_IRQ_EDGE] = VDSP_SYNC_IRQ_MODE_EDGE,
		[VDSP_IRQ_EDGE_SW] = VDSP_SYNC_IRQ_MODE_EDGE,
	};
	struct vdsp_hw *hw = hw_arg;
	struct vdsp_hw_sync_data *hw_sync_data =
		kzalloc(sizeof(*hw_sync_data), GFP_KERNEL);
	struct log_init_params *log_params = &hw->run_params;

	if (!hw_sync_data)
		return NULL;

	*hw_sync_data = (struct vdsp_hw_sync_data){
		.device_sysctl_base = hw->sysctl_phys,
		.device_tcm_base = hw->tcm_phys,
		.host_irq_mode = hw->host_irq_mode,
		.host_irq_id = hw->host_irq[0],
		.host_irq_type = hw->host_irq[1],
		.device_irq_mode = irq_mode[hw->device_irq_mode],
		.device_irq_id = hw->device_irq[0],
		.device_irq_type = hw->device_irq[1],
		.log_enable = log_params->log_enable,
		.log_level = log_params->log_level,
		.log_output_mode = log_params->log_output_mode,
		.log_output_modules = log_params->log_output_modules,
		.log_fifo_depth = log_params->log_fifo_depth,
		.log_fifo_width = log_params->log_fifo_width,
		.log_fifo_watermark = log_params->log_fifo_watermark,
	};
	*sz = sizeof(*hw_sync_data);
	return hw_sync_data;
}

static void send_irq(void *hw_arg, int irq_id)
{
	struct vdsp_hw *hw = hw_arg;

	vdsp_write32((u64)hw->irqtrig_regs, irq_id);
}

static unsigned long find_freq(void *hw_arg, unsigned long freq,
			u32 flags)
{
	int i;
	unsigned long atmost, atleast, f;
	struct vdsp_hw *hw = hw_arg;
	unsigned long opp_clk;

	atleast = hw->freq_table[0];
	atmost = hw->freq_table[hw->max_state-1];
	for (i = 0; i < hw->max_state; i++) {
		f = hw->freq_table[i];
		if (f <= freq)
			atmost = max(f, atmost);
		if (f >= freq)
			atleast = min(f, atleast);
	}
	if (flags & FREQ_FLAG_LEAST_UPPER_BOUND)
		opp_clk = atleast;
	else
		opp_clk = atmost;

	return opp_clk;
}

static int dvfs_vote(void *hw_arg, unsigned long coreclk,
			unsigned long mips, unsigned long *target_clk, int *is_update)
{
	struct vdsp_hw *hw = hw_arg;
	unsigned long cal_clk, opp_clk;
	unsigned long rfreq, old_freq;

	if ((coreclk == 0 && mips == 0) ||
			(coreclk > CORE_CLK_MAX) ||
			(mips > CORE_CLK_MAX))
		cal_clk = CORE_CLK_MAX;
	else if ((coreclk > 0) || (mips > 0))
		cal_clk = max(coreclk, mips);
	else
		cal_clk = CORE_CLK_MAX;

	if (cal_clk <= 0 || cal_clk > CORE_CLK_MAX)
		cal_clk = CORE_CLK_MAX;

	opp_clk = find_freq(hw, cal_clk, FREQ_FLAG_LEAST_UPPER_BOUND);

	rfreq = clk_round_rate(hw->coreclk, opp_clk);
	if ((long)rfreq <= 0) {
		pr_err("freq: Cannot find matching frequency for %lu.\n", cal_clk);
		return -EINVAL;
	}

	old_freq = clk_get_rate(hw->coreclk);

	if (rfreq == old_freq)
		*is_update = 0;
	else
		*is_update = 1;

	*target_clk = rfreq;

	VDBG("dvfs target_clk=%lu is_update:%d\n", *target_clk, *is_update);
	return 0;
}

static int dvfs_set(void *hw_arg, unsigned long clk)
{
	struct vdsp_hw *hw = hw_arg;
	struct vdsp_config_param *config;
	int rc = -EINVAL;

	rc = dev_pm_opp_set_rate(hw->vdsp->dev, clk);
	if (!rc) {
		config = (struct vdsp_config_param *)hw->param_addr;
		config->clk = clk;
	}

	return rc;
}

unsigned long get_coreclk_rate(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;

	return clk_get_rate(hw->coreclk);
}

static int bw_vote_update(void *hw_arg, unsigned long bw)
{
	struct vdsp_hw *hw = hw_arg;
	unsigned int icc_ab, icc_ib;
	int rc = 0;

	/*
	 * icc_ab/icc_ib: kilobytes per second.
	 * bw: bits per second
	 */
	if (bw) {
		icc_ab = bw / 1000;
		icc_ib = icc_ab * 2;
	} else {
		icc_ab = (CORE_CLK_MAX / 1000) * 8;
		icc_ib = icc_ab * 2;
	}

	rc = icc_set_bw(hw->vdsp_ddr, icc_ab, icc_ib);
	if (rc) {
		pr_err("Error: failed setting (%s) bus vote\n", rc);
		return rc;
	}

	VDBG("ddr bw icc_ab:%d icc_ib:%d\n", icc_ab, icc_ib);
	return rc;
}

static void update_rtc(void *hw_arg)
{
	struct vdsp_hw *hw = hw_arg;
	struct vdsp_config_param *config;
	struct timespec ts;
	uint64_t snapshot_lo = 0, snapshot_hi = 0;
	uint64_t snapshot;
	uint64_t sec, usec, rtc;
	int64_t sec_off, usec_off, temp;

	getnstimeofday(&ts);

	vdsp_write32((u64)hw->lpm_regs + SYSCNT_SNAP_CTRL, (1 << SNAPSHOT_2));
	snapshot_lo = vdsp_read32((u64)hw->lpm_regs + SYSCNT_SNAP2_LO);
	snapshot_hi = vdsp_read32((u64)hw->lpm_regs + SYSCNT_SNAP2_HI);
	snapshot = (snapshot_hi << 32) | snapshot_lo;

	rtc = (uint64_t)(snapshot * 10 / 192);
	sec = (uint64_t)(rtc / 1000000);
	usec = (uint64_t)(rtc % 1000000);

	temp = 1000000 + ts.tv_nsec / 1000 - usec;
	usec_off = temp % 1000000;
	sec_off = ts.tv_sec - 1 - sec + temp / 1000000;
	if (sec_off < 0) {
		pr_err("Invalid time offset:%ld.\n", sec_off);
		return;
	}

	config = (struct vdsp_config_param *)hw->param_addr;
	config->usec_offset = usec_off;
	config->sec_offset = sec_off;
}

static const struct vdsp_hw_ops hw_ops = {
	.enable = enable,
	.disable = disable,
	.is_active = is_active,
	.halt = halt,
	.release = release,
	.reset = reset,
	.dereset = dereset,
	.boot_cfg = boot_config,
	.load_firmware = load_firmware,
	.get_hw_sync_data = get_hw_sync_data,
	.send_irq = send_irq,
	.dvfs_vote = dvfs_vote,
	.dvfs_set = dvfs_set,
	.get_coreclk_rate = get_coreclk_rate,
	.bw_vote_update = bw_vote_update,
	.update_rtc = update_rtc,
};

static int vdsp_parse_freq_table(struct platform_device *pdev, struct vdsp_hw *hw)
{
	struct device *dev = NULL;
	unsigned long freq;
	struct dev_pm_opp *opp;
	struct device_node *np;
	int len, i;
	const char *name = "core-supply";
	int ret = -EINVAL;

	if (!pdev || !hw) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	dev = &pdev->dev;

	np = of_node_get(dev->of_node);
	if (of_find_property(np, name, NULL)) {
		name = "core";
		hw->opp_table = dev_pm_opp_set_regulators(dev, &name, 1);
		if (IS_ERR(hw->opp_table)) {
			ret = PTR_ERR(hw->opp_table);
			dev_err(dev, "Failed to set regulator: %d\n", ret);
			return ret;
		}
	}

	if (dev_pm_opp_of_add_table(dev)) {
		dev_err(dev, "failed to init OPP table: %d\n", ret);
		return ret;
	}

	len = dev_pm_opp_get_opp_count(dev);
	if (len <= 0)
		return -EPROBE_DEFER;

	hw->freq_table = devm_kzalloc(dev, len * sizeof(*hw->freq_table),
			     GFP_KERNEL);
	if (!hw->freq_table)
		return -ENOMEM;

	for (i = 0, freq = ULONG_MAX; i < len; i++, freq--) {
		opp = dev_pm_opp_find_freq_floor(dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto free_tables;
		}
		hw->freq_table[i] = freq;
		dev_pm_opp_put(opp);
	}

	hw->max_state = i;
	if (hw->max_state == 0)
		return -EINVAL;
	return 0;

free_tables:
	return ret;
}


int vdsp_mem_dt_parse_and_init(struct platform_device *pdev,
		struct vdsp_hw *hw, struct vdsp_mem_region *mem_region)
{
	struct resource *res;
	struct device_node *np;
	struct device_node *nc;
	struct device_node *lpm_np;
	struct device_node *top_crg_np;
	u64 offset;
	u64 addr_end = 0;
	int i = 0;
	char *key;
	const __be32 *basep;
	int rc = 0;

	if (!pdev || !hw || !mem_region) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	np = pdev->dev.of_node;
	nc = of_parse_phandle(np, "vdsp-priv-mem", 0);
	if (nc) {
		basep = of_get_address(nc,  0,
			&hw->mem_size, NULL);
		if (!basep) {
			pr_err("get reserved memmory failed.\n");
			return -ENODEV;
		}

		hw->mem_phys =
			of_translate_address(nc, basep);
		if (hw->mem_phys == OF_BAD_ADDR) {
			pr_err("get reserved memory physic address failed.\n");
			hw->mem_phys = 0;
			hw->mem_size = 0;
		} else {
			hw->mem_baseaddr = ioremap_nocache(
					hw->mem_phys, hw->mem_size);
		}
		of_node_put(nc);
	} else {
		pr_err("No reserved vdsp private memory.\n");
		return -ENODEV;
	}

	key = "vdsp_tcm_mem";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!res)
		goto missing_key;
	hw->tcm_phys = res->start;
	hw->tcm_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw->tcm_regs)) {
		rc = PTR_ERR(hw->tcm_regs);
		pr_err("map vdsp_tcm_mem failed .\n");
		return -ENODEV;
	}

	key = "vdsp_sysctl_mem";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!res)
		goto missing_key;
	hw->sysctl_phys = res->start;
	hw->sysctl_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw->sysctl_regs)) {
		rc = PTR_ERR(hw->sysctl_regs);
		pr_err("map vdsp_sysctl_mem failed .\n");
		return -ENODEV;
	}

	//top_crg base
	top_crg_np = of_find_compatible_node(NULL, NULL, "jlq,crg-base");
	if (!top_crg_np) {
		pr_err("jlq,crg-base No compatible node found\n");
		return -ENODEV;
	}
	hw->top_crg_regs = of_iomap(top_crg_np, 0);
	if (IS_ERR(hw->top_crg_regs)) {
		pr_err("top_crg reg ioremap failed\n");
		return -ENOMEM;
	}
	of_node_put(top_crg_np);

	key = "irqtrig_addr";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!res)
		goto missing_key;
	hw->irqtrig_phys = res->start;
	hw->irqtrig_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw->irqtrig_regs)) {
		rc = PTR_ERR(hw->irqtrig_regs);
		pr_err("map mailbox irq regs failed .\n");
		return -ENODEV;
	}

	/*  ioremap lpm-base */
	lpm_np = of_find_compatible_node(NULL, NULL, "jlq,lpm-base");
	if (!lpm_np) {
		pr_err("No lpm registers defined.\n");
		return -EINVAL;
	}

	hw->lpm_regs = of_iomap(lpm_np, 0);
	if (IS_ERR(hw->lpm_regs)) {
		rc = PTR_ERR(hw->lpm_regs);
		pr_err("map top_lpm_mem failed .\n");
		return -ENODEV;
	}

	key = "vdsp-start-addr";
	rc = of_property_read_u32(np, key,
				(u32 *)&mem_region->vdsp_start_addr);
	if (rc)
		goto missing_key;

	for (i = 0; i < VDSP_MEM_TYPE_MAX; i++) {
		if (i == VDSP_CODE_MEM)
			key = "code-region";
		else if (i == VDSP_CODE_BACKUP_MEM)
			key = "code-backup-region";
		else if (i == VDSP_RESERVED_MEM)
			key = "reserved-region";
		else if (i == VDSP_PARAM_MEM)
			key = "param-region";
		else if (i == VDSP_SYSDUMP_MEM)
			key = "sysdump-region";
		else if (i == VDSP_LOG_SYS_MEM)
			key = "log-sys-region";
		else if (i == VDSP_SMD_SYS_MEM)
			key = "smd-sys-region";
		else if (i == VDSP_DYNAMIC_ALLOC_MEM)
			key = "dynamic-alloc-region";
		else {
			pr_err("Unsupport memory type:%d.\n", i);
			return -ENODEV;
		}
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
		if (!res)
			goto missing_key;

		offset = res->start;
		if (i != VDSP_DYNAMIC_ALLOC_MEM) {
			mem_region->vdsp_mem_info[i].baseaddr = hw->mem_baseaddr + offset;
			mem_region->vdsp_mem_info[i].phys_addr = hw->mem_phys + offset;
		} else {
			mem_region->vdsp_mem_info[i].baseaddr = NULL;
			mem_region->vdsp_mem_info[i].phys_addr = 0;
		}
		mem_region->vdsp_mem_info[i].offset = offset;
		mem_region->vdsp_mem_info[i].size = resource_size(res);

		if (addr_end <= offset) {
			addr_end = offset + resource_size(res);
		} else {
			pr_err("memory region overlap(type:%d)\n", i);
			return -ENODEV;
		}
		VDBG("key:%s mem_info[%d] baseaddr:%p phys:0x%x, offset:0x%x, size:0x%x",
			key, i, mem_region->vdsp_mem_info[i].baseaddr,
			(unsigned int)mem_region->vdsp_mem_info[i].phys_addr,
			(unsigned int)mem_region->vdsp_mem_info[i].offset,
			(unsigned int)mem_region->vdsp_mem_info[i].size);
	}
	hw->firmware_backup_addr = mem_region->vdsp_mem_info[VDSP_CODE_BACKUP_MEM].baseaddr;
	hw->firmware_addr = mem_region->vdsp_mem_info[VDSP_CODE_MEM].baseaddr;
	hw->param_addr = mem_region->vdsp_mem_info[VDSP_PARAM_MEM].baseaddr;

	return 0;

missing_key:
	pr_err("missing key: %s.\n", key);
	return -ENODEV;
}

int vdsp_clk_dt_parse_and_init(struct platform_device *pdev, struct vdsp_hw *hw)
{
	struct device *dev = NULL;
	struct device_node *np = NULL;
	int rc = 0;

	if (!pdev || !hw) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	dev = &pdev->dev;
	np = dev->of_node;
	hw->coreclk = devm_clk_get(dev, "coreclk");
	if (IS_ERR(hw->coreclk)) {
		rc = PTR_ERR(hw->coreclk);
		pr_err("get coreclk fail.\n");
		goto out;
	}

	hw->bus_clk = devm_clk_get(dev, "bus-clk");
	if (IS_ERR(hw->bus_clk)) {
		rc = PTR_ERR(hw->bus_clk);
		pr_err("get ddr bus clk fail.\n");
		goto bus_clk_fail;
	}

	hw->coresight_clk = devm_clk_get(dev, "debug-clk");
	if (IS_ERR(hw->coresight_clk)) {
		rc = PTR_ERR(hw->coresight_clk);
		pr_err("get debug clk fail.\n");
		goto coresight_clk_fail;
	}

	rc = of_property_read_u32(np, "is-debug-enable", &hw->is_debug_enable);
	if (rc) {
		pr_err("Couldn't read is-debug-enable!!\n");
		goto get_debug_fail;
	}

	hw->regulator = devm_regulator_get(dev, "core");
	if (IS_ERR(hw->regulator)) {
		pr_err("vdsp regulator not specified\n");
		rc = PTR_ERR(hw->regulator);
		goto err_regulator;
	}

	return rc;

err_regulator:
get_debug_fail:
	devm_clk_put(dev, hw->coresight_clk);
coresight_clk_fail:
	devm_clk_put(dev, hw->bus_clk);
bus_clk_fail:
	devm_clk_put(dev, hw->coreclk);
out:
	return -EINVAL;
}

static long init_hw(struct platform_device *pdev, struct vdsp_hw *hw,
	struct vdsp_extern_module *module, struct vdsp_mem_region *mem_region)
{
	int rc = 0;
	char *key;
	struct device_node *np = pdev->dev.of_node;
	u32 device_irq_mode;
	u32 host_irq_mode;

	if (!hw || !module || !mem_region) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	rc = vdsp_mem_dt_parse_and_init(pdev, hw, mem_region);
	if (rc) {
		pr_err("parse vdsp mem fail!!\n");
		return -ENXIO;
	}

	rc = vdsp_clk_dt_parse_and_init(pdev, hw);
	if (rc) {
		pr_err("parse vdsp clk fail!!\n");
		return -ENXIO;
	}

	rc = vdsp_parse_freq_table(pdev, hw);
	if (rc) {
		pr_err("parse vdsp freq table fail!!\n");
		return -ENXIO;
	}


	module->tcm_baseaddr = hw->tcm_regs;
	/* smmu mem remap*/
	key = "is-support-smmu";
	rc = of_property_read_u32(np, key, &module->is_support_smmu);
	if (rc)
		goto missing_key;
	if (module->is_support_smmu) {
		hw->tbu_regulator = regulator_get(&pdev->dev, "tbu");
		if (IS_ERR(hw->tbu_regulator)) {
			pr_err("get smmu tbu regulator fail!\n");
			return -EINVAL;
		}
		hw->is_support_smmu = module->is_support_smmu;
	}

	key = "smd-irq-id";
	module->smd_irq_id = of_irq_get_byname(np, key);
	if (module->smd_irq_id < 0)
		goto missing_key;

	key = "panic-irq-id";
	module->panic_irq_id = of_irq_get_byname(np, key);
	if (module->panic_irq_id < 0)
		goto missing_key;

	key = "wdog-irq-id";
	module->wdog_bite_irq_id = of_irq_get_byname(np, key);
	if (module->wdog_bite_irq_id < 0)
		goto missing_key;

	key = "rtc-irq-id";
	module->rtc_irq_id = of_irq_get_byname(np, key);
	if (module->rtc_irq_id < 0)
		goto missing_key;

	key = "force_ramdump_tx_irq";
	rc = of_property_read_u32(np,
					key,
					&module->force_ramdump_irq_id);
	if (rc)
		goto missing_key;

	key = "device-irq";
	rc = of_property_read_u32_array(np,
					key,
					hw->device_irq,
					ARRAY_SIZE(hw->device_irq));
	if (rc)
		goto missing_key;

	key = "device-irq-mode";
	rc = of_property_read_u32(np,
					key,
					&device_irq_mode);
	if (rc)
		goto missing_key;
	if (device_irq_mode >= VDSP_IRQ_MAX) {
		rc = -EINVAL;
		pr_err("Invalid device-irq-mode.\n");
		return rc;
	}
	hw->device_irq_mode = device_irq_mode;

	key = "host-irq";
	rc = of_property_read_u32_array(np, key,
					hw->host_irq,
					ARRAY_SIZE(hw->host_irq));
	if (rc)
		goto missing_key;

	key = "host-irq-mode";
	rc = of_property_read_u32(np,
				key,
				&host_irq_mode);
	if (rc)
		goto missing_key;
	if (device_irq_mode >= VDSP_IRQ_MAX) {
		rc = -EINVAL;
		pr_err("Invalid host_irq_mode.\n");
		return rc;
	}
	hw->host_irq_mode = host_irq_mode;

	key = "log_init_setting";
	rc = of_property_read_u32_array(np,
					key,
					hw->log,
					ARRAY_SIZE(hw->log));
	if (rc)
		goto missing_key;

	memcpy(&(hw->run_params), hw->log, sizeof(hw->run_params));
	module->log_params = &hw->run_params;

	key = "vdsp-ddr";
	hw->vdsp_ddr = of_icc_get(&pdev->dev, key);
	if (IS_ERR(hw->vdsp_ddr)) {
		pr_err("Error: (%d) failed getting %s path\n",
			PTR_ERR(hw->vdsp_ddr), key);
		return PTR_ERR(hw->vdsp_ddr);
	}

	VDBG("host:irq:%d, type:%d mode:%d; device:irq = %d, type:%d, mode:%d log_enable:%d.\n",
			hw->host_irq[0], hw->host_irq[1], hw->host_irq_mode,
			hw->device_irq[0], hw->device_irq[1], hw->device_irq_mode,
			hw->log[0]);

	return 0;

missing_key:
	pr_err("missing key: %s.\n", key);
	return -ENODEV;
}

static long init_cma(struct platform_device *pdev, struct vdsp_hw *hw)
{
	long ret;
	struct vdsp_extern_module module;
	struct vdsp_mem_region mem_region;

	memset(&module, 0x0, sizeof(struct vdsp_extern_module));
	memset(&mem_region, 0x0, sizeof(struct vdsp_mem_region));
	ret = init_hw(pdev, hw, &module, &mem_region);
	if (ret < 0)
		return ret;

	return (long)vdsp_init(pdev, &hw_ops, hw, &module, &mem_region);
}

static const struct of_device_id vdsp_hw_match[] = {
	{
		.compatible = "jlq,vdsp-hw-vp6,cma",
		.data = init_cma,
	},
	{},
};
MODULE_DEVICE_TABLE(of, vdsp_hw_match);

static int vdsp_hw_probe(struct platform_device *pdev)
{
	struct vdsp_hw *hw =
		devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	const struct of_device_id *match;
	long (*init)(struct platform_device *pdev, struct vdsp_hw *hw);
	long ret;

	if (!hw)
		return -ENOMEM;

	match = of_match_device(of_match_ptr(vdsp_hw_match),
				&pdev->dev);
	if (!match)
		return -ENODEV;

	init = match->data;
	ret = init(pdev, hw);
	if (IS_ERR_VALUE(ret)) {
		pr_err("vdsp hw probe failed.\n");
		return ret;
	}

	ret = enable(hw);
	if (IS_ERR_VALUE(ret)) {
		pr_err("vdsp hw enable failed.\n");
		return ret;
	}

	//vdsp_loader_do();
	disable(hw);

	pr_info("vdsp hw probe success.\n");
	return 0;
}

static int vdsp_hw_remove(struct platform_device *pdev)
{
	struct vdsp *drvdata = platform_get_drvdata(pdev);
	struct vdsp_hw *hw = drvdata->hw_arg;

	pr_info("in\n");
	vdsp_deinit(pdev);
	devm_regulator_put(hw->regulator);
	dev_pm_opp_of_remove_table(&pdev->dev);
	if (hw->opp_table) {
		dev_pm_opp_put_regulators(hw->opp_table);
		hw->opp_table = NULL;
	}

	if ((hw->is_support_smmu) && (hw->tbu_regulator))
		regulator_put(hw->tbu_regulator);

	devm_kfree(&pdev->dev, hw->freq_table);
	devm_kfree(&pdev->dev, hw);
	pr_info("out\n");
	return 0;
}

static const struct dev_pm_ops vdsp_hw_pm_ops = {
	SET_RUNTIME_PM_OPS(vdsp_runtime_suspend,
			vdsp_runtime_resume, NULL)
};

static struct platform_driver vdsp_hw_driver = {
	.probe   = vdsp_hw_probe,
	.remove  = vdsp_hw_remove,
	.driver  = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(vdsp_hw_match),
		.pm = &vdsp_hw_pm_ops,
	},
};

module_platform_driver(vdsp_hw_driver);

MODULE_DESCRIPTION("JLQ VDSP driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: pm-ctrl-opp");
