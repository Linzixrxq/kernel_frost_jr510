/*
 * Copyright (c)2019-2021   JLQ Technology Co.,Ltd
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#ifndef __ADSP_LOCAL_H__
#define __ADSP_LOCAL_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/stat.h>
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <asm/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/devfreq.h>


/*PR INFO*/
//#define ADSP_PR_INFO
//#define ADSP_PR_DEBUG

#ifdef ADSP_PR_INFO
#define PR_INFO(fmt, args...) pr_info(fmt, ##args)
#else
#define PR_INFO(fmt, args...)
#endif

#ifdef ADSP_PR_DEBUG
#define PR_DEBUG(fmt, args...) pr_debug(fmt, ##args)
#else
#define PR_DEBUG(fmt, args...)
#endif


/*BIN/LMA address, only for soc backdoor debug*/
#define ADSP_BIN_BACKDOOR_LOAD_ADDR         0x96400000
#define ADSP_LMABIN_BACKDOOR_LOAD_ADDR      0x96E00000
#define ADSP_BIN_BACKDOOR_LOAD_ADDR_SIZE    (16*1024*1024)
#define ADSP_LMABIN_BACKDOOR_LOAD_ADDR_SIZE (1*1024*1024)

/*soc base address, use for independency interface*/
#define AUDIO_SYSCTRL_BASE          0x340EA000
#define TOP_LPM_BASE                0x34510000
#define LOCAL_ADSP_STATE_BASE       0x33081400

/*smem base*/
#define JR510_SMEM_BASE_ADDR      0x88300000  /*no cache to 0x68300000*/
#define JR510_SMEM_SIZE           0x00200000  //2MB

/*AP->ADSP*/
#define TOP_MAILBOX_ADSP_NINTR_SET          (0x340)
#define TOP_MAILBOX_ADSP_NINTR_EN           (0x344)
#define TOP_MAILBOX_ADSP_NINTR_SRC_EN       (0x348)
#define TOP_MAILBOX_ADSP_NINTR_STA          (0x34c)

/*ADSP->AP*/
#define TOP_MAILBOX_ADSP2AP_NINTR_SET       (0x360)
#define TOP_MAILBOX_ADSP2AP_NINTR_EN        (0x364)
#define TOP_MAILBOX_ADSP2AP_NINTR_SRC_EN    (0x368)
#define TOP_MAILBOX_ADSP2AP_NINTR_STA       (0x36C)

/*mbox irq usage*/
/*ap->adsp*/
#define MBOX_TOUCH_ABORT_TO_ADSP	23
#define MBOX_MDM_SSR_NOTI_TO_ADSP   24
#define MBOX_TEST_TO_ADSP           25
#define MBOX_GLINK_TX_TO_ADSP       26
/*adsp->ap*/
#define MBOX_DEVFREQ_UP_FROM_ADSP       21
#define MBOX_DEVFREQ_DOWN_FROM_ADSP     22
#define MBOX_DEVFREQ_ADJUST_FROM_ADSP   23 //reserve

/*subsys name, according to pil*/
#define SUBSYS_ADSP_NAME    "adsp"
#define SUBSYS_MODEM_NAME   "modem"

/*ssr wait adsp time*/
#define WAIT_ADSP_RERESET_TIME	500  //ms

/***lma information***/
#define LM_FLAG_LITERAL (1)
#define LM_FLAG_TEXT    (2)
#define LM_FLAG_RODATA  (3)
#define LM_FLAG_DATA    (4)
#define LM_FLAG_BSS     (5)

#define LMA_IDX_MAX		(512)

struct _LMA_IDX_ST {
    char secname[64];
    unsigned int size;
    unsigned int vma;
    unsigned int lma;
    unsigned int offs;
    unsigned int flag;
} __attribute__ ((packed));


/*adsp driver data*/
typedef struct _AdspLoadDrvData {
    struct platform_device *pdev;
    struct work_struct loader_work;
    struct kobject *loader_kobj;
    /*ssr subsys*/
    void *pil_h;
    /*adsp clk*/
    struct clk *clk;
    struct clk *bclk;
    struct clk *pbclk;
    /*core clk rate*/
    unsigned int pll_comm_rate;
    unsigned int pll_i2s_rate;
    /*adsp icc*/
    struct icc_path *icc;
    /*regulator*/
    struct regulator *regu;
    /*devfreq*/
    //struct devfreq *df;
    //struct devfreq_dev_profile profile;
    bool freq_in_khz;
    unsigned long *freq_table;
    unsigned int max_state;
} AdspLoadDrvData;

extern void __iomem *adsp_top_mbox_base;

void adsp_subsystem_get(void);
void adsp_subsys_reset_req(void);

void adsp_dump_dev_init(void);
void adsp_dump_dev_uninit(void);
void adsp_dump_available(void *dump_buff, size_t dump_size);


#endif
