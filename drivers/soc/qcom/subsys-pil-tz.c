// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "subsys-pil-tz: %s(): " fmt, __func__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/interconnect.h>
#include <dt-bindings/interconnect/qcom,lahaina.h>
#include <linux/dma-mapping.h>
#include <linux/qcom_scm.h>

#include <soc/qcom/subsystem_restart.h>
#include <soc/qcom/ramdump.h>

#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>

#include <linux/iopoll.h>
#include "peripheral-loader.h"
#include <dt-bindings/regulator/qcom,rpm-smd-regulator.h>

#define PIL_TZ_AVG_BW  0
#define PIL_TZ_PEAK_BW UINT_MAX

#define XO_FREQ			19200000
#define PROXY_TIMEOUT_MS	10000
#define MAX_SSR_REASON_LEN	256U
#define STOP_ACK_TIMEOUT_MS	1000
#define CRASH_STOP_ACK_TO_MS	200

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
static char last_ssr_reason[MAX_SSR_REASON_LEN] = "none";
static struct proc_dir_entry *last_ssr_reason_entry;

#define ERR_READY	0
#define PBL_DONE	1

#define desc_to_data(d) container_of(d, struct pil_tz_data, desc)
#define subsys_to_data(d) container_of(d, struct pil_tz_data, subsys_desc)

#define CHECK_NV_DESTROYED_MI 1
#ifdef CHECK_NV_DESTROYED_MI
#include <linux/workqueue.h>
#include <linux/slab.h>
#define STR_NV_SIGNATURE_DESTROYED "CRITICAL_DATA_CHECK_FAILED"
static char last_modem_sfr_reason[MAX_SSR_REASON_LEN] = "none";
#endif

/**
 * struct reg_info - regulator info
 * @reg: regulator handle
 * @uV: voltage in uV
 * @uA: current in uA
 */
struct reg_info {
	struct regulator *reg;
	int uV;
	int uA;
};

enum pas_id {
	PAS_MODEM,
	PAS_Q6,
	PAS_DSPS,
	PAS_TZAPPS,
	PAS_MODEM_SW,
	PAS_MODEM_FW,
	PAS_WCNSS,
	PAS_SECAPP,
	PAS_GSS,
	PAS_VIDC,
	PAS_VPU,
	PAS_BCSS,
};

void write_crash_reason(char *crash_reason)
{
	snprintf(last_ssr_reason, (size_t)MAX_SSR_REASON_LEN, "%s", crash_reason);
}
EXPORT_SYMBOL(write_crash_reason);
static int last_ssr_reason_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", last_ssr_reason);
	return 0;
}

static int last_ssr_reason_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, last_ssr_reason_proc_show, NULL);
}

static const struct file_operations last_ssr_reason_file_ops = {
	.owner = THIS_MODULE,
	.open  = last_ssr_reason_proc_open,
	.read  = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct icc_path *scm_perf_client;
static int scm_pas_bw_count;
static DEFINE_MUTEX(scm_pas_bw_mutex);
static int is_inited;

static void subsys_disable_others_irqs(struct pil_tz_data *d);
static void subsys_disable_wdog_irq(struct pil_tz_data *d);
static void subsys_enable_all_irqs(struct pil_tz_data *d);
static bool generic_read_status(struct pil_tz_data *d);

static int enable_debug;
module_param(enable_debug, int, 0644);
static int pil_msa_auth_modem_mdt(struct pil_desc *pil, const u8 *metadata, size_t size);
static int pil_msa_mba_auth_trusted(struct pil_desc *pil);
static int pil_msa_mba_auth_trusted_smc(struct pil_desc *pil);

static int pbl_mba_boot_timeout_ms = 10000;
module_param(pbl_mba_boot_timeout_ms, int, 0644);
static int modem_auth_timeout_ms = 3000;
module_param(modem_auth_timeout_ms, int, 0644);
/* If set to 0xAABADEAD, MBA failures trigger a kernel panic */
static uint modem_trigger_panic;
module_param(modem_trigger_panic, uint, 0644);

static int wait_for_err_ready(struct pil_tz_data *d)
{
	int ret;
	pr_err("[%s]wait_for_err_ready enter,[%d][%d][%d][%d]\n", d->desc.name,d->generic_irq,d->err_ready_irq,enable_debug,pil_is_timeout_disabled());

	/*
	 * If subsys is using generic_irq in which case err_ready_irq will be 0,
	 * don't return.
	 */
	if ((d->generic_irq <= 0 && !d->err_ready_irq) ||
				enable_debug == 1 || pil_is_timeout_disabled())
		return 0;

	ret = wait_for_completion_interruptible_timeout(&d->err_ready,
					  msecs_to_jiffies(10000));
	if (!ret) {
		pr_err("[%s]: Error ready timed out\n", d->desc.name);
		return -ETIMEDOUT;
	}

	return 0;
}

static int scm_pas_enable_bw(void)
{
	int ret = 0;

	if (IS_ERR(scm_perf_client))
		return -EINVAL;

	mutex_lock(&scm_pas_bw_mutex);
	if (!scm_pas_bw_count) {
		ret = icc_set_bw(scm_perf_client, PIL_TZ_AVG_BW,
						PIL_TZ_PEAK_BW);
		if (ret)
			goto err_bus;
		scm_pas_bw_count++;
	}

	mutex_unlock(&scm_pas_bw_mutex);
	return ret;

err_bus:
	pr_err("scm-pas; Bandwidth request failed (%d)\n", ret);
	icc_set_bw(scm_perf_client, 0, 0);

	mutex_unlock(&scm_pas_bw_mutex);
	return ret;
}

static void scm_pas_disable_bw(void)
{
	mutex_lock(&scm_pas_bw_mutex);
	if (scm_pas_bw_count-- == 1)
		icc_set_bw(scm_perf_client, 0, 0);
	mutex_unlock(&scm_pas_bw_mutex);
}

static int of_read_clocks(struct device *dev, struct clk ***clks_ref,
			  const char *propname)
{
	long clk_count;
	int i, len;
	struct clk **clks;

	if (!of_find_property(dev->of_node, propname, &len))
		return 0;

	clk_count = of_property_count_strings(dev->of_node, propname);
	if (IS_ERR_VALUE(clk_count)) {
		dev_err(dev, "Failed to get clock names\n");
		return -EINVAL;
	}

	clks = devm_kzalloc(dev, sizeof(struct clk *) * clk_count,
				GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	for (i = 0; i < clk_count; i++) {
		const char *clock_name;
		char clock_freq_name[50];
		u32 clock_rate = XO_FREQ;

		of_property_read_string_index(dev->of_node,
					      propname, i,
					      &clock_name);
		snprintf(clock_freq_name, ARRAY_SIZE(clock_freq_name),
						"qcom,%s-freq", clock_name);
		if (of_find_property(dev->of_node, clock_freq_name, &len))
			if (of_property_read_u32(dev->of_node, clock_freq_name,
								&clock_rate)) {
				dev_err(dev, "Failed to read %s clock's freq\n",
							clock_freq_name);
				return -EINVAL;
			}

		clks[i] = devm_clk_get(dev, clock_name);
		if (IS_ERR(clks[i])) {
			int rc = PTR_ERR(clks[i]);

			if (rc != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s clock\n",
								clock_name);
			return rc;
		}

		/* Make sure rate-settable clocks' rates are set */
		/* need enable first. then set rate
		if (clk_get_rate(clks[i]) == 0)
			clk_set_rate(clks[i], clk_round_rate(clks[i],
								clock_rate));
		*/
	}

	*clks_ref = clks;
	return clk_count;
}

#ifdef CHECK_NV_DESTROYED_MI
static struct kobject *checknv_kobj;
static struct kset *checknv_kset;
static const struct sysfs_ops checknv_sysfs_ops = {
};
static void kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}
static struct kobj_type checknv_ktype = {
	.sysfs_ops = &checknv_sysfs_ops,
	.release = kobj_release,
};
static void checknv_kobj_clean(struct work_struct *work)
{
	kobject_uevent(checknv_kobj, KOBJ_REMOVE);
	kobject_put(checknv_kobj);
	kset_unregister(checknv_kset);
}
static void checknv_kobj_create(struct work_struct *work)
{
	int ret;
	if (checknv_kset != NULL) {
		pr_err("checknv_kset is not NULL, should clean up.");
		kobject_uevent(checknv_kobj, KOBJ_REMOVE);
		kobject_put(checknv_kobj);
	}
	checknv_kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
	if (!checknv_kobj) {
		pr_err("kobject alloc failed.");
		return;
	}
	if (checknv_kset == NULL) {
		checknv_kset = kset_create_and_add("checknv_errimei", NULL, NULL);
		if (!checknv_kset) {
			pr_err("kset creation failed.");
			goto free_kobj;
		}
	}
	checknv_kobj->kset = checknv_kset;
	ret = kobject_init_and_add(checknv_kobj, &checknv_ktype, NULL, "%s", "errimei");
	if (ret) {
		pr_err("%s: Error in creation kobject", __func__);
		goto del_kobj;
	}
	kobject_uevent(checknv_kobj, KOBJ_ADD);
	return;
del_kobj:
	kobject_put(checknv_kobj);
	kset_unregister(checknv_kset);
free_kobj:
	kfree(checknv_kobj);
}
static DECLARE_DELAYED_WORK(create_kobj_work, checknv_kobj_create);
static DECLARE_WORK(clean_kobj_work, checknv_kobj_clean);
#endif

static int of_read_regs(struct device *dev, struct reg_info **regs_ref,
			const char *propname)
{
	long reg_count;
	int i, len, rc;
	struct reg_info *regs;

	if (!of_find_property(dev->of_node, propname, &len))
		return 0;

	reg_count = of_property_count_strings(dev->of_node, propname);
	if (IS_ERR_VALUE(reg_count)) {
		dev_err(dev, "Failed to get regulator names\n");
		return -EINVAL;
	}

	regs = devm_kzalloc(dev, sizeof(struct reg_info) * reg_count,
				GFP_KERNEL);
	if (!regs)
		return -ENOMEM;

	for (i = 0; i < reg_count; i++) {
		const char *reg_name;
		char reg_uV_uA_name[50];
		u32 vdd_uV_uA[2];

		of_property_read_string_index(dev->of_node,
					      propname, i,
					      &reg_name);

		regs[i].reg = devm_regulator_get(dev, reg_name);
		if (IS_ERR(regs[i].reg)) {
			int rc = PTR_ERR(regs[i].reg);

			if (rc != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s\n regulator\n",
								reg_name);
			return rc;
		}

		/*
		 * Read the voltage and current values for the corresponding
		 * regulator. The device tree property name is "qcom," +
		 *  "regulator_name" + "-uV-uA".
		 */
		rc = snprintf(reg_uV_uA_name, ARRAY_SIZE(reg_uV_uA_name),
			 "qcom,%s-uV-uA", reg_name);
		if (rc < strlen(reg_name) + 6) {
			dev_err(dev, "Failed to hold reg_uV_uA_name\n");
			return -EINVAL;
		}

		if (!of_find_property(dev->of_node, reg_uV_uA_name, &len))
			continue;

		len /= sizeof(vdd_uV_uA[0]);

		/* There should be two entries: one for uV and one for uA */
		if (len != 2) {
			dev_err(dev, "Missing uV/uA value\n");
			return -EINVAL;
		}

		rc = of_property_read_u32_array(dev->of_node, reg_uV_uA_name,
					vdd_uV_uA, len);
		if (rc) {
			dev_err(dev, "Failed to read uV/uA values(rc:%d)\n",
									rc);
			return rc;
		}

		regs[i].uV = vdd_uV_uA[0];
		regs[i].uA = vdd_uV_uA[1];
	}

	*regs_ref = regs;
	return reg_count;
}

#if IS_ENABLED(CONFIG_INTERCONNECT_QCOM)
static int of_read_bus_client(struct platform_device *pdev,
			     struct pil_tz_data *d)
{
	d->bus_client = of_icc_get(&pdev->dev, NULL);
	if (!d->bus_client)
		pr_warn("%s: Unable to register bus client\n", __func__);

	return 0;
}
static int do_bus_scaling_request(struct pil_desc *pil, int enable)
{
	int rc;
	struct pil_tz_data *d = desc_to_data(pil);
	u32 avg_bw = enable ? PIL_TZ_AVG_BW : 0;
	u32 peak_bw = enable ? PIL_TZ_PEAK_BW : 0;

	if (d->bus_client) {
		rc = icc_set_bw(d->bus_client, avg_bw, peak_bw);
		if (rc) {
			dev_err(pil->dev, "bandwidth request failed(rc:%d)\n",
									rc);
			return rc;
		}
	} else
		WARN(d->enable_bus_scaling, "Bus scaling not set up for %s!\n",
					d->subsys_desc.name);
	return 0;
}
#else
static int of_read_bus_client(struct platform_device *pdev,
			     struct pil_tz_data *d)
{
	return 0;
}
static int do_bus_scaling_request(struct pil_desc *pil, int enable)
{
	return 0;
}
#endif

static int piltz_resc_init(struct platform_device *pdev, struct pil_tz_data *d)
{
	int len, count, rc;
	struct device *dev = &pdev->dev;

	count = of_read_clocks(dev, &d->clks, "qcom,active-clock-names");
	if (count < 0) {
		dev_err(dev, "Failed to setup clocks(rc:%d).\n", count);
		return count;
	}
	d->clk_count = count;

	count = of_read_clocks(dev, &d->proxy_clks, "qcom,proxy-clock-names");
	if (count < 0) {
		dev_err(dev, "Failed to setup proxy clocks(rc:%d).\n", count);
		return count;
	}
	d->proxy_clk_count = count;

	count = of_read_regs(dev, &d->regs, "qcom,active-reg-names");
	if (count < 0) {
		dev_err(dev, "Failed to setup regulators(rc:%d).\n", count);
		return count;
	}
	d->reg_count = count;

	count = of_read_regs(dev, &d->proxy_regs, "qcom,proxy-reg-names");
	if (count < 0) {
		dev_err(dev, "Failed to setup proxy regulators(rc:%d).\n",
				count);
		return count;
	}
	d->proxy_reg_count = count;

	if (of_find_property(dev->of_node, "interconnects", &len)) {
		d->enable_bus_scaling = true;
		rc = of_read_bus_client(pdev, d);
		if (rc) {
			dev_err(dev, "Failed to setup bus scaling client(rc:%d).\n",
				rc);
			return rc;
		}
	}

	return 0;
}

static int enable_regulators(struct pil_tz_data *d, struct device *dev,
				struct reg_info *regs, int reg_count,
				bool reg_no_enable)
{
	int i, rc = 0;

	for (i = 0; i < reg_count; i++) {
		if (regs[i].uV > 0) {
			rc = regulator_set_voltage(regs[i].reg,
					regs[i].uV, INT_MAX);
			if (rc) {
				dev_err(dev, "Failed to request voltage(rc:%d)\n",
									rc);
				goto err_voltage;
			}
		}

		if (regs[i].uA > 0) {
			rc = regulator_set_load(regs[i].reg,
						regs[i].uA);
			if (rc < 0) {
				dev_err(dev, "Failed to set regulator mode(rc:%d)\n",
									rc);
				goto err_mode;
			}
		}

		if (d->keep_proxy_regs_on && reg_no_enable)
			continue;

		rc = regulator_enable(regs[i].reg);
		if (rc) {
			dev_err(dev, "Regulator enable failed(rc:%d)\n", rc);
			goto err_enable;
		}
	}

	return 0;
err_enable:
	if (regs[i].uA > 0) {
		regulator_set_voltage(regs[i].reg, 0, INT_MAX);
		regulator_set_load(regs[i].reg, 0);
	}
err_mode:
	if (regs[i].uV > 0)
		regulator_set_voltage(regs[i].reg, 0, INT_MAX);
err_voltage:
	for (i--; i >= 0; i--) {
		if (regs[i].uV > 0)
			regulator_set_voltage(regs[i].reg, 0, INT_MAX);

		if (regs[i].uA > 0)
			regulator_set_load(regs[i].reg, 0);

		if (d->keep_proxy_regs_on && reg_no_enable)
			continue;
		regulator_disable(regs[i].reg);
	}

	return rc;
}

static void disable_regulators(struct pil_tz_data *d, struct reg_info *regs,
					int reg_count, bool reg_no_disable)
{
	int i;

	for (i = 0; i < reg_count; i++) {
		if (regs[i].uV > 0)
			regulator_set_voltage(regs[i].reg, 0, INT_MAX);

		if (regs[i].uA > 0)
			regulator_set_load(regs[i].reg, 0);

		if (d->keep_proxy_regs_on && reg_no_disable)
			continue;
		regulator_disable(regs[i].reg);
	}
}

static int prepare_enable_clocks(struct device *dev, struct clk **clks,
								int clk_count)
{
	int rc = 0;
	int i;

	for (i = 0; i < clk_count; i++) {
		rc = clk_prepare_enable(clks[i]);
		if (rc) {
			dev_err(dev, "Clock enable failed(rc:%d)\n", rc);
			goto err;
		}
	}

	return 0;
err:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(clks[i]);

	return rc;
}

static void disable_unprepare_clocks(struct clk **clks, int clk_count)
{
	int i;

	for (i = --clk_count; i >= 0; i--)
		clk_disable_unprepare(clks[i]);
}

static int pil_make_proxy_vote(struct pil_desc *pil)
{
	struct pil_tz_data *d = desc_to_data(pil);
	int rc;

	if (d->subsys_desc.no_auth)
		return 0;

	rc = do_bus_scaling_request(pil, 1);
	if (rc)
		return rc;

	rc = enable_regulators(d, pil->dev, d->proxy_regs,
					d->proxy_reg_count, false);
	if (rc)
		return rc;

	rc = prepare_enable_clocks(pil->dev, d->proxy_clks,
							d->proxy_clk_count);
	if (rc)
		goto err_clks;

	return 0;

err_clks:
	disable_regulators(d, d->proxy_regs, d->proxy_reg_count, false);

	return rc;
}

static void pil_remove_proxy_vote(struct pil_desc *pil)
{
	struct pil_tz_data *d = desc_to_data(pil);

	if (d->subsys_desc.no_auth)
		return;

	disable_unprepare_clocks(d->proxy_clks, d->proxy_clk_count);

	disable_regulators(d, d->proxy_regs, d->proxy_reg_count, true);

	do_bus_scaling_request(pil, 0);
}

static int pil_make_vote_for_qtang_cxmx(const struct subsys_desc *desc)
{
	struct pil_tz_data *d = subsys_to_data(desc);
	int ret;
	pr_debug("enter\n");

	//vote Qtang2_cx and qtang2_mx
	d->reg_cx = devm_regulator_get_optional(d->dev, "vdd_cx");
	if (IS_ERR(d->reg_cx)) {
		pr_err("unable to get reg_cx regulator\n");
		return -ENODEV;
	}
	ret = regulator_set_voltage(d->reg_cx, RPM_SMD_REGULATOR_LEVEL_NOM, RPM_SMD_REGULATOR_LEVEL_NOM);
	if(ret) {
		pr_err("set reg_cx failed\n");
		regulator_put(d->reg_cx);
		return ret;
	}
	ret = regulator_enable(d->reg_cx);
	if (ret) {
		pr_err("Enable reg_cx failed\n");
		return ret;
	}

	d->reg_mx = devm_regulator_get_optional(d->dev, "vdd_mx");
	if (IS_ERR(d->reg_mx)) {
		pr_err("unable to get reg_mx regulator\n");
		return -ENODEV;
	}

	ret = regulator_set_voltage(d->reg_mx, RPM_SMD_REGULATOR_LEVEL_NOM, RPM_SMD_REGULATOR_LEVEL_NOM);
	if(ret) {
		pr_err("set reg_mx regulator failed\n");
		regulator_put(d->reg_mx);
		return ret;
	}
	ret = regulator_enable(d->reg_mx);
	if (ret) {
		pr_err("Enable reg_mx failed\n");
		return ret;
	}
	pr_debug("exit\n");

	return 0;
}

static void pil_remove_vote_for_qtang_cxmx(const struct subsys_desc *desc)
{
	struct pil_tz_data *d = subsys_to_data(desc);
	int ret;
	pr_debug("enter\n");

	//unvote Qtang2_cx and qtang2_mx
	ret = regulator_set_voltage(d->reg_cx, 0, INT_MAX);
	if(ret) {
		pr_err("set reg_cx failed\n");
		regulator_put(d->reg_cx);
		return ;
	}
	ret = regulator_disable(d->reg_cx);
	if (ret) {
		pr_err("Enable reg_cx failed\n");
		return;
	}

	ret = regulator_set_voltage(d->reg_mx,  0, INT_MAX);
	if(ret) {
		pr_err("set reg_mx regulator failed\n");
		regulator_put(d->reg_mx);
		return;
	}
	ret = regulator_disable(d->reg_mx);
	if (ret) {
		pr_err("Enable reg_mx failed\n");
		return;
	}
	pr_debug("exit\n");

	return;
}


static int pil_make_vote_for_npa(const struct subsys_desc *desc)
{
	struct pil_tz_data *d = subsys_to_data(desc);
	struct pil_desc *pil = &d->desc;
	int rc,i,len;
	const char *clock_name;
	char clock_freq_name[50];
	u32 clock_rate = XO_FREQ;
	pr_debug("enter!\n");

	if (!of_find_property(d->dev->of_node,  "qcom,proxy-clock-names", &len))
		return 0;

	for (i = 0; i < d->proxy_clk_count; i++) {

		of_property_read_string_index(d->dev->of_node,
					       "qcom,proxy-clock-names", i,
					      &clock_name);
		snprintf(clock_freq_name, ARRAY_SIZE(clock_freq_name),
						"qcom,%s-freq", clock_name);
		if (of_find_property(d->dev->of_node, clock_freq_name, &len))
			if (of_property_read_u32(d->dev->of_node, clock_freq_name,
								&clock_rate)) {
				pr_err("Failed to read %s clock's freq\n",
							clock_freq_name);
				return -EINVAL;
			}

		pr_debug("i:[%d],clock_name:[%s],clock_freq_name:[%s]clock_rate:[%ld]\n",i,clock_name,clock_freq_name,clock_rate);

		rc = clk_prepare_enable(d->proxy_clks[i]);
		if (rc) {
			pr_err("Clock enable failed(rc:%d)\n", rc);
			goto err;
		}

		/* Make sure rate-settable clocks' rates are set */
		clk_set_rate(d->proxy_clks[i], clk_round_rate(d->proxy_clks[i],
							clock_rate));
	}
	pr_debug("exit!\n");

	return rc;
err:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(d->proxy_clks[i]);

	return rc;

}

static void pil_remove_vote_for_npa(const struct subsys_desc *desc)
{
	struct pil_tz_data *d = subsys_to_data(desc);
	struct pil_desc *pil = &d->desc;

	disable_unprepare_clocks(d->proxy_clks, d->proxy_clk_count);
	pr_debug(" exit!\n");

}


static int pil_init_image_trusted(struct pil_desc *pil,
		const u8 *metadata, size_t size)
{
	struct pil_tz_data *d = desc_to_data(pil);
	u32 scm_ret = 0;
	int ret = 0;
	pr_debug("pil_init_image_trusted:[%d]!\n",d->subsys_desc.no_auth);
	ret = pil_msa_auth_modem_mdt(pil, metadata, size);
	if (ret)
		return ret;
	if (d->subsys_desc.no_auth){
		pr_err("pil_init_image_trusted no_auth=1,no need auth!\n");
		return 0;
	}
	return 0;
	ret = scm_pas_enable_bw();
	if (ret)
		return ret;

	scm_ret = qcom_scm_pas_init_image(d->pas_id, metadata, size);

	scm_pas_disable_bw();
	return scm_ret;
}

static int pil_mem_setup_trusted(struct pil_desc *pil, phys_addr_t addr,
			       size_t size)
{
	struct pil_tz_data *d = desc_to_data(pil);
	u32 scm_ret = 0;
	pr_debug("pil_mem_setup_trusted:[%d]!\n",d->subsys_desc.no_auth);

	if (d->subsys_desc.no_auth){
		pr_err("pil_mem_setup_trusted no_auth=1,no need auth!\n");
		return 0;
	}
	return 0;
	size += pil->extra_size;
	scm_ret = qcom_scm_pas_mem_setup(d->pas_id, addr, size);

	return scm_ret;
}

static int pil_auth_and_reset(struct pil_desc *pil)
{
	struct pil_tz_data *d = desc_to_data(pil);
	int rc = 0;
	u32 scm_ret = 0;
	unsigned long pfn_start, pfn_end, pfn;
	pr_debug("pil_auth_and_reset:[%d]!\n",d->subsys_desc.no_auth);
	if (d->tz_verify)
		rc = pil_msa_mba_auth_trusted(pil);
	else
		rc = pil_msa_mba_auth_trusted_smc(pil);
	if (rc)
		return rc;
	if (d->subsys_desc.no_auth){
		pr_err("pil_auth_and_reset no_auth=1,no need auth!\n");
		return 0;
	}
	return 0;
	rc = scm_pas_enable_bw();
	if (rc)
		return rc;

	rc = enable_regulators(d, pil->dev, d->regs, d->reg_count, false);
	if (rc)
		return rc;

	rc = prepare_enable_clocks(pil->dev, d->clks, d->clk_count);
	if (rc)
		goto err_clks;

	scm_ret = qcom_scm_pas_auth_and_reset(d->pas_id);

	pfn_start = pil->priv->region_start >> PAGE_SHIFT;
	if (pfn_valid(pfn_start) && !scm_ret) {
		pfn_end = (PAGE_ALIGN(pil->priv->region_start +
				pil->priv->region_size)) >> PAGE_SHIFT;
		for (pfn = pfn_start; pfn < pfn_end; pfn++)
			set_page_private(pfn_to_page(pfn), SECURE_PAGE_MAGIC);
	}

	scm_pas_disable_bw();
	if (rc)
		goto err_reset;

	return scm_ret;
err_reset:
	disable_unprepare_clocks(d->clks, d->clk_count);
err_clks:
	disable_regulators(d, d->regs, d->reg_count, false);

	return rc;
}

static int pil_shutdown_trusted(struct pil_desc *pil)
{
	struct pil_tz_data *d = desc_to_data(pil);
	u32 scm_ret = 0;
	int rc;
	unsigned long pfn_start, pfn_end, pfn;

	if (d->subsys_desc.no_auth){
		pr_err("pil_shutdown_trusted no_auth=1,no need auth!\n");
		return 0;
	}
	return 0;
	rc = do_bus_scaling_request(pil, 1);
	if (rc)
		return rc;

	rc = enable_regulators(d, pil->dev, d->proxy_regs,
					d->proxy_reg_count, true);
	if (rc)
		goto err_regulators;

	rc = prepare_enable_clocks(pil->dev, d->proxy_clks,
						d->proxy_clk_count);
	if (rc)
		goto err_clks;

	scm_ret = qcom_scm_pas_shutdown(d->pas_id);

	pfn_start = pil->priv->region_start >> PAGE_SHIFT;
	if (pfn_valid(pfn_start) && !scm_ret) {
		pfn_end = (PAGE_ALIGN(pil->priv->region_start +
				pil->priv->region_size)) >> PAGE_SHIFT;
		for (pfn = pfn_start; pfn < pfn_end; pfn++)
			set_page_private(pfn_to_page(pfn), 0);
	}

	disable_unprepare_clocks(d->proxy_clks, d->proxy_clk_count);
	disable_regulators(d, d->proxy_regs, d->proxy_reg_count, false);

	do_bus_scaling_request(pil, 0);

	if (rc)
		return rc;

	disable_unprepare_clocks(d->clks, d->clk_count);
	disable_regulators(d, d->regs, d->reg_count, false);

	return scm_ret;

err_clks:
	disable_regulators(d, d->proxy_regs, d->proxy_reg_count, false);
err_regulators:
	do_bus_scaling_request(pil, 0);

	return rc;
}

static int pil_deinit_image_trusted(struct pil_desc *pil)
{
	struct pil_tz_data *d = desc_to_data(pil);
	u32 scm_ret = 0;
	unsigned long pfn_start, pfn_end, pfn;

	if (d->subsys_desc.no_auth){
		pr_err("pil_deinit_image_trusted no_auth=1,no need auth!\n");
		return 0;
	}
	return 0;
	scm_ret = qcom_scm_pas_shutdown(d->pas_id);
	pfn_start = pil->priv->region_start >> PAGE_SHIFT;
	if (pfn_valid(pfn_start) && !scm_ret) {
		pfn_end = (PAGE_ALIGN(pil->priv->region_start +
			    pil->priv->region_size)) >> PAGE_SHIFT;
		for (pfn = pfn_start; pfn < pfn_end; pfn++)
			set_page_private(pfn_to_page(pfn), 0);
	}

	return scm_ret;
}

//read all QTANG_MSG_BUFFER_n registers for debug
static void modem_log_rmb_regs(void __iomem *base)
{

	pr_err("RMB_MBA_IMAGE: %08x\n", readl_relaxed(base + RMB_MBA_IMAGE));
	pr_err("RMB_PBL_STATUS: %08x\n", readl_relaxed(base + RMB_PBL_STATUS));
	pr_err("RMB_MBA_COMMAND: %08x\n",
				readl_relaxed(base + RMB_MBA_COMMAND));
	pr_err("RMB_MBA_STATUS: %08x\n", readl_relaxed(base + RMB_MBA_STATUS));
	pr_err("RMB_PMI_META_DATA: %08x\n",
				readl_relaxed(base + RMB_PMI_META_DATA));
	pr_err("RMB_PMI_CODE_START: %08x\n",
				readl_relaxed(base + RMB_PMI_CODE_START));
	pr_err("RMB_PMI_CODE_LENGTH: %08x\n",
				readl_relaxed(base + RMB_PMI_CODE_LENGTH));
	pr_err("RMB_PROTOCOL_VERSION: %08x\n",
				readl_relaxed(base + RMB_PROTOCOL_VERSION));
	pr_err("RMB_MBA_DEBUG_INFORMATION: %08x\n",
			readl_relaxed(base + RMB_MBA_DEBUG_INFORMATION));

	if (modem_trigger_panic == MSS_MAGIC)
		panic("%s: System ramdump is needed!!!\n", __func__);

}
int pil_mss_shutdown(struct pil_desc *pil)
{
	struct pil_tz_data *drv = desc_to_data(pil);

	if (drv->is_booted) {
		drv->is_booted = false;
	}

	return 0;
}

static int pil_notify_Qtang_load_msadp_ready(struct pil_desc *pil,char* name)
{
	struct pil_tz_data *drv = desc_to_data(pil);
	phys_addr_t start_addr = pil_get_entry_addr(pil);
	int ret = 0;
	u32 status = 0;
	u64 val = pbl_mba_boot_timeout_ms * 1000;//is_timeout_disabled() ? 0 : pbl_mba_boot_timeout_ms * 1000;
	pr_debug("pil_notify_Qtang_load_msadp_ready enter\n");

	if (drv->dp_phys)
		start_addr = drv->dp_phys;

	if(drv->dp_size){
		pr_debug("pil_notify_Qtang_load_msadp_ready write reg -start\n");
		writel_relaxed(start_addr, drv->rmb_base + RMB_DP_IMAGE_START);
		writel_relaxed(start_addr, drv->rmb_base + RMB_DP_CODE_START);
		writel_relaxed(drv->dp_size, drv->rmb_base + RMB_DP_CODE_LENGTH);//drv->dp_size = 0x3800
		mb();
		writel_relaxed(CMD_MSADP_LOAD_READY, drv->rmb_base + RMB_MBA_COMMAND);
		mb();

		pr_debug("pil_notify_Qtang_load_msadp_ready write reg -end\n");
	}else {
		writel_relaxed(0, drv->rmb_base + RMB_DP_IMAGE_START);
		writel_relaxed(0, drv->rmb_base + RMB_DP_CODE_LENGTH);
	}

	/* Make sure RMB regs are written before bringing modem out of reset */
	mb();

	/* Wait for msadp completion. */
	ret = readl_poll_timeout(drv->rmb_base + RMB_MBA_STATUS, status,
				(status & 0x08) == 0x08 || status < 0, POLL_INTERVAL_US, val);
	if (ret) {
		pr_err("msadp auth timed out (rc:%d)\n", ret);
		goto err_reset;
	}else if (status < 0) {
		pr_err("msadp auth timed out (rc:%d),status:[%02x]\n", ret,status);
		ret = -EINVAL;
		goto err_reset;
	}
	dev_info(pil->dev, "MSADP boot done\n");
	pr_debug("pil_notify_Qtang_load_msadp_ready exit!status:[%02x]\n",status);

	return 0;

err_reset:
	modem_log_rmb_regs(drv->rmb_base);
	return ret;

}

static int pil_notify_Qtang_load_qbl_ready(struct pil_desc *pil,char* name)
{
	struct pil_tz_data *drv = desc_to_data(pil);
	phys_addr_t start_addr = pil_get_entry_addr(pil);
	int ret = 0;
	u32 status = 0;
	u64 val = pbl_mba_boot_timeout_ms * 1000;//is_timeout_disabled() ? 0 : pbl_mba_boot_timeout_ms * 1000;
	pr_debug("pil_notify_Qtang_load_qbl_ready enter!\n");

	if (drv->qbl_phys)
		start_addr = drv->qbl_phys;

	/* Program Image Address */
	if (drv->self_auth) {
		writel_relaxed(start_addr, drv->rmb_base + RMB_MBA_IMAGE);
		/*
		 * Ensure write to RMB base occurs before reset
		 * is released.
		 */
		mb();
	}

	writel_relaxed(1, drv->rmb_base + QTANG_BOOT_REQUEST);

	/* Make sure RMB regs are written before bringing modem out of reset */
	mb();

	ret = readl_poll_timeout(drv->rmb_base + QTANG_BOOT_STATUS, status,
			 status == STATUS_PBL_SUCCESS, POLL_INTERVAL_US, val);
	if (ret) {
		pr_err("qbl-wait qtang_status timed out (rc:%d)\n", ret);
		goto err_reset;
	}

	ret = readl_poll_timeout(drv->rmb_base + RMB_PBL_STATUS, status,
			 status == 0x1, POLL_INTERVAL_US, val);
	if (ret) {
		pr_err("qbl-wait pbl_status timed out (rc:%d)\n", ret);
		goto err_reset;
	}

	pr_debug("pil_notify_Qtang_load_QBL_ready exit!\n");

	dev_info(pil->dev, "QBL boot done\n");

	return 0;

err_reset:
	modem_log_rmb_regs(drv->rmb_base);
	return ret;

}

static int pil_notify_Qtang_load_msadp_ready_trusted_smc(struct pil_desc *pil, char *name)
{
	struct pil_tz_data *drv = desc_to_data(pil);
	phys_addr_t start_addr = pil_get_entry_addr(pil);
	int ret = 0;
	u32 status = 0;
	int msadp_load_ready = 0x1;
	unsigned long timeout = jiffies + msecs_to_jiffies(pbl_mba_boot_timeout_ms);

	pr_debug("pil_notify_Qtang_load_msadp_ready_trusted enter!\n");

	if (drv->dp_phys)
		start_addr = drv->dp_phys;

	if (drv->dp_size) {
		pr_debug("pil_notify_Qtang_load_msadp_ready_trusted write reg -start\n");
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_DP_IMAGE_START, (u32 *)&start_addr, PIL_WRITE_QTANG2);
		if (ret) {
			pr_err("pil_notify_Qtang_load_msadp_ready_trusted write RMB_DP_IMAGE_START fail\n");
			return ret;
		}
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_DP_CODE_START, (u32 *)&start_addr, PIL_WRITE_QTANG2);
		if (ret) {
			pr_err("pil_notify_Qtang_load_msadp_ready_trusted write RMB_DP_CODE_START fail\n");
			return ret;
		}
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_DP_CODE_LENGTH, (u32 *)&drv->dp_size, PIL_WRITE_QTANG2);
		if (ret) {
			pr_err("pil_notify_Qtang_load_msadp_ready_trusted write RMB_DP_CODE_LENGTH fail\n");
			return ret;
		}
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_MBA_COMMAND, &(msadp_load_ready), PIL_WRITE_QTANG2);
		if (ret) {
			pr_err("pil_notify_Qtang_load_msadp_ready_trusted write RMB_MBA_COMMAND fail\n");
			return ret;
		}

		pr_debug("pil_notify_Qtang_load_msadp_ready_trusted write reg -end\n");
	} else {
		pr_err("pil_notify_Qtang_load_msadp_ready_trusted drv->dp_size is null\n");
	}

	do {
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_MBA_STATUS, &status, PIL_READ_QTANG2);
		if (ret) {
			pr_err("pil_notify_Qtang_load_msadp_ready_trusted read RMB_MBA_STATUS fail\n");
			return ret;
		}
		msleep(50);
		if (time_after(jiffies, timeout)) {
			pr_err("pil_notify_Qtang_load_msadp_ready_trusted read RMB_MBA_STATUS timed out (rc:%d),status:[%d]\n", ret, status);
			return -1;
		}
	} while ((status & 0x08) != 0x08);

	pr_debug("pil_notify_Qtang_load_msadp_ready_trusted read RMB_MBA_STATUS exit, status is:[%x]\n", status);

	return 0;
}

static int pil_notify_Qtang_load_msadp_ready_trusted(struct pil_desc *pil, char *name)
{
	int ret;
	struct verifier_reg_op verifier_reg;
	unsigned long timeout = jiffies + msecs_to_jiffies(pbl_mba_boot_timeout_ms);
	int offset;
	unsigned int status;
	char *buffer;
	size_t size;

	if (tz_verifier_get_shm_info(pil->tz_vi.shm_id, &buffer, &size)) {
		pr_err("msadp shm is NULL\n");
		return -ENOMEM;
	}

	pil->tz_vi.argsize = sizeof(struct verifier_image_info);
	if (size < pil->tz_vi.argsize) {
		pr_err("shm is too small! %d < %d\n", size, pil->tz_vi.argsize);
	}
	memcpy(buffer, pil->tz_vi.image_info, pil->tz_vi.argsize);

	ret = tz_verifier_invode_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, TZCMD_AUTH_BOOT, pil->tz_vi.argsize);
	if (ret) {
		pr_err("%s auth and boot fail! ret:%d\n", ret);
		return ret;
	}

	pil->tz_vi.argsize = sizeof(verifier_reg);
	verifier_reg.ssid = SS_MSADP;
	verifier_reg.op_type = TZCMD_REG_READ;
	verifier_reg.offset = RMB_MBA_STATUS;
	offset = offsetof(struct verifier_reg_op, value);
	memcpy(buffer, &verifier_reg, pil->tz_vi.argsize);

	do {
		msleep(50);
		ret = tz_verifier_invode_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, TZCMD_OP_REG, pil->tz_vi.argsize);
		if (ret) {
			pr_err("%s read RMB_MBA_STATUS fail ret:%d\n", __func__, ret);
			return ret;
		}
		memcpy(&status, &buffer[offset], sizeof(unsigned int));
		if (time_after(jiffies, timeout)) {
			pr_err("%s read RMB_MBA_STATUS timed out (rc:%d),status:[%d]\n", __func__, ret, status);
			return -ETIMEDOUT;
		}
	} while ((status & 0x08) != 0x08);

	pr_debug("%s read RMB_MBA_STATUS exit, status is:[%x]\n", __func__, status);

	return 0;
}

static int pil_notify_Qtang_load_qbl_ready_trusted_smc(struct pil_desc *pil, char *name)
{
	struct pil_tz_data *drv = desc_to_data(pil);
	phys_addr_t start_addr = pil_get_entry_addr(pil);
	int ret = 0;
	u32 status = 0;
	u32 status1 = 0;
	uint32_t boot_request = 0x1;
	unsigned long timeout = jiffies + msecs_to_jiffies(pbl_mba_boot_timeout_ms);
	unsigned long timeout1 = jiffies + msecs_to_jiffies(pbl_mba_boot_timeout_ms * 2);

	pr_debug("pil_notify_Qtang_load_qbl_ready_trusted enter!\n");

	if (drv->qbl_phys)
		start_addr = drv->qbl_phys;

	/* Program Image Address */
	ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_MBA_IMAGE, (u32 *)&start_addr, PIL_WRITE_QTANG2);
	if (ret) {
		pr_err("pil_notify_Qtang_load_qbl_ready_trusted write RMB_MBA_IMAGE fail\n");
		return ret;
	}

	ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + QTANG_BOOT_REQUEST, &boot_request, PIL_WRITE_QTANG2);
	if (ret) {
		pr_err("pil_notify_Qtang_load_qbl_ready_trusted write QTANG_BOOT_REQUEST fail\n");
		return ret;
	}

	do {
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + QTANG_BOOT_STATUS, &status, PIL_READ_QTANG2);
		if (ret) {
			pr_err("pil_notify_Qtang_load_qbl_ready_trusted read QTANG_BOOT_STATUS fail\n");
			return ret;
		}
		msleep(50);
		if (time_after(jiffies, timeout)) {
			pr_err("pil_notify_Qtang_load_qbl_ready_trusted read QTANG_BOOT_STATUS timed out (rc:%d),status:[%d]\n", ret, status);
			return -1;
		}
	} while (status != STATUS_PBL_SUCCESS);

	pr_debug("pil_notify_Qtang_load_qbl_ready_trusted read QTANG_BOOT_STATUS exit, status is:[%x]\n", status);

	do {
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_PBL_STATUS, &status1, PIL_READ_QTANG2);
		if (ret){
			pr_err("pil_notify_Qtang_load_qbl_ready_trusted read RMB_PBL_STATUS fail\n");
			return ret;
		}
		msleep(50);
		if (time_after(jiffies, timeout1)) {
			pr_err("pil_notify_Qtang_load_qbl_ready_trusted read RMB_PBL_STATUS timed out (rc:%d),status1:[%d]\n", ret, status1);
			return -1;
		}
	} while (status1 != 0x1);

	pr_debug("pil_notify_Qtang_load_qbl_ready_trusted read RMB_PBL_STATUS exit,status1 is:[%x]\n", status1);

	return 0;
}

static int pil_notify_Qtang_load_qbl_ready_trusted(struct pil_desc *pil,char* name)
{
	int ret;
	struct verifier_reg_op verifier_reg;
	//struct verifier_image_info verifier_qbl;
	unsigned long timeout = jiffies + msecs_to_jiffies(pbl_mba_boot_timeout_ms);
	unsigned long timeout1 = jiffies + msecs_to_jiffies(pbl_mba_boot_timeout_ms * 2);
	int offset;
	unsigned int status;
	char *buffer;
	size_t size;

	if (tz_verifier_get_shm_info(pil->tz_vi.shm_id, &buffer, &size)) {
		pr_err("qbl shm is NULL\n");
		return -ENOMEM;
	}

	pil->tz_vi.argsize = sizeof(struct verifier_image_info);
	if (size < pil->tz_vi.argsize) {
		pr_err("shm is too small! %d < %d\n", size, pil->tz_vi.argsize);
	}
	memcpy(buffer, pil->tz_vi.image_info, pil->tz_vi.argsize);

	ret = tz_verifier_invode_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, TZCMD_AUTH_BOOT, pil->tz_vi.argsize);
	if (ret) {
		pr_err("%s auth and boot fail! ret:%d\n", ret);
		return ret;
	}

	pil->tz_vi.argsize = sizeof(verifier_reg);
	verifier_reg.ssid = SS_QBL;
	verifier_reg.op_type = TZCMD_REG_READ;
	verifier_reg.offset = QTANG_BOOT_STATUS;
	offset = offsetof(struct verifier_reg_op, value);
	memcpy(buffer, &verifier_reg, pil->tz_vi.argsize);

	do {
		msleep(50);
		ret = tz_verifier_invode_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, TZCMD_OP_REG, pil->tz_vi.argsize);
		if (ret) {
			pr_err("%s read reg fail! ret:%d\n", ret);
			return ret;
		}
		memcpy(&status, &buffer[offset], sizeof(unsigned int));
		if (time_after(jiffies, timeout)) {
			pr_err("pil_notify_Qtang_load_qbl_ready_trusted read QTANG_BOOT_STATUS timed out (rc:%d),status:[%d]\n", ret, status);
			return -ETIMEDOUT;
		}
	} while (status != STATUS_PBL_SUCCESS);

	pr_debug("pil_notify_Qtang_load_qbl_ready_trusted read QTANG_BOOT_STATUS exit, status is:[%x]\n", status);

	//pil->tz_vi.argsize = sizeof(verifier_reg);
	//verifier_reg.ss_prop.ssid = SS_QBL;
	//verifier_reg.ss_prop.base = drv->rmb_base_phy;
	//verifier_reg.op_type = TZCMD_REG_READ;
	verifier_reg.offset = RMB_PBL_STATUS;
	//offset = offsetof(struct verifier_reg_op, value);
	memcpy(buffer, &verifier_reg, pil->tz_vi.argsize);

	do {
		msleep(50);
		ret = tz_verifier_invode_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, TZCMD_OP_REG, pil->tz_vi.argsize);
		if (ret) {
			pr_err("%s read reg fail! ret:%d\n", ret);
			return ret;
		}
		memcpy(&status, &buffer[offset], sizeof(unsigned int));

		if (time_after(jiffies, timeout1)) {
			pr_err("pil_notify_Qtang_load_qbl_ready_trusted read QTANG_BOOT_STATUS timed out (rc:%d),status:[%d]\n", ret, status);
			return -ETIMEDOUT;
		}
	} while (status != 0x1);

	pr_debug("pil_notify_Qtang_load_qbl_ready_trusted read RMB_PBL_STATUS exit,status is:[%x]\n", status);

	return 0;
}

static int pil_mss_reset_load_msadp(const struct subsys_desc *desc)
{
	struct pil_tz_data *drv = subsys_to_data(desc);
	struct pil_desc *pil = &drv->desc;
	const struct firmware *fw = NULL;
	char fw_name[10] = "msadp.mbn";
	char *sig_header = NULL;
	void *dp_virt;
	dma_addr_t dp_phys, dp_phys_end;
	int ret, count;
	const u8 *data;
	struct pil_priv *priv = pil->priv;
	pr_debug("load_msadp enter!\n");

	/* Load and authenticate mba image */
	ret = request_firmware(&fw, fw_name, pil->dev);
	if (ret) {
		pr_err("Failed to locate %s\n",
						fw_name);
		return ret;
	}

	drv->dp_size = fw->size;
	drv->dp_size = ALIGN(drv->dp_size, SZ_4K);

	//get msadp_phy_addr from dtsi
	if(priv){
		dp_phys = priv->msadp_pa;
		dp_virt = priv->msadp_va;
		pr_debug("load_msadp->,[%pa][%zu][%px]\n",&(dp_phys),drv->dp_size,(void*)dp_virt);
	}else{
		pr_err("load_msadp,can not get image phy address\n");
		ret = -ENOMEM;
		goto err_mss_reset;
	}
	drv->dp_phys = dp_phys;
	drv->dp_virt = dp_virt;
	dp_phys_end = dp_phys + drv->dp_size;

	/* Load the MBA image into memory */
	count = fw->size;
	data = fw ? fw->data : NULL;
	if (!data) {
		dev_err(pil->dev, "dp data is NULL\n");
		ret = -ENOMEM;
		goto err_mss_reset;
	}

	if (!drv->self_auth && drv->tz_verify) {
		size_t size = 0;

		if (count < drv->tz_header_size) {
			dev_err(pil->dev, "%s too small, NOT signed?\n", pil->fw_name);
			ret = -EINVAL;
			goto err_mss_reset;
		}

		if (pil->tz_vi.session_id < 0) {
			pil->tz_vi.session_id = tz_verifier_init();
			if (pil->tz_vi.session_id < 0) {
				dev_err(pil->dev, "%s verify get session fail\n", pil->fw_name);
				ret = -EBUSY;
				goto err_mss_reset;
			}
		}
		if (pil->tz_vi.shm_id >= 0) {
			tz_verifier_get_shm_info(pil->tz_vi.shm_id, NULL, &size);
		}

		pil->tz_vi.argsize = sizeof(struct verifier_image_info);

		if (size < TZ_SHM_SIZE(1)) {
			tz_verifier_free_shm(pil->tz_vi.shm_id);
			pil->tz_vi.shm_id = -1;
		}
		if (pil->tz_vi.shm_id < 0) {
			pil->tz_vi.shm_id = tz_verifier_alloc_shm(pil->tz_vi.argsize);
			if (pil->tz_vi.shm_id < 0) {
				dev_err(pil->dev, "%s verify alloc shm fail:%d\n", pil->fw_name, pil->tz_vi.shm_id);
				ret = -ENOMEM;
				goto err_mss_reset;
			}
		}

		pil->tz_vi.argsize = sizeof(struct verifier_image_info);
		pil->tz_vi.image_info = kzalloc(pil->tz_vi.argsize, GFP_KERNEL);
		if (!pil->tz_vi.image_info) {
			dev_err(pil->dev, "%s verify alloc mem %d fail\n", desc->fw_name, pil->tz_vi.argsize);
			ret = -ENOMEM;
			goto err_mss_reset;
		}
		sig_header = kmalloc(drv->tz_header_size, GFP_KERNEL);
		if (!sig_header) {
			pr_err("%s verify alloc metadata mem %d fail\n", fw_name, drv->tz_header_size);
			ret = -ENOMEM;
			goto out_memfree;
		}
		memcpy(sig_header, data, drv->tz_header_size);
		data += drv->tz_header_size;
		count -= drv->tz_header_size;

		pil->tz_vi.image_info->ss_prop.ssid = SS_MSADP;
		pil->tz_vi.image_info->ss_prop.para_num = 4;
		pil->tz_vi.image_info->ss_prop.paras[0].offset = RMB_DP_IMAGE_START;
		pil->tz_vi.image_info->ss_prop.paras[0].value = drv->dp_phys ? drv->dp_phys : pil_get_entry_addr(pil);
		pil->tz_vi.image_info->ss_prop.paras[1].offset = RMB_DP_CODE_START;
		pil->tz_vi.image_info->ss_prop.paras[1].value = drv->dp_phys ? drv->dp_phys : pil_get_entry_addr(pil);
		pil->tz_vi.image_info->ss_prop.paras[2].offset = RMB_DP_CODE_LENGTH;
		pil->tz_vi.image_info->ss_prop.paras[2].value = drv->dp_size;
		pil->tz_vi.image_info->ss_prop.paras[3].offset = RMB_MBA_COMMAND;
		pil->tz_vi.image_info->ss_prop.paras[3].value = 0x01;

		pil->tz_vi.image_info->header_paddr = virt_to_phys(sig_header);
		pil->tz_vi.image_info->header_size = drv->tz_header_size;
		pil->tz_vi.image_info->image_seg_num = 1;

		pil->tz_vi.image_info->ism[0].paddr = drv->dp_phys ? drv->dp_phys : pil_get_entry_addr(pil);
		pil->tz_vi.image_info->ism[0].sz = count;

	}

	memcpy(dp_virt, data, count);
	wmb();

	release_firmware(fw);
	fw = NULL;
	pr_debug("load_msadp ->pil_notify_Qtang_load_msadp_ready!\n");
	if(drv->self_auth){
		ret = pil_notify_Qtang_load_msadp_ready(pil, fw_name);
		if (ret) {
			pr_err("msadp boot failed(rc:%d)\n", ret);
			goto err_mss_reset;
		}
	}else{
		if (drv->tz_verify)
			ret = pil_notify_Qtang_load_msadp_ready_trusted(pil, fw_name);
		else
			ret = pil_notify_Qtang_load_msadp_ready_trusted_smc(pil, fw_name);
		if (ret) {
			pr_err("msadp boot failed(rc:%d)\n", ret);
			goto out_memfree;
		}
	}
	pr_info("load_msadp exit!\n");

	kfree(pil->tz_vi.image_info);
	pil->tz_vi.image_info = NULL;
	kfree(sig_header);

	return 0;

out_memfree:
	pr_err("load msadp fail! %s session:%d shmid:%d image_info:%lx sig_header:%lx\n", pil->name, pil->tz_vi.session_id, pil->tz_vi.shm_id, pil->tz_vi.image_info, sig_header);
	if (pil->tz_vi.image_info) {
		kfree(pil->tz_vi.image_info);
		pil->tz_vi.image_info = NULL;
	}
	kfree(sig_header);

/*	tz_verifier_free_shm(pil->tz_vi.shm_id);
	pil->tz_vi.shm_id = -1;
	tz_verifier_deinit(pil->tz_vi.session_id, 1);
	pil->tz_vi.session_id = -1;
*/

err_mss_reset:
	if (fw)
		release_firmware(fw);
	drv->dp_virt = NULL;

	return ret;
}

static int pil_mss_reset_load_qbl(const struct subsys_desc *desc)
{
	struct pil_tz_data *drv = subsys_to_data(desc);
	struct pil_desc *pil = &drv->desc;
	const struct firmware *fw = NULL;
	char fw_name[10] = "qbl.mbn";
	char *sig_header = NULL;
	void *qbl_virt;
	dma_addr_t qbl_phys, qbl_phys_end;
	int ret, count;
	const u8 *data;
	struct pil_priv *priv = pil->priv;
	pr_debug("load_qbl enter!\n");

	/* Load and authenticate mba image */
	ret = request_firmware(&fw, fw_name, pil->dev);
	if (ret) {
		pr_err("Failed to locate %s\n",
						fw_name);
		return ret;
	}

	drv->qbl_size = fw->size;
	drv->qbl_size = ALIGN(drv->qbl_size, SZ_1M);

	//get qbl_phy_addr from dtsi
	if(priv){
		qbl_phys = priv->qbl_pa;
		qbl_virt = priv->qbl_va;
		pr_debug("load_qbl->,[%pa][%zu][%px]\n",&(qbl_phys),drv->qbl_size,(void*)qbl_virt);
	}else{
		pr_err("load_qbl,can not get image phy address\n");
		ret = -ENOMEM;
		goto err_mss_reset;
	}

	drv->qbl_phys = qbl_phys;
	drv->qbl_virt = qbl_virt;
	qbl_phys_end = qbl_phys + drv->qbl_size;

	/* Load the MBA image into memory */
	count = fw->size;
	data = fw ? fw->data : NULL;
	if (!data) {
		pr_err("qbl data is NULL\n");
		ret = -ENOMEM;
		goto err_mss_reset;
	}
	if (!drv->self_auth && drv->tz_verify) {
		size_t size = 0;

		if (count < drv->tz_header_size) {
			dev_err(pil->dev, "%s too small, NOT signed?\n", pil->fw_name);
			ret = -EINVAL;
			goto err_mss_reset;
		}

		if (pil->tz_vi.session_id < 0) {
			pil->tz_vi.session_id = tz_verifier_init();
			if (pil->tz_vi.session_id < 0) {
				dev_err(pil->dev, "%s verify get session fail\n", pil->fw_name);
				ret = -EBUSY;
				goto err_mss_reset;
			}
		}
		if (pil->tz_vi.shm_id >= 0) {
			tz_verifier_get_shm_info(pil->tz_vi.shm_id, NULL, &size);
		}

		pil->tz_vi.argsize = sizeof(struct verifier_image_info);

		if (size < TZ_SHM_SIZE(0)) {
			tz_verifier_free_shm(pil->tz_vi.shm_id);
			pil->tz_vi.shm_id = -1;
		}
		if (pil->tz_vi.shm_id < 0) {
			pil->tz_vi.shm_id = tz_verifier_alloc_shm(pil->tz_vi.argsize);
			if (pil->tz_vi.shm_id < 0) {
				dev_err(pil->dev, "%s verify alloc shm fail:%d\n", pil->fw_name, pil->tz_vi.shm_id);
				ret = -ENOMEM;
				goto err_mss_reset;
			}
		}

		pil->tz_vi.argsize = sizeof(struct verifier_image_info);
		pil->tz_vi.image_info = kzalloc(pil->tz_vi.argsize, GFP_KERNEL);
		if (!pil->tz_vi.image_info) {
			pr_err("%s verify alloc mem %d fail\n", fw_name, pil->tz_vi.argsize);
			ret = -ENOMEM;
			goto err_mss_reset;
		}
		sig_header = kmalloc(drv->tz_header_size, GFP_KERNEL);
		if (!sig_header) {
			pr_err("%s verify alloc metadata mem %d fail\n", fw_name, drv->tz_header_size);
			ret = -ENOMEM;
			goto out_memfree;
		}
		memcpy(sig_header, data, drv->tz_header_size);
		data += drv->tz_header_size;
		count -= drv->tz_header_size;

		pil->tz_vi.image_info->ss_prop.ssid = SS_QBL;
		pil->tz_vi.image_info->ss_prop.para_num = 2;
		pil->tz_vi.image_info->ss_prop.paras[0].offset = RMB_MBA_IMAGE;
		pil->tz_vi.image_info->ss_prop.paras[0].value = drv->qbl_phys ? drv->qbl_phys : pil_get_entry_addr(pil);
		pil->tz_vi.image_info->ss_prop.paras[1].offset = QTANG_BOOT_REQUEST;
		pil->tz_vi.image_info->ss_prop.paras[1].value = 0x01;

		pil->tz_vi.image_info->header_paddr = virt_to_phys(sig_header);
		pil->tz_vi.image_info->header_size = drv->tz_header_size;
		pil->tz_vi.image_info->image_seg_num = 1;

		pil->tz_vi.image_info->ism[0].paddr = drv->qbl_phys ? drv->qbl_phys : pil_get_entry_addr(pil);
		pil->tz_vi.image_info->ism[0].sz = count;

	}

	memcpy(qbl_virt, data, count);
	wmb();

	release_firmware(fw);
	fw = NULL;

	pr_debug("load_qbl ->pil_notify_Qtang_load_qbl_ready!\n");
	if(drv->self_auth){
		ret = pil_notify_Qtang_load_qbl_ready(pil, fw_name);
		if (ret) {
			pr_err("qbl boot failed(rc:%d)\n", ret);
			goto err_mss_reset;
		}
	}else{
		if (drv->tz_verify)
			ret = pil_notify_Qtang_load_qbl_ready_trusted(pil, fw_name);
		else
			ret = pil_notify_Qtang_load_qbl_ready_trusted_smc(pil, fw_name);
		if (ret) {
			pr_err("qbl tz boot failed(rc:%d)\n", ret);
			goto out_memfree;
		}
	}
	pr_info("load_qbl exit!\n");

	kfree(pil->tz_vi.image_info);
	pil->tz_vi.image_info = NULL;
	kfree(sig_header);

	return 0;

out_memfree:
	pr_err("load qbl fail! %s session:%d shmid:%d image_info:%lx sig_header:%lx\n", pil->name, pil->tz_vi.session_id, pil->tz_vi.shm_id, pil->tz_vi.image_info, sig_header);
	if (pil->tz_vi.image_info) {
		kfree(pil->tz_vi.image_info);
		pil->tz_vi.image_info = NULL;
	}
	kfree(sig_header);

/*	tz_verifier_free_shm(pil->tz_vi.shm_id);
	pil->tz_vi.shm_id = -1;
	tz_verifier_deinit(pil->tz_vi.session_id, 1);
	pil->tz_vi.session_id = -1;
*/

err_mss_reset:
	if (fw)
		release_firmware(fw);
	drv->qbl_virt = NULL;

	return ret;
}

static int pil_msa_auth_modem_mdt(struct pil_desc *pil, const u8 *metadata,
				  size_t size)
{
	struct pil_tz_data *drv = desc_to_data(pil);
	void *mdata_virt;
	dma_addr_t mdata_phys;
	int ret = 0;
	struct pil_priv *priv = pil->priv;

	//get qbl_phy_addr from dtsi
	if(priv){
		mdata_phys = priv->modem_mdt_pa;
		mdata_virt = priv->modem_mdt_va;
		pr_debug("load_modem_mdt,[%pa][%zu][%px]\n",&(mdata_phys),size,(void*)mdata_virt);
	}else{
		pr_err("load_modem_mdt,can not get image phy address\n");
		ret = -ENOMEM;
		goto fail;
	}

	memcpy(mdata_virt, metadata, size);
	/* wmb() ensures copy completes prior to starting authentication. */
	wmb();

	if (pil->subsys_vmid > 0)
		pil_assign_mem_to_linux(pil, mdata_phys, ALIGN(size, SZ_4K));

	pr_debug("load_modem_mdt exit!ret:[%d]\n",ret);

	if (!ret)
		return ret;

fail:
	if(drv->self_auth){
		modem_log_rmb_regs(drv->rmb_base);
	}

	return ret;
}

static int pil_msa_mba_auth_trusted_smc(struct pil_desc *pil)
{
	struct pil_tz_data *drv = desc_to_data(pil);
	s32 status;
	int ret = 0;
	uint32_t mata_data_ready = 0x3;
	struct pil_priv *priv = pil->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(modem_auth_timeout_ms * 50);

	pr_debug("pil_msa_mba_auth_trusted enter,[%pa][%pa][%d][%pa]!\n", &priv->modem_mdt_pa, &priv->modem_pa, priv->region_size, &drv->rmb_base_phy);

	/* Pass address of meta-data to the MBA and perform authentication */
	ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_PMI_META_DATA, (u32 *)&(priv->modem_mdt_pa), PIL_WRITE_QTANG2);
	if (ret) {
		pr_err("pil_msa_mba_auth_trusted write RMB_PMI_META_DATA fail\n");
		return ret;
	}
	ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_PMI_CODE_START, (u32 *)&(priv->modem_pa), PIL_WRITE_QTANG2);
	if (ret) {
		pr_err("pil_msa_mba_auth_trusted write RMB_PMI_CODE_START fail\n");
		return ret;
	}
	ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_PMI_CODE_LENGTH, (u32 *)&(priv->region_size), PIL_WRITE_QTANG2);
	if (ret) {
		pr_err("pil_msa_mba_auth_trusted write RMB_PMI_CODE_LENGTH fail\n");
		return ret;
	}
	ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_MBA_COMMAND, &(mata_data_ready), PIL_WRITE_QTANG2);
	if (ret) {
		pr_err("pil_msa_mba_auth_trusted write RMB_MBA_COMMAND fail\n");
		return ret;
	}
	pr_debug("pil_msa_mba_auth_trusted enter loop for waiting 0x3F\n");

	do {
		ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, drv->rmb_base_phy + RMB_MBA_STATUS, &status, PIL_READ_QTANG2);
		if (ret) {
			pr_err("pil_msa_mba_auth_trusted read RMB_PBL_STATUS fail\n");
			return ret;
		}
		msleep(500);
		if (time_after(jiffies, timeout)) {
			pr_err("pil_msa_mba_auth_trusted read RMB_MBA_STATUS timed out status:[%x]\n", status);
			drv->is_booted = false;
			return -1;
		}
	} while ((status & 0x20) != 0x20);

	pr_info("pil_msa_mba_auth_trusted exit loop, read RMB_PBL_STATUS is:[%x]\n", status);
	drv->is_booted = true;

	return 0;
}
static int pil_msa_mba_auth_trusted(struct pil_desc *pil)
{
	int ret;
	struct pil_tz_data *drv = desc_to_data(pil);
	struct verifier_reg_op verifier_reg;
	unsigned long timeout = jiffies + msecs_to_jiffies(modem_auth_timeout_ms * 50);
	int offset;
	unsigned int status;
	char *buffer;
	size_t size;

	if (tz_verifier_get_shm_info(pil->tz_vi.shm_id, &buffer, &size)) {
		pr_err("modem shm is NULL\n");
		return -ENOMEM;
	}

	if (size < pil->tz_vi.argsize) {
		pr_err("shm is too small! %d < %d\n", size, pil->tz_vi.argsize);
	}
	memcpy(buffer, pil->tz_vi.image_info, pil->tz_vi.argsize);

	ret = tz_verifier_invode_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, TZCMD_AUTH_BOOT, pil->tz_vi.argsize);
	if (ret) {
		pr_err("%s auth and boot fail! ret:%d\n", __func__, ret);
		return ret;
	}
	pr_debug("pil_msa_mba_auth_trusted enter loop for waiting 0x3F\n");

	pil->tz_vi.argsize = sizeof(verifier_reg);
	verifier_reg.ssid = SS_MODEM;
	verifier_reg.op_type = TZCMD_REG_READ;
	verifier_reg.offset = RMB_MBA_STATUS;
	offset = offsetof(struct verifier_reg_op, value);
	memcpy(buffer, &verifier_reg, pil->tz_vi.argsize);

	do {
		msleep(500);
		ret = tz_verifier_invode_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, TZCMD_OP_REG, pil->tz_vi.argsize);
		if (ret) {
			pr_err("%s read RMB_MBA_STATUS fail ret:%d\n", __func__, ret);
			return ret;
		}
		memcpy(&status, &buffer[offset], sizeof(unsigned int));
		if (time_after(jiffies, timeout)) {
			pr_err("%s read RMB_MBA_STATUS timed out (rc:%d),status:[%d]\n", __func__, ret, status);
			drv->is_booted = false;
			return -ETIMEDOUT;
		}
	} while ((status & 0x20) != 0x20);

	pr_info("pil_msa_mba_auth_trusted exit loop, read RMB_PBL_STATUS is:[%x]\n", status);
	drv->is_booted = true;

	return 0;
}

static int pil_msa_mba_auth(struct pil_desc *pil)
{

	struct pil_tz_data *drv = desc_to_data(pil);
	s32 status;
	int ret = 0;
	u64 val = modem_auth_timeout_ms * 1000;//is_timeout_disabled() ? 0 : modem_auth_timeout_ms * 1000;
	struct pil_priv *priv = pil->priv;
	pr_debug("mba_auth enter,[%pa][%pa][%d]!\n",&priv->modem_mdt_pa,&priv->modem_pa,priv->region_size);

	/* Pass address of meta-data to the MBA and perform authentication */
	writel_relaxed(priv->modem_mdt_pa, drv->rmb_base + RMB_PMI_META_DATA);
	writel_relaxed(priv->modem_pa, drv->rmb_base + RMB_PMI_CODE_START);
	writel_relaxed(priv->region_size, drv->rmb_base + RMB_PMI_CODE_LENGTH);
	mb();
	writel_relaxed(CMD_META_DATA_READY, drv->rmb_base + RMB_MBA_COMMAND);
	mb();

	ret = readl_poll_timeout(drv->rmb_base + RMB_MBA_STATUS, status,
			(status & 0x20) == 0x20 || status < 0,
			POLL_INTERVAL_US, val);
	if (ret) {
		pr_err("MBA authentication of headers timed out(rc:%d)\n",
								ret);
	} else if (status < 0) {
		pr_err("MBA returned error %02x for headers\n",
				status);
		ret = -EINVAL;
	}

	if (ret){
		modem_log_rmb_regs(drv->rmb_base);
		drv->is_booted = false;
	}else
		drv->is_booted = true;

	pr_info("mba_auth exit!status:[%02x][%d]\n",status,drv->is_booted);

	return ret;

}

struct pil_reset_ops pil_msa_mss_ops_selfauth = {
	.init_image = pil_msa_auth_modem_mdt,
	.auth_and_reset = pil_msa_mba_auth,
	.shutdown = pil_mss_shutdown,
};

static struct pil_reset_ops pil_ops_trusted = {
	.init_image = pil_init_image_trusted,
	.mem_setup =  pil_mem_setup_trusted,
	.auth_and_reset = pil_auth_and_reset,
	.shutdown = pil_shutdown_trusted,
	.proxy_vote = pil_make_proxy_vote,
	.proxy_unvote = pil_remove_proxy_vote,
	.deinit_image = pil_deinit_image_trusted,
};

static void  subsys_disable_all_irqs_ex(const struct subsys_desc *desc)
{
	struct pil_tz_data *drv = subsys_to_data(desc);

	subsys_disable_others_irqs(drv);
	if(drv->wdog_irq_enabled == 1){
		subsys_disable_wdog_irq(drv);
	}
}

static void log_failure_reason(const struct pil_tz_data *d)
{
	size_t size;
	char *smem_reason, reason[MAX_SSR_REASON_LEN];
	const char *name = d->subsys_desc.name;

	if (d->smem_id == -1)
		return;

	smem_reason = qcom_smem_get(QCOM_SMEM_HOST_ANY, d->smem_id, &size);
	if (IS_ERR(smem_reason) || !size) {
		pr_err("%s SFR: (unknown, qcom_smem_get failed).\n",
									name);
		return;
	}
	if (!smem_reason[0]) {
		pr_err("%s SFR: (unknown, empty string found).\n", name);
		return;
	}
	strlcpy(last_modem_sfr_reason, smem_reason, min(size, (size_t)MAX_SSR_REASON_LEN));
	//pr_err("%s subsystem failure reason: %s.\n", name, last_modem_sfr_reason);
	strlcpy(reason, smem_reason, min(size, (size_t)MAX_SSR_REASON_LEN));
	pr_err("%s subsystem failure reason: %s.\n", name, reason);
	snprintf(last_ssr_reason, (size_t)MAX_SSR_REASON_LEN, "%s: %s", name, reason);
}

static int subsys_shutdown(const struct subsys_desc *subsys, bool force_stop)
{
	struct pil_tz_data *d = subsys_to_data(subsys);
	int ret;
	pr_err("subsys_shutdown enter:[%d][%d][%pa]\n", subsys_get_crash_status(d->subsys), force_stop,subsys->state);

	if (!subsys_get_crash_status(d->subsys) && force_stop &&
						d->state) {
		pr_info("subsys_shutdown->qcom_smem_state_update_bits.\n");
		qcom_smem_state_update_bits(d->state,
				BIT(d->force_stop_bit),
				BIT(d->force_stop_bit));
		ret = wait_for_completion_timeout(&d->stop_ack,
				msecs_to_jiffies(STOP_ACK_TIMEOUT_MS));
		if (!ret)
			pr_warn("Timed out on stop ack from %s.\n",
							subsys->name);
		qcom_smem_state_update_bits(d->state,
				BIT(d->force_stop_bit), 0);
	}

	if (0 != strcmp(d->desc.name, "adsp")) {
		pil_shutdown(&d->desc);
	} else {
		pr_info("adsp bypass pil shutdown. name:[%s]!\n",d->desc.name);
	}
	subsys_disable_all_irqs_ex(subsys);
	return 0;
}

static int subsys_powerup(const struct subsys_desc *subsys)
{
	struct pil_tz_data *d = subsys_to_data(subsys);
	int ret = 0;

	reinit_completion(&d->err_ready);

	if (d->stop_ack_irq)
		reinit_completion(&d->stop_ack);

	d->desc.fw_name = subsys->fw_name;
	if (0 != strcmp(subsys->name, "adsp")) {
		ret = pil_boot(&d->desc);
		if (ret) {
			pr_err("pil_boot failed for %s\n",  d->subsys_desc.name);
			return ret;
		}

		pr_info("pil_boot is successful from %s and waiting for error ready\n",
				d->subsys_desc.name);
	} else {
		pr_debug("adsp bypass pil boot. name:[%s]!\n",subsys->name);
	}

	if((0 == strcmp(subsys->name, "modem"))&&(subsys->vote_flag == 1))
	{
		pil_remove_vote_for_npa(subsys);
		pil_remove_vote_for_qtang_cxmx(subsys);
		notify_proxy_unvote(d->desc.dev);
	}
	subsys_enable_all_irqs(d);
	ret = wait_for_err_ready(d);
	if (ret) {
		pr_err("%s failed to get error ready for %s\n", __func__,
			d->subsys_desc.name);
		if (0 != strcmp(subsys->name, "adsp")) {
			pil_shutdown(&d->desc);
		}
		subsys_disable_all_irqs_ex(subsys);
	}

	return ret;
}

static int subsys_powerup_boot_enabled(const struct subsys_desc *subsys)
{
	struct pil_tz_data *d = subsys_to_data(subsys);
	int ret = 0;

	if (generic_read_status(d)) {
		pr_info("%s: subsystem %s is alive at during device bootup\n",
			 __func__, d->subsys_desc.name);
		subsys_enable_all_irqs(d);
		ret = wait_for_err_ready(d);
		if (ret) {
			pr_err("%s failed to get error ready for %s\n",
				__func__, d->subsys_desc.name);
			pil_shutdown(&d->desc);
			subsys_disable_all_irqs_ex(subsys);
		}
	} else {
		pil_shutdown(&d->desc);
		ret = -EAGAIN;
		pr_err("%s: subsystem %s is crashed while device booting\n",
			 __func__, d->subsys_desc.name);
	}

	/* Update the .powerup call back to regular subsys powerup function.*/
	d->subsys_desc.powerup = subsys_powerup;
	return ret;
}

static int subsys_ramdump(int enable, const struct subsys_desc *subsys)
{
	struct pil_tz_data *d = subsys_to_data(subsys);
	struct pil_desc *pil = &d->desc;
	u32 ramdump_flag,value;
	int ret = 0;

	pr_debug("subsys_ramdump enter enable:[%d],name:[%s]!\n", enable,d->desc.name);
	//original way to check if ramdump is supported. echo 1 > /sys/module/subsystem_restart/parameters/enable_ramdumps
	if (!enable)
		return 0;

	if (0 == strcmp(d->desc.name, "modem")){
		pr_debug("subsys_ramdump read RMB_MODEM_SSR_DUMP reg -start\n");
		if(d->self_auth){
			value = readl_relaxed(d->rmb_base + RMB_SSR_DUMP_FLAG);
		}else{
			if (d->tz_verify) {
				struct verifier_reg_op reg_op;

				reg_op.ssid = SS_MODEM;
				reg_op.offset = RMB_SSR_DUMP_FLAG;
				reg_op.op_type = TZCMD_REG_READ;
				ret = tz_verifier_reg_command(pil->tz_vi.session_id, pil->tz_vi.shm_id, &reg_op);
				if (ret) {
					pr_err("wq_func->read RMB_SSR_DUMP_FLAG(for modem restart) fail %d\n", ret);
					return -1;
				}
				value = reg_op.value;
			} else {
				ret = optee_handle_register(OPTEE_SMC_CALL_PIL_RW_QTANG2, d->rmb_base_phy + RMB_SSR_DUMP_FLAG, &value, PIL_READ_QTANG2);
				if (ret) {
					pr_err("%s read RMB_SSR_DUMP_FLAG fail\n", __func__);
					return -1;
				}
			}
		}
		ramdump_flag = (value & MODEM_SSR_DUMP_MASK);
		pr_debug("%s read RMB_MODEM_SSR_DUMP reg -end,ramdump_flag:[%x],value:[%x]\n", __func__, ramdump_flag, value);
		if(!(ramdump_flag == 0x02))
			return 0;
	}

	return pil_do_ramdump(&d->desc, d->ramdump_dev, d->minidump_dev);
}

static void subsys_free_memory(const struct subsys_desc *subsys)
{
	struct pil_tz_data *d = subsys_to_data(subsys);

	pil_free_memory(&d->desc);
}

static void subsys_crash_shutdown(const struct subsys_desc *subsys)
{
	struct pil_tz_data *d = subsys_to_data(subsys);
	pr_err("subsys_crash_shutdown is triggerred!\n");

	if (d->state && !subsys_get_crash_status(d->subsys)) {
	pr_err("subsys_crash_shutdown->update force_stop_bit\n");
		qcom_smem_state_update_bits(d->state,
			BIT(d->force_stop_bit),
			BIT(d->force_stop_bit));
		mdelay(CRASH_STOP_ACK_TO_MS);
	}
}


static irqreturn_t subsys_err_ready_intr_handler(int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;

	pr_err("Received error ready interrupt from %s\n",
					d->subsys_desc.name);
	complete(&d->err_ready);
	return IRQ_HANDLED;
}

static irqreturn_t subsys_err_fatal_intr_handler (int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;

	pr_err("Received error Fatal interrupt from %s!\n", d->subsys_desc.name);
	if (subsys_get_crash_status(d->subsys)) {
		pr_err("%s: Ignoring error fatal, restart in progress\n",
							d->subsys_desc.name);
		return IRQ_HANDLED;
	}
	subsys_set_crash_status(d->subsys, CRASH_STATUS_ERR_FATAL);
	log_failure_reason(d);
	subsystem_restart_dev(d->subsys);

	#ifdef CHECK_NV_DESTROYED_MI
	if (strnstr(last_modem_sfr_reason, STR_NV_SIGNATURE_DESTROYED, strlen(last_modem_sfr_reason))) {
		pr_err("errimei_dev: the NV has been destroyed, should restart to recovery\n");
		schedule_delayed_work(&create_kobj_work, msecs_to_jiffies(1*1000));
	}
	#endif
	return IRQ_HANDLED;
}

static irqreturn_t subsys_wdog_bite_irq_handler(int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;
	pr_err("Received wdog on %s!\n", d->subsys_desc.name);
	subsys_disable_wdog_irq(d);

	if (subsys_get_crash_status(d->subsys)){
		pr_err("%s: Ignoring wdog, restart in progress\n",
							d->subsys_desc.name);
		return IRQ_HANDLED;
	}
	pr_err("Watchdog bite received from %s![%d]\n", d->subsys_desc.name,d->subsys_desc.system_debug);

	if (d->subsys_desc.system_debug)
		panic("%s: wdog on modem and System ramdump requested. Triggering device restart!\n",
							__func__);
	subsys_set_crash_status(d->subsys, CRASH_STATUS_WDOG_BITE);
	log_failure_reason(d);
	subsystem_restart_dev(d->subsys);

	return IRQ_HANDLED;
}

static irqreturn_t subsys_qtang2_wdog_bite_irq_handler(int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;
	pr_err("Received wdog on qtang2!\n");

	panic("%s: wdog on qtang2 and System ramdump requested. Triggering device restart!\n",
						__func__);
	return IRQ_HANDLED;
}

static irqreturn_t subsys_stop_ack_intr_handler(int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;

	pr_err("Received stop ack interrupt from %s\n", d->subsys_desc.name);
	complete(&d->stop_ack);
	return IRQ_HANDLED;
}

static irqreturn_t subsys_shutdown_ack_intr_handler(int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;

	pr_err("Received shutdown ack interrupt from %s\n",
			d->subsys_desc.name);
	complete_shutdown_ack(&d->subsys_desc);
	return IRQ_HANDLED;
}

static irqreturn_t subsys_ramdump_disable_intr_handler(int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;

	pr_err("Received ramdump disable interrupt from %s\n",
			d->subsys_desc.name);
	d->subsys_desc.ramdump_disable = 1;
	return IRQ_HANDLED;
}

static void clear_pbl_done(struct pil_tz_data *d)
{
	uint32_t err_value;

	err_value =  __raw_readl(d->err_status);

	if (err_value) {
		uint32_t rmb_err_spare0;
		uint32_t rmb_err_spare1;
		uint32_t rmb_err_spare2;

		pr_debug("PBL_DONE received from %s!\n", d->subsys_desc.name);

		rmb_err_spare2 =  __raw_readl(d->err_status_spare);
		rmb_err_spare1 =  __raw_readl(d->err_status_spare-4);
		rmb_err_spare0 =  __raw_readl(d->err_status_spare-8);

		pr_err("PBL error status register: 0x%08x\n", err_value);

		pr_err("PBL error status spare0 register: 0x%08x\n",
			rmb_err_spare0);
		pr_err("PBL error status spare1 register: 0x%08x\n",
			rmb_err_spare1);
		pr_err("PBL error status spare2 register: 0x%08x\n",
			rmb_err_spare2);
	} else {
		pr_info("PBL_DONE - 1st phase loading [%s] completed ok\n",
			d->subsys_desc.name);
	}
	__raw_writel(BIT(d->bits_arr[PBL_DONE]), d->irq_clear);
}

static void clear_err_ready(struct pil_tz_data *d)
{
	pr_debug("Subsystem error services up received from %s\n",
							d->subsys_desc.name);

	pr_info("SW_INIT_DONE - 2nd phase loading [%s] completed ok\n",
		d->subsys_desc.name);

	__raw_writel(BIT(d->bits_arr[ERR_READY]), d->irq_clear);
	complete(&d->err_ready);
}

static void clear_sw_init_done_error(struct pil_tz_data *d, int err)
{
	uint32_t rmb_err_spare0;
	uint32_t rmb_err_spare1;
	uint32_t rmb_err_spare2;

	pr_info("SW_INIT_DONE - ERROR [%s] [0x%x].\n",
		d->subsys_desc.name, err);

	rmb_err_spare2 =  __raw_readl(d->err_status_spare);
	rmb_err_spare1 =  __raw_readl(d->err_status_spare-4);
	rmb_err_spare0 =  __raw_readl(d->err_status_spare-8);

	pr_err("spare0 register: 0x%08x\n", rmb_err_spare0);
	pr_err("spare1 register: 0x%08x\n", rmb_err_spare1);
	pr_err("spare2 register: 0x%08x\n", rmb_err_spare2);

	/* Clear the interrupt source */
	__raw_writel(BIT(d->bits_arr[ERR_READY]), d->irq_clear);
}



static void clear_wdog(struct pil_tz_data *d)
{
	/* Check crash status to know if device is restarting*/
	if (!subsys_get_crash_status(d->subsys)) {
		pr_err("wdog bite received from %s!\n", d->subsys_desc.name);
		__raw_writel(BIT(d->bits_arr[ERR_READY]), d->irq_clear);
		subsys_set_crash_status(d->subsys, CRASH_STATUS_WDOG_BITE);
		log_failure_reason(d);
		subsystem_restart_dev(d->subsys);
	}
}

static bool generic_read_status(struct pil_tz_data *d)
{
	uint32_t status_val, err_value;

	err_value =  __raw_readl(d->err_status_spare);
	status_val = __raw_readl(d->irq_status);

	if (status_val & BIT(d->bits_arr[ERR_READY])) {
		if (err_value == 0x44554d50) {
			pr_err("wdog bite is pending\n");
			__raw_writel(BIT(d->bits_arr[ERR_READY]), d->irq_clear);
			return false;
		}
	}

	return true;
}

static irqreturn_t subsys_generic_handler(int irq, void *drv_data)
{
	struct pil_tz_data *d = drv_data;
	uint32_t status_val, err_value;

	err_value =  __raw_readl(d->err_status_spare);
	status_val = __raw_readl(d->irq_status);

	if (status_val & BIT(d->bits_arr[ERR_READY])) {
		if (!err_value)
			clear_err_ready(d);
		else if (err_value == 0x44554d50)
			clear_wdog(d);
		else
			clear_sw_init_done_error(d, err_value);
	}

	if (status_val & BIT(d->bits_arr[PBL_DONE]))
		clear_pbl_done(d);

	return IRQ_HANDLED;
}

static void mask_scsr_irqs(struct pil_tz_data *d)
{
	uint32_t mask_val;

	/* Masking all interrupts from subsystem */
	mask_val = ~0;
	__raw_writel(mask_val, d->irq_mask);
}

static void unmask_scsr_irqs(struct pil_tz_data *d)
{
	uint32_t mask_val;

	/* Un masking interrupts from subsystem to be handled by HLOS */
	mask_val = ~0;
	__raw_writel(mask_val & ~BIT(d->bits_arr[ERR_READY]) &
			~BIT(d->bits_arr[PBL_DONE]), d->irq_mask);
}

static void subsys_enable_all_irqs(struct pil_tz_data *d)
{
	pr_debug("enable_all_irqs enter:[%d][%d][%d][%d][%d][%d]\n",d->err_ready_irq,d->wdog_bite_irq,d->qtang2_wdog_bite_irq,d->err_fatal_irq,d->stop_ack_irq,d->shutdown_ack_irq);

	if (d->err_ready_irq)
		enable_irq(d->err_ready_irq);
	if (d->wdog_bite_irq) {
		enable_irq(d->wdog_bite_irq);
		d->wdog_irq_enabled = 1; //1 means irq is enabled
		irq_set_irq_wake(d->wdog_bite_irq, 1);
	}
	if (d->qtang2_wdog_bite_irq) {
		enable_irq(d->qtang2_wdog_bite_irq);
		irq_set_irq_wake(d->qtang2_wdog_bite_irq, 1);
	}
	if (d->err_fatal_irq)
		enable_irq(d->err_fatal_irq);
	if (d->stop_ack_irq)
		enable_irq(d->stop_ack_irq);
	if (d->shutdown_ack_irq)
		enable_irq(d->shutdown_ack_irq);
	if (d->ramdump_disable_irq)
		enable_irq(d->ramdump_disable_irq);
	if (d->generic_irq) {
		unmask_scsr_irqs(d);
		enable_irq(d->generic_irq);
		irq_set_irq_wake(d->generic_irq, 1);
	}
}

static void subsys_disable_others_irqs(struct pil_tz_data *d)
{
	pr_debug("disable_others_irqs enter:[%d][%d][%d][%d][%d][%d]\n",d->err_ready_irq,d->wdog_bite_irq,d->qtang2_wdog_bite_irq,d->err_fatal_irq,d->stop_ack_irq,d->shutdown_ack_irq);

	if (d->err_ready_irq)
		disable_irq(d->err_ready_irq);
	if (d->qtang2_wdog_bite_irq) {
		disable_irq(d->qtang2_wdog_bite_irq);
		irq_set_irq_wake(d->qtang2_wdog_bite_irq, 0);
	}
	if (d->err_fatal_irq)
		disable_irq(d->err_fatal_irq);
	if (d->stop_ack_irq)
		disable_irq(d->stop_ack_irq);
	if (d->shutdown_ack_irq)
		disable_irq(d->shutdown_ack_irq);
	if (d->generic_irq) {
		mask_scsr_irqs(d);
		irq_set_irq_wake(d->generic_irq, 0);
		disable_irq(d->generic_irq);
	}
}

static void subsys_disable_wdog_irq(struct pil_tz_data *d)
{
	pr_debug("disable_wdog_irq enter:[%d]\n",d->wdog_bite_irq);

	if (d->wdog_bite_irq) {
		disable_irq_nosync(d->wdog_bite_irq);
		d->wdog_irq_enabled = 0; //0 means irq is disabled
		irq_set_irq_wake(d->wdog_bite_irq, 0);
	}
}


static int __get_irq(struct platform_device *pdev, const char *prop,
		unsigned int *irq)
{
	int irql = 0;
	struct device_node *dnode = pdev->dev.of_node;

	if (of_property_match_string(dnode, "interrupt-names", prop) < 0)
		return -ENOENT;

	irql = of_irq_get_byname(dnode, prop);
	if (irql < 0) {
		pr_err("[%s]: Error getting IRQ \"%s\"\n", pdev->name,
		prop);
		return irql;
	}
	*irq = irql;
	return 0;
}

static int __get_smem_state(struct pil_tz_data *d, const char *prop,
		int *smem_bit)
{
	struct device_node *dnode = d->dev->of_node;

	if (of_find_property(dnode, "qcom,smem-states", NULL)) {
		d->state = qcom_smem_state_get(d->dev, prop, smem_bit);
		if (IS_ERR_OR_NULL(d->state)) {
			pr_err("Could not get smem-states %s\n", prop);
			return PTR_ERR(d->state);
		}
		return 0;
	}
	return -ENOENT;
}


static int subsys_parse_irqs(struct platform_device *pdev)
{
	int ret;
	struct pil_tz_data *d = platform_get_drvdata(pdev);

	ret = __get_irq(pdev, "qcom,err-fatal", &d->err_fatal_irq);
	if (ret && ret != -ENOENT)
		return ret;

	ret = __get_irq(pdev, "qcom,err-ready", &d->err_ready_irq);
	if (ret && ret != -ENOENT)
		return ret;

	ret = __get_irq(pdev, "qcom,stop-ack", &d->stop_ack_irq);
	if (ret && ret != -ENOENT)
		return ret;

	ret = __get_irq(pdev, "qcom,ramdump-disabled",
			&d->ramdump_disable_irq);
	if (ret && ret != -ENOENT)
		return ret;

	ret = __get_irq(pdev, "qcom,shutdown-ack", &d->shutdown_ack_irq);
	if (ret && ret != -ENOENT)
		return ret;

	ret = __get_irq(pdev, "qcom,wdog", &d->wdog_bite_irq);
	if (ret && ret != -ENOENT)
		return ret;

	ret = __get_irq(pdev, "qcom,qtang2-wdog", &d->qtang2_wdog_bite_irq);
	if (ret && ret != -ENOENT)
		return ret;

	ret = __get_smem_state(d, "qcom,force-stop", &d->force_stop_bit);
	if (ret && ret != -ENOENT)
		return ret;

	if (of_property_read_bool(pdev->dev.of_node,
					"qcom,pil-generic-irq-handler")) {
		ret = platform_get_irq(pdev, 0);
		if (ret > 0)
			d->generic_irq = ret;
	}

	return 0;
}

static int subsys_setup_irqs(struct platform_device *pdev)
{
	int ret;
	struct pil_tz_data *d = platform_get_drvdata(pdev);

	if (d->err_fatal_irq) {
		ret = devm_request_threaded_irq(&pdev->dev, d->err_fatal_irq,
				NULL, subsys_err_fatal_intr_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				d->desc.name, d);
		if (ret < 0) {
			dev_err(&pdev->dev, "[%s]: Unable to register error fatal IRQ handler: %d, irq is %d\n",
				d->desc.name, ret, d->err_fatal_irq);
			return ret;
		}
		disable_irq(d->err_fatal_irq);
	}

	if (d->stop_ack_irq) {
		ret = devm_request_threaded_irq(&pdev->dev, d->stop_ack_irq,
				NULL, subsys_stop_ack_intr_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				d->desc.name, d);
		if (ret < 0) {
			dev_err(&pdev->dev, "[%s]: Unable to register stop ack handler: %d\n",
				d->desc.name, ret);
			return ret;
		}
		disable_irq(d->stop_ack_irq);
	}

	if (d->wdog_bite_irq) {
		ret = devm_request_irq(&pdev->dev, d->wdog_bite_irq,
			subsys_wdog_bite_irq_handler,
			IRQF_TRIGGER_HIGH, d->desc.name, d);
		if (ret < 0) {
			dev_err(&pdev->dev, "[%s]: Unable to register wdog bite handler: %d\n",
				d->desc.name, ret);
			return ret;
		}
		disable_irq(d->wdog_bite_irq);
	}

	if (d->qtang2_wdog_bite_irq) {
		ret = devm_request_irq(&pdev->dev, d->qtang2_wdog_bite_irq,
			subsys_qtang2_wdog_bite_irq_handler,
			IRQF_TRIGGER_HIGH, d->desc.name, d);
		if (ret < 0) {
			dev_err(&pdev->dev, "[%s]: Unable to register qtang2-wdog bite handler: %d\n",
				d->desc.name, ret);
			return ret;
		}
		disable_irq(d->qtang2_wdog_bite_irq);
	}

	if (d->shutdown_ack_irq) {
		ret = devm_request_threaded_irq(&pdev->dev,
				d->shutdown_ack_irq,
				NULL, subsys_shutdown_ack_intr_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				d->desc.name, d);
		if (ret < 0) {
			dev_err(&pdev->dev, "[%s]: Unable to register shutdown ack handler: %d\n",
				d->desc.name, ret);
			return ret;
		}
		disable_irq(d->shutdown_ack_irq);
	}

	if (d->ramdump_disable_irq) {
		ret = devm_request_threaded_irq(d->dev,
				d->ramdump_disable_irq,
				NULL, subsys_ramdump_disable_intr_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				d->desc.name, d);
		if (ret < 0) {
			dev_err(&pdev->dev, "[%s]: Unable to register shutdown ack handler: %d\n",
				d->desc.name, ret);
			return ret;
		}
		disable_irq(d->ramdump_disable_irq);
	}

	if (d->generic_irq) {
		ret = devm_request_irq(&pdev->dev, d->generic_irq,
			subsys_generic_handler,
			IRQF_TRIGGER_HIGH, d->desc.name, d);
		if (ret < 0) {
			dev_err(&pdev->dev, "[%s]: Unable to register generic irq handler: %d\n",
				d->desc.name, ret);
			return ret;
		}
		disable_irq(d->generic_irq);
	}

	if (d->err_ready_irq) {
		ret = devm_request_threaded_irq(d->dev,
				d->err_ready_irq,
				NULL, subsys_err_ready_intr_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				"error_ready_interrupt", d);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"[%s]: Unable to register err ready handler\n",
				d->desc.name);
			return ret;
		}
		disable_irq(d->err_ready_irq);
	}

	return 0;
}

static int pil_tz_generic_probe(struct platform_device *pdev)
{
	struct pil_tz_data *d;
	struct resource *res;
	u32 proxy_timeout, rmb_gp_reg_val;
	int len, rc;
	char md_node[20];

	/* Do not probe the generic PIL driver yet if the SCM BW driver
	 * is not yet registered. Return error if that driver returns with
	 * any error other than EPROBE_DEFER.
	 */
	#if 0 // not clear,need to do...
	if (!is_inited)
		return -EPROBE_DEFER;
	if (IS_ERR(scm_perf_client))
		return PTR_ERR(scm_perf_client);
	#endif
	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d){
		pr_err("%s: subsys_[%s] devm_kzalloc fail.\n",
				__func__, d->subsys_desc.name);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, d);

	if (of_property_read_bool(pdev->dev.of_node, "qcom,pil-no-auth"))
		d->subsys_desc.no_auth = true;

	d->keep_proxy_regs_on = of_property_read_bool(pdev->dev.of_node,
						"qcom,keep-proxy-regs-on");

	rc = of_property_read_string(pdev->dev.of_node, "qcom,firmware-name",
				      &d->desc.name);
	if (rc){
		pr_err("%s: subsys_[%s] read firmware-name from dts fail.\n",
				__func__, d->subsys_desc.name);
		return rc;
	}

	/* Defaulting smem_id to be not present */
	d->smem_id = -1;

	if (of_find_property(pdev->dev.of_node, "qcom,smem-id", &len)) {
		rc = of_property_read_u32(pdev->dev.of_node, "qcom,smem-id",
						&d->smem_id);
		if (rc) {
			dev_err(&pdev->dev, "Failed to get the smem_id(rc:%d)\n",
									rc);
			return rc;
		}
	}

	rc = of_property_read_u32(pdev->dev.of_node, "qcom,extra-size",
						&d->desc.extra_size);
	if (rc)
		d->desc.extra_size = 0;

	d->dev = &pdev->dev;
	d->desc.dev = &pdev->dev;
	d->desc.owner = THIS_MODULE;
	d->desc.ops = &pil_ops_trusted;
	d->desc.tz_vi.session_id = -1;
	d->desc.tz_vi.shm_id = -1;

	d->desc.proxy_timeout = PROXY_TIMEOUT_MS;
	rc = subsys_parse_irqs(pdev);
	if (rc){
		pr_err("%s: subsys_[%s] subsys_parse_irqs fail.\n",
				__func__, d->subsys_desc.name);
		return rc;
	}

	d->desc.clear_fw_region = true;
	if (0 == strcmp(d->desc.name, "modem")){
		d->self_auth = of_property_read_bool(pdev->dev.of_node,
									"qcom,pil-self-auth");
		pr_debug("pil_tz_driver_probe->self_auth:[%d]\n",d->self_auth);

		d->tz_verify = of_property_read_bool(pdev->dev.of_node,
									"qcom,tz_verify");
		pr_debug("pil_tz_driver_probe->tz_verify:[%d]\n", d->tz_verify);

		if (d->tz_verify) {
			rc = of_property_read_u32(pdev->dev.of_node,
							"qcom,tz_header_size", &d->tz_header_size);
			if (rc)
				d->tz_header_size = 4096;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"rmb_base");
		if (!res) {
				pr_err(":get resource failed for rmb_base!\n");
				return -ENODEV;
		}
		if (d->self_auth){
			d->rmb_base = devm_ioremap_resource(&pdev->dev, res);
			if (IS_ERR(d->rmb_base)){
				pr_err("pil_tz_driver_probe rmb_base error\n");
				goto err_deregister_bus;
			}
			d->desc.ops = &pil_msa_mss_ops_selfauth;
		}else{
			d->rmb_base_phy = res->start;
			pr_debug("pil_tz_driver_probe rmb_base_phy = %pa\n",
				&(d->rmb_base_phy));
		}
	}

	rc = of_property_read_u32(pdev->dev.of_node, "qcom,proxy-timeout-ms",
					&proxy_timeout);
	if (!rc)
		d->desc.proxy_timeout = proxy_timeout;

	if (!d->subsys_desc.no_auth) {
		rc = piltz_resc_init(pdev, d);
		if (rc){
			pr_err("%s: subsys_[%s] piltz_resc_init fail.\n",
				__func__, d->subsys_desc.name);
			return rc;
		}
		#if 0
		rc = of_property_read_u32(pdev->dev.of_node, "qcom,pas-id",
								&d->pas_id);
		if (rc) {
			dev_err(&pdev->dev, "Failed to find the pas_id(rc:%d)\n",
									rc);
			goto err_deregister_bus;
		}
		#endif
	}

	rc = pil_desc_init(&d->desc);
	if (rc){
		pr_err("%s: subsys_[%s] pil_desc_init fail.\n",
				__func__, d->subsys_desc.name);
		goto err_deregister_bus;
	}

	init_completion(&d->stop_ack);
	init_completion(&d->err_ready);

	d->subsys_desc.name = d->desc.name;
	d->subsys_desc.owner = THIS_MODULE;
	d->subsys_desc.dev = &pdev->dev;
	d->subsys_desc.shutdown = subsys_shutdown;
	d->subsys_desc.powerup = subsys_powerup;
	d->subsys_desc.ramdump = subsys_ramdump;
	d->subsys_desc.free_memory = subsys_free_memory;
	d->subsys_desc.crash_shutdown = subsys_crash_shutdown;
	d->subsys_desc.load_qbl = pil_mss_reset_load_qbl;
	d->subsys_desc.load_msadp = pil_mss_reset_load_msadp;
	d->subsys_desc.disable_irqs = subsys_disable_all_irqs_ex;
	d->subsys_desc.npa_vote = pil_make_vote_for_npa;
	d->subsys_desc.qtang_cxmx_vote = pil_make_vote_for_qtang_cxmx;

	if (of_property_read_bool(pdev->dev.of_node,
					"qcom,pil-generic-irq-handler")) {

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"rmb_general_purpose");
		d->rmb_gp_reg = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(d->rmb_gp_reg)) {
			dev_err(&pdev->dev, "Invalid resource for rmb_gp_reg\n");
			rc = PTR_ERR(d->rmb_gp_reg);
			goto load_from_pil;
		}

		rmb_gp_reg_val = __raw_readl(d->rmb_gp_reg);
		/*
		 * If subsystem is already bought out reset during the
		 * bootloader stage, need to check subsystem status instead
		 * of doing regular power. So override power up function
		 * to check subsystem crash status.
		 */
		if (!(rmb_gp_reg_val & BIT(0))) {
			d->boot_enabled = true;
			pr_info("spss is brought out of reset by UEFI\n");
			d->subsys_desc.powerup = subsys_powerup_boot_enabled;
		}
load_from_pil:
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"sp2soc_irq_status");
		d->irq_status = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(d->irq_status)) {
			dev_err(&pdev->dev, "Invalid resource for sp2soc_irq_status\n");
			rc = PTR_ERR(d->irq_status);
			goto err_ramdump;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"sp2soc_irq_clr");
		d->irq_clear = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(d->irq_clear)) {
			dev_err(&pdev->dev, "Invalid resource for sp2soc_irq_clr\n");
			rc = PTR_ERR(d->irq_clear);
			goto err_ramdump;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"sp2soc_irq_mask");
		d->irq_mask = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(d->irq_mask)) {
			dev_err(&pdev->dev, "Invalid resource for sp2soc_irq_mask\n");
			rc = PTR_ERR(d->irq_mask);
			goto err_ramdump;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"rmb_err");
		d->err_status = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(d->err_status)) {
			dev_err(&pdev->dev, "Invalid resource for rmb_err\n");
			rc = PTR_ERR(d->err_status);
			goto err_ramdump;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"rmb_err_spare2");
		d->err_status_spare = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(d->err_status_spare)) {
			dev_err(&pdev->dev, "Invalid resource for rmb_err_spare2\n");
			rc = PTR_ERR(d->err_status_spare);
			goto err_ramdump;
		}

		rc = of_property_read_u32_array(pdev->dev.of_node,
				       "qcom,spss-scsr-bits", d->bits_arr,
					ARRAY_SIZE(d->bits_arr));
		if (rc) {
			dev_err(&pdev->dev,
				"Failed to read qcom,spss-scsr-bits(rc:%d)\n",
				rc);
			goto err_ramdump;
		}
		mask_scsr_irqs(d);
	}

	d->desc.signal_aop = of_property_read_bool(pdev->dev.of_node,
						"qcom,signal-aop");
	if (d->desc.signal_aop) {
		d->desc.cl.dev = &pdev->dev;
		d->desc.cl.tx_block = true;
		d->desc.cl.tx_tout = 1000;
		d->desc.cl.knows_txdone = false;
		d->desc.mbox = mbox_request_channel(&d->desc.cl, 0);
		if (IS_ERR(d->desc.mbox)) {
			rc = PTR_ERR(d->desc.mbox);
			dev_err(&pdev->dev, "Failed to get mailbox channel %pK %d\n",
				d->desc.mbox, rc);
			goto err_ramdump;
		}
	}
	pr_info("pil_tz_driver_probe d->desc.name[%s]\n",d->desc.name);
	d->desc.sequential_loading = of_property_read_bool(pdev->dev.of_node,
						"qcom,sequential-fw-load");

	d->ramdump_dev = create_ramdump_device(d->subsys_desc.name,
								&pdev->dev);
	if (!d->ramdump_dev) {
		pr_err("%s: Unable to create a %s ramdump device.\n",
				__func__, d->subsys_desc.name);
		rc = -ENOMEM;
		goto err_ramdump;
	}

	scnprintf(md_node, sizeof(md_node), "md_%s", d->subsys_desc.name);

	d->minidump_dev = create_ramdump_device(md_node, &pdev->dev);
	if (!d->minidump_dev) {
		pr_err("%s: Unable to create a %s minidump device.\n",
				__func__, d->subsys_desc.name);
		rc = -ENOMEM;
		goto err_minidump;
	}

	d->subsys = subsys_register(&d->subsys_desc);
	if (IS_ERR(d->subsys)) {
		pr_err("%s: subsys_[%s] subsys_register fail.\n",
				__func__, d->subsys_desc.name);
		rc = PTR_ERR(d->subsys);
		goto err_subsys;
	}

	rc = subsys_setup_irqs(pdev);
	if (rc) {
		pr_err("%s: subsys_[%s] subsys_setup_irqs fail.\n",
				__func__, d->subsys_desc.name);
		subsys_unregister(d->subsys);
		goto err_subsys;
	}
	pr_info("pil_tz_driver_probe exit\n");
	return 0;
err_subsys:
	destroy_ramdump_device(d->minidump_dev);
err_minidump:
	destroy_ramdump_device(d->ramdump_dev);
err_ramdump:
	pil_desc_release(&d->desc);
	platform_set_drvdata(pdev, NULL);
err_deregister_bus:
	if (d->bus_client)
		icc_put(d->bus_client);

	return rc;
}

static int pil_tz_scm_pas_probe(struct platform_device *pdev)
{
	int ret = 0;

	scm_perf_client = of_icc_get(&pdev->dev, NULL);
	if (IS_ERR(scm_perf_client)) {
		ret = PTR_ERR(scm_perf_client);
		pr_err("scm-pas: Unable to register bus client: %d\n", ret);
	}
	is_inited = 1;

	return ret;
}

static const struct of_device_id pil_tz_match_table[] = {
	{.compatible = "qcom,pil-tz-generic", .data = pil_tz_generic_probe},
	{.compatible = "qcom,pil-tz-scm-pas", .data = pil_tz_scm_pas_probe},
	{}
};

static int pil_tz_driver_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	int (*pil_tz_probe)(struct platform_device *pdev);

	match = of_match_node(pil_tz_match_table, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	pil_tz_probe = match->data;
	return pil_tz_probe(pdev);
}

static int pil_tz_driver_exit(struct platform_device *pdev)
{
	struct pil_tz_data *d = platform_get_drvdata(pdev);
	const struct of_device_id *match;

	match = of_match_node(pil_tz_match_table, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	if (match->data == pil_tz_scm_pas_probe) {
		icc_put(scm_perf_client);
	} else {
		subsys_unregister(d->subsys);
		destroy_ramdump_device(d->ramdump_dev);
		destroy_ramdump_device(d->minidump_dev);
		pil_desc_release(&d->desc);
		if (d->bus_client)
			icc_put(d->bus_client);
	}

	return 0;
}

static struct platform_driver pil_tz_driver = {
	.probe = pil_tz_driver_probe,
	.remove = pil_tz_driver_exit,
	.driver = {
		.name = "subsys-pil-tz",
		.of_match_table = pil_tz_match_table,
	},
};

static int __init pil_tz_init(void)
{
	last_ssr_reason_entry = proc_create("last_mcrash", S_IFREG | S_IRUGO, NULL, &last_ssr_reason_file_ops);
	if (!last_ssr_reason_entry){
	    printk(KERN_ERR "pil: cannot create proc entry last_mcrash\n");
	}
	return platform_driver_register(&pil_tz_driver);
}
module_init(pil_tz_init);

static void __exit pil_tz_exit(void)
{
#ifdef CHECK_NV_DESTROYED_MI
	schedule_work(&clean_kobj_work);
#endif
    if (last_ssr_reason_entry){
	    remove_proc_entry("last_mcrash", NULL);
	    last_ssr_reason_entry = NULL;
	}
	platform_driver_unregister(&pil_tz_driver);
}
module_exit(pil_tz_exit);

MODULE_DESCRIPTION("Support for booting subsystems");
MODULE_LICENSE("GPL v2");
