/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 */

#ifndef __DRIVERS_INTERCONNECT_BUS_ICC_H__
#define __DRIVERS_INTERCONNECT_BUS_ICC_H__

#include <linux/regmap.h>

#define MAX_LINKS		64

#define RPM_BUS_MASTER_REQ		0x73616d62
#define RPM_BUS_SLAVE_REQ		0x766c7362

#define RPM_SLEEP_SET		MSM_RPM_CTX_SLEEP_SET
#define RPM_ACTIVE_SET		MSM_RPM_CTX_ACTIVE_SET
#define RPM_CLK_MAX_LEVEL		INT_MAX
#define RPM_CLK_MIN_LEVEL               19200000

#define DEFAULT_UTIL_FACTOR		100

#define to_bus_provider(_provider) \
	container_of(_provider, struct bus_icc_provider, provider)

enum bus_icc_rpm_context {
//	RPM_SLEEP_CXT,
	RPM_ACTIVE_CXT,
	RPM_NUM_CXT
};

struct bus_icc_peer {
	struct clk *clk;
	u64 rate;
};

/**
 * struct bus_icc_provider - JLQ specific interconnect provider
 * @provider: generic interconnect provider
 * @dev: reference to the NoC device
 * @bus_clk_cur_rate: current frequency of bus clock
 * @keepalive: flag used to indicate whether a keepalive is required
 * @init: flag to determine when init has completed.
 */
struct bus_icc_provider {
	struct icc_provider provider;
	struct device *dev;
	struct regmap *regmap;
	struct list_head probe_list;
	struct clk *clk;
	u32 fixed_freq;
	u32 util_factor;
	u64 bus_clk_cur_rate[RPM_NUM_CXT];
	bool keepalive;
	bool smd_bus;
	bool init;
	bool peer_bus;
	struct bus_icc_peer peer;
	struct opp_table *opp_table;
	struct dev_pm_set_opp_data data;
	u32 regulator_count;
};

/**
 * struct bus_icc_node - JLQ specific interconnect nodes
 * @name: the node name used in debugfs
 * @id: a unique node identifier
 * @links: an array of nodes where we can go next while traversing
 * @num_links: the total number of @links
 * @channels: num of channels at this node
 * @buswidth: width of the interconnect between a node and the bus (bytes)
 * @last_sum_avg: aggregated average bandwidth from previous aggregation
 * @sum_avg: current sum aggregate value of all avg bw requests
 * @max_peak: current max aggregate value of all peak bw requests
 * @dirty: flag used to indicate whether the node needs to be committed
 */
struct bus_icc_node {
	const char *name;
	u16 id;
	u16 links[MAX_LINKS];
	u16 num_links;
	u16 channels;
	u16 buswidth;
	u64 last_sum_avg[RPM_NUM_CXT];
	u64 sum_avg[RPM_NUM_CXT];
	u64 max_peak[RPM_NUM_CXT];
	struct regmap *regmap;
	bool dirty;
};

struct bus_icc_desc {
	const struct regmap_config *config;
	struct bus_icc_node **nodes;
	size_t num_nodes;
};

struct bus_peer_table {
	char name[50];
	u64 *freq;
	u32 cnt;
	u32 *level;
};

struct bus_info {
	struct dentry *dir;
	u32 start;
	struct mutex mutex;
	void *logbuf;
};

#define BUS_IPC_LOG_PAGES       (50)

int bus_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
			      u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
int bus_icc_set(struct icc_node *src, struct icc_node *dst);
void bus_icc_pre_aggregate(struct icc_node *node);
int bus_icc_peer_init(struct device *dev, struct bus_icc_provider *qp);
int bus_icc_set_rate(struct bus_icc_provider *qp, u64 target);
int bus_dev_target(struct device *dev, unsigned long freq);

#endif
