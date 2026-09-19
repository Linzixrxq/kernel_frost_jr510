// SPDX-License-Identifier: GPL-2.0+
/*
 * JLQ thermal sensor driver
 *
 * Copyright 2018~2019 JLQ Technology Co.,
 * Ltd. or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/platform_device.h>
#include "tsens.h"

static char *tsens_name[] = {
	"top_thermal",
	"gpu_thermal",
	"ddr_thermal",
	"cpu_thermal"
};

static char *get_tsens_name(int id)
{
	if (id < ARRAY_SIZE(tsens_name)) {
		return tsens_name[id];
	} else
		return NULL;

}

static const struct tsens_ops ops_jr510 = {
	.init = init_common,
	.calibrate = tsens_calibrate_common,
	.get_temp = get_temp_common,
	.set_trips = set_trips_common,
	.enable = enable_trip_irq_common,
	.disable = disable_pvt_common,
	.suspend = suspend_common,
	.resume = resume_common,
	.get_tsens_name = get_tsens_name,
};

const struct tsens_data data_jr510 = {
	.ops = &ops_jr510
};
