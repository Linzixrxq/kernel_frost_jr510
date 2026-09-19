/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2019, The Linux Foundation. All rights reserved.
 */

#ifndef __SUBSYS_RESTART_H
#define __SUBSYS_RESTART_H

#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include "vdsp_ramdump.h"
#include "vdsp-mem.h"

enum {
	RESET_SOC = 0,
	RESET_SUBSYS_COUPLED,
	RESET_LEVEL_MAX
};

enum crash_status {
	CRASH_STATUS_NO_CRASH = 0,
	CRASH_STATUS_ERR_FATAL,
	CRASH_STATUS_WDOG_BITE,
};

struct device;
struct module;

enum ssr_comm {
	SUBSYS_TO_SUBSYS_SYSMON,
	SUBSYS_TO_HLOS,
	HLOS_TO_SUBSYS_SYSMON_SHUTDOWN,
	NUM_SSR_COMMS,
};

enum subsys_state {
	SUBSYS_RUNNING,
	SUBSYS_CLOSED,
	SUBSYS_CRASHED,
	SUBSYS_RESTARTING,
};

enum ssr_event {
	SSR_EVENT_PANIC,
	SSR_EVENT_SUSPENED,
	SSR_EVENT_RESUME,
	SSR_EVENT_TX_IRQ,
};

/**
 * struct vdsp_subsys_desc - subsystem descriptor
 * @name: name of subsystem
 * @fw_name: firmware name
 * @pon_depends_on: subsystem this subsystem wants to power-on first. If the
 * dependednt subsystem is already powered-on, the framework won't try to power
 * it back up again.
 * @poff_depends_on: subsystem this subsystem wants to power-off first. If the
 * dependednt subsystem is already powered-off, the framework won't try to power
 * it off again.
 * @dev: parent device
 * @owner: module the descriptor belongs to
 * @shutdown: Stop a subsystem
 * @powerup: Start a subsystem
 * @crash_shutdown: Shutdown a subsystem when the system crashes (can't sleep)
 * @ramdump: Collect a ramdump of the subsystem
 * @free_memory: Free the memory associated with this subsystem
 * @is_not_loadable: Indicate if subsystem firmware is not loadable via pil
 * framework
 * @no_auth: Set if subsystem does not rely on PIL to authenticate and bring
 * it out of reset
 * @ssctl_instance_id: Instance id used to connect with SSCTL service
 * @sysmon_pid:	pdev id that sysmon is probed with for the subsystem
 * @sysmon_shutdown_ret: Return value for the call to sysmon_send_shutdown
 * @system_debug: If "set", triggers a device restart when the
 * subsystem's wdog bite handler is invoked.
 * @ignore_ssr_failure: SSR failures are usually fatal and results in panic. If
 * set will ignore failure.
 * @edge: GLINK logical name of the subsystem
 */

struct vdsp_subsys_desc {
	const char *name;
	char ramdump_path[256];
	struct device *dev;
	struct module *owner;
	struct vdsp_ramdump_device *dump_dev;
	int is_support_ramdump;
	int (*shutdown)(const struct vdsp_subsys_desc *desc, bool force_stop);
	int (*powerup)(const struct vdsp_subsys_desc *desc);
	void (*crash_shutdown)(const struct vdsp_subsys_desc *desc);
	void (*trigger_ramdump)(const struct vdsp_subsys_desc *desc);
	size_t (*ramdump)(const struct vdsp_subsys_desc *desc, char __user *buf,
			size_t count, loff_t *ppos);
	void (*free_memory)(const struct vdsp_subsys_desc *desc);
	unsigned int panic_irq;
	unsigned int wdog_bite_irq;
	int ramdump_disable_irq;
	int ramdump_disable;
	int force_ramdump_irq_id;
	struct vdsp_mem_t mem_info;
	void __iomem *tcm_baseaddr;
	bool system_debug;
	int boot_cnt;
};

/**
 * struct subsys_tracking - track state of a subsystem or restart order
 * @state: public state of subsystem/order
 * @s_lock: protects p_state
 * @lock: protects subsystem/order callbacks and state
 *
 * Tracks the state of a subsystem or a set of subsystems (restart order).
 * Doing this avoids the need to grab each subsystem's lock and update
 * each subsystems state when restarting an order.
 */
struct subsys_tracking {
	spinlock_t s_lock;
	enum subsys_state state;
	struct mutex lock;
};

/**
 * struct subsys_device - subsystem device
 * @desc: subsystem descriptor
 * @work: context for subsystem_restart_wq_func() for this device
 * @ssr_wlock: prevents suspend during subsystem_restart()
 * @wlname: name of wakeup source
 * @device_restart_work: work struct for device restart
 * @track: state tracking and locking
 * @notify: subsys notify handle
 * @dev: device
 * @owner: module that provides @desc
 * @count: reference count of subsystem_get()/subsystem_put()
 * @id: ida
 * @restart_level: restart level (0 - panic, 1 - related, 2 - independent, etc.)
 * @restart_order: order of other devices this devices restarts with
 * @crash_count: number of times the device has crashed
 * @do_ramdump_on_put: ramdump on subsystem_put() if true
 * @err_ready: completion variable to record error ready from subsystem
 * @crashed: indicates if subsystem has crashed
 */
struct vdsp_subsys_device {
	struct vdsp_subsys_desc *desc;
	struct work_struct work;
	struct wakeup_source *ssr_wlock;
	char wlname[64];
	struct work_struct device_restart_work;
	struct subsys_tracking track;

	struct device dev;
	struct module *owner;
	int count;
	int id;
	int restart_level;
	int crash_count;
	bool do_ramdump_on_put;
	struct cdev char_dev;
	dev_t dev_no;
	enum crash_status crashed;
	struct list_head list;
};

size_t vdsp_subsys_do_ramdump(struct vdsp_ramdump_device *dev, char __user *buf,
		size_t count, loff_t *ppos);
void vdsp_subsys_set_state(struct vdsp_subsys_device *subsys,
		enum subsys_state state);
struct vdsp_subsys_device *vdsp_subsys_register(struct vdsp_subsys_desc *desc);
void vdsp_subsys_unregister(struct vdsp_subsys_device *dev);
extern struct raw_notifier_head ssr_notifier_list;
#endif
