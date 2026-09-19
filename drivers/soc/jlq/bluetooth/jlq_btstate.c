// SPDX-License-Identifier: GPL-2.0+
/*
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static bool btpower_onoff_status;
bool btpower_get_onoff_status(void)
{
	return btpower_onoff_status;
}
EXPORT_SYMBOL(btpower_get_onoff_status);

void btpower_set_onoff_status(bool status)
{
	btpower_onoff_status = status;
}
EXPORT_SYMBOL(btpower_set_onoff_status);

static int __init jlq_btstate_init(void)
{
	btpower_onoff_status = false;
	return 0;
}


static void __exit jlq_btstate_exit(void)
{
	btpower_onoff_status = false;
}

module_init(jlq_btstate_init);
module_exit(jlq_btstate_exit);

MODULE_DESCRIPTION("JLQ btstate driver");
MODULE_LICENSE("GPL");
