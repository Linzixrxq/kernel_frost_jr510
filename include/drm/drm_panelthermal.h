/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __DRM_PANELTHERMAL_H__
#define __DRM_PANELTHERMAL_H__

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/notifier.h>

/* A hardware display blank change occurred */
#define DRM_PANEL_EVENT_BLANK		0x01
/* A hardware display blank early change occurred */
#define DRM_PANEL_EARLY_EVENT_BLANK	0x02

/*add for thermal begin*/
#define		DRM_EARLY_EVENT_BLANK   0x01
#define		DRM_EVENT_BLANK         0x02
/*add for thermal end*/

/*add for thermal begin*/
enum {
	DRM_BLANK_UNBLANK = 0,
	DRM_BLANK_LP1,
	DRM_BLANK_LP2,
	DRM_BLANK_STANDBY,
	DRM_BLANK_SUSPEND,
	DRM_BLANK_POWERDOWN,
};
struct drm_notify_data {
	bool is_primary;
	void *data;
};
/*add for thermal end*/

/*add for thermal begin*/
int drm_register_client(struct notifier_block *nb);
int drm_unregister_client(struct notifier_block *nb);
int drm_notifier_call_chain(unsigned long val, void *v);
/*add for thermal end*/

int jlq_panel_notifier_register(struct notifier_block *nb);
int jlq_panel_notifier_unregister(struct notifier_block *nb);
void jlq_thermal_brightness_set(int value);
int jlq_panel_status(void);





#endif
