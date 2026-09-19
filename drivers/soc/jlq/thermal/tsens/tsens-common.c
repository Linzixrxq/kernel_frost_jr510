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

#include <linux/err.h>
#include <linux/io.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include "tsens.h"
#ifdef CONFIG_ARCH_JA310
#include <jlq/ja310/jlq_sip.h>
#else
#include <soc/jlq/jlq_sip.h>
#endif

static int adc_to_temp(struct tsens_device *tmdev, int id, u32 adc_code)
{
	return (adc_code * tmdev->tsens_slope_numerator /
		tmdev->tsens_slope_denominator -
		tmdev->sensor[id].offset) * 1000;
}

static u32 temp_to_adc(struct tsens_device *tmdev, int id, int temp)
{
	return ((temp / 1000 + tmdev->sensor[id].offset) *
		tmdev->tsens_slope_denominator /
		tmdev->tsens_slope_numerator);
}

int get_temp_common(struct tsens_device *tmdev, int id, int *temp)
{
	struct tsens_sensor *s = &tmdev->sensor[id];
	struct pvt_reg *reg = tmdev->tsens_addr;
	u32 adc_code;

	adc_code = readl_relaxed(&reg->pvt_temp_value[s->hw_id]);
	*temp = adc_to_temp(tmdev, id, adc_code);

	return 0;
}

static int disable_trip_irq_common(struct tsens_device *tmdev, int hw_id)
{
	struct pvt_reg *reg = tmdev->tsens_addr;
	u32 val;

	val = readl_relaxed(&reg->pvt_temp_mon_intr_en);
	val &= ~(3 << hw_id * 2);
	writel_relaxed(val, &reg->pvt_temp_mon_intr_en);

	return 0;
}

int enable_trip_irq_common(struct tsens_device *tmdev, int hw_id)
{
	struct pvt_reg *reg = tmdev->tsens_addr;
	u32 val;

	val = readl_relaxed(&reg->pvt_temp_mon_intr_en);
	val |= 3 << hw_id * 2;
	writel_relaxed(val, &reg->pvt_temp_mon_intr_en);

	return 0;
}

void disable_pvt_common(struct tsens_device *tmdev)
{
	struct pvt_reg *reg = tmdev->tsens_addr;

	writel_relaxed(0, &reg->pvt_temp_mon_intr_en);
	writel_relaxed(0x279, &reg->pvt_ctrl);
}

int set_trips_common(struct tsens_device *tmdev, int id, int low, int high)
{
	struct tsens_sensor *s = &tmdev->sensor[id];
	struct pvt_reg *reg = tmdev->tsens_addr;
	u32 val;
	u32 tmp_low;
	u32 tmp_high;

	disable_trip_irq_common(tmdev, s->hw_id);

	tmp_low = temp_to_adc(tmdev, id, low);
	tmp_high = temp_to_adc(tmdev, id, high);

	val = LOW_TEMP_THRES(tmp_low) | HIGH_TEMP_THRES(tmp_high);
	writel_relaxed(val, &reg->pvt_thres_cfg[s->hw_id]);

	enable_trip_irq_common(tmdev, s->hw_id);

	return 0;
}
/*  code / 16 + (25 - PVT_OPT25/ 16)*/
#define TSENS_OTP_TARGET		(0x63c)
#define TSENS_OTP_SENSOR_ERR		(0x30)
#define PVT_CALIBRATE_VALID(code)	\
	(abs(code - TSENS_OTP_TARGET) < TSENS_OTP_SENSOR_ERR)
#define OFF_SET_OF_PVT0 0x20
#define OFF_SET_OF_PVT1 0x28

int tsens_calibrate_common(struct tsens_device *tmdev)
{
	u32 i, calibrate_code;

	if (!tmdev)
		return -ENODEV;

	tmdev->pvt_calibrate[0] = jlq_sip_call(JLQ_SIP_CALL_FN_ID, OFF_SET_OF_PVT0, 0);
	tmdev->pvt_calibrate[1] = jlq_sip_call(JLQ_SIP_CALL_FN_ID, OFF_SET_OF_PVT1, 0);

	for (i = 0; (i < tmdev->num_sensors) && (i < 8); i++) {
		if (i < 4) {
			calibrate_code = (tmdev->pvt_calibrate[0] >>
					  (i * 16)) & 0xfff;
		} else {
			calibrate_code = (tmdev->pvt_calibrate[1] >>
					  ((i - 4) * 16)) & 0xfff;
		}
		if (!PVT_CALIBRATE_VALID(calibrate_code)) {
			pr_debug("calib value invalid, use default\n");
			calibrate_code = TSENS_OTP_TARGET;
		}

		tmdev->sensor[i].offset = calibrate_code / 16 - 25;
		pr_debug("sensor%d offset 0x%x\n",
			 i, tmdev->sensor[i].offset);
	}

	return 0;
}

int init_common(struct tsens_device *tmdev)
{
	struct pvt_reg *reg = tmdev->tsens_addr;
	u32 val;

	val = PVT_EN | INI_CNT_VAL(0x80);
	writel_relaxed(val, &reg->pvt_ctrl);
	/* disable irq*/
	writel_relaxed(0, &reg->pvt_temp_mon_intr_en);
	/* clear irq status*/
	writel_relaxed(~0UL, &reg->pvt_temp_mon_intr);

	return 0;
}

int suspend_common(struct tsens_device *tmdev)
{
	disable_pvt_common(tmdev);

	return 0;
}

int resume_common(struct tsens_device *tmdev)
{
	return init_common(tmdev);
}
