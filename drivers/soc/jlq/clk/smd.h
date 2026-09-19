/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#ifndef __DRIVERS_CLK_SMD_CHANNEL_H__
#define __DRIVERS_CLK_SMD_CHANNEL_H__

int clk_smd_send_meassage(unsigned int cmd,
	unsigned int id, unsigned long *data);
int clk_smd_probe(struct platform_device *pdev);
#endif
