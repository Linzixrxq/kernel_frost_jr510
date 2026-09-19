/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2021, The Linux Foundation. All rights reserved.
 */
#ifndef __MSM_PERIPHERAL_LOADER_H
#define __MSM_PERIPHERAL_LOADER_H

#include <linux/mailbox_client.h>
#include <linux/mailbox/qmp.h>
#include "minidump_private.h"
#include <linux/ipc_logging.h>
//#include <../drivers/tee/optee/optee_msg.h>
//#include <../drivers/tee/optee/optee_private.h>
//#include <../drivers/tee/optee/optee_smc.h>

#include "soc/jlq/jlq_verifier.h"
#include <soc/jlq/jlq_sip.h>

#define OPTEE_MSG_FUNCID_PIL_RW_QTANG2       0x0005
#define PIL_READ_QTANG2            0x1
#define PIL_WRITE_QTANG2           0X2

#define OPTEE_SMC_FUNCID_PIL_RW_QTANG2 OPTEE_MSG_FUNCID_PIL_RW_QTANG2
#define OPTEE_SMC_CALL_PIL_RW_QTANG2 \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32, \
			   ARM_SMCCC_OWNER_TRUSTED_OS, (OPTEE_MSG_FUNCID_PIL_RW_QTANG2))

int optee_handle_register(u32 handle_cmd, u64 reg_addr, u32 *value, u32 handle_op);

#define SECURE_PAGE_MAGIC 0xEEEEEEEE
struct device;
struct module;
struct pil_priv;
extern void *pil_ipc_log;
#define pil_ipc(__msg, ...) \
do { \
	if (pil_ipc_log) \
		ipc_log_string(pil_ipc_log, \
			"[%s]: "__msg, __func__,  ##__VA_ARGS__); \
} while (0)
/**
 * struct pil_priv - Private state for a pil_desc
 * @proxy: work item used to run the proxy unvoting routine
 * @ws: wakeup source to prevent suspend during pil_boot
 * @wname: name of @ws
 * @desc: pointer to pil_desc this is private data for
 * @seg: list of segments sorted by physical address
 * @entry_addr: physical address where processor starts booting at
 * @base_addr: smallest start address among all segments that are relocatable
 * @region_start: address where relocatable region starts or lowest address
 * for non-relocatable images
 * @region_end: address where relocatable region ends or highest address for
 * non-relocatable images
 * @region: region allocated for relocatable images
 * @unvoted_flag: flag to keep track if we have unvoted or not.
 *
 * This struct contains data for a pil_desc that should not be exposed outside
 * of this file. This structure points to the descriptor and the descriptor
 * points to this structure so that PIL drivers can't access the private
 * data of a descriptor but this file can access both.
 */
struct pil_priv {
	struct delayed_work proxy;
	struct wakeup_source *ws;
	char wname[32];
	struct pil_desc *desc;
	int num_segs;
	struct list_head segs;
	phys_addr_t entry_addr;
	phys_addr_t base_addr;
	phys_addr_t region_start;
	phys_addr_t region_end;
	bool is_region_allocated;
	struct pil_image_info __iomem *info;
	int id;
	int unvoted_flag;
	size_t region_size;
	phys_addr_t modem_pa;
	phys_addr_t msadp_pa;
	phys_addr_t qbl_pa;
	phys_addr_t qbl_pa_end;
	phys_addr_t modem_pa_end;
	phys_addr_t modem_mdt_pa;
	uint32_t modem_mem_size;
	uint32_t msadp_mem_size;
	uint32_t qbl_mem_size;
	void *modem_va;
	void *msadp_va;
	void *qbl_va;
	void *modem_mdt_va;
};

/**
 * struct pil_desc - PIL descriptor
 * @name: string used for pil_get()
 * @fw_name: firmware name
 * @dev: parent device
 * @ops: callback functions
 * @owner: module the descriptor belongs to
 * @proxy_timeout: delay in ms until proxy vote is removed
 * @flags: bitfield for image flags
 * @priv: DON'T USE - internal only
 * @attrs: DMA attributes to be used during dma allocation.
 * @proxy_unvote_irq: IRQ to trigger a proxy unvote. proxy_timeout
 * is ignored if this is set.
 * @map_fw_mem: Custom function used to map physical address space to virtual.
 * This defaults to ioremap if not specified.
 * @unmap_fw_mem: Custom function used to undo mapping by map_fw_mem.
 * This defaults to iounmap if not specified.
 * @shutdown_fail: Set if PIL op for shutting down subsystem fails.
 * @modem_ssr: true if modem is restarting, false if booting for first time.
 * @clear_fw_region: Clear fw region on failure in loading.
 * @subsys_vmid: memprot id for the subsystem.
 */
struct pil_desc {
	const char *name;
	const char *fw_name;
	struct device *dev;
	const struct pil_reset_ops *ops;
	struct tz_verifier_info tz_vi;
	struct module *owner;
	unsigned long proxy_timeout;
	unsigned long flags;
#define PIL_SKIP_ENTRY_CHECK	BIT(0)
	struct pil_priv *priv;
	unsigned long attrs;
	unsigned int proxy_unvote_irq;
	void __iomem * (*map_fw_mem)(phys_addr_t phys, size_t size, void *data);
	void (*unmap_fw_mem)(void __iomem *virt, size_t size, void *data);
	void *map_data;
	bool shutdown_fail;
	bool modem_ssr;
	bool clear_fw_region;
	bool sequential_loading;
	u32 subsys_vmid;
	bool signal_aop;
	struct mbox_client cl;
	struct mbox_chan *mbox;
	struct md_ss_toc *minidump_ss;
	struct md_ss_toc **aux_minidump;
	int minidump_id;
	int *aux_minidump_ids;
	int num_aux_minidump_ids;
	u32 extra_size;
	bool minidump_as_elf32;
};
/**
 * struct pil_tz_data
 * @regs: regulators that should be always on when the subsystem is
 *	   brought out of reset
 * @proxy_regs: regulators that should be on during pil proxy voting
 * @clks: clocks that should be always on when the subsystem is
 *	  brought out of reset
 * @proxy_clks: clocks that should be on during pil proxy voting
 * @reg_count: the number of always on regulators
 * @proxy_reg_count: the number of proxy voting regulators
 * @clk_count: the number of always on clocks
 * @proxy_clk_count: the number of proxy voting clocks
 * @smem_id: the smem id used for read the subsystem crash reason
 * @ramdump_dev: ramdump device pointer
 * @pas_id: the PAS id for tz
 * @bus_client: bus client
 * @enable_bus_scaling: set to true if PIL needs to vote for
 *			bus bandwidth
 * @keep_proxy_regs_on: If set, during proxy unvoting, PIL removes the
 *			voltage/current vote for proxy regulators but leaves
 *			them enabled.
 * @stop_ack: state of completion of stop ack
 * @desc: PIL descriptor
 * @subsys: subsystem device pointer
 * @subsys_desc: subsystem descriptor
 * @u32 bits_arr[2]: array of bit positions in SCSR registers
 * @boot_enabled: true if subsystem is brough of reset during the bootloader.
 */
struct pil_tz_data {
	struct device *dev;
	struct reg_info *regs;
	struct reg_info *proxy_regs;
	struct clk **clks;
	struct clk **proxy_clks;
	int reg_count;
	int proxy_reg_count;
	int clk_count;
	int proxy_clk_count;
	int smem_id;
	void *ramdump_dev;
	void *minidump_dev;
	u32 pas_id;
	void __iomem *rmb_base;
	phys_addr_t rmb_base_phy;
	struct icc_path *bus_client;
	bool boot_enabled;
	bool enable_bus_scaling;
	bool keep_proxy_regs_on;
	struct completion err_ready;
	struct completion stop_ack;
	struct completion shutdown_ack;
	struct pil_desc desc;
	struct subsys_device *subsys;
	struct subsys_desc subsys_desc;
	void __iomem *irq_status;
	void __iomem *irq_clear;
	void __iomem *irq_mask;
	void __iomem *err_status;
	void __iomem *err_status_spare;
	void __iomem *rmb_gp_reg;
	u32 bits_arr[2];
	struct device mba_mem_dev;
	struct device *mba_mem_dev_fixed;
	unsigned long attrs_dma;
	bool self_auth;
	bool tz_verify;
	int tz_header_size;
	phys_addr_t dp_phys;
	void *dp_virt;
	size_t dp_size;
	phys_addr_t qbl_phys;
	void *qbl_virt;
	size_t qbl_size;
	int err_fatal_irq;
	int err_ready_irq;
	int stop_ack_irq;
	int wdog_bite_irq;
	int qtang2_wdog_bite_irq;
	int generic_irq;
	int ramdump_disable_irq;
	int shutdown_ack_irq;
	int force_stop_bit;
	struct qcom_smem_state *state;
	bool is_booted;
	int wdog_irq_enabled;
	struct regulator *reg_cx;
	struct regulator *reg_mx;
};

/**
 * struct pil_image_info - info in IMEM about image and where it is loaded
 * @name: name of image (may or may not be NULL terminated)
 * @start: indicates physical address where image starts (little endian)
 * @size: size of image (little endian)
 */
struct pil_image_info {
	char name[8];
	__le64 start;
	__le32 size;
} __attribute__((__packed__));

/**
 * struct pil_reset_ops - PIL operations
 * @init_image: prepare an image for authentication
 * @mem_setup: prepare the image memory region
 * @verify_blob: authenticate a program segment, called once for each loadable
 *		 program segment (optional)
 * @proxy_vote: make proxy votes before auth_and_reset (optional)
 * @auth_and_reset: boot the processor
 * @proxy_unvote: remove any proxy votes (optional)
 * @deinit_image: restore actions performed in init_image if necessary
 * @shutdown: shutdown the processor
 */
struct pil_reset_ops {
	int (*init_image)(struct pil_desc *pil, const u8 *metadata,
			  size_t size);
	int (*mem_setup)(struct pil_desc *pil, phys_addr_t addr, size_t size);
	int (*verify_blob)(struct pil_desc *pil, phys_addr_t phy_addr,
			   size_t size);
	int (*proxy_vote)(struct pil_desc *pil);
	int (*auth_and_reset)(struct pil_desc *pil);
	void (*proxy_unvote)(struct pil_desc *pil);
	int (*deinit_image)(struct pil_desc *pil);
	int (*shutdown)(struct pil_desc *pil);
};

#if IS_ENABLED(CONFIG_MSM_PIL)
extern int pil_desc_init(struct pil_desc *desc);
extern int pil_boot(struct pil_desc *desc);
extern void pil_shutdown(struct pil_desc *desc);
extern void pil_free_memory(struct pil_desc *desc);
extern void pil_desc_release(struct pil_desc *desc);
extern phys_addr_t pil_get_entry_addr(struct pil_desc *desc);
extern int pil_do_ramdump(struct pil_desc *desc, void *ramdump_dev,
			  void *minidump_dev);
extern int pil_assign_mem_to_subsys(struct pil_desc *desc, phys_addr_t addr,
						size_t size);
extern int pil_assign_mem_to_linux(struct pil_desc *desc, phys_addr_t addr,
						size_t size);
extern int pil_assign_mem_to_subsys_and_linux(struct pil_desc *desc,
						phys_addr_t addr, size_t size);
extern int pil_reclaim_mem(struct pil_desc *desc, phys_addr_t addr, size_t size,
						int VMid);
extern bool pil_is_timeout_disabled(void);
#else
static inline int pil_desc_init(struct pil_desc *desc) { return 0; }
static inline int pil_boot(struct pil_desc *desc) { return 0; }
static inline void pil_shutdown(struct pil_desc *desc) { }
static inline void pil_free_memory(struct pil_desc *desc) { }
static inline void pil_desc_release(struct pil_desc *desc) { }
static inline phys_addr_t pil_get_entry_addr(struct pil_desc *desc)
{
	return 0;
}
static inline int pil_do_ramdump(struct pil_desc *desc,
		void *ramdump_dev, void *minidump_dev)
{
	return 0;
}
static inline int pil_assign_mem_to_subsys(struct pil_desc *desc,
						phys_addr_t addr, size_t size)
{
	return 0;
}
static inline int pil_assign_mem_to_linux(struct pil_desc *desc,
						phys_addr_t addr, size_t size)
{
	return 0;
}
static inline int pil_assign_mem_to_subsys_and_linux(struct pil_desc *desc,
						phys_addr_t addr, size_t size)
{
	return 0;
}
static inline int pil_reclaim_mem(struct pil_desc *desc, phys_addr_t addr,
					size_t size, int VMid)
{
	return 0;
}
static inline bool pil_is_timeout_disabled(void) { return false; }
#endif

#endif
