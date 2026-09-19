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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sched_clock.h>
#include <linux/of_address.h>
#include <linux/cpufreq.h>
#include <linux/devfreq_cooling.h>
#include "devfreq_ddr_bridge.h"

#define KHZ_TO_HZ        1000
#define DDR_FREQ_MAX     100
#define PROP_TBL         "jlq,bw-tbl"
#define PROP_POLLINGTIME "polling_ms"
#define DDR_LOW_FREQ_IN_SUSPEND 0

struct df_ddr_data {
	struct devfreq *df;
	struct devfreq_dev_profile pf;
	unsigned long last_polled_at;
	unsigned int  last_polled_by;
	spinlock_t lock;
	/* notifier block for ddr freq change */
	struct notifier_block freq_transition;
	/* DDR frequency table used for platform */
	unsigned long ddr_freq_tbl[DDR_FREQ_MAX];
	unsigned int  ddr_freq_tbl_len;
	unsigned int  cur_ddr_idx;
	unsigned int  freq_in_khz;
	/* work quenue */
	struct work_struct dfs_wk;
	unsigned long dfs_freq;
	/* cooling dev */
	struct thermal_cooling_device *cdev;
	struct devfreq_cooling_power *dfc_power;
	/* vote record */
	unsigned long vote_num;
	/* ddr dfs bridge */
	struct ddr_mail *dfmail;
	/* used for debug interface */
	atomic_t is_disabled;
	#if DDR_LOW_FREQ_IN_SUSPEND
	unsigned long saved_freq;
	#endif
};

static struct df_ddr_data *ddrfreq_data;
static struct workqueue_struct *ddr_DFSwq;

struct userspace_data {
	unsigned long user_frequency;
	bool valid;
};

/*
 *        ddr hardware driver handler
 */
#define __ddr_hardware_driver_handler__
#define DDR_SUPPORT_PM_QOS 0

static void ddr_set_freq_wk(struct work_struct *work)
{
	struct df_ddr_data *data;

	data = ddrfreq_data;
	data->dfmail->mail_msg(DDR_SET_FREQ, &data->dfs_freq);
}

static int ddr_set_freq_handler(struct device *dev, struct df_ddr_data *data,
			unsigned long *tgt_rate)
{
	unsigned long cur_freq, tgt_freq;
	int ddr_idx, i, ret = 0;

	tgt_freq = *tgt_rate;

	/*Get value from PM QoS framework*/
#if DDR_SUPPORT_PM_QOS
	s32 QosMinFreq, QosMaxFreq, QosLatency;
	ddr_debug("dfs support pm QoS");
	QosMinFreq = dev_pm_qos_read_value(dev, DEV_PM_QOS_MIN_FREQUENCY);
	if ((tgt_freq < QosMinFreq) &&
		(QosMinFreq != 0) &&
		(QosMinFreq != FREQ_QOS_MIN_DEFAULT_VALUE))
		tgt_freq = QosMinFreq;
	QosMaxFreq = dev_pm_qos_read_value(dev, DEV_PM_QOS_MIN_FREQUENCY);
	if ((tgt_freq > QosMaxFreq) &&
		(QosMaxFreq != 0) &&
		(QosMaxFreq != FREQ_QOS_MAX_DEFAULT_VALUE))
		tgt_freq = QosMaxFreq;
	QosLatency = dev_pm_qos_read_value(dev, DEV_PM_QOS_RESUME_LATENCY);
#endif

	/*Get the current freq idx*/
	spin_lock(&data->lock);
	ddr_idx = data->cur_ddr_idx;
	spin_unlock(&data->lock);
	cur_freq = data->ddr_freq_tbl[ddr_idx];
	if ((ddr_idx >= data->ddr_freq_tbl_len) ||
		(ddr_idx < 0)) {
		ddr_debug("%s: invalid ddr_idx %u\n",
		__func__, ddr_idx);
		return -EINVAL;
	}

	/*Search target freq idx */
	ddr_idx = -1;
	for (i = 0; i < data->ddr_freq_tbl_len; i++) {
		if (data->ddr_freq_tbl[i] == tgt_freq) {
			ddr_idx = i;
			break; }
	}
	if (ddr_idx < 0) {
		ddr_debug("ddr idx err\n");
		*tgt_rate = cur_freq;
		return -EINVAL;
	}
	if (ddr_idx == data->cur_ddr_idx) {
		ddr_debug("ddr fc do not need\n");
		*tgt_rate = tgt_freq;
		return 0;
	}
	tgt_freq = data->ddr_freq_tbl[ddr_idx];

	/*Vote the target freq to scp*/
	if (ddr_idx >= 0) {
		tgt_freq /= 1000000;
		data->dfs_freq = tgt_freq;
		ret = queue_work(ddr_DFSwq, &data->dfs_wk);
		if (ret == true) {
			spin_lock(&data->lock);
			data->cur_ddr_idx = ddr_idx;
			spin_unlock(&data->lock);
			*tgt_rate = tgt_freq * 1000000;
			return 0;
		}
	}

	ddr_debug("Failed to do ddr freq change\n");
	*tgt_rate = cur_freq;
	return ret;
}

static int ddr_set_bm_handler(struct df_ddr_data *data,
			unsigned int is_worked)
{
	int ret = 0;
	unsigned long bm_state = is_worked;

	ret = data->dfmail->mail_msg(DDR_SET_BM, &bm_state);
	if (ret != -EINVAL)
		return 0;

	ddr_debug("Failed to set bm\n");
	return ret;
}

static int ddr_get_data_handler(struct df_ddr_data *data,
			unsigned int cmd, unsigned long *ret_data)
{
	int ddr_idx, i, ret = 0;

	*ret_data = 0;
	ret = data->dfmail->mail_msg(cmd, ret_data);
	if (ret == -EINVAL)
		return ret;

	switch (cmd) {
	case DDR_GET_FREQ:
		ddr_idx = -1;
		*ret_data *= 1000000;
		for (i = 0; i < data->ddr_freq_tbl_len; i++) {
			if (data->ddr_freq_tbl[i] == *ret_data) {
				ddr_idx = i;
				break; }
		}
		if (ddr_idx < 0) {
			ddr_debug("get current rate is not in opps \n");
			ret = -EINVAL;
		} else {
			spin_lock(&data->lock);
			data->cur_ddr_idx = ddr_idx;
			spin_unlock(&data->lock);
		}
		break;
	default:
		break;
	}
	return ret;
}

/*
 *        ddr freq interface
 */
#define __ddr_freq_interface__
int ddr_apss_freq_vote(int voter, unsigned long freq_hz)
{
	struct devfreq *devfreq = ddrfreq_data->df;
	struct userspace_data *data;
	unsigned long wanted;
	int err = 0;

	mutex_lock(&devfreq->lock);
	data = devfreq->data;
	wanted = freq_hz;
	data->user_frequency = wanted;
	data->valid = true;
	err = update_devfreq(devfreq);
	if (err == 0)
		ddrfreq_data->vote_num += 1;
	mutex_unlock(&devfreq->lock);

	return err;
}
EXPORT_SYMBOL(ddr_apss_freq_vote);

/*
 *        ddr sysfs for user
 */
#define __ddr_sysfs_for_user__
static ssize_t ddrfc_disable_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	int is_disabled;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	if (kstrtoint(buf, 10, &is_disabled))
		return -EINVAL;

	is_disabled = !!is_disabled;
	if (is_disabled == atomic_read(&data->is_disabled)) {
		ddr_debug("ddr fc is already %s\n",
			atomic_read(&data->is_disabled) ?
			"disabled" : "enabled");
		return size;
	}

	if (is_disabled)
		atomic_inc(&data->is_disabled);
	else
		atomic_dec(&data->is_disabled);

	ddr_debug("ddr fc is %s!\n",
		atomic_read(&data->is_disabled) ? "disabled" : "enabled");
	return size;
}

static ssize_t ddrfc_disable_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);
	return sprintf(buf, "ddr fc is_disabled = %d\n",
		 atomic_read(&data->is_disabled));
}

static ssize_t ddrfc_freq_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	unsigned long freq;
	int ret;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	if (!atomic_read(&data->is_disabled)) {
		ddr_debug("disable ddr fc at first\n");
		return -EPERM;
	}

	if (kstrtoint(buf, 10, (int *)&freq)) {
		ddr_debug("ddr_freq to set ddr rate(unit hz)\n");
			return -EINVAL;
	}

	/*direct vote to scp*/
	ret = ddr_set_freq_handler(dev, data, &freq);
	if (ret != -EINVAL)
		ddr_debug("set ddr freq = %dhz succeeded! \n", freq);

	return size;
}

static ssize_t ddrfc_freq_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	int ret;
	unsigned long freq;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	/*direct get freq from scp*/
	ret = ddr_get_data_handler(data, DDR_GET_FREQ, &freq);
	if (ret != -EINVAL)
		return sprintf(buf, "ddr freq = %4ldHz\n", freq);
	else
		return sprintf(buf, "ddr freq not get\n");
}

static ssize_t scp_bm_state_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	unsigned int is_worked;
	int ret;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	if (kstrtoint(buf, 10, &is_worked))
		return -EINVAL;

	is_worked = !!is_worked;
	ret = ddr_set_bm_handler(data, is_worked);
	if (ret != -EINVAL)
		ddr_debug("%d scp bm succeeded! \n", ((is_worked == 0) ? "DISABLE" : "ENABLE"));

	return size;
}

static ssize_t scp_bm_state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	int ret;
	unsigned long is_worked;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	ret = ddr_get_data_handler(data, DDR_GET_BM, &is_worked);
	if (ret != -EINVAL)
		return sprintf(buf, "scp bm state is %s\n",
		((is_worked == 0) ? "DISABLE" : "ENABLE"));
	else
		return sprintf(buf, "scp bm state not get\n");
}

static ssize_t ddr_vendor_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	int ret;
	unsigned long vendorID;
	char vendorName[20];

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	/*direct get freq from cm4*/
	ret = ddr_get_data_handler(data, DDR_GET_VENDOR, &vendorID);
	if (ret != -EINVAL) {
		switch (vendorID) {
		case SAMSUNG:
			strcpy(vendorName, "Samsung");
			break;
		case MICRON:
			strcpy(vendorName, "Micron");
			break;
		case HYNIX:
			strcpy(vendorName, "Hynix");
			break;
		case CXMT:
			strcpy(vendorName, "CXMT");
			break;
		default:
			strcpy(vendorName, "NOT GET, try again");
			break;
		}
		return sprintf(buf, "ddr vendor : %s\n", vendorName);
	} else
		return sprintf(buf, "ddr vendor not get\n");
}

static ssize_t ddr_sr_info_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	int ret;
	unsigned long srTime;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	/*direct get freq from cm4*/
	ret = ddr_get_data_handler(data, DDR_GET_SR_INFO, &srTime);
	if (ret != -EINVAL)
		return sprintf(buf, "ddr sr times : %d\n", srTime);
	else
		return sprintf(buf, "ddr sr times not get\n");
}

static ssize_t ddr_bw_params_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	unsigned int bw_param = 0, bw_cmd = 0;
	int ret;
	char tmpbuf[10], *p;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	memset(tmpbuf, 0x00, sizeof(tmpbuf));
	memcpy(tmpbuf, buf, strlen(buf));
	p = tmpbuf;

	if (!p || !*p || kstrtoint(p, 10, &bw_param))
		return -EINVAL;
	if (bw_param > 100)
		return -EINVAL;

	if (strcmp(attr->attr.name, "ddr_th_up") == 0)
		bw_cmd = DDR_SET_TH_UP;
	else if (strcmp(attr->attr.name, "ddr_th_down") == 0)
		bw_cmd = DDR_SET_TH_DW;
	else if (strcmp(attr->attr.name, "ddr_bw_effic") == 0)
		bw_cmd = DDR_SET_BW_EFFIC;
	else
		return -EINVAL;

	ret = data->dfmail->mail_msg(bw_cmd, (unsigned long *)&bw_param);
	if (ret != -EINVAL)
		ddr_debug("Set bw param succeeded! \n");

	return size;
}

static ssize_t ddr_bw_params_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	int ret;
	unsigned long bw_param = 0;
	unsigned int bw_cmd = 0;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	if (strcmp(attr->attr.name, "ddr_th_up") == 0)
		bw_cmd = DDR_GET_TH_UP;
	else if (strcmp(attr->attr.name, "ddr_th_down") == 0)
		bw_cmd = DDR_GET_TH_DW;
	else if (strcmp(attr->attr.name, "ddr_bw_effic") == 0)
		bw_cmd = DDR_GET_BW_EFFIC;
	else
		return -EINVAL;

	ret = data->dfmail->mail_msg(bw_cmd, &bw_param);
	if (ret != -EINVAL)
		return sprintf(buf, "Get bw param succeeded: %lld (CMD %d)\n",
		bw_param, bw_cmd);
	else
		return sprintf(buf, "Get bw param failed!\n");
}

static DEVICE_ATTR_RW(ddrfc_disable);
static DEVICE_ATTR_RW(ddrfc_freq);
static DEVICE_ATTR_RW(scp_bm_state);
static DEVICE_ATTR_RO(ddr_vendor);
static DEVICE_ATTR_RO(ddr_sr_info);
static DEVICE_ATTR(ddr_th_up, 0644, ddr_bw_params_show, ddr_bw_params_store);
static DEVICE_ATTR(ddr_th_down, 0644, ddr_bw_params_show, ddr_bw_params_store);
static DEVICE_ATTR(ddr_bw_effic, 0644, ddr_bw_params_show, ddr_bw_params_store);

static struct attribute *ddr_attrs[] = {
	&dev_attr_ddrfc_disable.attr,
	&dev_attr_ddrfc_freq.attr,
	&dev_attr_scp_bm_state.attr,
	&dev_attr_ddr_vendor.attr,
	&dev_attr_ddr_sr_info.attr,
	&dev_attr_ddr_th_up.attr,
	&dev_attr_ddr_th_down.attr,
	&dev_attr_ddr_bw_effic.attr,
	NULL,
};
static struct attribute_group ddr_attribute_group = {.attrs = ddr_attrs,};



/*
 *        devfreq framework handler
 */
#define __devfreq_framework_handler__
static int ddrfreq_notifer_call(struct notifier_block *nb,
		unsigned long val, void *data)
{
	return NOTIFY_OK;
}

int ddrfreq_register_dev_notifier(struct srcu_notifier_head
				 *ddrdev_notifier_chain)
{
	return srcu_notifier_chain_register(ddrdev_notifier_chain, &ddrfreq_data->freq_transition);
}
EXPORT_SYMBOL(ddrfreq_register_dev_notifier);

static int ddr_target(struct device *dev, unsigned long *freq,
		unsigned int flags)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	struct dev_pm_opp *opp;
	unsigned long rate;
	unsigned int state;
	int ret;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	spin_lock(&data->lock);
	state = atomic_read(&data->is_disabled);
	spin_unlock(&data->lock);

	if (state) {
		ddr_debug("ddr fc is disabled\n");
		return -EINVAL;
	}

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		ddr_debug("failed to find opp for %lu hz\n", *freq);
		return PTR_ERR(opp);
	}
	rate = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);

	ret = ddr_set_freq_handler(dev, data, &rate);
	if (ret != -EINVAL) {
		*freq = rate;
		return 0;
	}

	return 0;
}

static int ddr_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;
	unsigned int ddr_idx;
	unsigned long *freq_table;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

	ddr_idx = data->cur_ddr_idx;
	if ((ddr_idx >= data->ddr_freq_tbl_len) ||
		(ddr_idx < 0)) {
		ddr_debug("%s: invalid ddr_idx %u\n",
		__func__, ddr_idx);
		return -1;
	}

	freq_table = data->ddr_freq_tbl;
	*freq = freq_table[ddr_idx];
	return 0;
}

static int ddr_get_dev_status(struct device *dev,
			       struct devfreq_dev_status *stat)
{
	struct df_ddr_data *data = dev_get_drvdata(dev);
	struct devfreq *df = data->df;
	unsigned long polling_jiffies;
	unsigned long now = jiffies;

	 /* ignore the profiling if it is not from devfreq_monitor */
	 /* or there is no profiling*/
	polling_jiffies = msecs_to_jiffies(df->profile->polling_ms);
	if (!polling_jiffies || (polling_jiffies && data->last_polled_at &&
		time_before(now, (data->last_polled_at + polling_jiffies)))) {
		ddr_debug("No profiling or interval is not expired %lu, %lu, %lu\n",
			polling_jiffies, now, data->last_polled_at);
		return -EINVAL;
	}

	return 0;
}

/*
 *        thermal framework handler
 */
#define __thermal_framework_handler__
unsigned long ddr_getStaticPower(struct devfreq *devfreq,
			unsigned long voltage)
{
	//TBD: static power model after tuning
	return 0;
}

unsigned long ddr_getDynamicPower(struct devfreq *devfreq,
			unsigned long freq, unsigned long voltage)
{
	//TBD: dynamic power model after tuning
	return 0;
}

int ddr_getRealPower(struct devfreq *df, u32 *power, unsigned long freq, unsigned long voltage)
{
	//TBD: real power model after tuning
	return 0;
}

/*
 *        kernel driver probe
 */
#define __kernel_driver_probe__
static int ddr_opp_table_parse(struct device *dev, struct df_ddr_data *d)
{
	unsigned long freq;
	struct dev_pm_opp *opp;
	int len, i;
	int ret = -EINVAL;

	/* Use devfreq framework*/
	if (dev_pm_opp_of_add_table(dev))
		dev_err(dev, "Not use opp table!\n");

	/* Get opp table length*/
	len = dev_pm_opp_get_opp_count(dev);
	d->ddr_freq_tbl_len = len;
	if (len <= 0) {
		ret = -EPROBE_DEFER;
		goto put_opp_table;
	}

	/* Fill the opp tables*/
	d->freq_in_khz = 0; // TBD
	for (i = 0, freq = ULONG_MAX; i < len; i++, freq--) {
		opp = dev_pm_opp_find_freq_floor(dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto put_opp_table;
		}
		d->ddr_freq_tbl[i] = freq;
		dev_pm_opp_put(opp);
	}

	return 0;

put_opp_table:

	return ret;
}

static int ddr_pm_qos_init(struct device *dev)
{
	struct dev_pm_qos *qos;
	struct pm_qos_constraints *c;
	struct freq_constraints *qos_freq;
	struct blocking_notifier_head *n;

	qos = kzalloc(sizeof(*qos), GFP_KERNEL);
	if (!qos)
		return -ENOMEM;

	n = kzalloc(3 * sizeof(*n), GFP_KERNEL);
	if (!n) {
		kfree(qos);
		return -ENOMEM;
	}

	c = &qos->resume_latency;
	plist_head_init(&c->list);
	c->target_value = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
	c->default_value = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
	c->no_constraint_value = PM_QOS_RESUME_LATENCY_NO_CONSTRAINT;
	c->type = PM_QOS_MIN;
	c->notifiers = n;
	BLOCKING_INIT_NOTIFIER_HEAD(n);

	c = &qos->latency_tolerance;
	plist_head_init(&c->list);
	c->target_value = PM_QOS_LATENCY_TOLERANCE_DEFAULT_VALUE;
	c->default_value = PM_QOS_LATENCY_TOLERANCE_DEFAULT_VALUE;
	c->no_constraint_value = PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT;
	c->type = PM_QOS_MIN;

	qos_freq = &qos->freq;
	c = &qos_freq->min_freq;
	plist_head_init(&c->list);
	c->target_value = FREQ_QOS_MIN_DEFAULT_VALUE;
	c->default_value = FREQ_QOS_MIN_DEFAULT_VALUE;
	c->no_constraint_value = FREQ_QOS_MIN_DEFAULT_VALUE;
	c->type = PM_QOS_MAX;
	c->notifiers = &qos_freq->min_freq_notifiers;
	BLOCKING_INIT_NOTIFIER_HEAD(c->notifiers);

	c = &qos_freq->max_freq;
	plist_head_init(&c->list);
	c->target_value = FREQ_QOS_MAX_DEFAULT_VALUE;
	c->default_value = FREQ_QOS_MAX_DEFAULT_VALUE;
	c->no_constraint_value = FREQ_QOS_MAX_DEFAULT_VALUE;
	c->type = PM_QOS_MIN;
	c->notifiers = &qos_freq->max_freq_notifiers;
	BLOCKING_INIT_NOTIFIER_HEAD(c->notifiers);

	INIT_LIST_HEAD(&qos->flags.list);

	spin_lock_irq(&dev->power.lock);
	dev->power.qos = qos;
	spin_unlock_irq(&dev->power.lock);

	return 0;
}


static int ddr_devfreq_probe(struct platform_device *pdev)
{
	int i = 0, ret = 0;
	struct device *dev = &pdev->dev;
	struct df_ddr_data *data = NULL;
	struct devfreq_dev_profile *profile;
	unsigned int tmp;
	const char *gov_name;
	int len = 0;
	int ddr_table[DDR_FREQ_MAX];

	data = devm_kzalloc(dev, sizeof(struct df_ddr_data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(dev, "Cannot prepare for devfreq data.\n");
		return -ENOMEM;
	}
	ddrfreq_data = data;
	platform_set_drvdata(pdev, data);

	/*ddr dfs bridge setup*/
	ret = ddr_dfs_bridge_probe(pdev, &data->dfmail);
	if (ret)
		return -EINVAL;

	/*devfreq dev framework*/
	profile = &data->pf;

	/*use tbl table*/
	if (of_find_property(dev->of_node, PROP_TBL, &len)) {
		len /= sizeof(unsigned int);
		if (len >= DDR_FREQ_MAX)
			return -ENODATA;
		ret = of_property_read_u32_array(dev->of_node, PROP_TBL, ddr_table, len);
		if (ret)
			return -ENODATA;
		data->ddr_freq_tbl_len = len;
		for (i = 0; i < data->ddr_freq_tbl_len; i++)
			data->ddr_freq_tbl[i] = ddr_table[i];
	}

	/*use opp table*/
	ddr_opp_table_parse(dev, data);

	/*init vote num*/
	data->vote_num = 0;

	/*init qos data*/
	ddr_pm_qos_init(dev);

	/*init dfs wk*/
	INIT_WORK(&data->dfs_wk, ddr_set_freq_wk);
	ddr_DFSwq = create_freezable_workqueue("ddr_DFSwq");
	if (!ddr_DFSwq) {
		dev_err(&pdev->dev, "%s: couldn't create workqueue\n", __FILE__);
		return -ENOMEM;
	}

	/*Spin lock init*/
	spin_lock_init(&data->lock);

	/*devfreq profile*/
	data->cur_ddr_idx = 2;
	profile->initial_freq = 800000000;
	profile->target = ddr_target;
	profile->get_dev_status = ddr_get_dev_status;
	profile->get_cur_freq   = ddr_get_cur_freq;
	profile->polling_ms = 50;
	if (of_find_property(dev->of_node, PROP_POLLINGTIME, &len)) {
		ret = of_property_read_u32(dev->of_node, PROP_POLLINGTIME, &tmp);
		profile->polling_ms = tmp;
	}

	if (of_property_read_string(dev->of_node, "governor", &gov_name))
		gov_name = "userspace";

	data->df = devfreq_add_device(&pdev->dev, profile, gov_name, NULL);
	if (IS_ERR(data->df)) {
		dev_err(dev, "devfreq add error !\n");
		ret =  (unsigned long)data->df;
		goto err_devfreq;
	}

	/*cooling dev*/
	data->dfc_power = devm_kzalloc(dev, sizeof(struct devfreq_cooling_power), GFP_KERNEL);
	if (data->dfc_power  == NULL) {
		dev_err(dev, "Cannot prepare for devfreq cooling power data.\n");
		return -ENOMEM;
	}
	data->dfc_power->dyn_power_coeff = 1;
	data->dfc_power->get_static_power = ddr_getStaticPower;
	data->dfc_power->get_dynamic_power = ddr_getDynamicPower;
	data->dfc_power->get_real_power = ddr_getRealPower;
	data->cdev = of_devfreq_cooling_register_power(dev->of_node, data->df, data->dfc_power);

	/*Register notifier to CPUFREQ*/
	data->freq_transition.notifier_call = ddrfreq_notifer_call;
	data->last_polled_at = jiffies;
	cpufreq_register_notifier(&data->freq_transition,
					CPUFREQ_TRANSITION_NOTIFIER);

	/*sysfs file*/
	ret = sysfs_create_group(&pdev->dev.kobj, &ddr_attribute_group);
	if (ret) {
		dev_err(&pdev->dev, "jlq ddr devfreq sysfs file create failed.\n");
		goto err_file_create;
	}
	return 0;

err_file_create:
	devfreq_remove_device(data->df);
err_devfreq:
	if (data)
		kfree(data);
	return ret;
}

static int ddr_devfreq_remove(struct platform_device *pdev)
{
	struct df_ddr_data *data = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_ddrfc_disable);
	device_remove_file(&pdev->dev, &dev_attr_ddrfc_freq);
	devfreq_remove_device(data->df);
	devfreq_cooling_unregister(data->cdev);
	cancel_work_sync(&data->dfs_wk);
	destroy_workqueue(ddr_DFSwq);
	kfree(data);
	kfree(data->dfc_power);

	return 0;
}

#ifdef CONFIG_PM
static int ddr_devfreq_suspend(struct device *dev)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;

#if DDR_LOW_FREQ_IN_SUSPEND
	int ddr_idx;
	unsigned long new_ddrclk;
#endif

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

#if DDR_LOW_FREQ_IN_SUSPEND
	new_ddrclk = data->ddr_freq_tbl[0];
	ddr_idx = data->cur_ddr_idx;
	data->saved_freq = data->ddr_freq_tbl[ddr_idx];
	ddr_set_freq_handler(dev, data, &new_ddrclk);
#endif

	return 0;
}

static int ddr_devfreq_resume(struct device *dev)
{
	struct platform_device *pdev;
	struct df_ddr_data *data;

	pdev = container_of(dev, struct platform_device, dev);
	data = platform_get_drvdata(pdev);

#if DDR_LOW_FREQ_IN_SUSPEND
	ddr_set_freq_handler(dev, data, &data->saved_freq);
#endif

	return 0;
}

static const struct dev_pm_ops ddr_pm_ops = {
	.suspend	= ddr_devfreq_suspend,
	.resume		= ddr_devfreq_resume,
};
#endif

static const struct of_device_id devfreq_ddr_dt_match[] = {
	{.compatible = "jlq,ddr-devfreq" },
	{},
};
MODULE_DEVICE_TABLE(of, devfreq_ddr_dt_match);

static struct platform_driver ddr_devfreq_driver = {
	.probe  = ddr_devfreq_probe,
	.remove = ddr_devfreq_remove,
	.driver = {
		.name = "ddr-devfreq",
		.of_match_table = of_match_ptr(devfreq_ddr_dt_match),
		.owner = THIS_MODULE,
	#ifdef CONFIG_PM
		.pm = &ddr_pm_ops,
	#endif
	},
};

static int __init ddr_devfreq_init(void)
{
	return platform_driver_register(&ddr_devfreq_driver);
}
module_init(ddr_devfreq_init);

static void __exit ddr_devfreq_exit(void)
{
	platform_driver_unregister(&ddr_devfreq_driver);
}
module_exit(ddr_devfreq_exit);

MODULE_DESCRIPTION("ddr devfreq driver");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: pmctrl");
