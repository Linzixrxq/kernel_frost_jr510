// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 */

#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include "cpufreq-dt.h"

/*
 * Machines for which the cpufreq device is *always* created, mostly used for
 * platforms using "operating-points" (V1) property.
 */
static const struct of_device_id whitelist[] __initconst = {
	{ .compatible = "allwinner,sun4i-a10", },
	{ .compatible = "allwinner,sun5i-a10s", },
	{ .compatible = "allwinner,sun5i-a13", },
	{ .compatible = "allwinner,sun5i-r8", },
	{ .compatible = "allwinner,sun6i-a31", },
	{ .compatible = "allwinner,sun6i-a31s", },
	{ .compatible = "allwinner,sun7i-a20", },
	{ .compatible = "allwinner,sun8i-a23", },
	{ .compatible = "allwinner,sun8i-a83t", },
	{ .compatible = "allwinner,sun8i-h3", },

	{ .compatible = "apm,xgene-shadowcat", },

	{ .compatible = "arm,integrator-ap", },
	{ .compatible = "arm,integrator-cp", },

	{ .compatible = "hisilicon,hi3660", },

	{ .compatible = "fsl,imx27", },
	{ .compatible = "fsl,imx51", },
	{ .compatible = "fsl,imx53", },

	{ .compatible = "marvell,berlin", },
	{ .compatible = "marvell,pxa250", },
	{ .compatible = "marvell,pxa270", },

	{ .compatible = "samsung,exynos3250", },
	{ .compatible = "samsung,exynos4210", },
	{ .compatible = "samsung,exynos5250", },
#ifndef CONFIG_BL_SWITCHER
	{ .compatible = "samsung,exynos5800", },
#endif

	{ .compatible = "renesas,emev2", },
	{ .compatible = "renesas,r7s72100", },
	{ .compatible = "renesas,r8a73a4", },
	{ .compatible = "renesas,r8a7740", },
	{ .compatible = "renesas,r8a7743", },
	{ .compatible = "renesas,r8a7744", },
	{ .compatible = "renesas,r8a7745", },
	{ .compatible = "renesas,r8a7778", },
	{ .compatible = "renesas,r8a7779", },
	{ .compatible = "renesas,r8a7790", },
	{ .compatible = "renesas,r8a7791", },
	{ .compatible = "renesas,r8a7792", },
	{ .compatible = "renesas,r8a7793", },
	{ .compatible = "renesas,r8a7794", },
	{ .compatible = "renesas,sh73a0", },

	{ .compatible = "rockchip,rk2928", },
	{ .compatible = "rockchip,rk3036", },
	{ .compatible = "rockchip,rk3066a", },
	{ .compatible = "rockchip,rk3066b", },
	{ .compatible = "rockchip,rk3188", },
	{ .compatible = "rockchip,rk3228", },
	{ .compatible = "rockchip,rk3288", },
	{ .compatible = "rockchip,rk3328", },
	{ .compatible = "rockchip,rk3366", },
	{ .compatible = "rockchip,rk3368", },
	{ .compatible = "rockchip,rk3399",
	  .data = &(struct cpufreq_dt_platform_data)
		{ .have_governor_per_policy = true, },
	},

	{ .compatible = "st-ericsson,u8500", },
	{ .compatible = "st-ericsson,u8540", },
	{ .compatible = "st-ericsson,u9500", },
	{ .compatible = "st-ericsson,u9540", },

	{ .compatible = "ti,omap2", },
	{ .compatible = "ti,omap3", },
	{ .compatible = "ti,omap4", },
	{ .compatible = "ti,omap5", },

	{ .compatible = "xlnx,zynq-7000", },
	{ .compatible = "xlnx,zynqmp", },

	{ }
};

static bool __init cpu0_node_has_opp_v2_prop(void)
{
	struct device *cpu_dev;
	struct device_node *np;
	bool ret = false;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev)
		np = of_get_cpu_node(0, NULL);
	else
		np = of_node_get(cpu_dev->of_node);

	if (of_get_property(np, "operating-points-v2", NULL))
		ret = true;

	of_node_put(np);
	return ret;
}

static struct platform_device cpufreq_dt_pdev = {
	.name		= "cpufreq-dt-jlq",
};

static int __init cpufreq_dt_platdev_init(void)
{
	struct device_node *np = of_find_node_by_path("/");
	const struct of_device_id *match;
	const void *data = NULL;

	if (!np)
		return -ENODEV;

	match = of_match_node(whitelist, np);
	if (match) {
		data = match->data;
		goto create_pdev;
	}

	if (cpu0_node_has_opp_v2_prop())
		goto create_pdev;
	of_node_put(np);

	return -ENODEV;

create_pdev:
	of_node_put(np);
	return platform_device_register(&cpufreq_dt_pdev);
}

static void __exit cpufreq_dt_platdev_exit(void)
{
}

module_init(cpufreq_dt_platdev_init);
module_exit(cpufreq_dt_platdev_exit);

MODULE_LICENSE("GPL");
