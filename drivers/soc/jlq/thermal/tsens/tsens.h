/* SPDX-License-Identifier: GPL-2.0+
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

#ifndef __JLQ_TSENS_H__
#define __JLQ_TSENS_H__

#include <linux/thermal.h>
#include <linux/io.h>

struct tsens_device;

struct tsens_sensor {
	struct tsens_device		*tmdev;
	struct thermal_zone_device	*tzd;
	int offset;
	int id;
	int hw_id;
};

/**
 * struct tsens_ops - operations as supported by the tsens device
 * @init: Function to initialize the tsens device
 * @calibrate: Function to calibrate the tsens device
 * @get_temp: Function which returns the temp in millidegC
 * @enable: Function to enable (clocks/power) tsens device
 * @disable: Function to disable the tsens device
 * @suspend: Function to suspend the tsens device
 * @resume: Function to resume the tsens device
 * @get_trend: Function to get the thermal/temp trend
 */
struct tsens_ops {
	/* mandatory callbacks */
	int (*init)(struct tsens_device *tmdev);
	int (*calibrate)(struct tsens_device *tmdev);
	int (*get_temp)(struct tsens_device *tmdev, int id, int *temp);
	/* optional callbacks */
	int (*enable)(struct tsens_device *tmdev, int id);
	void (*disable)(struct tsens_device *tmdev);
	int (*suspend)(struct tsens_device *tmdev);
	int (*resume)(struct tsens_device *tmdev);
	int (*get_trend)(struct tsens_device *tmdev, int id, enum thermal_trend *trend);
	int (*set_trips)(struct tsens_device *tmdev, int id, int low, int high);
	char * (*get_tsens_name)(int id);

};

/**
 * struct tsens_data - tsens instance specific data
 * @num_sensors: Max number of sensors supported by platform
 * @ops: operations the tsens instance supports
 * @hw_ids: Subset of sensors ids supported by platform, if not the first n
 */
struct tsens_data {
	const struct tsens_ops *ops;
};

struct tsens_device {
	struct platform_device *pdev;
	u32 num_sensors;
	void __iomem *tsens_addr;
	struct resource *tsens_mem;
	struct notifier_block panic_notifier;
	u32 tsens_slope_numerator;
	u32 tsens_slope_denominator;
	int tsens_irq;
	int tsens_overheat_irq;
	uint64_t pvt_calibrate[2];
	const struct tsens_ops *ops;
	struct mutex irq_lock;
	struct tsens_sensor sensor[0];
};

#define PVT_ADC_PD	BIT(9)
#define PVT_EN	BIT(8)
#define INI_CNT_VAL(x)	(x & GENMASK(7, 0))
#define LOW_TEMP_THRES(x)	((x << 16) & GENMASK(27, 16))
#define HIGH_TEMP_THRES(x)	(x & GENMASK(11, 0))

#define PVT_TEMP5_DN_INTR_X	BIT(11)
#define PVT_TEMP5_UP_INTR_X	BIT(10)
#define PVT_TEMP4_DN_INTR_X	BIT(9)
#define PVT_TEMP4_UP_INTR_X	BIT(8)
#define PVT_TEMP3_DN_INTR_X	BIT(7)
#define PVT_TEMP3_UP_INTR_X	BIT(6)
#define PVT_TEMP2_DN_INTR_X	BIT(5)
#define PVT_TEMP2_UP_INTR_X	BIT(4)
#define PVT_TEMP1_DN_INTR_X	BIT(3)
#define PVT_TEMP1_UP_INTR_X	BIT(2)
#define PVT_TEMP0_DN_INTR_X	BIT(1)
#define PVT_TEMP0_UP_INTR_X	BIT(0)

#define TEMPx_VALUE(x)	(x & GENMASK(11, 0))
#define VOLx_VALUE(x)	(x & GENMASK(11, 0))

struct pvt_reg {
	u32 pvt_ctrl;
	u32 pvt_thres_cfg[6];
	u32 pvt_temp_mon_intr_en;
	u32 pvt_temp_mon_intr;
	u32 pvt_mon_intr_raw;
	u32 pvt_temp_value[6];
	u32 pvt_vol_value[4];
};

int init_common(struct tsens_device *tmdev);
int get_temp_common(struct tsens_device *tmdev, int id, int *temp);
int enable_trip_irq_common(struct tsens_device *tmdev, int id);
void disable_pvt_common(struct tsens_device *tmdev);
int set_trips_common(struct tsens_device *tmdev, int id, int low, int high);
int tsens_calibrate_common(struct tsens_device *tmdev);
int suspend_common(struct tsens_device *tmdev);
int resume_common(struct tsens_device *tmdev);
extern const struct tsens_data data_ja310, data_jr510;

#endif /* __JLQ_TSENS_H__ */
