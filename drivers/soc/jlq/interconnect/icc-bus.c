// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#include <asm/div64.h>
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "icc-bus.h"

//#define ICC_DEBUG
#ifdef ICC_DEBUG
#define icc_debug(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#else
#define icc_debug(fmt, ...)
#endif

#define NUM_PEER_CLK_MAX		10
#define FREQ_TABLE_END			~1u
static struct bus_peer_table g_tbl[NUM_PEER_CLK_MAX];
static unsigned int g_num;

//maybe unreasonable, TBD
static int bus_icc_peer_level(struct bus_peer_table *table, u64 freq)
{
	size_t level;

	for (level = 0; level < table->cnt; level++)
		if (freq <= table->freq[level])
			break;
	return level;
}

static int bus_icc_peer_set(struct bus_icc_provider *qp, u64 target_freq)
{
	struct bus_icc_peer *peer;
	static struct bus_peer_table *table;
	struct clk *clk;
	unsigned long new_freq, old_freq;
	size_t i, old_level;
	int new_level;
	unsigned int div = 2;

	clk = qp->clk;
	old_freq = clk_get_rate(clk);
	new_freq = clk_round_rate(clk, target_freq);

	icc_debug("%s:%s:clk(%s),target_freq(%lu),old_freq(%lu),new_freq(%lu)\n",
		dev_name(qp->dev), __func__, __clk_get_name(clk), target_freq,
		old_freq, new_freq);

	peer = &qp->peer;
	table = g_tbl;

	for (i = 0; i < g_num; i++)
		if (!strcmp(table[i].name, __clk_get_name(peer->clk)))
			break;

	new_level = bus_icc_peer_level(&table[i], new_freq);
	old_level = bus_icc_peer_level(&table[i], old_freq);

	if (table[i].level[old_level])
		table[i].level[old_level]--;
	table[i].level[new_level]++;

	//just for debug
	for (new_level = (table[i].cnt - 1); new_level >= 0; new_level--)
		icc_debug("%s:%s:table[%d].level[%d](%d)\n",
			dev_name(qp->dev), __func__, i, new_level,
			table[i].level[new_level]);

	for (new_level = (table[i].cnt - 1); new_level >= 0; new_level--)
		if (table[i].level[new_level])
			break;

	old_freq = clk_get_rate(peer->clk);
	target_freq = (table[i].freq[new_level] / div);
	new_freq = clk_round_rate(peer->clk, target_freq);
	clk_set_rate(peer->clk, new_freq);

	icc_debug("%s:%s:clk(%s),target_freq(%lu),old_freq(%lu),new_freq(%lu)\n",
		dev_name(qp->dev), __func__, __clk_get_name(peer->clk), target_freq,
		old_freq, new_freq);

	return 0;
}

int bus_icc_peer_init(struct device *dev, struct bus_icc_provider *qp)
{
	struct bus_icc_peer *peer;
	struct dev_pm_opp *opp;
	unsigned long freq;
	size_t cnt = 0, i = 0;

	qp->peer_bus = of_property_read_bool(dev->of_node, "bus,peer-bus");
	if (qp->peer_bus) {
		if (g_num >= NUM_PEER_CLK_MAX)
			return -EINVAL;
		peer = &qp->peer;
		peer->clk = devm_clk_get(dev, "peer");
		if (IS_ERR(peer->clk))
			goto free;

		for (i = 0; i < g_num; i++)
			if (!strcmp(g_tbl[i].name, __clk_get_name(peer->clk)))
				return 0;

		strcpy(g_tbl[g_num].name, __clk_get_name(peer->clk));

		cnt = dev_pm_opp_get_opp_count(dev);
		if (cnt <= 0)
			goto free;

		g_tbl[g_num].freq = devm_kcalloc(dev, (cnt + 1), sizeof(u64), GFP_KERNEL);
		g_tbl[g_num].level = devm_kcalloc(dev, (cnt + 1), sizeof(u32), GFP_KERNEL);
		g_tbl[g_num].cnt = (cnt + 1);

		for (i = 0, freq = 0; i < cnt; i++, freq++) {
			opp = dev_pm_opp_find_freq_ceil(dev, &freq);
			if (IS_ERR(opp))
				goto free;
			g_tbl[g_num].freq[i] = freq;
			dev_pm_opp_put(opp);
		}
		g_tbl[g_num].freq[i] = FREQ_TABLE_END;
		g_num++;
	}

	return 0;
free:
	if (g_tbl[g_num].freq)
		devm_kfree(dev, g_tbl[g_num].freq);
	if (g_tbl[g_num].level)
		devm_kfree(dev, g_tbl[g_num].level);
	if (peer->clk)
		devm_clk_put(dev, peer->clk);

	return -ENOMEM;
}
EXPORT_SYMBOL(bus_icc_peer_init);

/**
 * bus_icc_pre_aggregate - cleans up stale values from prior icc_set
 * @node: icc node to operate on
 */
void bus_icc_pre_aggregate(struct icc_node *node)
{
	size_t i;
	struct bus_icc_node *qn;

	qn = node->data;

	for (i = 0; i < RPM_NUM_CXT; i++) {
		qn->sum_avg[i] = 0;
		qn->max_peak[i] = 0;
	}
}
EXPORT_SYMBOL_GPL(bus_icc_pre_aggregate);

/**
 * bus_icc_aggregate - aggregate bw for buckets indicated by tag
 * @node: node to aggregate
 * @tag: tag to indicate which buckets to aggregate
 * @avg_bw: new bw to sum aggregate
 * @peak_bw: new bw to max aggregate
 * @agg_avg: existing aggregate avg bw val
 * @agg_peak: existing aggregate peak bw val
 */
int bus_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
		       u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	size_t i;
	struct bus_icc_node *qn;

	qn = node->data;
	tag = BIT(RPM_ACTIVE_CXT);

	for (i = 0; i < RPM_NUM_CXT; i++) {
		if (tag & BIT(i)) {
			qn->sum_avg[i] += avg_bw;
			qn->max_peak[i] = max_t(u32, qn->max_peak[i], peak_bw);
		}
	}

	*agg_avg += avg_bw;
	*agg_peak = max_t(u32, *agg_peak, peak_bw);

	qn->dirty = true;

	return 0;
}
EXPORT_SYMBOL_GPL(bus_icc_aggregate);

int bus_icc_set_rate(struct bus_icc_provider *qp, u64 target)
{
	unsigned long freq = target;
	unsigned int i = 0;
	int ret = 0;

	if (qp->fixed_freq)
		freq = qp->fixed_freq;

	if (qp->smd_bus)
		ret = clk_set_rate(qp->clk, freq);
	else {
		if (freq == 0)
			freq = RPM_CLK_MIN_LEVEL;

		if (qp->peer_bus)
			bus_icc_peer_set(qp, freq);
		ret = bus_dev_target(qp->dev, freq);
	}

	if (ret)
		return ret;

	for (i = 0; i < RPM_NUM_CXT; i++)
		qp->bus_clk_cur_rate[i] = freq;

	return 0;
}
EXPORT_SYMBOL_GPL(bus_icc_set_rate);
/**
 * bus_icc_set - set the constraints based on path
 * @src: source node for the path to set constraints on
 * @dst: destination node for the path to set constraints on
 *
 * Return: 0 on success, or an error code otherwise
 */
int bus_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct bus_icc_provider *qp;
	struct bus_icc_node *qn;
	struct icc_node *n, *node;
	struct icc_provider *provider;
	int ret, i;
	u64 clk_rate, sum_avg, max_peak;
	u64 bus_clk_rate[RPM_NUM_CXT] = {0};

	if (!src)
		node = dst;
	else
		node = src;

	qp = to_bus_provider(node->provider);
	qn = node->data;

	if (!qn->dirty)
		return 0;

	provider = node->provider;

	list_for_each_entry(n, &provider->nodes, node_list) {
		qn = n->data;
		for (i = 0; i < RPM_NUM_CXT; i++) {
			sum_avg = icc_units_to_bps(qn->sum_avg[i]);

			sum_avg *= qp->util_factor;
			do_div(sum_avg, DEFAULT_UTIL_FACTOR);

			do_div(sum_avg, qn->channels);
			max_peak = icc_units_to_bps(qn->max_peak[i]);

			clk_rate = max(sum_avg, max_peak);
			do_div(clk_rate, qn->buswidth);

			bus_clk_rate[i] = max(bus_clk_rate[i], clk_rate);
		}
	}

	for (i = 0; i < RPM_NUM_CXT; i++) {
		if (qp->bus_clk_cur_rate[i] != bus_clk_rate[i]) {
			ret = bus_icc_set_rate(qp, bus_clk_rate[i]);
			if (ret) {
				pr_err("dev_pm_opp_set_rate error: %d\n", ret);
				return ret;
			}

			qp->bus_clk_cur_rate[i] = bus_clk_rate[i];
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(bus_icc_set);

MODULE_LICENSE("GPL v2");
