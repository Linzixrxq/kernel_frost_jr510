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

#ifndef _DEVFREQ_ICC_PORT_H
#define _DEVFREQ_ICC_PORT_H

#include <linux/devfreq.h>
#include <linux/interconnect.h>

/* Has to be ULL to prevent overflow where this macro is used. */
#define MBYTE (1ULL << 20)
#define HZ_TO_MBPS(hz, w)	(mult_frac(w, hz, MBYTE))
#define MBPS_TO_HZ(mbps, w)	(mult_frac(mbps, MBYTE, w))
#define PROP_ACTIVE	"jlq,active-only"
#define ACTIVE_ONLY_TAG	0x3

enum dev_type {
	ddr_devfreq_port0,
	ddr_devfreq_port1,
	ddr_devfreq_port2,
	ddr_devfreq_port3,
	ddr_devfreq_port4
};

struct df_icc_spec {
	enum dev_type type;
};

struct dev_data {
	struct icc_path *icc_path;
	u32				cur_ab;
	u32				cur_ib;
	unsigned long	gov_ab;
	const char		**spec;
	unsigned int	width;
	struct devfreq	*df;
	struct devfreq_dev_profile	dp;
};

#ifdef CONFIG_PM_DVFS_ICC_PORT
	int devfreq_add_icc(struct device *dev);
	int devfreq_remove_icc(struct device *dev);
	int devfreq_suspend_icc(struct device *dev);
	int devfreq_resume_icc(struct device *dev);
#else
	static inline int devfreq_add_icc(struct device *dev)			{	return 0; }
	static inline int devfreq_remove_icc(struct device *dev)		{	return 0; }
	static inline int devfreq_suspend_icc(struct device *dev)		{	return 0; }
	static inline int devfreq_resume_icc(struct device *dev)		{	return 0; }
#endif

#endif /* _DEVFREQ_ICC_PORT_H */
