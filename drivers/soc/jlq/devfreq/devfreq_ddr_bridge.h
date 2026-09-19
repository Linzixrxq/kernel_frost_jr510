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
#ifndef _DEVFREQ_DDR_BRIDGE_H
#define _DEVFREQ_DDR_BRIDGE_H

#include "../pmctrl/pmctrl.h"

#define DDR_MAIL_DEBUG  0
#if DDR_MAIL_DEBUG
	#define ddr_debug(fmt, ...) printk(KERN_ERR fmt, ##__VA_ARGS__)
#else
	#define ddr_debug(fmt, ...)
#endif

/*
 * cmd
 */
#define DDR_GET_FREQ    51
#define DDR_SET_FREQ    52
#define DDR_GET_VENDOR  53
#define DDR_GET_MR      54
#define DDR_SET_BM      55
#define DDR_GET_BM      56
#define DDR_SET_TH_UP   57
#define DDR_SET_TH_DW   58
#define DDR_GET_TH_UP   59
#define DDR_GET_TH_DW   60
#define DDR_GET_SR_INFO 61
#define DDR_SET_BW_EFFIC 62
#define DDR_GET_BW_EFFIC 63

/*
 * DDR vendor: JESD_166
 */
#define SAMSUNG  0x01
#define HYNIX    0x06
#define CXMT     0x13
#define MICRON   0xff

struct ddr_mail {
	struct device *dev;
	struct mutex lock;
	wait_queue_head_t wait;
	atomic_t ack;
	struct pmctl_msg snd;
	struct pmctl_msg rcv;
	unsigned int initialized;
	int (*mail_msg)(unsigned int cmd, unsigned long *data);
};

extern int ddr_dfs_bridge_probe(struct platform_device *pdev, struct ddr_mail **mail);

#endif /* _DEVFREQ_DDR_BRIDGE_H */
