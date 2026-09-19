// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021   JLQ
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
 */

#include <linux/module.h>

#include <linux/of_device.h>
#include <linux/of.h>

#include <soc/jlq/jr510/jlq-bridge.h>
#include "smd.h"

#define CLK_SMD_DEBUG
//#define CLK_SMD_LOOP
#define CLK_SMD_SUCC	0x53554343
#define CLK_SMD_FAIL	0x4641494C

#define CLK_SMD_TIMEOUT	50

static struct completion cmd_complete;

struct clk_smd_msg {
	unsigned int seq;
	unsigned int id;
	unsigned long frequency;
	unsigned int action;
	unsigned int status;
};

enum {
	SMD_TO_CM4 = 1,
	SMD_TO_ADSP = 2,
};

struct clk_smd {
	struct device *dev;
	struct mutex lock;
	struct bridge_channel *ch;
	wait_queue_head_t wait;
	atomic_t ack;
	struct clk_smd_msg snd;
	struct clk_smd_msg rcv;
	unsigned int initialized;
};

static struct clk_smd *smd;

int clk_smd_send_meassage(unsigned int cmd, unsigned int id, unsigned long *data)
{
	int ret = 0;
	struct clk_smd_msg *msg = &smd->snd;
	struct clk_smd_msg *ack = &smd->rcv;

	msg->seq++;
	msg->id = id;
	msg->frequency = *data;
	msg->action = cmd;
	msg->status = 0;

	reinit_completion(&cmd_complete);
#ifdef CLK_SMD_DEBUG
	pr_debug("%s:msg.seq(%d)),id(0x%x),frequency(%ld),action(0x%x),status(%d)\n",
			__func__,
			msg->seq, msg->id, msg->frequency,
			msg->action, msg->status);
#endif

	ret = bridge_write(smd->ch, (const char *)msg, sizeof(*msg));

	while (1) {
		ret = wait_for_completion_timeout(&cmd_complete,
				msecs_to_jiffies(CLK_SMD_TIMEOUT));
		if (ret == 0) {
			pr_err("%s:wait cm4 ack timeout, break out\n", __func__);
			return -EINVAL;
		}

		ret = bridge_read(smd->ch, (char *)ack, sizeof(*ack));

		if ((ret == sizeof(*ack)) && (ack->seq == msg->seq)
			&& (ack->id == msg->id))
			break;
	}

	*data = ack->frequency;
	if (ack->status != 0)
		return -EINVAL;

#ifdef CLK_SMD_DEBUG
	pr_debug("%s:ack.seq(%d)),id(0x%x),frequency(%lld),action(0x%x),status(%d)\n",
			__func__,
			ack->seq, ack->id, ack->frequency,
			ack->action, ack->status);
#endif

	return 0;
}

void clk_smd_notify(void *priv, unsigned int flags)
{
	if (flags == BG_EV_RX)
		complete(&cmd_complete);
}

int clk_smd_probe(struct platform_device *pdev)
{
	int probe_status = 0;

	if (smd && smd->initialized)
		return 0;

	smd = devm_kzalloc(&pdev->dev, sizeof(*smd), GFP_KERNEL);
	if (!smd) {
		probe_status = -ENOMEM;
		return probe_status;
	}

	probe_status = bridge_name_open("CLK", 1, &smd->ch,
					smd, clk_smd_notify);

	if (probe_status) {
		dev_err(&pdev->dev, "no bridge channel(%d)\n", probe_status);
		goto free;
	}

	smd->dev = &pdev->dev;
	mutex_init(&smd->lock);
	init_completion(&cmd_complete);
	dev_set_drvdata(&pdev->dev, smd);

#ifdef CLK_SMD_LOOP
	pr_debug("%s:before bus_smd_send_meassage\n", __func__);
	bus_smd_send_meassage(0, 1, 2);
	pr_debug("%s:after bus_smd_send_meassage\n", __func__);
#endif
	smd->initialized = 1;
	dev_info(&pdev->dev, "clk smd channel is ready\n");

	return 0;
free:
	devm_kfree(&pdev->dev, smd);

	return probe_status;
}
MODULE_LICENSE("GPL");
