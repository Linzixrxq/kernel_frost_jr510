/*
 * Copyright (c) 2014, 2019, The Linux Foundation. All rights reserved.
 */

#ifndef _GOVERNOR_BUSMON_BW_H
#define _GOVERNOR_BUSMON_BW_H

#include <linux/kernel.h>
#include <linux/devfreq.h>

//#define pr_fmt(fmt) "bw-hwmon: " fmt
#define NUM_MBPS_ZONES		10
#define UP_WAKE				1
#define DOWN_WAKE			2
#define MIN_MS				10U
#define MAX_MS				500U
#define SAMPLE_MIN_MS		1U
#define SAMPLE_MAX_MS		50U
#define MIN_MBPS			500UL
#define HIST_PEAK_TOL		60

static DEFINE_SPINLOCK(irq_lock);
static LIST_HEAD(hwmon_list);
static DEFINE_MUTEX(list_lock);
static int use_cnt;
static DEFINE_MUTEX(state_lock);

/**
 * struct bw_hwmon - dev BW HW monitor info
 * @start_hwmon:		Start the HW monitoring of the dev BW
 * @stop_hwmon:			Stop the HW monitoring of dev BW
 * @set_thres:			Set the count threshold to generate an IRQ
 * @get_bytes_and_clear:	Get the bytes transferred since the last call
 *				and reset the counter to start over.
 * @set_throttle_adj:		Set throttle adjust field to the given value
 * @get_throttle_adj:		Get the value written to throttle adjust field
 * @dev:			Pointer to device that this HW monitor can
 *				monitor.
 * @of_node:			OF node of device that this HW monitor can
 *				monitor.
 * @gov:			devfreq_governor struct that should be used
 *				when registering this HW monitor with devfreq.
 *				Only the name field is expected to be
 *				initialized.
 * @df:				Devfreq node that this HW monitor is being
 *				used for. NULL when not actively in use and
 *				non-NULL when in use.
 *
 * One of dev, of_node or governor_name needs to be specified for a
 * successful registration.
 *
 */

//TBD
struct bw_hwmon {
	unsigned int port_num;
	int (*start_hwmon)(struct bw_hwmon *hw);
	void (*stop_hwmon)(struct bw_hwmon *hw);
	int (*suspend_hwmon)(struct bw_hwmon *hw);
	int (*resume_hwmon)(struct bw_hwmon *hw);
	unsigned long (*set_thres)(struct bw_hwmon *hw);
	unsigned long (*set_hw_events)(struct bw_hwmon *hw, unsigned int sample_ms);
	unsigned int (*get_bw_hwmon_mode)(struct bw_hwmon *hw);
	unsigned int (*get_bw_hwmon_period)(struct bw_hwmon *hw);
	unsigned long (*get_bytes_and_clear)(struct bw_hwmon *hw);
	int (*set_throttle_adj)(struct bw_hwmon *hw, uint adj);
	u32 (*get_throttle_adj)(struct bw_hwmon *hw);
	struct device *dev;
	struct device_node *of_node;
	struct devfreq_governor *gov;
	unsigned long up_wake_mbps;
	unsigned long down_wake_mbps;
	unsigned int down_cnt;
	struct devfreq *df;
};

struct hwmon_node {
	unsigned int		guard_band_mbps;
	unsigned int		decay_rate;
	unsigned int		io_percent;
	unsigned int		bw_step;
	unsigned int		sample_ms;
	unsigned int		up_scale;
	unsigned int		up_thres;
	unsigned int		down_thres;
	unsigned int		down_count;
	unsigned int		hist_memory;
	unsigned int		hyst_trigger_count;
	unsigned int		hyst_length;
	unsigned int		idle_mbps;
	unsigned int		use_ab;
	unsigned int		mbps_zones[NUM_MBPS_ZONES];

	unsigned long		prev_ab;
	unsigned long		*dev_ab;
	unsigned long		resume_freq;
	unsigned long		resume_ab;
	unsigned long		bytes;
	unsigned long		max_mbps;
	unsigned long		hist_max_mbps;
	unsigned long		hist_mem_cnt;
	unsigned long		hyst_peak;
	unsigned long		hyst_mbps;
	unsigned long		hyst_trig_win;
	unsigned long		hyst_en;
	unsigned long		prev_req;
	unsigned int		wake;
	unsigned int		down_cnt;
	ktime_t				prev_ts;
	ktime_t				hist_max_ts;
	bool				sampled;
	bool				mon_started;
	struct list_head	list;
	void				*orig_data;
	struct bw_hwmon		*hw;
	struct devfreq_governor	*gov;
	struct attribute_group	*attr_grp;
	struct mutex		mon_lock;
};

#define show_attr(name) \
	static ssize_t name##_show(struct device *dev,				\
				struct device_attribute *attr, char *buf)	\
	{									\
		struct devfreq *df = to_devfreq(dev);				\
		struct hwmon_node *hw = df->data;				\
		return scnprintf(buf, PAGE_SIZE, "%u\n", hw->name);	\
	}

#define store_attr(name, _min, _max) \
	static ssize_t name##_store(struct device *dev,			\
				struct device_attribute *attr, const char *buf, \
				size_t count)					\
	{									\
		struct devfreq *df = to_devfreq(dev);				\
		struct hwmon_node *hw = df->data;				\
		int ret;							\
		unsigned int val;						\
		ret = kstrtoint(buf, 10, &val);				\
		if (ret < 0)							\
			return ret;					\
		val = max(val, _min);						\
		val = min(val, _max);						\
		hw->name = val;						\
		return count;							\
	}

#define show_list_attr(name, n) \
	static ssize_t name##_show(struct device *dev,			\
				struct device_attribute *attr, char *buf)	\
	{									\
		struct devfreq *df = to_devfreq(dev);				\
		struct hwmon_node *hw = df->data;				\
		unsigned int i, cnt = 0;					\
										\
		for (i = 0; i < n && hw->name[i]; i++)				\
			cnt += scnprintf(buf + cnt, PAGE_SIZE, "%u ", hw->name[i]);\
		cnt += scnprintf(buf + cnt, PAGE_SIZE, "\n");			\
		return cnt;						\
	}

#define store_list_attr(name, n, _min, _max) \
	static ssize_t name##_store(struct device *dev,		\
				struct device_attribute *attr, const char *buf, \
				size_t count)					\
	{									\
		struct devfreq *df = to_devfreq(dev);				\
		struct hwmon_node *hw = df->data;				\
		int ret, numvals;						\
		unsigned int i = 0, val;					\
		char **strlist;						\
										\
		strlist = argv_split(GFP_KERNEL, buf, &numvals);		\
		if (!strlist)							\
			return -ENOMEM;					\
		numvals = min(numvals, n - 1);					\
		for (i = 0; i < numvals; i++) {				\
			ret = kstrtouint(strlist[i], 10, &val);		\
			if (ret < 0)						\
				goto out;					\
			val = max(val, _min);					\
			val = min(val, _max);					\
			hw->name[i] = val;					\
		}								\
		ret = count;							\
	out:									\
		argv_free(strlist);					\
		hw->name[i] = 0;						\
		return ret;						\
	}

#if IS_ENABLED(CONFIG_PM_GOV_BUSMON)
	int register_busmon_bw(struct device *dev, struct bw_hwmon *hwmon);
	int update_bw_hwmon(struct bw_hwmon *hwmon);
	int bw_hwmon_sample_end(struct bw_hwmon *hwmon);
#else
	static inline int register_busmon_bw(struct device *dev, struct bw_hwmon *hwmon) {return 0; }
	static inline int update_bw_hwmon(struct bw_hwmon *hwmon) {return 0; }
	static inline int bw_hwmon_sample_end(struct bw_hwmon *hwmon) {return 0; }
#endif

#endif /* _GOVERNOR_BUSMON_BW_H */
