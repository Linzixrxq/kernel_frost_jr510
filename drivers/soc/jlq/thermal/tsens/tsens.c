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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include "tsens.h"
#include <linux/kernel.h>
#include <linux/notifier.h>

static int tsens_get_temp(void *data, int *temp)
{
	const struct tsens_sensor *s = data;
	struct tsens_device *tmdev = s->tmdev;
	int ret = -EOPNOTSUPP;

	if (tmdev->ops->get_temp)
		ret = tmdev->ops->get_temp(tmdev, s->id, temp);

	dev_dbg(&tmdev->pdev->dev, "sensor %d - temp: %d, ret: %d\n",
		s->id, *temp, ret);

	return ret;
}

static int tsens_set_trips(void *data, int low, int high)
{
	const struct tsens_sensor *s = data;
	struct tsens_device *tmdev = s->tmdev;
	int ret = -EOPNOTSUPP;

	low = clamp_val(low, -40000, 125000);
	high = clamp_val(high, -40000, 125000);

	if (tmdev->ops->set_trips)
		ret = tmdev->ops->set_trips(tmdev, s->id, low, high);

	dev_dbg(&tmdev->pdev->dev, "%s: sensor %d: low: %d, high %d\n",
		__func__, s->id, low, high);

	return ret;
}

static int  __maybe_unused tsens_suspend(struct device *dev)
{
	struct tsens_device *tmdev = dev_get_drvdata(dev);

	if (tmdev->ops && tmdev->ops->suspend)
		return tmdev->ops->suspend(tmdev);

	return 0;
}

static int __maybe_unused tsens_resume(struct device *dev)
{
	struct tsens_device *tmdev = dev_get_drvdata(dev);

	if (tmdev->ops && tmdev->ops->resume)
		return tmdev->ops->resume(tmdev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tsens_pm_ops, tsens_suspend, tsens_resume);

static const struct of_device_id tsens_table[] = {
	{
		.compatible = "jlq,ja310-tsens",
		.data = &data_ja310,
	},
	{
		.compatible = "jlq,jr510-tsens",
		.data = &data_jr510,
	},
	{}
};
MODULE_DEVICE_TABLE(of, tsens_table);

static const struct thermal_zone_of_device_ops tsens_of_ops = {
	.get_temp = tsens_get_temp,
	.set_trips = tsens_set_trips,
};

static int tsens_register(struct tsens_device *tmdev)
{
	int i;
	struct thermal_zone_device *tzd = NULL;

	for (i = 0;  i < tmdev->num_sensors; i++) {
		tmdev->sensor[i].tmdev = tmdev;
		tmdev->sensor[i].id = i;

		tzd = devm_thermal_zone_of_sensor_register(&tmdev->pdev->dev, i,
							   &tmdev->sensor[i],
							   &tsens_of_ops);

		if (IS_ERR(tzd))
			continue;
		tmdev->sensor[i].tzd = tzd;
	}
	return 0;
}
static irqreturn_t tsens_isr_thread_fn(int irq, void *private)

{
	struct tsens_device *tmdev = private;
	struct pvt_reg *reg = tmdev->tsens_addr;
	u32 status;
	int i;

	mutex_lock(&tmdev->irq_lock);
	status = readl_relaxed(&reg->pvt_temp_mon_intr);

	dev_crit(&tmdev->pdev->dev, "trip intr reg = 0x%x\n", status);

	for (i = 0;  i < tmdev->num_sensors; i++) {
		if (!tmdev->sensor[i].tzd)
			continue;

		if (status & (3 << tmdev->sensor[i].hw_id * 2)) {
			thermal_zone_device_update(tmdev->sensor[i].tzd,
						THERMAL_EVENT_UNSPECIFIED);
		}
	}
	writel_relaxed(status, &reg->pvt_temp_mon_intr);
	mutex_unlock(&tmdev->irq_lock);

	return IRQ_HANDLED;
}

static irqreturn_t tsens_overheat_fn(int irq, void *private)
{
	struct tsens_device *tmdev = private;
	int i, temp;
	char *sensor_name = "null";

	if (tmdev->ops && tmdev->ops->get_temp) {
		for (i = 0; i < tmdev->num_sensors; i++) {
			tmdev->ops->get_temp(tmdev, i, &temp);
			if (tmdev->ops && tmdev->ops->get_tsens_name)
				sensor_name = tmdev->ops->get_tsens_name(i);
			dev_err(&tmdev->pdev->dev, "tsen[%d]_%s temp=%d\n", i, sensor_name, temp);
		}
	}
	panic("qtang thermal overheat\n");
	return IRQ_HANDLED;
}

static int dump_panic_temp(struct notifier_block *self, unsigned long v,
			   void *p)
{
	struct tsens_device *tmdev;
	char *sensor_name = "null";
	int i, temp;

	tmdev = container_of(self, struct tsens_device, panic_notifier);

	if (tmdev->ops && tmdev->ops->get_temp) {
		for (i = 0;  i < tmdev->num_sensors; i++) {
			tmdev->ops->get_temp(tmdev, i, &temp);
			if (tmdev->ops->get_tsens_name)
				sensor_name = tmdev->ops->get_tsens_name(i);
			dev_err(&tmdev->pdev->dev, "tsen[%d]_%s temp=%d\n", i, sensor_name, temp);
		}
	}

	return NOTIFY_DONE;
}

static int tsens_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct tsens_sensor *s;
	struct tsens_device *tmdev;
	const struct tsens_data *data;
	const struct of_device_id *id;
	struct resource *tsens_mem;
	u32 tsens_num_sensors;
	u32 *hw_id;
	int ret, i;

	id = of_match_node(tsens_table, np);
	if (id)
		data = id->data;
	else
		data = &data_jr510;

	ret = of_property_read_u32(np,
			"jlq,sensors", &tsens_num_sensors);
	if (ret) {
		dev_err(&pdev->dev, "missing sensor number\n");
		return -ENODEV;
	}

	if (tsens_num_sensors <= 0) {
		dev_err(dev, "invalid number of sensors\n");
		return -EINVAL;
	}

	tmdev = devm_kzalloc(dev, sizeof(*tmdev) +
			     tsens_num_sensors * sizeof(*s), GFP_KERNEL);
	if (!tmdev)
		return -ENOMEM;

	tmdev->pdev = pdev;
	tmdev->num_sensors = tsens_num_sensors;
	tmdev->ops = data->ops;

	hw_id = devm_kcalloc(dev, tsens_num_sensors, sizeof(u32), GFP_KERNEL);
	if (!hw_id)
		return -ENOMEM;

	ret = of_property_read_u32(np,
		"jlq,tsens-slope_numerator", &tmdev->tsens_slope_numerator);
	if (ret) {
		pr_info("tsens slope numerator not defined, use default\n");
		tmdev->tsens_slope_numerator = 1;
	}
	ret = of_property_read_u32(np,
		"jlq,tsens-slope_denominator", &tmdev->tsens_slope_denominator);
	if (ret) {
		pr_info("tsens slope denoinator not defined, use default\n");
		tmdev->tsens_slope_denominator = 16;
	}

	ret = of_property_read_u32_array(np,
		"jlq,sensor-id", hw_id, tsens_num_sensors);

	if (ret) {
		pr_info("Default sensor id mapping\n");
		for (i = 0; i < tsens_num_sensors; i++)
			tmdev->sensor[i].hw_id = i;
	} else {
		pr_info("Use specified sensor id mapping\n");
		for (i = 0; i < tsens_num_sensors; i++)
			tmdev->sensor[i].hw_id = hw_id[i];
	}

	/* TSENS register region */
	tsens_mem = platform_get_resource_byname(tmdev->pdev,
					IORESOURCE_MEM, "tsens_base");
	if (!tsens_mem) {
		pr_err("Could not get tsens physical address resource\n");
		return -EINVAL;
	}

	tmdev->tsens_addr = devm_ioremap_resource(&tmdev->pdev->dev, tsens_mem);
	if (IS_ERR(tmdev->tsens_addr)) {
		pr_err("Failed to IO map TSENS registers.\n");
		return -ENOMEM;
	}

	if (!tmdev->ops || !tmdev->ops->init || !tmdev->ops->get_temp)
		return -EINVAL;

	if (tmdev->ops->calibrate) {
		ret = tmdev->ops->calibrate(tmdev);
		if (ret < 0) {
			dev_err(dev, "tsens calibration failed\n");
			return ret;
		}
	}

	ret = tmdev->ops->init(tmdev);
	if (ret < 0) {
		dev_err(dev, "tsens init failed\n");
		return ret;
	}

	tmdev->tsens_irq = platform_get_irq_byname(pdev, "tsens-upper-lower");

	ret = devm_request_threaded_irq(dev, tmdev->tsens_irq, NULL,
					tsens_isr_thread_fn,
					IRQF_ONESHOT, "tsens_interrupt", tmdev);
	if (!ret)
		enable_irq_wake(tmdev->tsens_irq);
	else
		return ret;

	tmdev->tsens_overheat_irq = platform_get_irq_byname(pdev, "overheat");
	if (tmdev->tsens_overheat_irq > 0) {
		ret = devm_request_threaded_irq(dev, tmdev->tsens_overheat_irq,
			tsens_overheat_fn, NULL, IRQF_ONESHOT,
			"overheat_interrupt", tmdev);
		if (!ret)
			enable_irq_wake(tmdev->tsens_overheat_irq);
		else
			return ret;
	}

	ret = tsens_register(tmdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, tmdev);
	tmdev->panic_notifier.notifier_call = dump_panic_temp;

	atomic_notifier_chain_register(&panic_notifier_list,
					   &tmdev->panic_notifier);

	return ret;
}

static int tsens_remove(struct platform_device *pdev)
{
	struct tsens_device *tmdev = platform_get_drvdata(pdev);

	if (tmdev->ops->disable)
		tmdev->ops->disable(tmdev);

	return 0;
}

#define TSENS_DRIVER_NAME		"jlq-tsens"
static struct platform_driver tsens_driver = {
	.probe = tsens_probe,
	.remove = tsens_remove,
	.driver = {
		.name = TSENS_DRIVER_NAME,
		.pm	= &tsens_pm_ops,
		.of_match_table = tsens_table,
	},
};
module_platform_driver(tsens_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("JLQ Temperature Sensor driver");
MODULE_ALIAS("platform:jlq-tsens");
