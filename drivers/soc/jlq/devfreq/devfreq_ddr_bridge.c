// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include "devfreq_ddr_bridge.h"

static struct ddr_mail *ddrmail;

int ddr_dfs_send_message(unsigned int cmd, unsigned long *data)
{
	int ret = 0;
	struct pmctl_msg *ack;

	ack = pmctrl_send_wait_ack_msg(DDR_AP_MSG, cmd, (unsigned int *)data);
	*data = (unsigned long)ack->data[0];

#if DDR_MAIL_DEBUG
	if (ack->status == SMD_EVENT_SUCCESS) {
		ddr_debug("scp ack id:%d successfully!!\n", ack->id);
		ddr_debug(" data(hex):%x-%x-%x-%x\n", ack->data[0], ack->data[1],
				ack->data[2], ack->data[3]);
		ddr_debug(" data(dig):%d-%d-%d-%d\n", ack->data[0], ack->data[1],
				ack->data[2], ack->data[3]);
	} else {
		ddr_debug("scp id:0x%x ack error!!\n", DDR_AP_MSG);
	}
#endif

	if (ack->status != SMD_EVENT_SUCCESS)
		ret = -EINVAL;

	return ret;
}

int ddr_dfs_bridge_probe(struct platform_device *pdev, struct ddr_mail **mail)
{
	int probe_status = 0;

	if (ddrmail && ddrmail->initialized)
		return 0;

	ddrmail = devm_kzalloc(&pdev->dev, sizeof(*ddrmail), GFP_KERNEL);
	if (!ddrmail) {
		probe_status = -ENOMEM;
		goto free;
	}

	ddrmail->dev = &pdev->dev;
	mutex_init(&ddrmail->lock);
	init_waitqueue_head(&ddrmail->wait);
	ddrmail->initialized = 1;
	ddrmail->mail_msg = ddr_dfs_send_message;
	*mail = ddrmail;
	dev_info(&pdev->dev, "ddr dfs channel is ready\n");

	return 0;

free:
	devm_kfree(&pdev->dev, ddrmail);

	return probe_status;
}
EXPORT_SYMBOL(ddr_dfs_bridge_probe);

MODULE_LICENSE("GPL v2");
