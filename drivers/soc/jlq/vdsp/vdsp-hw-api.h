/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2017 Cadence Design Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Alternatively you can use and distribute this file under the terms of
 * the GNU General Public License version 2 or later.
 */

/*!
 * \file vdsp-hw-api.h
 * \brief Interface between generic and HW-specific kernel drivers.
 */
#ifndef _VDSP_HW_API_H_
#define _VDSP_HW_API_H_
#include "vdsp-mem.h"

struct log_init_params {
	__u32 log_enable;
	__u32 log_output_mode;
	__u32 log_output_modules;
	__u32 log_level;
	__u32 log_fifo_depth;
	__u32 log_fifo_width;
	__u32 log_fifo_watermark;
};

struct vdsp_extern_module {
	unsigned int is_support_smmu;
	unsigned int smd_irq_id;
	unsigned int force_ramdump_irq_id;
	unsigned int panic_irq_id;
	unsigned int wdog_bite_irq_id;
	unsigned int rtc_irq_id;
	void __iomem *tcm_baseaddr;
	struct log_init_params *log_params;
};

/*!
 * Hardware-specific operation entry points.
 * Hardware-specific driver passes a pointer to this structure to xrp_init
 * at initialization time.
 */
struct vdsp_hw_ops {
	/*!
	 * Enable power/clock, but keep the core stalled.
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	int (*enable)(void *hw_arg);
	/*!
	 * Disable power/clock.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*disable)(void *hw_arg);
	/*!
	 * Reset the core.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*reset)(void *hw_arg);
	/*!
	 * De-Reset the core.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*dereset)(void *hw_arg);
	/*!
	 * Unstall the core.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*release)(void *hw_arg);

	int (*is_active)(void *hw_arg);

	/*!
	 * Stall the core.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*halt)(void *hw_arg);

	/*!
	 * configure the core.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*boot_cfg)(void *hw_arg);

	/*!
	 * load firmware.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	int (*load_firmware)(void *hw_arg);

	/*! Get HW-specific data to pass to the DSP on synchronization
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 * \param sz: return size of sync data here
	 * \return a buffer allocated with kmalloc that the caller will free
	 */
	void *(*get_hw_sync_data)(void *hw_arg, size_t *sz);

	/*!
	 * Send IRQ to the core.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*send_irq)(void *hw_arg, int irq_id);

	/*!
	 * Check DSP status.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 * \return whether the core has crashed and needs to be restarted
	 */
	bool (*panic_check)(void *hw_arg);

	/*!
	 * Calculate dvfs.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 * \param coreclk: core clock value
	 * \param mips: total cycle.
	 * \param target_clk: target core clock value
	 * \param is_update: whether update core clock.
	 */
	int (*dvfs_vote)(void *hw_arg, unsigned long coreclk,
				unsigned long mips, unsigned long *target_clk, int *is_update);

	/*!
	 * Set dvfs.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 * \param clk: target core clock
	 */
	int (*dvfs_set)(void *hw_arg, unsigned long clk);

	/*!
	 * get clock frequency.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	unsigned long (*get_coreclk_rate)(void *hw_arg);

	/*!
	 * bandwidth update.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 * \param bw: bandwidth value
	 */
	int (*bw_vote_update)(void *hw_arg, unsigned long bw);

	/*!
	 * Update vdsp time base.
	 *
	 * \param hw_arg: opaque parameter passed to vdsp_init at initialization
	 *                time
	 */
	void (*update_rtc)(void *hw_arg);
};

/*!
 * Initialize generic VDSP kernel driver from jlq,vdsp-hw-vp6,cma-compatible device
 * tree node.
 *
 * \param pdev: pointer to platform device associated with the XRP device
 *              instance
 * \param hw: pointer to vdsp_hw_ops structeure for this device
 * \param hw_arg: opaque pointer passed back to hw-specific functions
 * \param module: other modules configure info
 * \param mem_region: memory info
 * \return 0 on success, negative error code otherwise
 */
int vdsp_init(struct platform_device *pdev,
		const struct vdsp_hw_ops *hw_ops,
		void *hw_arg,
		struct vdsp_extern_module *module,
		struct vdsp_mem_region *mem_region);

/*!
 * Deinitialize generic VDSP kernel driver.
 *
 * \param pdev: pointer to platform device associated with the XRP device
 *              instance
 * \return 0 on success, negative error code otherwise
 */
int vdsp_deinit(struct platform_device *pdev);

/*!
 * Resume generic VDSP operation of the device dev.
 *
 * \param dev: device which operation shall be resumed
 * \return 0 on success, negative error code otherwise
 */
int vdsp_runtime_resume(struct device *dev);

/*!
 * Suspend generic VDSP operation of the device dev.
 *
 * \param dev: device which operation shall be suspended
 * \return 0 on success, negative error code otherwise
 */
int vdsp_runtime_suspend(struct device *dev);

/*!
 * send irq to VDSP by mailbox.
 *
 * \param dev: get send irq operation by dev
 * \param irq_id: the irq number
 */
void send_irq_to_vdsp(struct device *dev, int irq_id);
#endif /* _VDSP_HW_API_H_ */
