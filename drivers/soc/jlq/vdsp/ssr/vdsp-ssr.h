/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _VDSP_SSR_H
#define _VDSP_SSR_H

#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include "../vdsp-hw-vp6.h"
#include "vdsp_subsystem_restart.h"

struct vdsp_ssr_info {
	struct vdsp_subsys_desc subsys_desc;
	struct vdsp_subsys_device *subsys_dev;
	struct mutex mutex;
};

int vdsp_ssr_set_status(struct vdsp_ssr_info *ssr_info, enum subsys_state state);
int vdsp_ssr_init(struct platform_device *pdev,
			struct vdsp_ssr_info **ssr_info,
			struct vdsp_extern_module *module, struct vdsp_mem_t *mem_info);
int vdsp_ssr_uninit(struct vdsp_ssr_info *ssr_info);

#endif /* _VDSP_SSR_H */
