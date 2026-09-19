// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.	4
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
#include <linux/module.h>
#include <linux/string.h>
#include <drm/jlq_display_cmdline.h>

#define MAX_PANEL_NAME_LEN	32
#define DISPLAY_INIT_TAG	"display_init="
#define PANEL_NAME_DEFAULT	"panel_default"

static int display_init_status;
static char display_panel_name[MAX_PANEL_NAME_LEN];

static int __init jlq_display_cmdline_setup(char *s)
{
	char *option;
	long buf;
	u8 str_size;

	pr_info("%s, Panel cfg: %s\n", __func__, s);
	option = strchr(s, ',');

	str_size = (option - s);
	strncpy(display_panel_name, s, str_size);
	display_panel_name[str_size] = 0;

	option++;
	display_init_status = 0;
	if (strncmp(option, DISPLAY_INIT_TAG, strlen(DISPLAY_INIT_TAG))) {
		pr_info("display init not specified\n");
		return 0;
	}

	option += strlen(DISPLAY_INIT_TAG);
	if (!kstrtoul(option, 10, &buf))
		display_init_status = buf;

	return 1;
}
__setup("panel_cfg=", jlq_display_cmdline_setup);

int jlq_get_display_init_status(void)
{
	return display_init_status;
}
EXPORT_SYMBOL(jlq_get_display_init_status);

int jlq_get_display_panel_name(char *panel_name)
{
	u8 str_size;

	if (!panel_name)
		return -EINVAL;

	str_size = strlen(display_panel_name);
	if (str_size)
		strncpy(panel_name, display_panel_name, str_size + 1);
	else {
		str_size = strlen(PANEL_NAME_DEFAULT);
		strncpy(panel_name, PANEL_NAME_DEFAULT, str_size + 1);
	}

	return 0;
}
EXPORT_SYMBOL(jlq_get_display_panel_name);

MODULE_DESCRIPTION("JLQ DISPLAY CMDLINE");
MODULE_LICENSE("GPL v2");
