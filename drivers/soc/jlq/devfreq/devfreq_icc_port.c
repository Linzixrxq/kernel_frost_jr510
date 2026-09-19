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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/devfreq.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <trace/events/power.h>
#include <linux/platform_device.h>
#include <linux/interconnect.h>
#include "devfreq_icc_port.h"

static int icc_target_setfreq(struct device *dev, u32 new_ib, u32 new_ab)
{
	struct dev_data *d = dev_get_drvdata(dev);
	int ret = 0;
	u32 icc_ib = new_ib, icc_ab = new_ab;

	if (d->cur_ib == new_ib && d->cur_ab == new_ab)
		return 0;

	icc_ib = MBPS_TO_HZ(new_ib, d->width);
	icc_ab = MBPS_TO_HZ(new_ab, d->width);

	ret = icc_set_bw(d->icc_path, icc_ab, icc_ib);
	if (ret < 0) {
		dev_err(dev, "icc set bandwidth request failed (%d)\n", ret);
	} else {
		dev_info(dev, "icc set bandwidth: ICC BW: AB: %u IB: %u\n", icc_ab, icc_ib);
		d->cur_ib = new_ib;
		d->cur_ab = new_ab;
	}

	return ret;
}

static int icc_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct dev_data *d = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (!IS_ERR(opp))
		dev_pm_opp_put(opp);

	return icc_target_setfreq(dev, *freq, d->gov_ab);
}

static int icc_get_dev_status(struct device *dev,
				struct devfreq_dev_status *stat)
{
	struct dev_data *d = dev_get_drvdata(dev);

	stat->private_data = &d->gov_ab;
	return 0;
}

int devfreq_add_icc(struct device *dev)
{
	struct dev_data *d;
	struct devfreq_dev_profile *p;
	const char *gov_name;
	int ret;

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	dev_set_drvdata(dev, d);

	d->spec = (const char **)of_device_get_match_data(dev);
	if (!d->spec) {
		dev_err(dev, "Unknown device type!\n");
		return -ENODEV;
	}

	p = &d->dp;
	p->polling_ms  = 2000;
	p->initial_freq = 25600;
	p->target = icc_target;
	p->get_dev_status = icc_get_dev_status;
	d->width = 32;

	//opp
	ret = dev_pm_opp_of_add_table(dev);
	if (ret < 0)
		dev_err(dev, "Couldn't parse OPP table:%d\n", ret);

	//icc
	d->icc_path = of_icc_get(dev, NULL);
	if (IS_ERR(d->icc_path)) {
		ret = PTR_ERR(d->icc_path);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to register icc path: %d\n", ret);
		return ret;
	}
	if (of_property_read_bool(dev->of_node, PROP_ACTIVE))
		icc_set_tag(d->icc_path, ACTIVE_ONLY_TAG);

	//gov
	if (of_property_read_string(dev->of_node, "governor", &gov_name))
		gov_name = "gov-busmon-bw";

	d->df = devfreq_add_device(dev, p, gov_name, NULL);
	if (IS_ERR(d->df)) {
		icc_put(d->icc_path);
		return PTR_ERR(d->df);
	} else
		dev_info(dev, "%s: set devfreq dev %s success@~\n", __func__, *(d->spec));

	return 0;
}

int devfreq_remove_icc(struct device *dev)
{
	struct dev_data *data = dev_get_drvdata(dev);

	icc_put(data->icc_path);
	devfreq_remove_device(data->df);
	kfree(data);

	return 0;
}

int devfreq_suspend_icc(struct device *dev)
{
	struct dev_data *d = dev_get_drvdata(dev);

	return devfreq_suspend_device(d->df);
}

int devfreq_resume_icc(struct device *dev)
{
	struct dev_data *d = dev_get_drvdata(dev);

	return devfreq_resume_device(d->df);
}

static int devfreq_icc_probe(struct platform_device *pdev)
{
	return devfreq_add_icc(&pdev->dev);
}

static int devfreq_icc_remove(struct platform_device *pdev)
{
	return devfreq_remove_icc(&pdev->dev);
}

static const struct df_icc_spec spec1[] = {
	[0] = { ddr_devfreq_port0 },
	[1] = { ddr_devfreq_port1 },
	[2] = { ddr_devfreq_port2 },
	[3] = { ddr_devfreq_port3 },
	[4] = { ddr_devfreq_port4 },
};

static const char *const spec2[] = {
		"icc_ddr_port0",
		"icc_ddr_port1",
		"icc_ddr_port2",
		"icc_ddr_port3",
		"icc_ddr_port4"
};

static const struct of_device_id devfreq_icc_match_table[] = {
	{ .compatible = "jlq,ddr-devfreq-port0-nc", .data = &spec2[0] },
	{ .compatible = "jlq,ddr-devfreq-port1-nc", .data = &spec2[1] },
	{ .compatible = "jlq,ddr-devfreq-port2-nc", .data = &spec2[2] },
	{ .compatible = "jlq,ddr-devfreq-port3-nc", .data = &spec2[3] },
	{ .compatible = "jlq,ddr-devfreq-port4-nc", .data = &spec2[4] },
	{}
};

static struct platform_driver devfreq_icc_driver = {
	.probe  = devfreq_icc_probe,
	.remove = devfreq_icc_remove,
	.driver = {
		.name = "devfreq-icc-port",
		.of_match_table = devfreq_icc_match_table,
		.suppress_bind_attrs = true,
	},
};

static int __init devfreq_icc_init(void)
{
	int ret;

	ret = platform_driver_register(&devfreq_icc_driver);
	if (ret)
		pr_err("devfreq_icc register failed %d\n", ret);
	return ret;
}
late_initcall(devfreq_icc_init);

static __exit void devfreq_icc_exit(void)
{
	platform_driver_unregister(&devfreq_icc_driver);
}
module_exit(devfreq_icc_exit);

MODULE_DESCRIPTION("DDR controller ports devfreq devices driver");
MODULE_LICENSE("GPL");
