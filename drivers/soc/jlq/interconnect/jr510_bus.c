// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#include <dt-bindings/interconnect/jr510.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_opp.h>
#include <linux/pm_domain.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/ipc_logging.h>
#include <opp.h>
#include "icc-bus.h"

#define VDD_NUM	2

static LIST_HEAD(bus_probe_list);
static DEFINE_MUTEX(probe_list_lock);

static int probe_count;

/*ddr_p3*/
static struct bus_icc_node mas_qtang_modem0 = {
	.name = "mas_modem0",
	.id = MASTER_QTANG_MODEM0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT3 },
};

static struct bus_icc_node slv_ddr_port3 = {
	.name = "slv_ddr_port3",
	.id = SLAVE_DDR_PORT3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct bus_icc_node *bus_p3_nodes[] = {
	[MASTER_QTANG_MODEM0] = &mas_qtang_modem0,
	[SLAVE_DDR_PORT3] = &slv_ddr_port3,
};

static __maybe_unused struct bus_icc_desc bus_p3 = {
	.nodes = bus_p3_nodes,
	.num_nodes = ARRAY_SIZE(bus_p3_nodes),
};

/*ddr_p0*/
static struct bus_icc_node mas_cpu_bus = {
	.name = "mas_cpu_bus",
	.id = MASTER_CPU_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT0 },
};

static struct bus_icc_node slv_ddr_port0 = {
	.name = "slv_ddr_port0",
	.id = SLAVE_DDR_PORT0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct bus_icc_node *bus_p0_nodes[] = {
	[MASTER_CPU_BUS] = &mas_cpu_bus,
	[SLAVE_DDR_PORT0] = &slv_ddr_port0,
};

static __maybe_unused struct bus_icc_desc bus_p0 = {
	.nodes = bus_p0_nodes,
	.num_nodes = ARRAY_SIZE(bus_p0_nodes),
};

/*ddr_bus2*/
static struct bus_icc_node mas_display_bus = {
	.name = "mas_display_bus",
	.id = MASTER_DISPLAY_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT2 },
};

static struct bus_icc_node mas_qtang_camrt_bus = {
	.name = "mas_qtang_camrt_bus",
	.id = MASTER_CAMRT_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT2 },
};

static struct bus_icc_node mas_ap2sw2_bus = {
	.name = "mas_ap2sw2_bus",
	.id = MASTER_AP2SW2_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT2 },
};

static struct bus_icc_node slv_ddr_port2 = {
	.name = "slv_ddr_port2",
	.id = SLAVE_DDR_PORT2,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct bus_icc_node *bus_sw2_nodes[] = {
	[MASTER_DISPLAY_BUS] = &mas_display_bus,
	[MASTER_CAMRT_BUS] = &mas_qtang_camrt_bus,
	[MASTER_AP2SW2_BUS] = &mas_ap2sw2_bus,
	[SLAVE_DDR_PORT2] = &slv_ddr_port2,
};

static struct bus_icc_desc bus_sw2 = {
	.nodes = bus_sw2_nodes,
	.num_nodes = ARRAY_SIZE(bus_sw2_nodes),
};

/*ddr_bus1*/
static struct bus_icc_node mas_gpu_bus = {
	.name = "mas_gpu_bus",
	.id = MASTER_GPU_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT4 },
};

static struct bus_icc_node mas_ai_bus = {
	.name = "mas_ai_bus",
	.id = MASTER_AI_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT4 },
};

static struct bus_icc_node mas_vpu_bus = {
	.name = "mas_vpu_bus",
	.id = MASTER_VPU_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT4 },
};

static struct bus_icc_node mas_qtang_camnrt_bus = {
	.name = "mas_qtang_camnrt_bus",
	.id = MASTER_QTANG_CAMNRT_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT4 },
};

static struct bus_icc_node mas_ap2sw1_bus = {
	.name = "mas_ap2sw1_bus",
	.id = MASTER_AP2SW1_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT4 },
};

static struct bus_icc_node slv_ddr_port4 = {
	.name = "slv_ddr_port4",
	.id = SLAVE_DDR_PORT4,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct bus_icc_node *bus_sw1_nodes[] = {
	[MASTER_GPU_BUS] = &mas_gpu_bus,
	[MASTER_AI_BUS] = &mas_ai_bus,
	[MASTER_VPU_BUS] = &mas_vpu_bus,
	[MASTER_QTANG_CAMNRT_BUS] = &mas_qtang_camnrt_bus,
	[MASTER_AP2SW1_BUS] = &mas_ap2sw1_bus,
	[SLAVE_DDR_PORT4] = &slv_ddr_port4,
};

static struct bus_icc_desc bus_sw1 = {
	.nodes = bus_sw1_nodes,
	.num_nodes = ARRAY_SIZE(bus_sw1_nodes),
};

/*fbe_bus*/
static struct bus_icc_node mas_qtang_ufs_axi_bus = {
	.name = "mas_qtang_ufs_axi_bus",
	.id = MASTER_QTANG_UFS_AXI_BUS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SLAVE_FBE_BUS },
};

static struct bus_icc_node slv_fbe_bus = {
	.name = "slv_fbe_bus",
	.id = SLAVE_FBE_BUS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { MASTER_HBLK_BUS },
};

static struct bus_icc_node *fbe_bus_nodes[] = {
	[MASTER_QTANG_UFS_AXI_BUS] = &mas_qtang_ufs_axi_bus,
	[SLAVE_FBE_BUS] = &slv_fbe_bus,
};

static __maybe_unused struct bus_icc_desc fbe_bus = {
	.nodes = fbe_bus_nodes,
	.num_nodes = ARRAY_SIZE(fbe_bus_nodes),
};

/*top_bus*/
static struct bus_icc_node mas_audio_bus = {
	.name = "mas_audio_bus",
	.id = MASTER_AUDIO_BUS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SLAVE_TOP_BUS },
};

static struct bus_icc_node mas_cm4_bus = {
	.name = "mas_cm4_bus",
	.id = MASTER_CM4_BUS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SLAVE_TOP_BUS },
};

static struct bus_icc_node mas_hblk_bus = {
	.name = "mas_hblk_peri_bus",
	.id = MASTER_HBLK_BUS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SLAVE_TOP_BUS },
};

//TBD
static struct bus_icc_node mas_top_dmas_bus = {
	.name = "mas_top_dmas_bus",
	.id = MASTER_TOP_DMAS_BUS,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SLAVE_TOP_BUS },
};

static struct bus_icc_node slv_top_bus = {
	.name = "slv_top_bus",
	.id = SLAVE_TOP_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { MASTER_TOP_BUS },
};

static struct bus_icc_node *top_bus_nodes[] = {
	[MASTER_AUDIO_BUS] = &mas_audio_bus,
	[MASTER_CM4_BUS] = &mas_cm4_bus,
	[MASTER_HBLK_BUS] = &mas_hblk_bus,
	[MASTER_TOP_DMAS_BUS] = &mas_top_dmas_bus,
	[SLAVE_TOP_BUS] = &slv_top_bus,
};

static struct bus_icc_desc top_bus = {
	.nodes = top_bus_nodes,
	.num_nodes = ARRAY_SIZE(top_bus_nodes),
};

/*qtang_xm1_axi_bus*/
static struct bus_icc_node mas_qtang_xm1_axi_bus = {
	.name = "mas_qtang_xm1_axi_bus",
	.id = MASTER_QTANG_XM1_AXI_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_QTANG_XM1_AXI_BUS },
};

static struct bus_icc_node slv_qtang_xm1_axi_bus = {
	.name = "slv_qtang_xm1_axi_bus",
	.id = SLAVE_QTANG_XM1_AXI_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { MASTER_QTANG_MODEM1_BUS },
};

static struct bus_icc_node *qtang_xm1_bus_nodes[] = {
	[MASTER_QTANG_XM1_AXI_BUS] = &mas_qtang_xm1_axi_bus,
	[SLAVE_QTANG_XM1_AXI_BUS] = &slv_qtang_xm1_axi_bus,
};

static __maybe_unused struct bus_icc_desc qtang_xm1_bus = {
	.nodes = qtang_xm1_bus_nodes,
	.num_nodes = ARRAY_SIZE(qtang_xm1_bus_nodes),
};

/*ddr_bus0*/
static struct bus_icc_node mas_top_bus = {
	.name = "mas_top_bus",
	.id = MASTER_TOP_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT1 },
};

static struct bus_icc_node mas_qtang_modem1_bus = {
	.name = "mas_qtang_modem1_bus",
	.id = MASTER_QTANG_MODEM1_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT1 },
};

static struct bus_icc_node mas_vdsp_bus = {
	.name = "mas_vdsp_bus",
	.id = MASTER_VDSP_BUS,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SLAVE_DDR_PORT1 },
};

static struct bus_icc_node slv_ddr_port1 = {
	.name = "slv_ddr_port1",
	.id = SLAVE_DDR_PORT1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 0,
};

static struct bus_icc_node *bus_sw0_nodes[] = {
	[MASTER_TOP_BUS] = &mas_top_bus,
	[MASTER_QTANG_MODEM1_BUS] = &mas_qtang_modem1_bus,
	[MASTER_VDSP_BUS] = &mas_vdsp_bus,
	[SLAVE_DDR_PORT1] = &slv_ddr_port1,
};

static struct bus_icc_desc bus_sw0 = {
	.nodes = bus_sw0_nodes,
	.num_nodes = ARRAY_SIZE(bus_sw0_nodes),
};

/*qt_sys_noc_rt*/
static struct bus_icc_node mas_qtang_camrt = {
	.name = "mas_qtang_camrt",
	.id = MASTER_QTANG_CAMRT,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SLAVE_QTANG_CAMRT },
};

static struct bus_icc_node slv_qtang_camrt = {
	.name = "slv_qtang_camrt",
	.id = SLAVE_QTANG_CAMRT,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { MASTER_CAMRT_BUS },
};

static struct bus_icc_node *qt_sys_noc_rt_nodes[] = {
	[MASTER_QTANG_CAMRT] = &mas_qtang_camrt,
	[SLAVE_QTANG_CAMRT] = &slv_qtang_camrt,
};

static struct bus_icc_desc qt_sys_noc_rt = {
	.nodes = qt_sys_noc_rt_nodes,
	.num_nodes = ARRAY_SIZE(qt_sys_noc_rt_nodes),
};

/*qt_sys_noc_nrt*/
static struct bus_icc_node mas_qtang_camnrt = {
	.name = "mas_qtang_camnrt",
	.id = MASTER_QTANG_CAMNRT,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SLAVE_QTANG_CAMNRT },
};

static struct bus_icc_node slv_qtang_camnrt = {
	.name = "slv_qtang_camnrt",
	.id = SLAVE_QTANG_CAMNRT,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { MASTER_QTANG_CAMNRT_BUS },
};

static struct bus_icc_node *qt_sys_noc_nrt_nodes[] = {
	[MASTER_QTANG_CAMNRT] = &mas_qtang_camnrt,
	[SLAVE_QTANG_CAMNRT] = &slv_qtang_camnrt,
};

static struct bus_icc_desc qt_sys_noc_nrt = {
	.nodes = qt_sys_noc_nrt_nodes,
	.num_nodes = ARRAY_SIZE(qt_sys_noc_nrt_nodes),
};

/*qt_sys_noc*/
static struct bus_icc_node mas_sys_noc_camnrt = {
	.name = "mas_sys_noc_camnrt",
	.id = MASTER_SYS_NOC_CAMNRT,
	.channels = 1,
	.buswidth = 64,
	.num_links = 1,
	.links = { SLAVE_SYS_NOC_CAMNRT },
};

static struct bus_icc_node slv_sys_noc_camnrt = {
	.name = "slv_sys_noc_camnrt",
	.id = SLAVE_SYS_NOC_CAMNRT,
	.channels = 1,
	.buswidth = 64,
	.num_links = 0,
};

static struct bus_icc_node *qt_sys_noc_nodes[] = {
	[MASTER_SYS_NOC_CAMNRT] = &mas_sys_noc_camnrt,
	[SLAVE_SYS_NOC_CAMNRT] = &slv_sys_noc_camnrt,
};

static __maybe_unused struct bus_icc_desc qt_sys_noc = {
	.nodes = qt_sys_noc_nodes,
	.num_nodes = ARRAY_SIZE(qt_sys_noc_nodes),
};

/*qt_snoc*/
static struct bus_icc_node mas_snoc_ipa = {
	.name = "mas_snoc_ipa",
	.id = MASTER_SNOC_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SLAVE_SNOC_IPA },
};

static struct bus_icc_node slv_snoc_ipa = {
	.name = "slv_snoc_ipa",
	.id = SLAVE_SNOC_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 0,
};

static struct bus_icc_node *qt_snoc_nodes[] = {
	[MASTER_SNOC_IPA] = &mas_snoc_ipa,
	[SLAVE_SNOC_IPA] = &slv_snoc_ipa,
};

static struct bus_icc_desc qt_snoc = {
	.nodes = qt_snoc_nodes,
	.num_nodes = ARRAY_SIZE(qt_snoc_nodes),
};

/*qt_cnoc*/
static struct bus_icc_node mas_cnoc_ipa = {
	.name = "mas_cnoc_ipa",
	.id = MASTER_CNOC_IPA,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SLAVE_CNOC_IPA },
};

static struct bus_icc_node slv_cnoc_ipa = {
	.name = "slv_cnoc_ipa",
	.id = SLAVE_CNOC_IPA,
	.channels = 1,
	.buswidth = 4,
	.num_links = 0,
};

static struct bus_icc_node *qt_cnoc_nodes[] = {
	[MASTER_CNOC_IPA] = &mas_cnoc_ipa,
	[SLAVE_CNOC_IPA] = &slv_cnoc_ipa,
};

static struct bus_icc_desc qt_cnoc = {
	.nodes = qt_cnoc_nodes,
	.num_nodes = ARRAY_SIZE(qt_cnoc_nodes),
};

static struct bus_info bus_info;
static void bus_dump(struct clk *clk, unsigned long rate,
		struct dev_pm_opp_info *opp, unsigned int count)
{
	struct bus_info *businfo = &bus_info;
	unsigned int i;
	char buf[200];
	char *p = buf;

	for (i = 0; i < count; i++) {
		sprintf(p, "%8lu mv,", (opp->supplies[i].u_volt / 1000));
		p = buf + strlen(buf);
	}
	sprintf(p, "\n");

	mutex_lock(&businfo->mutex);
	if (businfo->logbuf)
		ipc_log_string(businfo->logbuf,
				"%20s: %8lu khz, %s",
				__clk_get_name(clk), (rate/1000), buf);

	mutex_unlock(&businfo->mutex);
}

static void bus_log_start(unsigned int start)
{
	struct bus_info *businfo = &bus_info;

	mutex_lock(&businfo->mutex);
	if (start) {
		businfo->logbuf = ipc_log_context_create(BUS_IPC_LOG_PAGES, "bus", 0);
		if (!businfo->logbuf)
			pr_err("create bus log buf failed\n");
	} else {
		ipc_log_context_destroy(businfo->logbuf);
		businfo->logbuf = NULL;
	}
	mutex_unlock(&businfo->mutex);

	businfo->start = start;
}

static ssize_t bus_debug_write(struct file *filp,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	struct bus_info *businfo = &bus_info;
	unsigned int start = 0;
	unsigned int cnt = 0;
	char buf[10];

	cnt = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, buffer, cnt))
		return -EFAULT;
	buf[cnt] = '\0';
	if (kstrtoint(buf, 0, &start)) {
		pr_info("Please input 1 for start or 0 for stop!\n");
		return count;
	}

	start = !!start;

	if (start == businfo->start) {
		pr_info("bus log is already %s\n",
			start ? "started" : "stopped");
		return count;
	}

	pr_info("bus log %s\n", start ? "started" : "stopped");

	bus_log_start(start);

	return count;
}

static const struct file_operations bus_debug_fops = {
	.write          = bus_debug_write,
};

static void bus_log_init(void)
{
	struct bus_info *businfo = &bus_info;

	businfo->dir = debugfs_create_dir("bus_debug", NULL);
	if (!businfo->dir)
		return;

	if (!debugfs_create_file("log_enable", 0644,
			businfo->dir, NULL, &bus_debug_fops)) {
		debugfs_remove_recursive(businfo->dir);
		businfo->dir = NULL;
		return;
	}

	mutex_init(&businfo->mutex);

	bus_log_start(true);
}

/**
 * bus_opp_supply_set_opp() - do the opp supply transition
 * @data:	information on regulators and new and old opps provided by
 *		opp core to use in transition
 *
 * Return: If successful, 0, else appropriate error value.
 */
static int bus_set_opp(struct dev_pm_set_opp_data *data)
{
	struct dev_pm_opp_info *old_opp = &data->old_opp;
	struct dev_pm_opp_info *new_opp = &data->new_opp;
	unsigned long old_freq = old_opp->rate;
	unsigned long new_freq = new_opp->rate;
	unsigned int regulator_count = data->regulator_count;
	struct device *dev = data->dev;
	struct clk *clk = data->clk;
	unsigned int i;
	int ret = 0;

	if (new_freq >= old_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						new_opp->supplies[i].u_volt_min,
						new_opp->supplies[i].u_volt,
						new_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to set regulator[%d]: %d\n",
						__func__, i, ret);
				goto restore;
			}
		}
	}

	ret = clk_set_rate(clk, new_freq);
	if (ret) {
		dev_err(dev, "%s: failed to set clk[%s][%d]: %d\n",
			__func__, __clk_get_name(clk), new_freq, ret);
		goto restore;
	}

	if (new_freq < old_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						new_opp->supplies[i].u_volt_min,
						new_opp->supplies[i].u_volt,
						new_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to set regulator[%d]: %d\n",
						__func__, i, ret);
				goto restore;
			}
		}
	}

	bus_dump(clk, new_freq, new_opp, regulator_count);

	return 0;

restore:
	if (old_freq >= new_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						old_opp->supplies[i].u_volt_min,
						old_opp->supplies[i].u_volt,
						old_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to restore regulator[%d]: %d\n",
						__func__, i, ret);
				return -EINVAL;
			}
		}
	}

	ret = clk_set_rate(clk, old_freq);
	if (ret) {
		dev_err(dev, "%s: failed to restore clk[%s][%d]: %d\n",
			__func__, __clk_get_name(clk), old_freq, ret);
		return -EINVAL;
	}

	if (old_freq < new_freq) {
		for (i = 0; i < regulator_count; i++) {
			ret = regulator_set_voltage_triplet(data->regulators[i],
						old_opp->supplies[i].u_volt_min,
						old_opp->supplies[i].u_volt,
						old_opp->supplies[i].u_volt_max);
			if (ret) {
				dev_err(dev, "%s: failed to restore regulator[%d]: %d\n",
						__func__, i, ret);
				return -EINVAL;
			}
		}
	}

	return 0;
}

int bus_dev_target(struct device *dev, unsigned long freq)
{
	struct bus_icc_provider *qp = dev_get_drvdata(dev);
	unsigned long new_freq, old_freq;
	struct dev_pm_opp *new_opp = NULL, *old_opp = NULL;
	struct dev_pm_set_opp_data *data;
	int ret = 0;

	new_freq = clk_round_rate(qp->clk, freq);
	if (IS_ERR_VALUE(new_freq)) {
		dev_err(dev, "Cannot find matching frequency for %lu\n",
			freq);
		return new_freq;
	}

	new_opp = dev_pm_opp_find_freq_ceil(dev, &new_freq);
	if (IS_ERR(new_opp)) {
		ret = PTR_ERR(new_opp);
		goto out;
	}

	old_freq = clk_get_rate(qp->clk);

	old_opp = dev_pm_opp_find_freq_ceil(dev, &old_freq);
	if (IS_ERR(old_opp)) {
		ret = PTR_ERR(old_opp);
		goto out;
	}

	data = &qp->data;
	if (qp->regulator_count)
		data->regulators = qp->opp_table->regulators;
	data->regulator_count = qp->regulator_count;
	data->clk = qp->clk;
	data->dev = dev;

	data->old_opp.rate = old_freq;
	data->old_opp.supplies = old_opp->supplies;

	data->new_opp.rate = new_freq;
	data->new_opp.supplies = new_opp->supplies;

	ret = bus_set_opp(data);
	if (ret)
		dev_err(dev, "cannot target frequency for %lu\n", freq);
out:
	if (old_opp)
		dev_pm_opp_put(old_opp);
	if (new_opp)
		dev_pm_opp_put(new_opp);

	return ret;
}

static int find_supply_name(struct device *dev, const char *name)
{
	struct device_node *np;
	struct property *pp;
	char names[50] ;
	unsigned int found = 0;

	np = of_node_get(dev->of_node);

	/* This must be valid for sure */
	if (WARN_ON(!np))
		return 0;

	strcpy(names, name);
	strcat(names, "-supply");
	pp = of_find_property(np, names, NULL);
	if (pp)
		found = 1;

	of_node_put(np);
	return found;
}

static int bus_opp_init(struct device *dev, struct bus_icc_provider *qp)
{
	const char * const names[VDD_NUM] = {"vdd_core", "vdd_ddr"};
	unsigned int count = 0, i;
	int ret = 0;

	if (qp->smd_bus)
		return 0;

	for (i = 0; i < VDD_NUM; i++) {
		if (find_supply_name(dev, names[i]))
			count++;
	}

	if (count) {
		qp->opp_table = dev_pm_opp_set_regulators(dev, names, count);
		if (IS_ERR(qp->opp_table)) {
			ret = PTR_ERR(qp->opp_table);
			dev_err(dev, "Failed to set regulator: %d\n", ret);
			return ret;
		}
		qp->regulator_count = count;
	}

	ret = dev_pm_opp_of_add_table(dev);

	if (ret) {
		dev_err(dev, "%s: couldn't find opp table for %s\n",
				 __func__, dev_name(dev));
		goto out_put_regulator;
	}

	return 0;

out_put_regulator:
	if (count)
		dev_pm_opp_put_regulators(qp->opp_table);
	return ret;
}


static int bus_freq_init(struct device *dev, struct bus_icc_provider *qp)
{
	unsigned long freq = RPM_CLK_MAX_LEVEL;
	int ret = 0;

	ret = bus_icc_set_rate(qp, freq);

	return ret;
}

static int bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct bus_icc_desc *desc;
	struct icc_onecell_data *data;
	struct icc_provider *provider;
	struct bus_icc_node **qnodes;
	struct bus_icc_provider *qp;
	struct icc_node *node, *tmp;
	size_t num_nodes, i;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	qnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	qp = devm_kzalloc(dev, sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		goto free;

	provider = &qp->provider;
	provider->dev = dev;
	provider->set = bus_icc_set;
	provider->pre_aggregate = bus_icc_pre_aggregate;
	provider->aggregate = bus_icc_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;
	qp->dev = &pdev->dev;

	qp->clk = devm_clk_get(dev, "bus");
	if (IS_ERR(qp->clk))
		goto free;

	qp->init = true;
	qp->smd_bus = of_property_read_bool(dev->of_node, "bus,smd-bus");

	if (of_property_read_u32(dev->of_node, "bus,util-factor",
				 &qp->util_factor))
		qp->util_factor = DEFAULT_UTIL_FACTOR;

	if (of_property_read_u32(dev->of_node, "bus,fixed-freq",
				&qp->fixed_freq))
		qp->fixed_freq = 0;

	ret = bus_opp_init(dev, qp);
	if (ret) {
		dev_err(dev, "%s: %s : bus_opp_init failure!\n",
			__func__, dev_name(dev));
		goto free;
	}

	ret = bus_icc_peer_init(dev, qp);
	if (ret) {
		dev_err(dev, "%s: %s : bus_icc_peer_init failure!\n",
			__func__, dev_name(dev));
		goto free;
	}

	ret = icc_provider_add(provider);
	if (ret) {
		dev_err(dev, "error adding interconnect provider: %d\n", ret);
		goto free;
	}

	for (i = 0; i < num_nodes; i++) {
		size_t j;

		if (!qnodes[i])
			continue;

		node = icc_node_create(qnodes[i]->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = qnodes[i]->name;
		node->data = qnodes[i];
		icc_node_add(node, provider);

		for (j = 0; j < qnodes[i]->num_links; j++)
			icc_link_create(node, qnodes[i]->links[j]);

		data->nodes[i] = node;
	}
	data->num_nodes = num_nodes;

	platform_set_drvdata(pdev, qp);

	if (bus_freq_init(dev, qp))
		goto err;

	bus_log_init();

	dev_info(dev, "Registered JLQ ICC\n");

	mutex_lock(&probe_list_lock);
	list_add_tail(&qp->probe_list, &bus_probe_list);
	mutex_unlock(&probe_list_lock);

	return 0;

err:
	list_for_each_entry_safe(node, tmp, &provider->nodes, node_list) {
		icc_node_del(node);
		icc_node_destroy(node->id);
	}

	icc_provider_del(provider);
free:
	if (qp->clk)
		devm_clk_put(dev, qp->clk);
	if (data)
		devm_kfree(dev, data);
	if (qp)
		devm_kfree(dev, qp);
	return -EINVAL;
}

static int bus_remove(struct platform_device *pdev)
{
	struct bus_icc_provider *qp = platform_get_drvdata(pdev);
	struct icc_provider *provider = &qp->provider;
	struct icc_node *n, *tmp;

	list_for_each_entry_safe(n, tmp, &provider->nodes, node_list) {
		icc_node_del(n);
		icc_node_destroy(n->id);
	}

	return icc_provider_del(provider);
}

static const struct of_device_id bus_of_match[] = {
	{ .compatible = "jlq,qt_snoc",
	  .data = &qt_snoc},
	{ .compatible = "jlq,qt_cnoc",
	  .data = &qt_cnoc},
	{ .compatible = "jlq,qt_sys_noc_rt",
	  .data = &qt_sys_noc_rt},
	{ .compatible = "jlq,qt_sys_noc_nrt",
	  .data = &qt_sys_noc_nrt},
	{ .compatible = "jlq,bus_sw0",
	  .data = &bus_sw0},
	{ .compatible = "jlq,top_bus",
	  .data = &top_bus},
	{ .compatible = "jlq,bus_sw1",
	  .data = &bus_sw1},
	{ .compatible = "jlq,bus_sw2",
	  .data = &bus_sw2},
	{ }
};
MODULE_DEVICE_TABLE(of, bus_of_match);

static void __maybe_unused bus_sync_state(struct device *dev)
{
	struct bus_icc_provider *qp = dev_get_drvdata(dev);

	mutex_lock(&probe_list_lock);
	probe_count++;

	if (probe_count < ARRAY_SIZE(bus_of_match) - 1) {
		mutex_unlock(&probe_list_lock);
		return;
	}

	list_for_each_entry(qp, &bus_probe_list, probe_list) {
		qp->init = false;
	}

	mutex_unlock(&probe_list_lock);

	pr_err("JLQ ICC Sync State done\n");
}

static struct platform_driver bus_driver = {
	.probe = bus_probe,
	.remove = bus_remove,
	.driver = {
		.name = "jlq-bus",
		.of_match_table = bus_of_match,
		//.sync_state = bus_sync_state,
	},
};

static int __init bus_driver_init(void)
{
	return platform_driver_register(&bus_driver);
}
#ifdef MODULE
module_init(bus_driver_init)
#else
subsys_initcall(bus_driver_init);
#endif

static void __exit bus_driver_exit(void)
{
	platform_driver_unregister(&bus_driver);
}
module_exit(bus_driver_exit);

MODULE_DESCRIPTION("JLQ BUS driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: jlq-smd");
