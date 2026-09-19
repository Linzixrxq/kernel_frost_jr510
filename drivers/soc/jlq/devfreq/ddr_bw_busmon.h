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

#ifndef _SYS_BUSMON_REG_H_
#define _SYS_BUSMON_REG_H_

#include "governor_busmon-bw.h"

#define BM_DEBUG 0
#if BM_DEBUG
	#define bm_debug(fmt, ...) printk(KERN_ERR fmt, ##__VA_ARGS__)
#else
	#define bm_debug(fmt, ...)
#endif

/*
 * cmd
 */
#define BWBmStartCmd        0
#define AddMasterCmd        1
#define DelMasterCmd        2
#define CfgPeriodCmd        3
#define ResetCmd            4
#define BusLpCmd            5

/*
 * Macro
 */
#define to_busmon_info(ptr)        container_of(ptr, struct busmon_info, hw[ptr->port_num])
#define MAX_NAME_SIZE              32
#define MAX_OPTION_SIZE            512
#define Channel_Mark(nr_channels)  ((1 << nr_channels) - 1)
#define BUSMON_BW                  0
#define BUSMON_LAT                 1
#define BUSMON_ALL                 2
#define BUSMON_GROUP_BW_ENABLE     BIT(0)
#define BUSMON_GROUP_BW_FLAG       1
#define PORT0_ID                   0
#define PORT1_ID                   1
#define PORT2_ID                   2
#define PORT3_ID                   3
#define PORT4_ID                   4
#define PORT_NUM                   5
#define MASTER_NUM                 22
#define BW_GROUP_NUM               16
#define LUT_GROUP_NUM              3
#define MAX_MEMBER_NUM             15

/*
 * Config
 */
#define BUSMON_BW_MODE             0 //0 = periodicity updata
#define PERF_MNTR_BW_TYPE          1
#define SCP_BM_COMPATIBLE          1
#define SCP_USE_RTOSTIMER          0

/*
 * Period
 */
#define BUSMONITOR_CLK             160 // pclk = top_bus_clk
#define DEFAULT_PERIOD_SHORT       600
#define DEFAULT_PERIOD_LONG        96000
#define DEFAULT_PERIOD_CNT         1

/*
 * Struct
 */
struct bmPeriodCfg {
	uint32_t short_period;
	uint32_t long_period;
	uint8_t  loopCnt;
	uint8_t  waitCnt;
};

struct bmMasterCfg {
	const char   *name;
	uint8_t       usrID;
	uint8_t       portNum;
	uint32_t      base;
	unsigned int  reg;
	unsigned char wen_ar;
	unsigned char wen_aw;
	unsigned char offset_ar;
	unsigned char offset_aw;
};

struct bmPortInfo {
	uint8_t       portNum;    //fix with ddr axt port
	const char   *members[MAX_MEMBER_NUM];
	uint8_t       end;         //0xff:end Others:not end
};

struct bmGroupInfo {
	uint8_t       groupNum;    //fix with ddr axt port
	uint8_t       groupStat;   //2bit: bit0 config, bit1 enable
	/*config*/
	uint8_t       portNum;
	uint8_t       usrID;
	const char   *masterName;
	/*result*/
	u64           bytescale[2];
	u64           bytescaleWin[2];
	u32           updata;
};

struct busmon_data {
	char name[MAX_NAME_SIZE];
	struct device	*dev;
	bool			done;
	/*irq and wq*/
	int				irq;
	int				irqEn;
	int				irqGroupNum;
	wait_queue_head_t wq_data_avail;
	struct mutex	lock;
	/*clk*/
	struct clk *bmclk;
	int bmclk_rate;
	/*infor*/
	const struct bmMasterCfg *masterList;
	struct bmPeriodCfg  period;
	struct bmPortInfo  *port;
	struct bmGroupInfo *group;
	u32			masterNum;
	u32			portsNum;
	u32			groupNum;
	u32			groupMark;
	u32			buslpState;
	u32			buslpEn[10];
	u32			qtUserSel;
	u32			buslpEnabled;
	u32			portGroupPtr;
	u32			timeGroupId;
	u64			timescale[2];
	u32			byteOverFlow;
	/*basereg*/
	void __iomem	*bus_monitor;
	void __iomem	*top_crg;
	void __iomem	*top_sysctrl;
	void __iomem	*hblk_sysctrl;
	void __iomem	*cm4_sysctrl;
	void __iomem	*audio_sysctrl;
	void __iomem	*smmu_sysctrl;
	/*work quenue*/
	u32				bmWkEn;
	struct delayed_work busmon_wk;
	struct delayed_work busmon_swk;
	u32			polling_ms;
	atomic_t	is_disabled;
	atomic_t	is_stopped;
	atomic_t	is_perf_start;
	int			is_perf_enable;
	struct delayed_work perf_work;
	struct timespec start_ts;
	struct timespec stop_ts;
	/*governor hw virtual*/
	struct bwmon_spec const *spec;
	struct bw_hwmon *hw;
	u32		hw_timer_hz;
	u32		throttle_adj;
	u32		sample_size_ms;
	u32		intr_status;
	u32		thres_lim;
	u32		byte_mask;
};

struct bwmon_spec {
	bool wrap_on_thres;
	bool overflow;
	bool throt_adj;
	bool hw_sampling;
	bool has_global_base;
	//enum mon_reg_type reg_type;
};

/*
 * Register
 */
#define BUS_LP_EN_CTL0					 (0x404)
#define BUS_LP_EN_CTL1					 (0x408)
#define BUS_LP_EN_CTL2					 (0x40c)
#define BUS_LP_EN_CTL3					 (0x410)
#define DDR_TOP_LP_EN_CTL0				 (0x414)
#define CPU_SYS_LP_EN_CTL0				 (0x418)
#define DDR_CRG_CG_SFCTL2				 (0x024)
#define DDR_RST_SFCTL2					 (0x0268)

#define HBLK_BUS_USER					 (0x100)
#define HBLK_BUS_USER1					 (0x104)

#define VDSPCORE_IDMA_USER_CFG			 (0x58)
#define TOPAP_DMAS_USER_CFG				 (0x48)
#define QTMODEM0_QTMODEM1_USER_CFG		 (0x64)
#define DISP_VPU_USER_CFG				 (0x44)
#define AP_GPU_USER_CFG					 (0x40)
#define AI_QTANG_USER_CFG				 (0x54)
#define QTCAMRT_QTCAMNRT_USER_CFG		 (0x60)
#define AP_SDIO_USER_CFG				 (0x4C)
#define EMMC_USB_USER_CFG				 (0x50)
#define USB_CIPHER_USER_CFG				 (0x5C)
#define ADSP_QOS_USER					 (0x18)

#define BUSMON_WRAP_REG_BASE			 (0xB00)
#define BUSMON_BW_REG_BASE				 (0x0)
#define BUSMON_LAT_REG_BASE				 (0x800)

#define USER_BW_SHORT_TIM                (BUSMON_WRAP_REG_BASE +  0x0)
#define USER_BW_LONG_TIM                 (BUSMON_WRAP_REG_BASE +  0x4)
#define ALL_BW_SHORT_TIM                 (BUSMON_WRAP_REG_BASE +  0x8)
#define ALL_BW_LONG_TIM                  (BUSMON_WRAP_REG_BASE +  0xC)
#define USER_BW_RPT                      (BUSMON_WRAP_REG_BASE +  0x18)
#define ALL_BW_RPT                       (BUSMON_WRAP_REG_BASE +  0x1C)
#define ALL_BW_RLT                       (BUSMON_WRAP_REG_BASE +  0x20)
#define ALL_BW_RESULT                    (BUSMON_WRAP_REG_BASE +  0x24)
#define ALL_BYTESCAL_R_LO                (BUSMON_WRAP_REG_BASE +  0x28)
#define ALL_BYTESCAL_R_HI                (BUSMON_WRAP_REG_BASE +  0x2C)
#define ALL_BYTESCAL_W_LO                (BUSMON_WRAP_REG_BASE +  0x30)
#define ALL_BYTESCAL_W_HI                (BUSMON_WRAP_REG_BASE +  0x34)
#define ALL_TIMESCAL_R_LO                (BUSMON_WRAP_REG_BASE +  0x38)
#define ALL_TIMESCAL_W_LO                (BUSMON_WRAP_REG_BASE +  0x40)
#define BW_TIMESCAL_SEL                  (BUSMON_WRAP_REG_BASE +  0x48)
#define BW_TIMESCAL_R_LO                 (BUSMON_WRAP_REG_BASE +  0x4C)
#define BW_TIMESCAL_W_LO                 (BUSMON_WRAP_REG_BASE +  0x54)
#define ALL_WIN_BYTESCAL_R_LO_MAX        (BUSMON_WRAP_REG_BASE +  0x5C)
#define ALL_WIN_BYTESCAL_R_HI_MAX        (BUSMON_WRAP_REG_BASE +  0x60)
#define ALL_WIN_BYTESCAL_W_LO_MAX        (BUSMON_WRAP_REG_BASE +  0x64)
#define ALL_WIN_BYTESCAL_W_HI_MAX        (BUSMON_WRAP_REG_BASE +  0x68)
#define ALL_BW_WITHIN_BYTE_LO            (BUSMON_WRAP_REG_BASE +  0x6C)
#define ALL_BW_WITHIN_BYTE_HI            (BUSMON_WRAP_REG_BASE +  0x70)
#define ALL_BW_OUTOF_BYTE_LO             (BUSMON_WRAP_REG_BASE +  0x74)
#define ALL_BW_OUTOF_BYTE_HI             (BUSMON_WRAP_REG_BASE +  0x78)
#define BW_FINISH_FLG_ALL_USER           (BUSMON_WRAP_REG_BASE +  0x7C)
#define BW_FINISH_FLG_ALL                (BUSMON_WRAP_REG_BASE +  0x80)
#define BW_ALL_THR_WITHIN_FLG            (BUSMON_WRAP_REG_BASE +  0x84)
#define BW_ALL_THR_OUTOF_FLG             (BUSMON_WRAP_REG_BASE +  0x88)
#define BW_FINISH_INT_ALL_EN             (BUSMON_WRAP_REG_BASE +  0x8C)
#define BW_ALL_INT_THR_WITIN_EN          (BUSMON_WRAP_REG_BASE +  0x90)
#define BW_ALL_INT_THR_OUTOF_EN          (BUSMON_WRAP_REG_BASE +  0x94)
#define BW_FINISH_INT_ALL_CLR            (BUSMON_WRAP_REG_BASE +  0x98)
#define BW_ALL_INT_WITHIN                (BUSMON_WRAP_REG_BASE +  0x9C)
#define BW_ALL_INT_OUTOF                 (BUSMON_WRAP_REG_BASE +  0xA0)
#define BW_FINISH_RAW_INTR_ALL           (BUSMON_WRAP_REG_BASE +  0xA4)
#define BW_ALL_RAW_INTR_WITHIN           (BUSMON_WRAP_REG_BASE +  0xA8)
#define BW_ALL_RAW_INTR_OUTOF            (BUSMON_WRAP_REG_BASE +  0xAC)
#define ALL_BW_PRE_BYTE_LO               (BUSMON_WRAP_REG_BASE +  0xB4)
#define ALL_BW_PRE_BYTE_HI               (BUSMON_WRAP_REG_BASE +  0xB8)
#define ALL_BW_PRE_TIME_LO               (BUSMON_WRAP_REG_BASE +  0xBC)
#define ALL_BW_BYTE_LO                   (BUSMON_WRAP_REG_BASE +  0xC4)
#define ALL_BW_BYTE_HI                   (BUSMON_WRAP_REG_BASE +  0xC8)
#define ALL_BW_TIME_LO                   (BUSMON_WRAP_REG_BASE +  0xCC)
#define USER_BW_PERIOD                   (BUSMON_WRAP_REG_BASE +  0xD4)
#define BW_QTANG_CNT_RPT_LO              (BUSMON_WRAP_REG_BASE +  0xD8)
#define BW_QTANG_CNT_RPT_HI              (BUSMON_WRAP_REG_BASE +  0xDC)
#define ALL_ARBYTE_OVF                   (BUSMON_WRAP_REG_BASE +  0xE0)
#define ALL_AWBYTE_OVF                   (BUSMON_WRAP_REG_BASE +  0xE4)
#define BW_QTANG_ALL_CNT_RPT_LO          (BUSMON_WRAP_REG_BASE +  0xE8)
#define BW_QTANG_ALL_CNT_RPT_HI          (BUSMON_WRAP_REG_BASE +  0xEC)
#define ALL_LONG_ARBYTE_OVF              (BUSMON_WRAP_REG_BASE +  0xF0)
#define ALL_LONG_AWBYTE_OVF              (BUSMON_WRAP_REG_BASE +  0xF4)
#define LAT_END                          (BUSMON_WRAP_REG_BASE +  0x100)
#define LAT_RLT                          (BUSMON_WRAP_REG_BASE +  0x104)
#define LAT_SHORT_TIM                    (BUSMON_WRAP_REG_BASE +  0x108)
#define LAT_LONG_TIM                     (BUSMON_WRAP_REG_BASE +  0x10C)
#define LAT_PERIOD                       (BUSMON_WRAP_REG_BASE +  0x118)
#define LAT_QTANG_CNT_RPT_LO             (BUSMON_WRAP_REG_BASE +  0x11C)
#define LAT_QTANG_CNT_RPT_HI             (BUSMON_WRAP_REG_BASE +  0x120)
#define INTR_MASK_BW                     (BUSMON_WRAP_REG_BASE +  0x240)
#define INTR_MASK_LAT                    (BUSMON_WRAP_REG_BASE +  0x244)
#define INTR_RAW_BW                      (BUSMON_WRAP_REG_BASE +  0x288)
#define INTR_RAW_LAT                     (BUSMON_WRAP_REG_BASE +  0x28C)
#define INTR_RPT                         (BUSMON_WRAP_REG_BASE +  0x290)
#define LAT_DEBUG_GLB_CNT                (BUSMON_WRAP_REG_BASE +  0x2C4)

#define USER_BW_RLT(userid)              (BUSMON_BW_REG_BASE + (userid*0x80) +  0x0)
#define USER_BW_RESULT(userid)           (BUSMON_BW_REG_BASE + (userid*0x80) +  0x4)
#define BYTESCAL_R_LO(userid)            (BUSMON_BW_REG_BASE + (userid*0x80) +  0x8)
#define BYTESCAL_R_HI(userid)            (BUSMON_BW_REG_BASE + (userid*0x80) +  0xC)
#define BYTESCAL_W_LO(userid)            (BUSMON_BW_REG_BASE + (userid*0x80) +  0x10)
#define BYTESCAL_W_HI(userid)            (BUSMON_BW_REG_BASE + (userid*0x80) +  0x14)
#define WIN_BYTESCAL_R_LO_MAX(userid)    (BUSMON_BW_REG_BASE + (userid*0x80) +  0x18)
#define WIN_BYTESCAL_W_LO_MAX(userid)	 (BUSMON_BW_REG_BASE + (userid*0x80) +  0x20)
#define BW_FINISH_FLG(userid)            (BUSMON_BW_REG_BASE + (userid*0x80) +  0x28)
#define BW_FINISH_INT_EN(userid)         (BUSMON_BW_REG_BASE + (userid*0x80) +  0x2C)
#define BW_FINISH_INT_CLR(userid)        (BUSMON_BW_REG_BASE + (userid*0x80) +  0x30)
#define BW_FINISH_RAW_INTR(userid)       (BUSMON_BW_REG_BASE + (userid*0x80) +  0x3C)
#define ARBYTESCNT_OVF(userid)           (BUSMON_BW_REG_BASE + (userid*0x80) +  0x40)
#define AWBYTESCNT_OVF(userid)           (BUSMON_BW_REG_BASE + (userid*0x80) +  0x44)
#define LONG_ARBYTE_OVF(userid)          (BUSMON_BW_REG_BASE + (userid*0x80) +  0x48)
#define LONG_AWBYTE_OVF(userid)          (BUSMON_BW_REG_BASE + (userid*0x80) +  0x4C)
#define BW_USER_CFG(userid)              (BUSMON_BW_REG_BASE + (userid*0x80) +  0x50)

#define LAT_R_CFG(userid)                (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x0)
#define LAT_W_CFG(userid)                (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x4)
#define LAT_R_RLT(userid)                (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x8)
#define LAT_W_RLT(userid)                (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xC)
#define LAT_R_MAX(userid)                (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x10)
#define LAT_W_MAX(userid)                (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x14)
#define LAT_MAX_R_OUTOF_THR(userid)      (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x28)
#define LAT_MAX_W_OUTOF_THR(userid)      (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x2C)
#define LAT_MAX_R_WITHIN_THR(userid)     (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x30)
#define LAT_MAX_W_WITHIN_THR(userid)     (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x34)
#define LAT_W_FINISH_FLG(userid)         (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x38)
#define LAT_R_FINISH_FLG(userid)         (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x3C)
#define LAT_R_FINISH_INT_EN(userid)      (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x50)
#define LAT_W_FINISH_INT_EN(userid)      (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x54)
#define LAT_MAX_OUTOF_INT_R_EN(userid)   (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x58)
#define LAT_MAX_OUTOF_INT_W_EN(userid)   (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x5C)
#define LAT_MAX_WITHIN_INT_R_EN(userid)  (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x60)
#define LAT_MAX_WITHIN_INT_W_EN(userid)  (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x64)
#define LAT_W_FINISH_INT_CLR(userid)     (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x68)
#define LAT_R_FINISH_INT_CLR(userid)     (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x6C)
#define LAT_MAX_OUTOF_INT_R(userid)      (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x70)
#define LAT_MAX_OUTOF_INT_W(userid)      (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x74)
#define LAT_MAX_WITHIN_INT_R(userid)     (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x78)
#define LAT_MAX_WITHIN_INT_W(userid)     (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x7C)
#define LAT_W_FINISH_INT(userid)         (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x80)
#define LAT_R_FINISH_INT(userid)         (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x84)
#define LAT_MAX_OUTOF_THR_R_INT(userid)  (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x88)
#define LAT_MAX_OUTOF_THR_W_INT(userid)  (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x8C)
#define LAT_MAX_WITHIN_THR_R_INT(userid) (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x90)
#define LAT_MAX_WITHIN_THR_W_INT(userid) (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x94)
#define LAT_R_TOTAL_LO(userid)           (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x98)
#define LAT_R_TOTAL_HI(userid)           (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0x9C)
#define LAT_W_TOTAL_LO(userid)           (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xA0)
#define LAT_W_TOTAL_HI(userid)           (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xA4)
#define LAT_R_TOTAL_TIM_LO(userid)       (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xA8)
#define LAT_W_TOTAL_TIM_LO(userid)       (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xB0)
#define LAT_R_CUR_OVF(userid)            (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xB4)
#define LAT_W_CUR_OVF(userid)            (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xB8)
#define LAT_R_TOTAL_OVF(userid)          (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xBC)
#define LAT_W_TOTAL_OVF(userid)          (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xC0)
#define LAT_USER_CFG(userid)             (BUSMON_LAT_REG_BASE + (userid * 0x100) + 0xC4)

static inline unsigned int busmon_readl(volatile void *addr) {return readl(addr); }
static inline void busmon_writel(volatile void *addr, unsigned int val) {writel(val, addr); }

#endif
