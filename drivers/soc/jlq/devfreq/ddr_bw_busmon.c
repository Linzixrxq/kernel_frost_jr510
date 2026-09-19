/*
 * Copyright 2018~2021 JLQ Technology Co., Ltd. or its affiliates.
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
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <vdso/bits.h>
#include <linux/clk.h>
#include "ddr_bw_busmon.h"

#define DRIVER_NAME "jlq-bus-monitor"

static struct busmon_data *gBmdata;
static struct workqueue_struct *bm_BWwq;

/*
 *  cmd - master - table
 */
const static char *sysfsCmd[] = {
	"bmstart",
	"masteradd:",
	"masterdel:",
	"periodcfg:",
	"reset",
	"buslp:"
};

const static struct bmMasterCfg master_config_lists[] = {
	/*name      usrID  portNum  base    reg                    reg config  */
	{"A55",       15, PORT0_ID, 0, AP_GPU_USER_CFG,            20, 16,  4, 0},
	{"vDSP_DMA",  3,  PORT1_ID, 0, VDSPCORE_IDMA_USER_CFG,     28, 24, 12, 8},
	{"vDSP_CORE", 0,  PORT1_ID, 0, VDSPCORE_IDMA_USER_CFG,     20, 16,  4, 0},
	{"aDSP",      12, PORT1_ID, 3, ADSP_QOS_USER,              0xff, 0xff, 0, 0},
	{"TOP_DMAS",  2,  PORT1_ID, 0, TOPAP_DMAS_USER_CFG,        20, 16,  4, 0},
	{"UFS",       6,  PORT1_ID, 1, HBLK_BUS_USER1,             20, 16,  4, 0},
	{"EMMC",      1,  PORT1_ID, 1, HBLK_BUS_USER,              0xff, 0xff, 24, 20},
	{"SDIO0",     9,  PORT1_ID, 1, HBLK_BUS_USER,              20, 16,  4, 0},
	{"SDIO1",     10, PORT1_ID, 1, HBLK_BUS_USER,              20, 16,  4, 0},
	{"CIPHER",    7,  PORT1_ID, 1, HBLK_BUS_USER,              20, 16,  4, 0},
	{"AP_DMAS",   4,  PORT1_ID, 0, TOPAP_DMAS_USER_CFG,        28, 24, 12, 8},
	{"USB",       11, PORT1_ID, 0, USB_CIPHER_USER_CFG,        28, 24, 12, 8},
	{"QT2modem1", 5,  PORT1_ID, 0, QTMODEM0_QTMODEM1_USER_CFG, 28, 24, 12, 8},
	{"DPU",       8,  PORT2_ID, 0, DISP_VPU_USER_CFG,          20, 16,  4, 0},
	{"ISPRT",     9,  PORT2_ID, 0, QTCAMRT_QTCAMNRT_USER_CFG,  20, 16,  4, 0},
	{"QT2modem0", 10, PORT3_ID, 0, QTMODEM0_QTMODEM1_USER_CFG, 20, 16,  4, 0},
	{"GPU",       11, PORT4_ID, 0, AP_GPU_USER_CFG,            28, 24, 12, 8},
	{"AI",        12, PORT4_ID, 0, AI_QTANG_USER_CFG,          20, 16,  4, 0},
	{"VPU",       13, PORT4_ID, 0, DISP_VPU_USER_CFG,          28, 24, 12, 8},
	{"ISPNRT",    14, PORT4_ID, 0, QTCAMRT_QTCAMNRT_USER_CFG,  28, 24, 12, 8},
	//{"CM4",       3,  PORT1_ID, 2, 1,                          20, 16,  4, 0},
	//{"SMMU",      12, PORT1_ID, 4, 1,                          20, 16,  4, 0},
	{NULL, 0, 0, 0, 0, 0, 0, 0}
};

const static u32 group_port_match_tabele[] =
/* group id:     0  1  2  3  4  5  6  7  8  9 10 11 12 13 13 15
 * port  id: */{ 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 4, 4, 4, 4, 0};

/*
 *        bus monitor hardware driver
 */
#define __bus_monitor_hardware_driver__
static void busmon_save_qtUserSel(struct busmon_data *info)
{
	info->qtUserSel = busmon_readl(info->top_sysctrl + 0x320);
}

static void busmon_set_qtUserSel(struct busmon_data *info, u32 val)
{
	busmon_writel(info->top_sysctrl + 0x320, val);
}

static void busmon_buslp_save(struct busmon_data *info)
{
	info->buslpEn[0] = busmon_readl(info->top_crg + BUS_LP_EN_CTL0);
	info->buslpEn[1] = busmon_readl(info->top_crg + BUS_LP_EN_CTL1);
	info->buslpEn[2] = busmon_readl(info->top_crg + BUS_LP_EN_CTL2);
	info->buslpEn[3] = busmon_readl(info->top_crg + BUS_LP_EN_CTL3);
	info->buslpEn[4] = busmon_readl(info->top_crg + DDR_TOP_LP_EN_CTL0);
	info->buslpEn[5] = busmon_readl(info->top_crg + CPU_SYS_LP_EN_CTL0);
}

static void busmon_buslp_enable(struct busmon_data *info, int enable)
{
	if (!enable) {
		if(info->buslpState)
			busmon_buslp_save(info);
		busmon_writel(info->top_crg + BUS_LP_EN_CTL0, 0x0);
		busmon_writel(info->top_crg + BUS_LP_EN_CTL1, 0x0);
		busmon_writel(info->top_crg + BUS_LP_EN_CTL2, 0x0);
		busmon_writel(info->top_crg + BUS_LP_EN_CTL3, 0x0);
		busmon_writel(info->top_crg + DDR_TOP_LP_EN_CTL0, 0x0);
		busmon_writel(info->top_crg + CPU_SYS_LP_EN_CTL0, 0x0);
		info->buslpState = 0;
	} else {
		busmon_writel(info->top_crg + BUS_LP_EN_CTL0, info->buslpEn[0]);
		busmon_writel(info->top_crg + BUS_LP_EN_CTL1, info->buslpEn[1]);
		busmon_writel(info->top_crg + BUS_LP_EN_CTL2, info->buslpEn[2]);
		busmon_writel(info->top_crg + BUS_LP_EN_CTL3, info->buslpEn[3]);
		busmon_writel(info->top_crg + DDR_TOP_LP_EN_CTL0, info->buslpEn[4]);
		busmon_writel(info->top_crg + CPU_SYS_LP_EN_CTL0, info->buslpEn[5]);
		info->buslpState = 1;
	}
	info->buslpEnabled = !!enable;
}

static inline int busmon_group_byteScal(struct busmon_data *info, int group, u64 *bytescal)
{
	u64 bytescal_rd_lo, bytescal_rd_hi;
	u64 bytescal_wr_lo, bytescal_wr_hi;

	bytescal_rd_lo = busmon_readl(info->bus_monitor + BYTESCAL_R_LO(group));
	bytescal_rd_hi = busmon_readl(info->bus_monitor + BYTESCAL_R_HI(group));
	bytescal_wr_lo = busmon_readl(info->bus_monitor + BYTESCAL_W_LO(group));
	bytescal_wr_hi = busmon_readl(info->bus_monitor + BYTESCAL_W_HI(group));

	bytescal[0] = (u64)(bytescal_rd_hi << 32 | bytescal_rd_lo); //rd
	bytescal[1] = (u64)(bytescal_wr_hi << 32 | bytescal_wr_lo); //wr
	return 0;
}

static inline int busmon_group_byteScalWin(struct busmon_data *info, int group, u64 *winbytescal)
{
	winbytescal[0] = (u64)busmon_readl(info->bus_monitor + WIN_BYTESCAL_R_LO_MAX(group));
	winbytescal[1] = (u64)busmon_readl(info->bus_monitor + WIN_BYTESCAL_W_LO_MAX(group));
	return 0;
}

static inline int busmon_group_timeScalSelSet(struct busmon_data *info, int groupID)
{
	u32 val;

	val = (groupID << 8) + groupID;
	info->timeGroupId = groupID;
	info->timescale[0] = 0;
	info->timescale[1] = 0;
	busmon_writel(info->bus_monitor + BW_TIMESCAL_SEL, val);
	return 0;
}

static inline int busmon_group_timeScal(struct busmon_data *info, int group, u64 *timescal)
{
	timescal[0] = busmon_readl(info->bus_monitor + BW_TIMESCAL_R_LO);
	timescal[1] = busmon_readl(info->bus_monitor + BW_TIMESCAL_W_LO);
	return 0;
}

static inline int busmon_group_resultClear(struct busmon_data *info, int group)
{
	busmon_writel(info->bus_monitor + USER_BW_RESULT(group), 1);
	return 0;
}

static inline int busmon_group_periodSet(struct busmon_data *info, struct bmPeriodCfg *period)
{
	/*parameter check*/
	/*12bit long*/
	if (period->short_period >> 12) {
		dev_err(info->dev, "<%s>invalid short period %d, set to %d\n",
			__func__, period->short_period, DEFAULT_PERIOD_SHORT);
		period->short_period = DEFAULT_PERIOD_SHORT;
	}
	/*20 bit long*/
	if (period->long_period >> 20) {
		dev_err(info->dev, "<%s>invalid long period %d, set to %d\n",
			__func__, period->long_period, DEFAULT_PERIOD_LONG);
		period->long_period = DEFAULT_PERIOD_LONG;
	}
	if (period->loopCnt < 1) {
		dev_err(info->dev, "invalid repeat times, set to one time");
		period->loopCnt = 1;
	}
	info->period.short_period = period->short_period;
	info->period.long_period  = period->long_period;
	info->period.loopCnt      = period->loopCnt;
	info->period.waitCnt      = period->waitCnt;
	busmon_writel(info->bus_monitor + USER_BW_SHORT_TIM, period->short_period);
	busmon_writel(info->bus_monitor + USER_BW_LONG_TIM,  period->long_period);

	return 0;
}

static inline int busmon_group_modeSet(struct busmon_data *info)
{
	busmon_writel(info->bus_monitor + USER_BW_RPT, BUSMON_BW_MODE);
	return 0;
}

static inline int busmon_group_userIdSet(struct busmon_data *info, uint8_t group)
{
	u32 userID = info->group[group].usrID;

	busmon_writel(info->bus_monitor + BW_USER_CFG(group), userID);
	return 0;
}

static int busmon_group_disableCh(struct busmon_data *info, uint8_t group)
{
	busmon_writel(info->bus_monitor + USER_BW_RLT(group), 0);
	busmon_writel(info->bus_monitor + BW_FINISH_FLG(group), BUSMON_GROUP_BW_FLAG);
	busmon_writel(info->bus_monitor + BW_FINISH_INT_CLR(group), 1);
	busmon_writel(info->bus_monitor + BW_FINISH_INT_EN(group), 0);
	busmon_group_resultClear(info, group);

	return 0;
}

static int busmon_group_enableCh(struct busmon_data *info, uint8_t group)
{
	busmon_group_resultClear(info, group);
	busmon_writel(info->bus_monitor + USER_BW_RLT(group), BUSMON_GROUP_BW_ENABLE);
	busmon_writel(info->bus_monitor + BW_FINISH_FLG(group), BUSMON_GROUP_BW_FLAG);
	busmon_writel(info->bus_monitor + BW_FINISH_INT_CLR(group), 1);
	//busmon_writel(info->bus_monitor + BW_FINISH_INT_EN(group), 1);

	return 0;
}

static inline int busmon_group_enableGrobal(struct busmon_data *info)
{
	busmon_writel(info->bus_monitor + USER_BW_PERIOD, BUSMON_GROUP_BW_ENABLE);
	return 0;
}

static inline int busmon_group_disableGrobal(struct busmon_data *info)
{
	busmon_writel(info->bus_monitor + USER_BW_PERIOD, 0);
	return 0;
}

static int busmon_group_disableAll(struct busmon_data *info)
{
	int group;

	/*Disable group use bw monitor*/
	for (group = 0; group < (info->groupNum); group++)
		busmon_group_disableCh(info, group);
	/*Disable group use bw timer */
	busmon_group_disableGrobal(info);
	if (!SCP_BM_COMPATIBLE) {
	/*Disable group all bw monitor */
		busmon_writel(info->bus_monitor + ALL_BW_RLT, 0);
	/*Disable group use lat monitor and timer */
		busmon_writel(info->bus_monitor + LAT_PERIOD, 0);
	}
	/*Set the init mask value */
	info->groupMark = 0;
	info->byteOverFlow = 0;
	return 0;
}

static int master_find_base(struct busmon_data *info,
	struct bmMasterCfg const *master, void **base)
{
	switch (master->base) {
	case 0:
		*base = info->top_sysctrl; break;
	case 1:
		*base = info->hblk_sysctrl; break;
	case 2:
		*base = info->cm4_sysctrl; break;
	case 3:
		*base = info->audio_sysctrl; break;
	case 4:
		*base = info->smmu_sysctrl; break;
	default:
		dev_info(info->dev, "Not found match port_id, Please check\n");
	}
	return 0;
}
static int busmon_master_usridCfg(struct busmon_data *info,
								struct bmMasterCfg const *master)
{
	uint32_t val;
	uint32_t usrid = master->usrID;
	void __iomem *base;

	master_find_base(info, master, &base);
	val = busmon_readl(base + master->reg);
	if (master->wen_ar != 0xff)
		val |= (1 << master->wen_ar);
	if (master->wen_aw != 0xff)
		val |= (1 << master->wen_aw);
	if (master->offset_ar != 0xff) {
		val &= ~(0xf << master->offset_ar);
		val |= (usrid << master->offset_ar);
	}
	if (master->offset_aw != 0xff) {
		val &= ~(0xf << master->offset_aw);
		val |= (usrid << master->offset_aw);
	}
	busmon_writel(base + master->reg, val);

	return 0;
}

static int busmon_master_groupCfg(struct busmon_data *data,
								struct bmMasterCfg const *master,
								u32 enable)
{
	u32 num, groupPtr, fdGroup = 0;
	u32 usrid = master->usrID;
	u32 portNum = master->portNum;
	struct bmGroupInfo *group;

	/*Search match in current config table*/
	for (num = 0; num < data->groupNum; num++) {
		group = &data->group[num];
		if ((group->masterName != NULL) &&
			(!strncmp(master->name, group->masterName, 6))) {
			fdGroup = 1;
			break;
		}
	}

	/*If find and del cmd, just disable it*/
	if ((fdGroup) && (!enable)) {
		group->groupStat = 0;
		data->groupMark &= (~(1 << group->groupNum));
		return 0;
	}

	/*If not find before, Find match group*/
	if (!fdGroup) {
		if (!enable)
			return 0;
		if (portNum != PORT1_ID) {
			group = &data->group[master->usrID];
		} else {
			for (num = 0; num < 8; num++) {
				group = &data->group[num];
				if ((group->groupStat == 0) || (group->masterName == NULL)) {
					fdGroup = 1;
					break;
				}
			}
			if (!fdGroup) {
				groupPtr = data->portGroupPtr;
				groupPtr++;
				group = &data->group[groupPtr];
				data->portGroupPtr = groupPtr;
			}
		}
	}

	/*Enable Match Group*/
	group->groupStat = 0x02;
	group->masterName = master->name;
	group->usrID = usrid;
	data->groupMark |= (1 << group->groupNum);
	busmon_group_userIdSet(data, group->groupNum);
	return 0;
}

static inline int busmon_group_ovfCheck(struct busmon_data *data)
{
	u32 num = 0, value = 0;

	for (num = 0; num < data->groupNum; num++) {
		if (((data->groupMark >> num) & 0x01) || (data->group[num].groupStat != 0)) {
			value += busmon_readl(data->bus_monitor + ARBYTESCNT_OVF(num));
			value += busmon_readl(data->bus_monitor + AWBYTESCNT_OVF(num));
			value += busmon_readl(data->bus_monitor + LONG_ARBYTE_OVF(num));
			value += busmon_readl(data->bus_monitor + LONG_AWBYTE_OVF(num));
			if (value) {
				value = data->byteOverFlow;
				value += 1 << num;
				data->byteOverFlow = value;
			}
		}
	}
	return 0;
}

static void busmon_userBw_dataBuf(struct busmon_data *data, char **buff)
{
	u32 i = 0;
	int clkHz, clkMhz;
	u64 periodShort, periodLong, loopCnt, periodTimeUs;
	u64 bwKbps[2];
	struct bmGroupInfo *group;

	clkHz = data->bmclk_rate;
	clkMhz = clkHz/1000000;
	periodShort = data->period.short_period;
	periodLong = data->period.long_period;
	loopCnt = data->period.loopCnt;
	periodTimeUs = (periodShort + 1) * (periodLong + 1) / clkMhz * loopCnt;

	(*buff) += sprintf((*buff), "----------Bus monitor INFORMATION----------\n");
	for (i = 0; i < data->groupNum; i++) {
		group = &data->group[i];
		if ((group->groupStat >= 0x2) || (((data->groupMark>>i) & 0x01))) {
			bwKbps[0] = group->bytescale[0] / 1024 * 1000;
			bwKbps[0] = bwKbps[0] * 1000 / periodTimeUs;
			(*buff) += sprintf((*buff),
			"Timer[%2d][%9s]  Read:%8dKBps (%llubytes, %lldus)\n", i,
			group->masterName, bwKbps[0],
			group->bytescale[0], periodTimeUs);
			bwKbps[1] = group->bytescale[1] / 1024 * 1000;
			bwKbps[1] = bwKbps[1] * 1000 / periodTimeUs;
			(*buff) += sprintf((*buff),
			"Timer[%2d][%9s] Write:%8dKBps (%llubytes, %lldus)\n", i,
			group->masterName, bwKbps[1],
			group->bytescale[1], periodTimeUs);
		} else
			(*buff) += sprintf((*buff), "Timer[%2d]--Disable\n", i);
	}
	(*buff) += sprintf((*buff), "\n");
}

static int busmon_AllbwAndLat_intEn(struct busmon_data *data, int en)
{
	int num;

	/*Setup all bm int*/
	busmon_writel(data->bus_monitor + BW_FINISH_INT_ALL_EN, en);
	busmon_writel(data->bus_monitor + BW_ALL_INT_THR_WITIN_EN, en);
	busmon_writel(data->bus_monitor + BW_ALL_INT_THR_OUTOF_EN, en);
	busmon_writel(data->bus_monitor + INTR_MASK_BW, !en);
	busmon_writel(data->bus_monitor + BW_FINISH_INT_ALL_CLR, 1);
	busmon_writel(data->bus_monitor + BW_ALL_INT_WITHIN, 1);
	busmon_writel(data->bus_monitor + BW_ALL_INT_OUTOF, 1);
	/*Setup lat bm int*/
	busmon_writel(data->bus_monitor + INTR_MASK_LAT, !en);
	for (num = 0; num < LUT_GROUP_NUM; num++) {
		busmon_writel(data->bus_monitor + LAT_W_FINISH_INT_CLR(num), 1);
		busmon_writel(data->bus_monitor + LAT_R_FINISH_INT_CLR(num), 1);
		busmon_writel(data->bus_monitor + LAT_MAX_OUTOF_INT_R(num), 1);
		busmon_writel(data->bus_monitor + LAT_MAX_OUTOF_INT_W(num), 1);
		busmon_writel(data->bus_monitor + LAT_MAX_WITHIN_INT_R(num), 1);
		busmon_writel(data->bus_monitor + LAT_MAX_WITHIN_INT_W(num), 1);
	}
	return 0;
}

static int busmon_userBw_intEn(struct busmon_data *data, int en)
{
	unsigned int channel;

	if (!!en) {
		/*Enable one use bm int*/
		data->irqEn = 0;
		data->irqGroupNum = 0;
		for (channel = 0; channel < (data->groupNum); channel++) {
			busmon_writel(data->bus_monitor + BW_FINISH_INT_CLR(channel), 0);
			if (data->irqEn)
				break;
			if ((data->group[channel].groupStat >= 0x2) ||
				(((data->groupMark>>channel) & 0x01))) {
				busmon_writel(data->bus_monitor + BW_FINISH_INT_EN(channel), 1);
				data->irqEn = 1;
				data->irqGroupNum = channel;
			} else
				busmon_writel(data->bus_monitor + BW_FINISH_INT_EN(channel), 0);
		}
	} else {
		/*Disable use bm int*/
		for (channel = 0; channel < (data->groupNum); channel++) {
			busmon_writel(data->bus_monitor + BW_FINISH_INT_CLR(channel), 0);
			busmon_writel(data->bus_monitor + BW_FINISH_INT_EN(channel),  0);
		}
		if ((!SCP_BM_COMPATIBLE) || (SCP_USE_RTOSTIMER))
			busmon_AllbwAndLat_intEn(data, 0);
	}
	return 0;
}

static inline int busmon_userBw_intMask(struct busmon_data *data, int mask)
{
	if (!!mask)
		busmon_writel(data->bus_monitor + INTR_MASK_BW, 1);
	else
		busmon_writel(data->bus_monitor + INTR_MASK_BW, 0);
	return 0;
}

static int busmon_userBW_groupInit(struct busmon_data *info)
{
	struct bmPeriodCfg period;
	u32 i;

	period.short_period = DEFAULT_PERIOD_SHORT;
	period.long_period  = DEFAULT_PERIOD_LONG;
	period.loopCnt      = DEFAULT_PERIOD_CNT;
	period.waitCnt      = DEFAULT_PERIOD_CNT + 1;
	info->portGroupPtr = 0;
	info->byteOverFlow = 0;
	info->buslpState = 1;
	busmon_group_periodSet(info, &period);
	busmon_group_modeSet(info);
	busmon_group_disableAll(info);
	busmon_group_timeScalSelSet(info, 15);
	for (i = 0; i < info->groupNum; i++) {
		info->group[i].groupNum = i;
		info->group[i].portNum = group_port_match_tabele[i];
		info->group[i].groupStat = 0;
		info->group[i].bytescale[0] = 0;
		info->group[i].bytescale[1] = 0;
		info->group[i].bytescaleWin[0] = 0;
		info->group[i].bytescaleWin[1] = 0;
		info->group[i].updata = 0;
		info->group[i].masterName = NULL;
	}
	return 0;
}

/*
 *        CMD handler
 */
#define __cmd_handler__
static int CMD_userBW_initConfig(struct busmon_data *info)
{
	info->masterList = master_config_lists;
	info->masterNum = ARRAY_SIZE(master_config_lists);
	info->groupMark = 0;
	info->buslpEnabled = 1;

	/*Save default config*/
	busmon_buslp_save(info);
	busmon_save_qtUserSel(info);
	busmon_userBw_intEn(info, 0);

	/*User groups config */
	busmon_userBW_groupInit(info);

	return 0;
}

static int CMD_userBW_buslpSetup(struct busmon_data *info, int enable)
{
	busmon_buslp_enable(info, enable);
	return 0;
}

static int CMD_userBW_periodSetup(struct busmon_data *info, struct bmPeriodCfg *period)
{
	busmon_group_periodSet(info, period);
	return 0;
}

static int CMD_userBW_masterEnable(struct busmon_data *info, char *name)
{
	bool fd_master = false;
	struct bmMasterCfg const *master = info->masterList;

	/*Get the master info by name*/
	while (master->name != NULL) {
		if (!strcmp(master->name, name)) {
			fd_master = true;
			break;
		} else
			master++;
	}
	/*Add the master to bm-group */
	if (fd_master == true) {
		busmon_master_usridCfg(info, master);
		busmon_master_groupCfg(info, master, 1);
		return 0;
	} else
		goto exit_error;

exit_error:
	dev_err(info->dev, "Not found match member in list, Please check the name\n");
	return 0;
}

static int CMD_userBW_masterDisable(struct busmon_data *info, char *name)
{
	bool fd_master = false;
	struct bmMasterCfg const *master = info->masterList;

	/*Get the master info by name*/
	while (master->name != NULL) {
		if (!strcmp(master->name, name)) {
			fd_master = true;
			break;
		} else
			master++;
	}
	/*Del the master in bm-group */
	if (fd_master == true) {
		busmon_master_groupCfg(info, master, 0);
		return 0;
	} else
		goto exit_error;

exit_error:
	dev_err(info->dev, "Not found match member in list, Please check the name\n");
	return 0;
}

static irqreturn_t busmon_irq_thread(int irq, void *dev_id);
static irqreturn_t busmon_irq_handler(int irq, void *dev_id);

static int busmon_register_irq(struct busmon_data *data)
{
	int ret;
	struct platform_device *pdev;
	struct device *dev = data->dev;

	pdev = container_of(dev, struct platform_device, dev);
	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		dev_err(&pdev->dev, "busmon get platform irq failed\n");
		return -ENXIO;
	}
	ret = devm_request_threaded_irq(
				dev,
				data->irq,
				busmon_irq_handler,
				busmon_irq_thread,
				IRQF_ONESHOT | IRQF_SHARED,
				"Busmon",
				data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to register interrupt handler! (%d)\n", ret);
		return -ENXIO;
	}
	return 0;
}

static int busmon_unregister_irq(struct busmon_data *data)
{
	devm_free_irq(data->dev, data->irq, data);
	return 0;
}

static int CMD_userBW_Enable(struct busmon_data *data)
{
	u32 num;
	u32 groupMark = data->groupMark;

	/*Clear user bw monitor*/
	for (num = 0; num < data->groupNum; num++) {
		if ((data->group[num].groupStat >= 0x2) || ((groupMark>>num) & 0x01))
			busmon_group_disableCh(data, num);
	}
	/*Clear grobal monitor */
	busmon_group_disableGrobal(data);
	/*Clear default bw int */
	busmon_userBw_intEn(data, 0);
	/*Clear all and lat int*/
	if (SCP_BM_COMPATIBLE)
		busmon_AllbwAndLat_intEn(data, 0);
	/*Buslp and Int config */
	busmon_buslp_enable(data, 0);
	busmon_userBw_intEn(data, 1);
	busmon_userBw_intMask(data, 0);
	/*Enable user bw monitor*/
	for (num = 0; num < data->groupNum; num++) {
		if ((data->group[num].groupStat >= 0x2) || ((groupMark>>num) & 0x01))
			busmon_group_enableCh(data, num);
	}
	/*Config qt user ID select*/
	busmon_set_qtUserSel(data, 0);
	/*Register irq threaded*/
	busmon_register_irq(data);
	/*Enable grobal bw monitor*/
	busmon_group_enableGrobal(data);

	return 0;
}

static int CMD_userBW_Disable(struct busmon_data *data)
{
	u32 num;
	u32 groupMark = data->groupMark;

	/****Unregister irq****/
	busmon_unregister_irq(data);
	/*Clear user bw monitor*/
	for (num = 0; num < data->groupNum; num++) {
		if ((data->group[num].groupStat >= 0x2) || ((groupMark>>num) & 0x01))
			busmon_group_disableCh(data, num);
	}
	/*Clear grobal monitor*/
	busmon_group_disableGrobal(data);
	/*Clear default bw int*/
	busmon_userBw_intEn(data, 0);
	/*Lpm and Int restore */
	busmon_buslp_enable(data, 1);
	/*All and lat restore */
	if (SCP_BM_COMPATIBLE)
		busmon_AllbwAndLat_intEn(data, 1);
	/*Qt user sel restore */
	busmon_set_qtUserSel(data, data->qtUserSel);

	return 0;
}

/*
 *        sysfs operation
 */
#define __sysfs_operation__
static void sysfs_parse_master_handler(char *spec, char *newmaster)
{
	char *p;

	p = strsep(&spec, " ");
	if (!p || !*p)
		return;
	strcpy(newmaster, p);
}

static void sysfs_parse_period_handler(char *spec, struct bmPeriodCfg *info)
{
	char *p;
	unsigned long buf;

	p = strsep(&spec, " ");
	if (!p || !*p || kstrtoul(p, 10, &buf))
		return;
	info->short_period = buf;
	p = strsep(&spec, " ");
	if (!p || !*p || kstrtoul(p, 10, &buf))
		return;
	info->long_period = buf;
	p = strsep(&spec, " ");
	if (!p || !*p || kstrtoul(p, 10, &buf))
		return;
	info->loopCnt = buf;
	info->waitCnt = info->loopCnt + 1;
}

static void sysfs_parse_buslp_handler(char *spec, int *buslp_en)
{
	char *p;

	p = strsep(&spec, " ");
	if (!p || !*p)
		return;
	if (!strncmp(p, "enable", 6))
		*buslp_en = 1;
	else if (!strncmp(p, "disable", 7))
		*buslp_en = 0;
}

static ssize_t bm_monitor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	char *buff = buf;
	struct busmon_data *info = dev_get_drvdata(dev);
	unsigned int timeout;
	int loopCnt = info->period.loopCnt;
	int waitCnt = info->period.waitCnt;
	int clkKhz;
	u32 num;
	u64 byteNum[16][2] = {0};
	u64 byteWinNum[16][2] = {0};

	info->bmclk_rate = clk_get_rate(info->bmclk);
	clkKhz = info->bmclk_rate/1000;
	timeout = info->period.short_period * info->period.long_period / clkKhz;
	timeout += 60;
	mutex_lock(&info->lock);
	while (waitCnt--) {
		if (loopCnt == 0)
			break;
		loopCnt--;
		CMD_userBW_Enable(info);
		ret = wait_event_interruptible_timeout(info->wq_data_avail,
							info->done, msecs_to_jiffies(timeout));
		if (ret == 0) {
			dev_err(dev, "wait event once timeout\n");
			ret = -ETIMEDOUT;
		} else if (ret == -ERESTARTSYS)
			dev_err(dev, "return -ERESTARTSYS\n");
		else {
			info->done = 0;
		}
		if (info->byteOverFlow)
			dev_err(dev, "Data over flow: loop%d, 0x%x\n", loopCnt, info->byteOverFlow);
		CMD_userBW_Disable(info);
		for (num = 0; num < info->groupNum; num++) {
			byteNum[num][0] += info->group[num].bytescale[0];
			byteNum[num][1] += info->group[num].bytescale[1];
			byteWinNum[num][0] += info->group[num].bytescaleWin[0];
			byteWinNum[num][1] += info->group[num].bytescaleWin[1];
			info->group[num].bytescale[0] = 0;
			info->group[num].bytescale[1] = 0;
			info->group[num].bytescaleWin[0] = 0;
			info->group[num].bytescaleWin[1] = 0;

		}
	}
	mutex_unlock(&info->lock);
	for (num = 0; num < info->groupNum; num++) {
		info->group[num].bytescale[0] = byteNum[num][0];
		info->group[num].bytescale[1] = byteNum[num][1];
		info->group[num].bytescaleWin[0] = byteWinNum[num][0];
		info->group[num].bytescaleWin[1] = byteWinNum[num][1];
	}
	busmon_userBw_dataBuf(info, &buff);

	return (buff - buf);
}

static ssize_t bm_monitor_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	char tmpbuf[MAX_OPTION_SIZE];
	char *options;
	char *this_opt;
	bool bm_en_cmd;
	bool master_new  = false;
	bool master_del  = false;
	bool update_period = false;
	bool update_buslp_en = false;
	bool reset = false;

	struct busmon_data *info = dev_get_drvdata(dev);

	/*for parse command line*/
	char new_master[MAX_NAME_SIZE];
	char del_master[MAX_NAME_SIZE];
	char *master;
	struct bmPeriodCfg period = {DEFAULT_PERIOD_SHORT, DEFAULT_PERIOD_LONG};
	int buslp_en = false;

	memset(tmpbuf, 0x00, sizeof(tmpbuf));
	memcpy(tmpbuf, buf, strlen(buf));
	options = tmpbuf;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		/*Removes leading and trailing whitespace*/
		this_opt = strim(this_opt);
		if (strncmp(this_opt, sysfsCmd[BWBmStartCmd], 7)
			&& strncmp(this_opt, sysfsCmd[AddMasterCmd], 10)
			&& strncmp(this_opt, sysfsCmd[DelMasterCmd], 10)
			&& strncmp(this_opt, sysfsCmd[CfgPeriodCmd], 10)
			&& strncmp(this_opt, sysfsCmd[ResetCmd], 5)
			&& strncmp(this_opt, sysfsCmd[BusLpCmd], 6))
			bm_debug("command error, try again\n");
		else if (!strncmp(this_opt, sysfsCmd[BWBmStartCmd], 7)) {
			this_opt = strim(this_opt + 7);
			bm_en_cmd = true;
		} else if (!strncmp(this_opt, sysfsCmd[AddMasterCmd], 10)) {
			this_opt = strim(this_opt + 10);
			sysfs_parse_master_handler(this_opt, new_master);
			master_new = true;
		} else if (!strncmp(this_opt, sysfsCmd[DelMasterCmd], 10)) {
			this_opt = strim(this_opt + 10);
			sysfs_parse_master_handler(this_opt, del_master);
			master_del = true;
		} else if (!strncmp(this_opt, sysfsCmd[CfgPeriodCmd], 10)) {
			this_opt = strim(this_opt + 10);
			sysfs_parse_period_handler(this_opt, &period);
			update_period = true;
		} else if (!strncmp(this_opt, sysfsCmd[ResetCmd], 5)) {
			this_opt = strim(this_opt + 6);
			reset = true;
		} else if (!strncmp(this_opt, sysfsCmd[BusLpCmd], 6)) {
			this_opt = strim(this_opt + 6);
			sysfs_parse_buslp_handler(this_opt, &buslp_en);
			update_buslp_en = true;
		}
	}

	mutex_lock(&info->lock);
	if (bm_en_cmd) {
		bm_en_cmd = false;
		CMD_userBW_Enable(info);
	}
	if (master_new) {
		master_new = false;
		master = new_master;
		CMD_userBW_masterEnable(info, master);
	}
	if (master_del) {
		master_del = false;
		master = del_master;
		CMD_userBW_masterDisable(info, master);
	}
	if (update_period) {
		update_period = false;
		CMD_userBW_periodSetup(info, &period);
	}
	if (reset) {
		reset = false;
		CMD_userBW_initConfig(info);
	}
	if (update_buslp_en) {
		update_buslp_en = false;
		CMD_userBW_buslpSetup(info, buslp_en);
	}
	mutex_unlock(&info->lock);

	return size;
}

static ssize_t bm_dumpregs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char *buff = buf;
	struct busmon_data *info = dev_get_drvdata(dev);
	struct bmMasterCfg const *config = info->masterList;
	uint32_t val;
	void __iomem *base;

	mutex_lock(&info->lock);

	while (config->name) {
		master_find_base(info, config, &base);
		val = busmon_readl(base + config->reg);
		buff += sprintf(buff, "    %s\t[0x%08x]: 0x%08x,\n",
			config->name, config->reg, val);
		config++;
	}

	buff += sprintf(buff, "\nBUS_MONITOR regs:\n");
	buff += sprintf(buff, "    --USER_BW_SHORT_TIM[0x%x]: 0x%x,\n", USER_BW_SHORT_TIM,
		busmon_readl(info->bus_monitor + USER_BW_SHORT_TIM));
	buff += sprintf(buff, "    --USER_BW_LONG_TIM[0x%x]: 0x%x,\n", USER_BW_LONG_TIM,
		busmon_readl(info->bus_monitor + USER_BW_LONG_TIM));
	buff += sprintf(buff, "    --USER_BW_RPT[0x%x]: 0x%x,\n", USER_BW_RPT,
		busmon_readl(info->bus_monitor + USER_BW_RPT));
	buff += sprintf(buff, "INTR_RPT[0X%X] INTR_RAW_BW[0x%x] BW_FINISH_RAW_INTR_ALL[0x%x]\n",
		busmon_readl(info->bus_monitor + INTR_RPT),
		busmon_readl(info->bus_monitor + INTR_RAW_BW),
		busmon_readl(info->bus_monitor + BW_FINISH_RAW_INTR_ALL));
	buff += sprintf(buff, "  BUS_LP_EN_CTL0  [0x%x]: 0x%x,\n", BUS_LP_EN_CTL0,
		busmon_readl(info->top_crg + BUS_LP_EN_CTL0));
	buff += sprintf(buff, "  BUS_LP_EN_CTL1  [0x%x]: 0x%x,\n", BUS_LP_EN_CTL1,
			busmon_readl(info->top_crg + BUS_LP_EN_CTL1));
	buff += sprintf(buff, "  BUS_LP_EN_CTL2  [0x%x]: 0x%x,\n", BUS_LP_EN_CTL2,
		busmon_readl(info->top_crg + BUS_LP_EN_CTL2));
	buff += sprintf(buff, "  BUS_LP_EN_CTL3  [0x%x]: 0x%x,\n", BUS_LP_EN_CTL3,
		busmon_readl(info->top_crg + BUS_LP_EN_CTL3));

	mutex_unlock(&info->lock);

	return (buff - buf);
}

static ssize_t bm_dumpinfo_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char *buff = buf;
	struct busmon_data *info = dev_get_drvdata(dev);

	busmon_userBw_dataBuf(info, &buff);
	return (buff - buf);
}

static DEVICE_ATTR_RW(bm_monitor);
static DEVICE_ATTR_RO(bm_dumpregs);
static DEVICE_ATTR_RO(bm_dumpinfo);

static struct attribute *bm_attrs[] = {
	&dev_attr_bm_monitor.attr,
	&dev_attr_bm_dumpregs.attr,
	&dev_attr_bm_dumpinfo.attr,
	NULL,
};

static struct attribute_group busmon_attribute_group = {.attrs = bm_attrs,};

static const char *const busmon_hw_spec[] = {
		"busmon_port0_target-dev",
		"busmon_port1_target-dev",
		"busmon_port2_target-dev",
		"busmon_port3_target-dev",
		"busmon_port4_target-dev"
};

/*
 *         parse dt handler
 */
 #define __parse_dt_handler__
static int busmon_parse_dt_handler
(
	struct platform_device *pdev,
	struct busmon_data *info
)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	struct resource *res;
	int val, err;

	/*port group nums and polling_ms*/
	err = of_property_read_u32(dev_node, "busmon_port_nums", &val);
	if (!err && val)
		info->portsNum = val;
	else
		return -ENODEV;
	err = of_property_read_u32(dev_node, "busmon_group_nums", &val);
	if (!err && val)
		info->groupNum = val;
	else
		return -ENODEV;
	err = of_property_read_u32(dev_node, "busmon_polling_ms", &val);
	if (!err && val)
		info->polling_ms = val;
	else
		return -ENODEV;

	/*bus_monitor base*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "bm_base");
	if (!res) {
		dev_err(dev, "bm_base not found!\n");
		return -EINVAL;
	}
	info->bus_monitor = devm_ioremap(dev, res->start, resource_size(res));
	if (!info->bus_monitor) {
		dev_err(dev, "Unable map bm_base!\n");
		return -ENOMEM;
	}

	/*sys_ctrl base*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sysctrl_base");
	if (!res) {
		dev_err(dev, "sysctrl_base not found!\n");
		return -EINVAL;
	}
	info->top_sysctrl = devm_ioremap(dev, res->start, resource_size(res));
	if (!info->top_sysctrl) {
		dev_err(dev, "Unable map sysctrl_base!\n");
		return -ENOMEM;
	}

	/*HBLK_SCTRL_BASE*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hblkctrl_base");
	if (!res) {
		dev_err(dev, "HBLK_SCTRL_BASE not found!\n");
		return -EINVAL;
	}
	info->hblk_sysctrl = devm_ioremap(dev, res->start, resource_size(res));
	if (!info->hblk_sysctrl) {
		dev_err(dev, "Unable map HBLK_SCTRL_BASE!\n");
		return -ENOMEM;
	}

	/*CM4_SYSCTRL_BASE*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cm4ctrl_base");
	if (!res) {
		dev_err(dev, "CM4_SYSCTRL_BASE not found!\n");
		return -EINVAL;
	}
	info->cm4_sysctrl = devm_ioremap(dev, res->start, resource_size(res));
	if (!info->cm4_sysctrl) {
		dev_err(dev, "Unable map CM4_SYSCTRL_BASE!\n");
		return -ENOMEM;
	}

	/*AUDIO_SYSCTRL_BASE*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audioctrl_base");
	if (!res) {
		dev_err(dev, "AUDIO_SYSCTRL_BASE not found!\n");
		return -EINVAL;
	}
	info->audio_sysctrl = devm_ioremap(dev, res->start, resource_size(res));
	if (!info->audio_sysctrl) {
		dev_err(dev, "Unable map AUDIO_SYSCTRL_BASE!\n");
		return -ENOMEM;
	}

	/*SMMU_SYSCTRL_BASE*/
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smmuctrl_base");
	if (!res) {
		dev_err(dev, "SMMU_SYSCTRL_BASE not found!\n");
		return -EINVAL;
	}
	info->smmu_sysctrl = devm_ioremap(dev, res->start, resource_size(res));
	if (!info->smmu_sysctrl) {
		dev_err(dev, "Unable map SMMU_SYSCTRL_BASE!\n");
		return -ENOMEM;
	}

	/*top_crg base*/
	dev_node = of_find_compatible_node(NULL, NULL, "jlq,crg-base");
	if (!dev_node) {
		pr_err("jlq,crg-base No compatible node found\n");
		return -ENODEV;
	}
	info->top_crg = of_iomap(dev_node, 0);
	if (IS_ERR(info->top_crg)) {
		dev_err(dev, "top_crg reg ioremap failed\n");
		return -ENOMEM;
	}
	of_node_put(dev_node);

	return 0;
}

unsigned int mbps_to_bytes(unsigned long mbps, unsigned int ms,
				  unsigned int tolerance_percent)
{
	mbps *= (100 + tolerance_percent) * ms;
	mbps /= 100;
	mbps = DIV_ROUND_UP(mbps, MSEC_PER_SEC);
	mbps *= SZ_1M;
	return mbps;
}

static __always_inline int __start_bw_hwmon(struct bw_hwmon *hw)
{
	return 0;
}

static __always_inline int __stop_bw_hwmon(struct bw_hwmon *hw)
{
	return 0;
}

/*
 * irq handler
 */
static inline int busmon_group_irqStatus(struct busmon_data *info, u32 *valid)
{
	u32 group;
	u32 bwRawIrq;

	bwRawIrq = busmon_readl(info->bus_monitor + BW_FINISH_RAW_INTR(info->irqGroupNum));
	*valid = 0;
	if (bwRawIrq) {
		for (group = 0; group < info->groupNum; group++) {
			if ((info->groupMark >> group) & 0x01) {
				*valid += 1;
				info->group[group].updata = 1;
			}
			busmon_writel(info->bus_monitor + BW_FINISH_INT_CLR(group), 1);
			busmon_writel(info->bus_monitor + BW_FINISH_INT_EN(group), 0);
		}
	}
	return 0;
}

static int busmon_group_dataUpdataFromIrq(struct busmon_data *data)
{
	u32 num;

	for (num = 0; num < data->groupNum; num++) {
		if ((data->group[num].groupStat >= 0x2) || (((data->groupMark>>num) & 0x01))) {
			busmon_group_byteScal(data, num, data->group[num].bytescale);
			busmon_group_byteScalWin(data, num, data->group[num].bytescaleWin);
			busmon_group_timeScal(data, num, data->timescale);
			busmon_group_resultClear(data, num);
			data->group[num].updata = 0;
		}
	}
	return 0;
}

static inline int busmon_irq_mask_all_and_lat(struct busmon_data *data)
{
	uint32_t latInt, allInt, allWithInt, allOutfInt;

	latInt = busmon_readl(data->bus_monitor + INTR_RAW_LAT);
	allInt = busmon_readl(data->bus_monitor + BW_FINISH_RAW_INTR_ALL);
	allWithInt = busmon_readl(data->bus_monitor + BW_ALL_RAW_INTR_WITHIN);
	allOutfInt = busmon_readl(data->bus_monitor + BW_ALL_RAW_INTR_OUTOF);

	return (latInt || allInt || allWithInt || allOutfInt);
}

static irqreturn_t busmon_irq_handler(int irq, void *dev_id)
{
	uint32_t valid = 0;
	struct busmon_data *info = dev_id;

	if (busmon_irq_mask_all_and_lat(info))
		return IRQ_NONE;

	busmon_group_irqStatus(info, &valid);
	if (!valid)
		return IRQ_NONE;
	else
		return IRQ_WAKE_THREAD;
}

static void busmon_bw_update_handler(struct busmon_data *info)
{
	busmon_group_dataUpdataFromIrq(info);
	busmon_group_ovfCheck(info);
}

static irqreturn_t busmon_irq_thread(int irq, void *dev_id)
{
	struct busmon_data *info = dev_id;

	busmon_bw_update_handler(info);
	/*wake up sysfs read*/
	info->done = true;
	wake_up_interruptible(&info->wq_data_avail);

	return IRQ_HANDLED;
}

/*
 *        wq and thread
 */
#define __wq_and_thread__
static void busmon_bw_wk(struct work_struct *work)
{
	struct busmon_data *info;

	info = gBmdata;
	dev_info(info->dev, " bm_BWwq: wk on, updata the bw value by hwmon ~@~\n");
	busmon_bw_update_handler(info);
	queue_delayed_work(bm_BWwq, &info->busmon_wk, msecs_to_jiffies(info->polling_ms));
}

static void busmon_bw_swk(struct work_struct *work)
{
	struct busmon_data *info;

	info = gBmdata;
	dev_info(info->dev, " busmon bw swk: schedule wk on ~@~\n");
	busmon_bw_update_handler(info);
	schedule_delayed_work(&info->busmon_swk, msecs_to_jiffies(info->polling_ms));
}

void busmon_bw_wk_start(struct busmon_data *info)
{
	dev_info(info->dev, "busmon wq: init and start ~@~\n");
	CMD_userBW_initConfig(info);
	if (info->bmWkEn) {
		INIT_DEFERRABLE_WORK(&info->busmon_wk, busmon_bw_wk);
		INIT_DEFERRABLE_WORK(&info->busmon_swk, busmon_bw_swk);
		schedule_delayed_work(&info->busmon_swk, msecs_to_jiffies(info->polling_ms));
		if (info->polling_ms)
			queue_delayed_work(bm_BWwq, &info->busmon_wk,
						msecs_to_jiffies(info->polling_ms));
	}
}

void busmon_bw_wk_stop(struct busmon_data *data)
{
	cancel_delayed_work_sync(&data->busmon_wk);
}

/*
 *        kernel driver probe and remove
 */
#define __kernel_driver_probe_and_remove__
static int ddr_busmon_probe(struct platform_device *pdev)
{
	int ret = -EINVAL;
	int num = 0;
	int extra_size = 0;
	struct busmon_data *data;
	struct busmon_data tmp = {};

#ifdef CONFIG_OF
	ret = busmon_parse_dt_handler(pdev, &tmp);
	if (ret) {
		dev_err(&pdev->dev, "busmon parse dt failed\n");
		return -ENOMEM;
	}
#endif

	/*kzalloc memory and map*/
	extra_size = sizeof(struct busmon_data) +
				sizeof(*data->group) * tmp.groupNum +
				sizeof(*data->port) * tmp.portsNum +
				sizeof(*data->hw) * tmp.portsNum +
				sizeof(*data->spec);
	data = kzalloc(extra_size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	memcpy(data, &tmp, sizeof(struct busmon_data));
	data->group = ((void *)data + sizeof(*data));
	data->port = ((void *)data->group + sizeof(*data->group) * data->groupNum);
	data->hw  =  ((void *)data->port + sizeof(*data->port) * data->portsNum);
	data->spec = ((void *)data->hw + sizeof(*data->hw) * data->portsNum);

	/****store data****/
	gBmdata = data;
	data->dev = &pdev->dev;
	strcpy(data->name, DRIVER_NAME);
	platform_set_drvdata(pdev, data);

	/*****sysfs file*****/
	ret = sysfs_create_group(&pdev->dev.kobj, &busmon_attribute_group);
	if (ret) {
		dev_err(&pdev->dev, "jlq busmon sysfs file create failed.\n");
		goto exit_free_irq;
	}

	/*****workqueue*****/
	data->bmWkEn = 0;
	bm_BWwq = create_freezable_workqueue("ddr_bm_BWwq");
	if (!bm_BWwq) {
		dev_err(&pdev->dev, "%s: couldn't create workqueue\n", __FILE__);
		return -ENOMEM;
	}

	/****get bm pclk****/
	data->bmclk = devm_clk_get(&pdev->dev, "bm_clk");
	if (IS_ERR(data->bmclk)) {
		dev_err(&pdev->dev, "Failed to get clk\n");
		ret = PTR_ERR(data->bmclk);
		goto exit_free_data;
	}

	/**hw monitor init**/
	busmon_bw_wk_start(data);

	/**init wq for irq**/
	init_waitqueue_head(&data->wq_data_avail);
	mutex_init(&data->lock);

	/*Virtual the hardware device and register to Governor busmon-bw for every port*/
	//TBD [for port dvfs]
	data->hw_timer_hz = 19200000;
	strcpy(data->name, "test!");
	for (num = 0; num < data->portsNum; num++) {
		data->hw[num].port_num = num;
		data->hw[num].dev = &pdev->dev;
		//data->hw[num].of_node = of_parse_phandle(pdev->dev.of_node, busmon_hw_spec[num], 0);
		//ret = register_busmon_bw(data->dev, &data->hw[num]);
	}
	return 0;

exit_free_irq:
	free_irq(data->irq, data);
exit_free_data:
	kfree(data);
	return ret;
}

static int ddr_busmon_remove(struct platform_device *pdev)
{
	struct busmon_data *data = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &busmon_attribute_group);
	kfree(data);
	free_irq(data->irq, data);
	cancel_delayed_work_sync(&data->busmon_wk);
	cancel_delayed_work_sync(&data->busmon_swk);
	dev_pm_opp_of_remove_table(data->dev);

	return 0;
}

static const struct of_device_id busmon_dt_match[] = {
	{.compatible = "jlq,busmon",},
	{}
};

static struct platform_driver busmon_driver = {
	.probe  = ddr_busmon_probe,
	.remove = ddr_busmon_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = busmon_dt_match,
	},
};

static int __init busmon_init(void)
{
	return platform_driver_register(&busmon_driver);
}

static void __exit busmon_exit(void)
{
	platform_driver_unregister(&busmon_driver);
}

module_init(busmon_init);
module_exit(busmon_exit);

MODULE_DESCRIPTION("System bus monitor bw driver");
MODULE_LICENSE("GPL");
