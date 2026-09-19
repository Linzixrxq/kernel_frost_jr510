// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/uaccess.h>
#include <linux/of_address.h>
#include <linux/poll.h>
#include <linux/completion.h>
#include <linux/vdsp-ioctl.h>
#include <linux/ion.h>
#include <linux/dma-buf.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/syscalls.h>
#include "wdt/vdsp-wdt.h"
#include "vdsp-driver.h"
#include "smmu/vdsp-smmu-api.h"
#include "log/vdsp-log-intf.h"
#include "vdsp-hw-api.h"
#include "vdsp-loader.h"
#include "vdsp-runtime-type.h"

#undef VDBG
#define VDBG(fmt, args...) pr_debug(fmt, ##args)

#define VDSP_CDEV_NAME "vdsp"

#define EVT_TIMEOUT 3000000

static int firmware_command_timeout = VDSP_DEFAULT_TIMEOUT;
static struct vdsp *g_drvdata;

static int vdsp_synchronize(struct vdsp *drvdata);

static inline void vdsp_comm_write32(void __iomem *addr, u32 v)
{
	__raw_writel(v, addr);
}

static inline u32 vdsp_comm_read32(void __iomem *addr)
{
	return __raw_readl(addr);
}

static inline void vdsp_comm_write(void __iomem *addr, const void *p,
				  size_t sz)
{
	size_t sz32 = sz & ~3;
	u32 v;

	while (sz32) {
		memcpy(&v, p, sizeof(v));
		__raw_writel(v, addr);
		p += 4;
		addr += 4;
		sz32 -= 4;
	}
	sz &= 3;
	if (sz) {
		v = 0;
		memcpy(&v, p, sz);
		__raw_writel(v, addr);
	}
}

static inline void vdsp_comm_read(void __iomem *addr, void *p,
				  size_t sz)
{
	size_t sz32 = sz & ~3;
	u32 v;

	while (sz32) {
		v = __raw_readl(addr);
		memcpy(p, &v, sizeof(v));
		p += 4;
		addr += 4;
		sz32 -= 4;
	}
	sz &= 3;
	if (sz) {
		v = __raw_readl(addr);
		memcpy(p, &v, sz);
	}
}

static inline int vdsp_enable_dsp(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->enable)
		return drvdata->hw_ops->enable(drvdata->hw_arg);
	else
		return 0;
}

static inline void vdsp_disable_dsp(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->disable)
		drvdata->hw_ops->disable(drvdata->hw_arg);
}

static inline void vdsp_reset_dsp(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->reset)
		drvdata->hw_ops->reset(drvdata->hw_arg);
}

static inline void vdsp_dereset_dsp(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->dereset)
		drvdata->hw_ops->dereset(drvdata->hw_arg);
}

static inline int vdsp_is_active(struct vdsp *drvdata)
{
	int rc = 0;

	if (drvdata->hw_ops->is_active)
		rc = drvdata->hw_ops->is_active(drvdata->hw_arg);
	return rc;
}

static inline void vdsp_halt_dsp(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->halt)
		drvdata->hw_ops->halt(drvdata->hw_arg);
}

static inline void vdsp_release_dsp(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->release)
		drvdata->hw_ops->release(drvdata->hw_arg);
}

static inline void vdsp_config_dsp(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->boot_cfg)
		drvdata->hw_ops->boot_cfg(drvdata->hw_arg);
}

static int vdsp_load_dram_firmware(struct vdsp *drvdata)
{
	int rc = -EINVAL;

	if (drvdata->hw_ops->load_firmware)
		rc = drvdata->hw_ops->load_firmware(drvdata->hw_arg);
	else {
		pr_err("there is no firmware.\n");
		rc = 0;
	}

	return rc;
}

static inline void vdsp_send_device_irq(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->send_irq)
		drvdata->hw_ops->send_irq(drvdata->hw_arg,
			((struct vdsp_hw *)(drvdata->hw_arg))->device_irq[0]);
}

static inline bool vdsp_panic_check(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->panic_check)
		return drvdata->hw_ops->panic_check(drvdata->hw_arg);
	else
		return false;
}

static int ssr_event_notify(struct notifier_block *self,
			unsigned long action, void *data)
{
	int rc = -EINVAL;

	if (action == SSR_EVENT_PANIC) {
		vdsp_panic_notify();
	} else if (action == SSR_EVENT_SUSPENED) {
		rc = vdsp_runtime_suspend(data);
		if (rc)
			return NOTIFY_BAD;
	} else if (action == SSR_EVENT_RESUME) {
		rc = vdsp_runtime_resume(data);
		if (rc)
			return NOTIFY_BAD;
	} else if (action == SSR_EVENT_TX_IRQ) {
		struct vdsp_subsys_desc *desc = data;

		send_irq_to_vdsp(desc->dev, (int)desc->force_ramdump_irq_id);
	}

	return NOTIFY_OK;
}

struct notifier_block ssr_event_notifier = {
	.notifier_call = ssr_event_notify,
};

static int vdsp_bw_vote_update(struct vdsp *drvdata, unsigned long bw)
{
	int rc = 0;

	if (drvdata->hw_ops->bw_vote_update)
		rc = drvdata->hw_ops->bw_vote_update(drvdata->hw_arg, bw);
	else
		pr_err("Unsupport bw vote.\n");

	return rc;
}

static int vdsp_dvfs_vote(struct vdsp *drvdata,
		struct vdsp_dvfs_params params, unsigned long *clk, int *is_update)
{
	int rc = 0;

	if (drvdata->hw_ops->dvfs_vote)
		rc = drvdata->hw_ops->dvfs_vote(drvdata->hw_arg,
				params.core_clk, params.mips, clk, is_update);
	else
		pr_err("Unsupport dvfs vote.\n");

	return rc;
}

static int vdsp_dvfs_set(struct vdsp *drvdata, unsigned long target_clk)
{
	int rc = 0;

	if (drvdata->hw_ops->dvfs_set)
		rc = drvdata->hw_ops->dvfs_set(drvdata->hw_arg, target_clk);
	else
		pr_err("Unsupport dvfs set.\n");

	return rc;
}

static unsigned long vdsp_get_coreclk_rate(struct vdsp *drvdata)
{
	unsigned long freq = 0;

	if (drvdata->hw_ops->get_coreclk_rate)
		freq = drvdata->hw_ops->get_coreclk_rate(drvdata->hw_arg);
	else
		pr_err("Unsupport get core clock rate.\n");

	return freq;
}

static void vdsp_update_rtc(struct vdsp *drvdata)
{
	if (drvdata->hw_ops->update_rtc)
		drvdata->hw_ops->update_rtc(drvdata->hw_arg);
	else
		pr_err("Unsupport update rtc.\n");
}

static int vdsp_boot_firmware(struct vdsp *drvdata)
{
	int rc;

	memset_io(drvdata->comm, 0x0, VDSP_CMD_STRIDE);
	vdsp_halt_dsp(drvdata);
	vdsp_dereset_dsp(drvdata);

	rc = vdsp_load_dram_firmware(drvdata);
	if (rc < 0) {
		pr_err("load firmware failed.\n");
		return rc;
	}

	vdsp_release_dsp(drvdata); //vdsp start run

	rc = vdsp_synchronize(drvdata);
	if (rc < 0) {
		vdsp_halt_dsp(drvdata);
		pr_err("couldn't synchronize with the DSP core\n");
		return rc;
	}

	return 0;
}

static inline void __iomem *vdsp_comm_put_tlv(void __iomem **addr,
			uint32_t type,
			uint32_t length)
{
	struct vdsp_tlv __iomem *tlv = *addr;

	vdsp_comm_write32(&tlv->type, type);
	vdsp_comm_write32(&tlv->length, length);
	*addr = tlv->value + ((length + 3) / 4);
	return tlv->value;
}

static inline void __iomem *vdsp_comm_get_tlv(void __iomem **addr,
			uint32_t *type,
			uint32_t *length)
{
	struct vdsp_tlv __iomem *tlv = *addr;

	*type = vdsp_comm_read32(&tlv->type);
	*length = vdsp_comm_read32(&tlv->length);
	*addr = tlv->value + ((*length + 3) / 4);
	return tlv->value;
}

static void vdsp_sync_v2(struct vdsp *drvdata,
			void *hw_sync_data, size_t sz)
{
	struct vdsp_sync_v2 __iomem *shared_sync = drvdata->comm;
	void __iomem *addr = shared_sync->hw_sync_data;

	vdsp_comm_write(vdsp_comm_put_tlv(&addr,
			VDSP_SYNC_TYPE_HW_SPEC_DATA, sz),
		hw_sync_data, sz);
	if (drvdata->n_queues > 1) {
		struct vdsp_sync_v2 __iomem *queue_sync;
		unsigned int i;

		vdsp_comm_write(vdsp_comm_put_tlv(&addr,
				VDSP_SYNC_TYPE_HW_QUEUES,
				drvdata->n_queues * sizeof(u32)),
			drvdata->queue_priority,
			drvdata->n_queues * sizeof(u32));
		for (i = 1; i < drvdata->n_queues; ++i) {
			queue_sync = drvdata->queue[i].comm;
			vdsp_comm_write32(&queue_sync->sync,
					VDSP_SYNC_IDLE);
		}
	}
	vdsp_comm_put_tlv(&addr, VDSP_SYNC_TYPE_LAST, 0);
}

static int vdsp_sync_complete_v2(struct vdsp *drvdata, size_t sz)
{
	struct vdsp_sync_v2 __iomem *shared_sync = drvdata->comm;
	void __iomem *addr = shared_sync->hw_sync_data;
	u32 type, len;

	vdsp_comm_get_tlv(&addr, &type, &len);
	if (len != sz) {
		pr_err("HW spec data size modified by the DSP\n");
		return -EINVAL;
	}
	if (!(type & VDSP_SYNC_TYPE_ACCEPT))
		pr_info("HW spec data not recognized by the DSP\n");

	if (drvdata->n_queues > 1) {
		void __iomem *p = vdsp_comm_get_tlv(&addr, &type, &len);

		if (len != drvdata->n_queues * sizeof(u32)) {
			pr_err("Queue priority size modified by the DSP\n");
			return -EINVAL;
		}
		if (type & VDSP_SYNC_TYPE_ACCEPT) {
			vdsp_comm_read(p, drvdata->queue_priority,
				drvdata->n_queues * sizeof(u32));
		} else {
			pr_info("Queue priority data not recognized by the DSP\n");
			drvdata->n_queues = 1;
		}
	}
	return 0;
}

static int vdsp_synchronize(struct vdsp *drvdata)
{
	size_t sz;
	void *hw_sync_data;
	unsigned long deadline = jiffies + firmware_command_timeout * HZ;
	struct xrp_dsp_sync_v1 __iomem *shared_sync = drvdata->comm;
	int rc;
	u32 v, v1;
	int res;

	hw_sync_data = drvdata->hw_ops->get_hw_sync_data(drvdata->hw_arg, &sz);
	if (!hw_sync_data) {
		rc = -ENOMEM;
		goto err;
	}
	rc = -ENODEV;
	vdsp_comm_write32(&shared_sync->sync, VDSP_SYNC_START);
	mb(); //wait write register done
	do {
		v = vdsp_comm_read32(&shared_sync->sync);
		if (v != VDSP_SYNC_START)
			break;
		if (vdsp_panic_check(drvdata))
			goto err;
		schedule();
	} while (time_before(jiffies, deadline));

	switch (v) {
	case VDSP_SYNC_DSP_READY_V1:
		if (drvdata->n_queues > 1) {
			pr_info("Queue priority data not recognized by the DSP\n");
			drvdata->n_queues = 1;
		}
		vdsp_comm_write(&shared_sync->hw_sync_data, hw_sync_data, sz);
		break;
	case VDSP_SYNC_DSP_READY_V2:
		vdsp_sync_v2(drvdata, hw_sync_data, sz);
		break;
	case VDSP_SYNC_START:
		pr_err("DSP is not ready for synchronization\n");
		goto err;
	default:
		pr_err("DSP response to VDSP_SYNC_START is not recognized\n");
		goto err;
	}

	mb();//wait read and write register done
	vdsp_comm_write32(&shared_sync->sync, VDSP_SYNC_HOST_TO_DSP);

	do {
		mb(); //wait write register done
		v1 = vdsp_comm_read32(&shared_sync->sync);
		if (v1 == VDSP_SYNC_DSP_TO_HOST)
			break;
		if (vdsp_panic_check(drvdata))
			goto err;
		schedule();
	} while (time_before(jiffies, deadline));

	if (v1 != VDSP_SYNC_DSP_TO_HOST) {
		pr_err("DSP haven't confirmed initialization data reception\n");
		goto err;
	}

	if (v == VDSP_SYNC_DSP_READY_V2) {
		rc = vdsp_sync_complete_v2(drvdata, sz);
		if (rc < 0)
			goto err;
	}

	vdsp_send_device_irq(drvdata);

	res = wait_for_completion_timeout(&drvdata->queue[0].completion,
			firmware_command_timeout * HZ);
	rc = -ENODEV;
	if (vdsp_panic_check(drvdata))
		goto err;
	if (res == 0) {
		pr_err("host IRQ mode is requested, but DSP couldn't deliver IRQ during synchronization\n");
		goto err;
	}
	rc = 0;
err:
	kfree(hw_sync_data);
	vdsp_comm_write32(&shared_sync->sync, VDSP_SYNC_IDLE);
	return rc;
}

static int vdsp_dma_direction(unsigned int flags)
{
	static const enum dma_data_direction vdsp_dma_direction[] = {
		[0] = DMA_NONE,
		[VDSP_FLAG_READ] = DMA_TO_DEVICE,
		[VDSP_FLAG_WRITE] = DMA_FROM_DEVICE,
		[VDSP_FLAG_READ_WRITE] = DMA_BIDIRECTIONAL,
	};
	return vdsp_dma_direction[flags & VDSP_FLAG_READ_WRITE];
}

static void vdsp_dma_sync_for_device(struct dma_buf *dmabuf,
				unsigned long flags)
{
	dma_buf_end_cpu_access(dmabuf, vdsp_dma_direction(flags));
}

static void vdsp_dma_sync_for_cpu(struct dma_buf *dmabuf,
				unsigned long flags)
{
	dma_buf_begin_cpu_access(dmabuf, vdsp_dma_direction(flags));
}

static bool vdsp_cmd_complete(struct vdsp_comm *comm)
{
	struct vdsp_cmd __iomem *cmd = comm->comm;
	u32 flags = vdsp_comm_read32(&cmd->flags);

	rmb(); //wait read register done
	return (flags & (VDSP_CMD_FLAG_REQUEST_VALID |
			 VDSP_CMD_FLAG_RESPONSE_VALID)) ==
		(VDSP_CMD_FLAG_REQUEST_VALID |
		 VDSP_CMD_FLAG_RESPONSE_VALID);
}

static long vdsp_complete_cmd_irq(struct vdsp *drvdata, struct vdsp_comm *comm,
				 bool (*cmd_complete)(struct vdsp_comm *p))
{
	long timeout;

	if (cmd_complete(comm))
		return 0;

	if (atomic_read(&drvdata->is_crash)) {
		pr_err("===== vdsp core crash =====.\n");
		return -EBUSY;
	}

	do {
		timeout = wait_for_completion_interruptible_timeout(&comm->completion,
						msecs_to_jiffies(EVT_TIMEOUT));

		if (cmd_complete(comm))
			return 0;

		if (atomic_read(&drvdata->is_crash)) {
			pr_err("===== vdsp core crash while waiting =====.\n");
			return -EBUSY;
		}

	} while (timeout > 0);

	if (timeout == 0)
		return -EBUSY;

	return timeout;
}

irqreturn_t vdsp_irq_handler(int irq, void *dev_id)
{
	struct vdsp *drvdata = (struct vdsp *)dev_id;
	unsigned int i, n = 0;

	if (!drvdata->comm)
		return IRQ_NONE;

	for (i = 0; i < drvdata->n_queues; ++i) {
		if (vdsp_cmd_complete(drvdata->queue + i)) {
			complete(&drvdata->queue[i].completion);
			++n;
		}
	}
	return n ? IRQ_HANDLED : IRQ_NONE;
}

irqreturn_t vdsp_rtc_handler(int irq, void *dev_id)
{
	struct vdsp *drvdata = (struct vdsp *)dev_id;

	vdsp_update_rtc(drvdata);

	return IRQ_HANDLED;
}

static struct vdsp_dma_buf_info *vdsp_find_mapping_by_ion(
	struct vdsp *drvdata, int ion_fd)
{
	struct vdsp_dma_buf_info *mapping = NULL;

	if (ion_fd < 0 || !drvdata) {
		pr_err("Invalid ion fd %d or drvdata:%p.\n",
			ion_fd, drvdata);
		return NULL;
	}

	mutex_lock(&drvdata->buf_mutex);
	list_for_each_entry(mapping, &drvdata->smmu_buf_list, list) {
		if (mapping->dmafd == ion_fd) {
			VDBG("Find ion_fd %d.\n", ion_fd);
			mutex_unlock(&drvdata->buf_mutex);
			return mapping;
		}
	}

	mutex_unlock(&drvdata->buf_mutex);
	return NULL;
}

static struct vdsp_dma_buf_info *vdsp_find_mapping_by_dma_buf(
	struct vdsp *drvdata, struct dma_buf *dmabuf)
{
	struct vdsp_dma_buf_info *mapping = NULL;

	if (!drvdata || !dmabuf) {
		pr_err("Invalid dmabuf:%p or drvdata:%p.\n",
			dmabuf, drvdata);
		return NULL;
	}

	mutex_lock(&drvdata->buf_mutex);
	list_for_each_entry(mapping, &drvdata->smmu_buf_list, list) {
		if (mapping->dmabuf == dmabuf) {
			VDBG("Find dmabuf %p.\n", dmabuf);
			mutex_unlock(&drvdata->buf_mutex);
			return mapping;
		}
	}

	mutex_unlock(&drvdata->buf_mutex);
	return NULL;
}

static void vdsp_clean_dma_buf_list(struct vdsp *drvdata)
{
	struct vdsp_dma_buf_info *mapping_info, *temp;

	VDBG("Enter.\n");
	if (!drvdata) {
		pr_err("Invalid drvdata:%p.\n", drvdata);
		return;
	}

	mutex_lock(&drvdata->buf_mutex);
	if (list_empty_careful(&drvdata->smmu_buf_list))
		goto out;

	list_for_each_entry_safe(mapping_info, temp,
			&drvdata->smmu_buf_list, list) {
		VDBG("Free mapping address 0x%x, fd = %d",
			mapping_info->dmaaddr, mapping_info->dmafd);

		if (mapping_info->dmaaddr) {
			dma_buf_unmap_attachment(mapping_info->attach,
				mapping_info->table, mapping_info->dir);
			dma_buf_detach(mapping_info->dmabuf, mapping_info->attach);
			mapping_info->dmaaddr = 0;
		}
		if (mapping_info->kaddr) {
			dma_buf_vunmap(mapping_info->dmabuf, (void *)mapping_info->kaddr);
			mapping_info->kaddr = 0;
		}

		dma_buf_put(mapping_info->dmabuf);
		mapping_info->dmabuf = NULL;
		list_del_init(&mapping_info->list);
		/* free one buffer */
		kfree(mapping_info);
		mapping_info = NULL;
	}
out:
	mutex_unlock(&drvdata->buf_mutex);
	VDBG("Exit.\n");
}

static int vdsp_ion_munmap(struct vdsp *drvdata, struct vdsp_dma_buf_info *mapping)
{
	if (!drvdata || !mapping) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	mutex_lock(&drvdata->buf_mutex);
	if (mapping->dmaaddr) {
		pr_debug("munmap dma_fd:%d, kaddr:0x%llx dmaaddr:0x%x\n",
				mapping->dmafd, mapping->kaddr, mapping->dmaaddr);

		dma_buf_unmap_attachment(mapping->attach,
			mapping->table, mapping->dir);
		dma_buf_detach(mapping->dmabuf, mapping->attach);
	}
	if (mapping->kaddr)
		dma_buf_vunmap(mapping->dmabuf, (void *)mapping->kaddr);

	dma_buf_put(mapping->dmabuf);
	mapping->dmabuf = NULL;
	//spin_lock(&drvdata->slock);
	list_del_init(&mapping->list);
	//spin_unlock(&drvdata->slock);
	/* free one buffer */
	kfree(mapping);
	mapping = NULL;

	mutex_unlock(&drvdata->buf_mutex);

	return 0;
}

static int vdsp_ion_munmap_user(struct vdsp *drvdata, int dmafd)
{
	int rc = 0;
	struct vdsp_dma_buf_info *mapping = NULL;

	if (!drvdata || dmafd < 0) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	mapping = vdsp_find_mapping_by_ion(drvdata, dmafd);
	if (!mapping) {
		pr_err("Invalid params fd = %d.\n", dmafd);
		return -EINVAL;
	}

	rc = vdsp_ion_munmap(drvdata, mapping);
	if (rc < 0) {
		pr_err("ion munmap failed.\n");
		return -EFAULT;
	}

	return rc;
}

static int vdsp_ion_munmap_kernel(struct vdsp *drvdata, struct dma_buf *dmabuf)
{
	int rc = 0;
	struct vdsp_dma_buf_info *mapping = NULL;

	if (!drvdata || !dmabuf) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	mapping = vdsp_find_mapping_by_dma_buf(drvdata, dmabuf);
	if (!mapping) {
		pr_err("mapping is NULL.\n");
		return -EINVAL;
	}

	rc = vdsp_ion_munmap(drvdata, mapping);
	if (rc < 0) {
		pr_err("ion munmap failed.\n");
		return -EFAULT;
	}

	return rc;
}

static int vdsp_ion_mmap(struct vdsp *drvdata,
				int dmafd, int flags, enum vdsp_memory_type_t type,
				struct dma_buf *dmabuf,
				struct vdsp_dma_buf_info **ion_mapping)
{
	dma_addr_t hw_vaddr = 0;
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *table = NULL;
	enum dma_data_direction dma_dir = DMA_BIDIRECTIONAL;
	struct vdsp_dma_buf_info *mapping = NULL;
	int rc = 0;
	void *tmp_addr = NULL;
	uint64_t kaddr = 0;
	uint32_t dmaaddr = 0;
	struct page *page;
	phys_addr_t paddr;

	if (!drvdata || !drvdata->dev || !dmabuf) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	mutex_lock(&drvdata->buf_mutex);
	VDBG("flags:%d.\n", flags);
	if (flags & MEM_FLAG_DMA_RW) {
		attach = dma_buf_attach(dmabuf, drvdata->dev);
		if (IS_ERR_OR_NULL(attach)) {
			rc = PTR_ERR(attach);
			pr_err("Dma buf attach failed.\n");
			goto err_out;
		}

		table = dma_buf_map_attachment(attach, dma_dir);
		if (IS_ERR_OR_NULL(table)) {
			rc = PTR_ERR(table);
			pr_err("Dma map attachment failed, size=%zu.\n",
				dmabuf->size);
			goto err_detach;
		}

		hw_vaddr = sg_dma_address(table->sgl);
		page = sg_page(table->sgl);
		paddr = PFN_PHYS(page_to_pfn(page));
		if (table->sgl) {
			dmaaddr = cpu_to_le32(hw_vaddr);
			pr_debug("mmap dma_fd:%d, size:%ld, phy_addr:0x%llx dmaaddr:0x%x\n",
				dmafd, (size_t)dmabuf->size, paddr, dmaaddr);
		} else {
			rc = -EINVAL;
			pr_err("table sgl is null.\n");
			goto err_unmap_sg;
		}
	}
	if (flags & MEM_FLAG_KERNEL) {
		tmp_addr = dma_buf_vmap(dmabuf);
		if (IS_ERR_OR_NULL(tmp_addr)) {
			pr_err("dmabuf map to kernel addr failed.\n");
			kaddr = 0;
			rc = -ENOSPC;
			goto err_unmap_sg;
		}
		kaddr = (uint64_t)tmp_addr;
	}

	/* fill up mapping_info */
	mapping = kzalloc(sizeof(struct vdsp_dma_buf_info), GFP_KERNEL);
	if (!mapping) {
		rc = -ENOMEM;
		goto err_vunmap;
	}

	mapping->dmafd = dmafd;
	mapping->dmabuf = dmabuf;
	mapping->attach = attach;
	mapping->table = table;
	mapping->dmaaddr = dmaaddr;
	mapping->kaddr = kaddr;
	mapping->len = (size_t)dmabuf->size;
	mapping->dir = dma_dir;
	mapping->type = type;

	if (((flags & MEM_FLAG_DMA_RW) && !hw_vaddr) ||
			((flags & MEM_FLAG_KERNEL) && !kaddr)) {
		pr_err("Space Allocation failed.\n");
		kzfree(mapping);
		mapping = NULL;
		rc = -EINVAL;
		goto err_vunmap;
	}

	*ion_mapping = mapping;

	/* add to the list */
	//spin_lock(&drvdata->slock);
	list_add(&mapping->list,
		&drvdata->smmu_buf_list);
	//spin_unlock(&drvdata->slock);
	mutex_unlock(&drvdata->buf_mutex);
	return rc;

err_vunmap:
	dma_buf_vunmap(dmabuf, (void *)kaddr);
err_unmap_sg:
	mapping->dmaaddr = 0;
	if (flags & MEM_FLAG_DMA_RW)
		dma_buf_unmap_attachment(attach, table, dma_dir);
err_detach:
	if (flags & MEM_FLAG_DMA_RW)
		dma_buf_detach(dmabuf, attach);
err_out:
	if (dmabuf)
		dma_buf_put(dmabuf);

	mutex_unlock(&drvdata->buf_mutex);
	return rc;
}

static int vdsp_ion_mmap_user(struct vdsp *drvdata,
				int dmafd, int flags,
				struct vdsp_dma_buf_info **ion_mmaping)
{
	struct dma_buf *dmabuf = NULL;
	int rc = 0;

	if (dmafd < 0) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	dmabuf = dma_buf_get(dmafd);
	if (IS_ERR_OR_NULL((void *)(dmabuf))) {
		pr_err("Failed to import dma_buf through fd.\n");
		return -EINVAL;
	}

	rc = vdsp_ion_mmap(drvdata, dmafd, flags,
		VDSP_MEMORY_TYPE_USER, dmabuf, ion_mmaping);
	if (rc < 0) {
		pr_err("mmap ion buffer failed.\n");
		return rc;
	}
	return rc;
}

static int vdsp_ion_mmap_kernel(struct vdsp *drvdata,
		int flags, struct dma_buf *dmabuf, struct vdsp_dma_buf_info **ion_mmaping)
{
	int rc = 0;

	rc = vdsp_ion_mmap(drvdata, -1, flags,
		VDSP_MEMORY_TYPE_KERNEL, dmabuf, ion_mmaping);
	if (rc < 0) {
		pr_err("mmap ion buffer failed.\n");
		return rc;
	}
	return rc;
}

static int vdsp_ion_kernel_alloc_mmap(struct vdsp *drvdata,
		unsigned int heap_id_mask, size_t size,
		unsigned int mem_flags,
		struct vdsp_dma_buf_info **ion_mapping)
{
	size_t len;
	int rc;
	struct dma_buf *dmabuf = NULL;

	len = ALIGN_UP(size, 4096);
	dmabuf = ion_alloc(len, heap_id_mask, 0);
	if (!dmabuf) {
		pr_err("ion alloc memory failed.\n");
		rc = -ENOMEM;
		goto err_out;
	}

	rc = vdsp_ion_mmap_kernel(drvdata, mem_flags, dmabuf, ion_mapping);
	if (rc < 0) {
		pr_err("ion buffer mmap failed.\n");
		goto err_out;
	}

err_out:
	return rc;
}

static int vdsp_ion_kernel_free(struct vdsp *drvdata, struct vdsp_dma_buf_info *mapping)
{
	int rc = 0;

	if (!mapping) {
		pr_err("Invalid parameter.\n");
		return -EINVAL;
	}

	rc = vdsp_ion_munmap_kernel(drvdata, mapping->dmabuf);
	if (rc < 0) {
		pr_err("ion munmap failed.\n");
		return -EFAULT;
	}

	return rc;
}

static int ion_alloc_fd(size_t len, unsigned int heap_id_mask,
			unsigned int flags)
{
	int fd;
	struct dma_buf *dmabuf;

	dmabuf = ion_alloc(len, heap_id_mask, flags);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (fd < 0)
		dma_buf_put(dmabuf);

	return fd;
}

static int vdsp_ion_ioctl_alloc_mmap(struct file *filp,
		struct vdsp_ioctl_alloc __user *p)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_alloc alloc_info;
	long rc;
	unsigned int heap_id_mask;
	unsigned int mem_flags = MEM_FLAG_DMA_RW | MEM_FLAG_KERNEL;
	struct vdsp_dma_buf_info *ion_mapping = NULL;
	size_t len;
	int dmafd;

	VDBG("enter.\n");
	if (copy_from_user(&alloc_info, p, sizeof(*p)))
		return -EFAULT;

	if (alloc_info.heap_id_mask >= 0)
		heap_id_mask = (1 << alloc_info.heap_id_mask);
	else
		heap_id_mask = ION_HEAP_SYSTEM;

	if (heap_id_mask == ION_HEAP_SYSTEM)
		len = ALIGN_UP(alloc_info.size, 4096);
	else
		len = ALIGN_UP(alloc_info.size, 0x200000);

	dmafd = ion_alloc_fd(len, heap_id_mask, 0);
	if (dmafd < 0) {
		pr_err("ion alloc memory failed.\n");
		return -ENOMEM;
	}

	rc = vdsp_ion_mmap_user(drvdata,
					dmafd, mem_flags, &ion_mapping);
	if (rc < 0 || !ion_mapping) {
		pr_err("ion allocate and map buffer failed.\n");
		return rc;
	}

	alloc_info.dmafd = ion_mapping->dmafd;
	alloc_info.flags = mem_flags;

	if (copy_to_user(p, &alloc_info, sizeof(*p))) {
		pr_err("copy alloc info to user failed.\n");
		return -EFAULT;
	}

	VDBG("exit.\n");
	return rc;
}

static long vdsp_ion_ioctl_free(struct file *filp,
			struct vdsp_ioctl_alloc __user *p)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_alloc alloc_info;

	VDBG("enter.\n");
	if (copy_from_user(&alloc_info, p, sizeof(*p)))
		return -EFAULT;

	if (!drvdata || (alloc_info.dmafd < 0)) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	if (vdsp_ion_munmap_user(drvdata, alloc_info.dmafd)) {
		pr_err("Free dmafd:%d failed.\n", alloc_info.dmafd);
		return -EFAULT;
	}

	if (copy_to_user(p, &alloc_info, sizeof(*p))) {
		pr_err("copy alloc info to user failed.\n");
		return -EFAULT;
	}

	VDBG("exit.\n");
	return 0;
}

static int vdsp_ion_ioctl_mmap(struct file *filp,
		struct vdsp_ioctl_alloc __user *p)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_alloc mmap_info;
	long rc;
	unsigned int mem_flags = MEM_FLAG_DMA_RW;
	struct vdsp_dma_buf_info *ion_mapping = NULL;

	VDBG("enter.\n");
	if (copy_from_user(&mmap_info, p, sizeof(*p)))
		return -EFAULT;

	if (mmap_info.dmafd < 0) {
		pr_err("Invalid dmafd:%d.\n", mmap_info.dmafd);
		return -EINVAL;
	}

	rc = vdsp_ion_mmap_user(drvdata,
					mmap_info.dmafd, mem_flags, &ion_mapping);
	if (rc < 0 || !ion_mapping) {
		pr_err("ion map host buffer failed.\n");
		return rc;
	}

	mmap_info.flags = mem_flags;

	if (copy_to_user(p, &mmap_info, sizeof(*p))) {
		pr_err("copy mmap info to user failed.\n");
		return -EFAULT;
	}

	VDBG("exit.\n");
	return rc;
}

static long vdsp_ion_ioctl_munmap(struct file *filp,
			struct vdsp_ioctl_alloc __user *p)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_alloc mmap_info;

	VDBG("enter.\n");
	if (copy_from_user(&mmap_info, p, sizeof(*p)))
		return -EFAULT;

	if (!drvdata || (mmap_info.dmafd < 0)) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	if (vdsp_ion_munmap_user(drvdata, mmap_info.dmafd)) {
		pr_err("Free dmafd:%d failed.\n", mmap_info.dmafd);
		return -EFAULT;
	}

	if (copy_to_user(p, &mmap_info, sizeof(*p))) {
		pr_err("copy mmap info to user failed.\n");
		return -EFAULT;
	}

	VDBG("exit.\n");
	return 0;
}

static long vdsp_get_ion_dmaaddr(struct vdsp *drvdata, int dmafd,
				unsigned int *dmaaddr, struct vdsp_dma_buf_info *mapping)
{
	struct vdsp_dma_buf_info *mapping_info = NULL;

	if (!drvdata || dmafd < 0) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	mapping_info = vdsp_find_mapping_by_ion(drvdata, dmafd);
	if (!mapping_info) {
		pr_err("Invalid params fd = %d.\n", dmafd);
		return -EINVAL;
	}

	*dmaaddr = mapping_info->dmaaddr;
	memcpy(mapping, mapping_info, sizeof(struct vdsp_dma_buf_info));
	return 0;
}

static void vdsp_unmap_request_nowb(struct file *filp, struct vdsp_request *rq)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	size_t n_buffers = rq->n_buffers;

	if (n_buffers > VDSP_CMD_INLINE_BUFFER_COUNT)
		vdsp_ion_kernel_free(drvdata, &rq->dsp_buffer_mapping);

	if (n_buffers)
		kfree(rq->buffer_mapping);
}

static long vdsp_unmap_request(struct file *filp, struct vdsp_request *rq)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	size_t n_buffers = rq->n_buffers;
	long rc = 0;

	if (rq->ioctl_queue.out_data_size <= VDSP_CMD_INLINE_DATA_SIZE) {
		if (copy_to_user((void __user *)(unsigned long)rq->ioctl_queue.out_data_addr,
				rq->out_data,
				rq->ioctl_queue.out_data_size)) {
			pr_err("out_data could not be copied\n");
			rc = -EFAULT;
		}
	}

	if (n_buffers > VDSP_CMD_INLINE_BUFFER_COUNT)
		rc = vdsp_ion_kernel_free(drvdata, &rq->dsp_buffer_mapping);

	if (n_buffers) {
		kfree(rq->buffer_mapping);
		rq->n_buffers = 0;
	}

	return rc;
}

static long vdsp_map_request(struct file *filp, struct vdsp_request *rq)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_buffer __user *buffer;
	size_t n_buffers = rq->ioctl_queue.buffer_size /
		sizeof(struct vdsp_ioctl_buffer);
	unsigned int heap_id_mask;
	unsigned int mem_flags;
	size_t size;
	struct vdsp_dma_buf_info *ion_mapping = NULL;
	size_t i;
	long rc = 0;

	VDBG("enter.\n");
	if ((rq->ioctl_queue.flags & VDSP_QUEUE_FLAG_NSID) &&
		copy_from_user(rq->nsid,
			(void __user *)(unsigned long)rq->ioctl_queue.nsid_addr,
			sizeof(rq->nsid))) {
		pr_err("nsid could not be copied.\n ");
		return -EINVAL;
	}

	if (rq->ioctl_queue.in_data_size > VDSP_CMD_INLINE_INDATA_SIZE) {
		rc = vdsp_get_ion_dmaaddr(drvdata, rq->ioctl_queue.in_data_dmafd,
			&rq->in_data_dmaaddr, &rq->in_data_mapping);
		if (rc < 0) {
			pr_err("get in_data_dmaaddr failed.\n");
			return rc;
		}
		VDBG("flag:0x%x, indata_dmafd:%d, indatasize:%d, indata_dmaaddr:0x%x",
			rq->ioctl_queue.flags,
			rq->ioctl_queue.in_data_dmafd,
			rq->ioctl_queue.in_data_size,
			rq->in_data_dmaaddr);
	} else {
		if (copy_from_user(rq->in_data,
				   (void __user *)(unsigned long)rq->ioctl_queue.in_data_addr,
				   rq->ioctl_queue.in_data_size)) {
			pr_err("in_data could not be copied\n");
			return -EFAULT;
		}
		VDBG("flag:0x%x, indata:%d, indatasize:%d",
			rq->ioctl_queue.flags,
			((char *)rq->in_data)[0],
			rq->ioctl_queue.in_data_size);
	}

	if (rq->ioctl_queue.out_data_size > VDSP_CMD_INLINE_DATA_SIZE) {
		rc = vdsp_get_ion_dmaaddr(drvdata, rq->ioctl_queue.out_data_dmafd,
			&rq->out_data_dmaaddr, &rq->out_data_mapping);
		if (rc < 0) {
			pr_err("get out_data damaddr failed.\n");
			return rc;
		}
	}

	rq->n_buffers = n_buffers;
	if (n_buffers) {
		rq->buffer_mapping = kcalloc(1, n_buffers * sizeof(*rq->buffer_mapping),
						GFP_KERNEL);
		if (n_buffers > VDSP_CMD_INLINE_BUFFER_COUNT) {
			heap_id_mask = ION_HEAP_SYSTEM;
			mem_flags = (MEM_FLAG_DMA_RW | MEM_FLAG_KERNEL);
			size = n_buffers * sizeof(*rq->dsp_buffer);

			rc = vdsp_ion_kernel_alloc_mmap(drvdata, heap_id_mask, size,
				mem_flags, &ion_mapping);
			if (rc < 0 || !ion_mapping) {
				kfree(rq->buffer_mapping);
				pr_err("ion allocate and map buffer failed.\n");
				return rc;
			}
			ion_mapping->cache_flag = VDSP_FLAG_READ_WRITE;
			rq->dsp_buffer = (struct vdsp_buffer *)(ion_mapping->kaddr);
		} else {
			rq->dsp_buffer = rq->buffer_data;
		}
	}

	buffer = (void __user *)(unsigned long)rq->ioctl_queue.buffer_addr;

	for (i = 0; i < n_buffers; ++i) {
		struct vdsp_ioctl_buffer ioctl_buffer;
		struct vdsp_dma_buf_info *mapping_info = NULL;

		if (copy_from_user(&ioctl_buffer, buffer + i,
				sizeof(ioctl_buffer))) {
			rc = -EFAULT;
			goto share_err;
		}
		if (ioctl_buffer.flags & VDSP_FLAG_READ_WRITE) {
			/* get dma access address */
			VDBG("dmafd:%d.\n", ioctl_buffer.dmafd);
			if (ioctl_buffer.dmafd < 0) {
				pr_err("Invalid ion buffer dmafd:%d.\n", ioctl_buffer.dmafd);
				rc = -EFAULT;
				goto share_err;
			}
			mapping_info = vdsp_find_mapping_by_ion(drvdata, ioctl_buffer.dmafd);
			if (!mapping_info) {
				pr_err("Cannt find ion buffer dmafd:%d.\n", ioctl_buffer.dmafd);
				rc = -EFAULT;
				goto share_err;
			}
		} else {
			pr_err("Invalid flags:%x.\n", ioctl_buffer.flags);
			rc = -EFAULT;
			goto share_err;
		}

		memcpy(rq->buffer_mapping + i, mapping_info, sizeof(struct vdsp_dma_buf_info));

		if (n_buffers > VDSP_CMD_INLINE_BUFFER_COUNT)
			vdsp_dma_sync_for_cpu(ion_mapping->dmabuf, ion_mapping->cache_flag);
		rq->dsp_buffer[i] = (struct vdsp_buffer){
			.flags = ioctl_buffer.flags,
			.size = ioctl_buffer.size,
			.addr = mapping_info->dmaaddr,
		};
		VDBG("dsp_buffer[%d] flags:0x%x, size:%d, dmafd:%d, addr:0x%x.\n",
			(int)i, rq->dsp_buffer[i].flags, (int)rq->dsp_buffer[i].size,
			ioctl_buffer.dmafd, rq->dsp_buffer[i].addr);
		if (n_buffers > VDSP_CMD_INLINE_BUFFER_COUNT)
			vdsp_dma_sync_for_device(ion_mapping->dmabuf, ion_mapping->cache_flag);
	}

	if (n_buffers > VDSP_CMD_INLINE_BUFFER_COUNT) {
		/* get dsp buffer dma access address */
		if (ion_mapping) {
			rq->dsp_buffer_dmaaddr = ion_mapping->dmaaddr;
			rq->dsp_buffer_fd = ion_mapping->dmafd;
			memcpy(&rq->dsp_buffer_mapping, ion_mapping,
				sizeof(struct vdsp_dma_buf_info));
			VDBG("dsp_buffer dmaaddr:0x%x, dmafd:%d.\n",
				(unsigned long)rq->dsp_buffer_dmaaddr,
				rq->dsp_buffer_fd);
		}
	}
share_err:
	if (rc < 0)
		vdsp_unmap_request_nowb(filp, rq);
	VDBG("exit rc:%d.\n", rc);
	return rc;
}

static void vdsp_fill_hw_request(struct vdsp_cmd __iomem *cmd,
			struct vdsp_request *rq)
{
	vdsp_comm_write32(&cmd->in_data_size, rq->ioctl_queue.in_data_size);
	vdsp_comm_write32(&cmd->out_data_size, rq->ioctl_queue.out_data_size);
	vdsp_comm_write32(&cmd->buffer_size,
			rq->n_buffers * sizeof(struct vdsp_buffer));

	if (rq->ioctl_queue.in_data_size > VDSP_CMD_INLINE_INDATA_SIZE)
		vdsp_comm_write32(&cmd->in_data_addr, rq->in_data_dmaaddr);
	else
		vdsp_comm_write(&cmd->in_data, rq->in_data,
			rq->ioctl_queue.in_data_size);

	if (rq->ioctl_queue.out_data_size > VDSP_CMD_INLINE_DATA_SIZE)
		vdsp_comm_write32(&cmd->out_data_addr, rq->out_data_dmaaddr);

	if (rq->n_buffers > VDSP_CMD_INLINE_BUFFER_COUNT)
		vdsp_comm_write32(&cmd->buffer_addr, (unsigned int)rq->dsp_buffer_dmaaddr);
	else
		vdsp_comm_write(&cmd->buffer_data, rq->dsp_buffer,
			rq->n_buffers * sizeof(struct vdsp_buffer));

	if (rq->ioctl_queue.flags & VDSP_QUEUE_FLAG_NSID)
		vdsp_comm_write(&cmd->nsid, rq->nsid, sizeof(rq->nsid));

	wmb();//wait for hw register write done
	/* update flags */
	vdsp_comm_write32(&cmd->flags,
			(rq->ioctl_queue.flags & ~VDSP_CMD_FLAG_RESPONSE_VALID) |
			VDSP_CMD_FLAG_REQUEST_VALID);

	{
		struct vdsp_cmd dsp_cmd;

		vdsp_comm_read(cmd, &dsp_cmd, sizeof(dsp_cmd));
		VDBG("cmd for DSP: falgs:0x%x in_data_size:%d out_data_size:%d.\n",
			dsp_cmd.flags, dsp_cmd.in_data_size, dsp_cmd.out_data_size);
	}
}

static long vdsp_complete_hw_request(struct vdsp_cmd __iomem *cmd,
				    struct vdsp_request *rq)
{
	u32 flags = vdsp_comm_read32(&cmd->flags);

	if (rq->ioctl_queue.out_data_size <= VDSP_CMD_INLINE_DATA_SIZE)
		vdsp_comm_read(&cmd->out_data, rq->out_data,
			rq->ioctl_queue.out_data_size);
	if (rq->n_buffers <= VDSP_CMD_INLINE_BUFFER_COUNT)
		vdsp_comm_read(&cmd->buffer_data, rq->dsp_buffer,
			rq->n_buffers * sizeof(struct vdsp_buffer));
	vdsp_comm_write32(&cmd->flags, 0);

	return (flags & VDSP_CMD_FLAG_RESPONSE_DELIVERY_FAIL) ? -ENXIO : 0;
}

static long vdsp_ioctl_submit_sync(struct file *filp,
				  struct vdsp_ioctl_queue __user *p)
{
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_comm *queue = drvdata->queue;
	struct vdsp_request vdsp_rq, *rq = &vdsp_rq;
	long rc = 0;
	unsigned int n;

	if (copy_from_user(&rq->ioctl_queue, p, sizeof(*p)))
		return -EFAULT;

	VDBG("flags:0x%x n_queues:%d.\n",
		rq->ioctl_queue.flags, drvdata->n_queues);
	if (rq->ioctl_queue.flags & ~VDSP_QUEUE_VALID_FLAGS) {
		pr_err("invalid flags 0x%x\n", rq->ioctl_queue.flags);
		return -EINVAL;
	}

	if (drvdata->n_queues > 1) {
		n = (rq->ioctl_queue.flags & VDSP_QUEUE_FLAG_PRIO) >>
			VDSP_QUEUE_FLAG_PRIO_SHIFT;

		if (n >= drvdata->n_queues)
			n = drvdata->n_queues - 1;
		queue = drvdata->queue_ordered[n];
		VDBG("priority: %d -> %d\n", n, queue->priority);
	}

	rc = vdsp_map_request(filp, rq);
	if (rc < 0)
		return rc;

	mutex_lock(&queue->lock);
	if (drvdata->ssr_info->subsys_dev->track.state != SUBSYS_RUNNING) {
		rc = -ENODEV;
	} else {
		vdsp_fill_hw_request(queue->comm, rq);

		vdsp_send_device_irq(drvdata);

		rc = vdsp_complete_cmd_irq(drvdata, queue,
					vdsp_cmd_complete);
		VDBG("Is timeout?%ld.\n", rc);
		/* copy back inline data */
		if (rc == 0)
			rc = vdsp_complete_hw_request(queue->comm, rq);
	}
	mutex_unlock(&queue->lock);

	VDBG("rc:%ld", rc);
	if (rc == 0)
		rc = vdsp_unmap_request(filp, rq);
	else
		vdsp_unmap_request_nowb(filp, rq);

	return rc;
}

int vdsp_dvfs_commit_sync_nolock(unsigned long freq)
{
	struct vdsp *drvdata = g_drvdata;
	struct vdsp_comm *queue = NULL;
	struct vdsp_runtime_request_t runtime_rq;
	struct vdsp_request rq;
	int rc = -EINVAL;

	if (!drvdata) {
		pr_err("drvdata:%p is NULL.\n", drvdata);
		return -EINVAL;
	}

	queue = drvdata->queue;
	if (!queue) {
		pr_err("drvdata queue is NULL.\n");
		return -EINVAL;
	}

	if (drvdata->ssr_info->subsys_dev->track.state != SUBSYS_RUNNING) {
		pr_err("vdsp is power off or crashed.\n");
		return -ENODEV;
	}

	memset(&runtime_rq, 0, sizeof(runtime_rq));
	runtime_rq.type = VDSP_RUNTIME_SET_FREQ;
	runtime_rq.set_freq_payload.set_core_clk_freq = 1;
	runtime_rq.set_freq_payload.core_clk_freq = (unsigned int)freq;

	memset(&rq, 0, sizeof(rq));
	memcpy(rq.nsid, (void *)RUNTIME_NSID, VDSP_CMD_NAMESPACE_ID_SIZE);
	memcpy(rq.in_data, &runtime_rq, sizeof(runtime_rq));
	rq.ioctl_queue.in_data_size = sizeof(runtime_rq);
	rq.ioctl_queue.out_data_size = sizeof(struct vdsp_runtime_response_t);
	rq.ioctl_queue.flags = VDSP_QUEUE_FLAG_NSID;

	vdsp_fill_hw_request(queue->comm, &rq);

	vdsp_send_device_irq(drvdata);

	rc = vdsp_complete_cmd_irq(drvdata, queue,
			vdsp_cmd_complete);
	/* copy back inline data */
	if (rc == 0) {
		rc = vdsp_complete_hw_request(queue->comm, &rq);
	} else {
		pr_err("vdsp command timeout.\n");
		return -ENODEV;
	}

	return rc;
}

static int vdsp_update_dvfs(struct vdsp *drvdata, unsigned long target_freq)
{
	int rc = -EINVAL;

	vdsp_wdt_hw_deinit();
	rc = vdsp_dvfs_commit_sync_nolock(target_freq);
	if (rc < 0) {
		pr_err("submit dvfs to vdsp failed.\n");
		goto err_out;
	}

	vdsp_halt_dsp(drvdata);

	rc = vdsp_dvfs_set(drvdata, target_freq);
	if (rc < 0)
		pr_err("VDSP update dvfs failed!!\n");

	vdsp_release_dsp(drvdata);

err_out:
	vdsp_wdt_hw_init();
	return rc;
}

static int vdsp_ioctl_init(struct file *filp, struct vdsp_ioctl_params __user *p)
{
	int rc = -EINVAL;
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_params *params = &vdsp_file->params;
	struct vdsp_ioctl_params params_new;
	struct vdsp_dvfs_params *dvfs_new, *dvfs_current;
	struct vdsp_dvfs_params *dvfs_total, dvfs_tmp;
	unsigned long target_freq = 0;
	int is_update = 0;
	int i;

	VDBG("enter.\n");
	if (!drvdata) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	if (atomic_inc_return(&drvdata->log_inited) == 1) {
		rc = vdsp_log_start();
		if (rc < 0) {
			atomic_dec(&drvdata->log_inited);
			pr_err("start log failed.\n");
			return rc;
		}
	}

	if (copy_from_user(&params_new, p, sizeof(*p))) {
		pr_err("copy params to kernel failed.\n");
		return -EFAULT;
	}

	mutex_lock(&drvdata->bw_mutex);
	dvfs_current = &params->dvfs_params;
	dvfs_new = &params_new.dvfs_params;
	dvfs_total = &(drvdata->params.dvfs_params);

	dvfs_tmp.bps = dvfs_total->bps + dvfs_new->bps;
	dvfs_tmp.mips = dvfs_total->mips + dvfs_new->mips;
	if (dvfs_new->core_clk > dvfs_total->core_clk)
		dvfs_tmp.core_clk = dvfs_new->core_clk;
	else
		dvfs_tmp.core_clk = dvfs_total->core_clk;

	rc = vdsp_dvfs_vote(drvdata, dvfs_tmp, &target_freq, &is_update);
	if (rc < 0) {
		pr_err("Core clock vote failed.\n");
		goto err_out;
	}
	drvdata->init_coreclk = target_freq;
	rc = pm_runtime_get_sync(drvdata->dev);
	if (rc < 0) {
		pr_err("vdsp hw open failed rc:%d.\n", rc);
		goto err_out;
	}

	rc = vdsp_bw_vote_update(drvdata, dvfs_tmp.bps);
	if (rc < 0) {
		pr_err("Bandwidth vote failed.\n");
		goto err_out;
	}

	dvfs_total->bps = dvfs_tmp.bps;
	dvfs_current->bps = dvfs_new->bps;

	if (target_freq != vdsp_get_coreclk_rate(drvdata)) {
		for (i = 0; i < drvdata->n_queues; ++i)
			mutex_lock(&drvdata->queue[i].lock);
		rc = vdsp_update_dvfs(drvdata, target_freq);
		if (rc < 0)
			pr_err("Failed to update core clock.\n");
		for (i = 0; i < drvdata->n_queues; ++i)
			mutex_unlock(&drvdata->queue[i].lock);
	} else {
		rc = 0;
	}

	if (!rc) {
		dvfs_total->core_clk = dvfs_tmp.core_clk;
		dvfs_total->mips = dvfs_tmp.mips;
		dvfs_current->core_clk = dvfs_new->core_clk;
		dvfs_current->mips = dvfs_new->mips;
	}

err_out:
	VDBG("after total(bps:%ld mips:%ld core_clk:%ld) curret(bps:%ld mips:%ld core_clk:%ld).\n",
		dvfs_total->bps, dvfs_total->mips, dvfs_total->core_clk,
		dvfs_current->bps, dvfs_current->mips, dvfs_current->core_clk);

	mutex_unlock(&drvdata->bw_mutex);

	VDBG("exit.\n");
	return rc;
}

static int vdsp_ioctl_restart(struct file *filp, struct vdsp_ioctl_params __user *p)
{
	int rc = -EINVAL;
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_params reset_info;
	int i;

	VDBG("enter.\n");
	if (!drvdata) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	mutex_lock(&drvdata->restart_mutex);
	if (copy_from_user(&reset_info, p, sizeof(*p))) {
		pr_err("copy params to kernel failed.\n");
		mutex_unlock(&drvdata->restart_mutex);
		return -EFAULT;
	}

	if ((atomic_read(&drvdata->is_crash) &&
			(reset_info.reset_type == VDSP_RESET_TYPE_PANIC)) ||
			(reset_info.reset_type == VDSP_RESET_TYPE_NORMAL)) {
		vdsp_ssr_set_status(drvdata->ssr_info, SUBSYS_RESTARTING);
		/*wakeup all wait thread*/
		for (i = 0; i < drvdata->n_queues; ++i)
			complete(&drvdata->queue[i].completion);

		for (i = 0; i < drvdata->n_queues; ++i)
			mutex_lock(&drvdata->queue[i].lock);

		vdsp_wdt_hw_deinit();

		vdsp_halt_dsp(drvdata);
		vdsp_reset_dsp(drvdata);
		msleep(20);
		vdsp_update_rtc(drvdata);
		vdsp_config_dsp(drvdata);
		rc = vdsp_boot_firmware(drvdata);
		if (rc < 0) {
			vdsp_disable_dsp(drvdata);
			drvdata->status = VDSP_POWER_OFF;
		}

		if (rc == 0) {
			vdsp_wdt_hw_init();
			vdsp_ssr_set_status(drvdata->ssr_info, SUBSYS_RUNNING);
			atomic_set(&drvdata->is_crash, 0);
			pr_info("restart successfully.\n");
		}

		for (i = 0; i < drvdata->n_queues; ++i)
			mutex_unlock(&drvdata->queue[i].lock);
	} else {
		rc = 0;
	}
	mutex_unlock(&drvdata->restart_mutex);

	VDBG("exit.\n");
	return rc;
}

static int vdsp_ioctl_updata_dvfs(struct file *filp, struct vdsp_ioctl_params __user *p)
{
	int rc = -EINVAL;
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_params *params = &vdsp_file->params;
	struct vdsp_ioctl_params params_new;
	struct vdsp_dvfs_params *dvfs_new, *dvfs_current;
	struct vdsp_dvfs_params *dvfs_total, dvfs_tmp;
	unsigned long target_freq = 0;
	int is_update = 0;
	int i;

	VDBG("enter.\n");
	if (!drvdata) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	if (copy_from_user(&params_new, p, sizeof(*p))) {
		pr_err("copy params to kernel failed.\n");
		return -EFAULT;
	}

	mutex_lock(&drvdata->bw_mutex);
	dvfs_current = &params->dvfs_params;
	dvfs_new = &params_new.dvfs_params;
	dvfs_total = &(drvdata->params.dvfs_params);

	dvfs_tmp.bps = dvfs_total->bps - dvfs_current->bps + dvfs_new->bps;
	dvfs_tmp.mips = dvfs_total->mips - dvfs_current->mips + dvfs_new->mips;
	if (dvfs_new->core_clk > dvfs_total->core_clk)
		dvfs_tmp.core_clk = dvfs_new->core_clk;
	else
		dvfs_tmp.core_clk = dvfs_total->core_clk;

	rc = vdsp_bw_vote_update(drvdata, dvfs_tmp.bps);
	if (rc < 0) {
		pr_err("Bandwidth vote failed.\n");
		goto out;
	}
	dvfs_total->bps = dvfs_tmp.bps;
	dvfs_current->bps = dvfs_new->bps;

	rc = vdsp_dvfs_vote(drvdata, dvfs_tmp, &target_freq, &is_update);
	if (rc < 0) {
		pr_err("Core clock vote failed.\n");
		goto out;
	}

	if (!is_update) {
		pr_err("The freq:%lu is not required update.\n", target_freq);
		goto update;
	}

	for (i = 0; i < drvdata->n_queues; ++i)
		mutex_lock(&drvdata->queue[i].lock);

	rc = vdsp_update_dvfs(drvdata, target_freq);

	for (i = 0; i < drvdata->n_queues; ++i)
		mutex_unlock(&drvdata->queue[i].lock);

update:
	if (((rc == 0) && is_update) || !is_update) {
		dvfs_total->core_clk = dvfs_tmp.core_clk;
		dvfs_total->mips = dvfs_tmp.mips;
		dvfs_current->core_clk = dvfs_new->core_clk;
		dvfs_current->mips = dvfs_new->mips;
	}

out:
	VDBG("total(bps:%ld mips:%ld core_clk:%ld) curret(bps:%ld mips:%ld core_clk:%ld).\n",
		dvfs_total->bps, dvfs_total->mips, dvfs_total->core_clk,
		dvfs_current->bps, dvfs_current->mips, dvfs_current->core_clk);

	mutex_unlock(&drvdata->bw_mutex);

	return rc;
}

static int vdsp_get_loading(struct vdsp_ioctl_params *parm)
{
	int rc = -EINVAL;
	struct vdsp_runtime_request_t runtime_rq;
	struct vdsp_runtime_response_t *response;
	struct vdsp_request rq;

	if (!parm) {
		pr_err("parm is NULL.\n");
		return -EINVAL;
	}

	memset(&runtime_rq, 0, sizeof(runtime_rq));
	runtime_rq.type = VDSP_RUNTIME_GET_CPULOADING;

	memset(&rq, 0, sizeof(rq));
	memcpy(rq.nsid, (void *)RUNTIME_NSID, VDSP_CMD_NAMESPACE_ID_SIZE);
	memcpy(rq.in_data, &runtime_rq, sizeof(runtime_rq));
	rq.ioctl_queue.in_data_size = sizeof(runtime_rq);
	rq.ioctl_queue.out_data_size = sizeof(struct vdsp_runtime_response_t);
	rq.ioctl_queue.flags = VDSP_QUEUE_FLAG_NSID;

	rc = vdsp_kernel_commit_sync(&rq);
	if (rc) {
		pr_err("submit command to vdsp failed.\n");
		return -EFAULT;
	}

	response = (struct vdsp_runtime_response_t *)rq.out_data;
	if (response->result) {
		pr_err("set parameters failed.\n");
		return -EFAULT;
	}

	parm->loading = response->cpu_usage;
	VDBG("get vdsp core loading:%d.\n", parm->loading);

	return rc;
}

static int vdsp_ioctl_get_loading(struct file *filp, struct vdsp_ioctl_params __user *p)
{
	struct vdsp_ioctl_params loading_info;
	int rc;

	rc = vdsp_get_loading(&loading_info);
	if (rc < 0) {
		pr_err("get loading failed.\n");
		return -EFAULT;
	}

	if (copy_to_user(p, &loading_info, sizeof(*p))) {
		pr_err("copy params to kernel failed.\n");
		return -EFAULT;
	}

	return rc;
}

static int vdsp_ioctl_dqevent(struct file *filp, struct vdsp_notify_info __user *p)
{
	int rc = -EINVAL;
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp_notify_info e_info;

	VDBG("enter.\n");
	if (!vdsp_file) {
		pr_err("Invalid argument.\n");
		return -EINVAL;
	}

	rc = vdsp_event_dequeue(vdsp_file->e_ctrl,
		&e_info, true);
	if (rc < 0) {
		pr_err("event dequeue failed.\n");
		return rc;
	}

	rc = copy_to_user(p, &e_info, sizeof(e_info));
	if (rc != 0) {
		pr_err("copy to user failed.\n");
		return -EFAULT;
	}
	VDBG("exit.\n");

	return rc;
}

static long vdsp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long rc = -EINVAL;

	VDBG("enter cmd:0x%x\n", cmd);
	switch (cmd) {
	case VDSP_IOCTL_ALLOC:
		rc = vdsp_ion_ioctl_alloc_mmap(filp,
					(struct vdsp_ioctl_alloc __user *)arg);
		break;
	case VDSP_IOCTL_FREE:
		rc = vdsp_ion_ioctl_free(filp,
					(struct vdsp_ioctl_alloc __user *)arg);
		break;
	case VDSP_IOCTL_MMAP:
		rc = vdsp_ion_ioctl_mmap(filp,
					(struct vdsp_ioctl_alloc __user *)arg);
		break;
	case VDSP_IOCTL_MUNMAP:
		rc = vdsp_ion_ioctl_munmap(filp,
					(struct vdsp_ioctl_alloc __user *)arg);
		break;
	case VDSP_IOCTL_QUEUE:
	case VDSP_IOCTL_QUEUE_NS:
		rc = vdsp_ioctl_submit_sync(filp,
					(struct vdsp_ioctl_queue __user *)arg);
		break;
	case VDSP_IOCTL_INIT:
		rc = vdsp_ioctl_init(filp,
					(struct vdsp_ioctl_params __user *)arg);
		break;
	case VDSP_IOCTL_RESET:
		rc = vdsp_ioctl_restart(filp,
					(struct vdsp_ioctl_params __user *)arg);
		break;
	case VDSP_IOCTL_DVFS:
		rc = vdsp_ioctl_updata_dvfs(filp,
					(struct vdsp_ioctl_params __user *)arg);
		break;
	case VDSP_IOCTL_GET_LOADING:
		rc = vdsp_ioctl_get_loading(filp,
					(struct vdsp_ioctl_params __user *)arg);
		break;
	case VDSP_IOCTL_DQEVENT:
		rc = vdsp_ioctl_dqevent(filp,
			(struct vdsp_notify_info __user *)arg);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	VDBG("exit rc:%d\n", rc);

	return rc;
}

static unsigned int vdsp_poll(struct file *filp,
	struct poll_table_struct *wait)
{
	struct vdsp_file *vdsp_file = filp->private_data;

	poll_wait(filp, &vdsp_file->waitq, wait);

	if (vdsp_event_pending(vdsp_file->e_ctrl) > 0)
		return POLLIN | POLLRDNORM;

	return 0;
}

static int vdsp_open(struct inode *inode, struct file *filp)
{
	struct vdsp *drvdata = NULL;
	struct vdsp_file *vdsp_file = NULL;
	int rc = 0;

	VDBG("enter.\n");
	drvdata = container_of(filp->private_data, struct vdsp, miscdev);
	vdsp_file = devm_kzalloc(drvdata->dev, sizeof(struct vdsp_file), GFP_KERNEL);
	if (!vdsp_file)
		return -ENOMEM;

	filp->private_data = vdsp_file;
	vdsp_file->drvdata = drvdata;
	vdsp_event_init(&vdsp_file->e_ctrl);
	init_waitqueue_head(&vdsp_file->waitq);

	spin_lock(&drvdata->flock);
	list_add(&vdsp_file->list, &drvdata->file_list);
	spin_unlock(&drvdata->flock);

	atomic_inc(&drvdata->opened);
	pr_info("%s:%d opened count:%d.\n", __func__, __LINE__, atomic_read(&drvdata->opened));
	VDBG("exit.\n");
	return rc;
}

static int vdsp_close(struct inode *inode, struct file *filp)
{
	int rc = 0;
	struct vdsp_file *vdsp_file = filp->private_data;
	struct vdsp *drvdata = vdsp_file->drvdata;
	struct vdsp_ioctl_params *params = &vdsp_file->params;
	struct vdsp_dvfs_params *dvfs_current;
	struct vdsp_dvfs_params *dvfs_total;
	unsigned long target_freq = 0;
	int is_update = 0;
	int i;

	VDBG("enter.\n");
	pr_info("%s:%d opened:%d.\n", __func__, __LINE__, atomic_read(&drvdata->opened));

	mutex_lock(&drvdata->bw_mutex);
	dvfs_current = &params->dvfs_params;
	dvfs_total = &(drvdata->params.dvfs_params);

	dvfs_total->bps -= dvfs_current->bps;
	dvfs_total->mips -= dvfs_current->mips;

	/* update bandwidth */
	vdsp_bw_vote_update(drvdata, dvfs_total->bps);

	/* update core clock and voltage */
	if (!atomic_dec_and_test(&drvdata->opened)) {
		rc = vdsp_dvfs_vote(drvdata, drvdata->params.dvfs_params,
				&target_freq, &is_update);
		if (rc < 0) {
			pr_err("Core clock vote failed.\n");
			goto err_unlock;
		}

		if (!is_update) {
			VDBG("The freq:%lu is not required update.\n", target_freq);
			goto err_unlock;
		}

		for (i = 0; i < drvdata->n_queues; ++i)
			mutex_lock(&drvdata->queue[i].lock);

		vdsp_update_dvfs(drvdata, target_freq);

		for (i = 0; i < drvdata->n_queues; ++i)
			mutex_unlock(&drvdata->queue[i].lock);
	}
err_unlock:
	VDBG("total(bps:%ld mips:%ld core_clk:%ld) current(bps:%ld mips:%ld core_clk:%ld).\n",
		dvfs_total->bps, dvfs_total->mips, dvfs_total->core_clk,
		dvfs_current->bps, dvfs_current->mips, dvfs_current->core_clk);

	mutex_unlock(&drvdata->bw_mutex);

	pm_runtime_put_sync(drvdata->dev);

	if (atomic_dec_and_test(&drvdata->log_inited)) {
		vdsp_log_stop();
		VDBG("inited:%d.\n", atomic_read(&drvdata->log_inited));
		vdsp_clean_dma_buf_list(drvdata);
	}

	vdsp_event_uninit(vdsp_file->e_ctrl);
	spin_lock(&drvdata->flock);
	list_del_init(&vdsp_file->list);
	spin_unlock(&drvdata->flock);

	VDBG("exit.\n");
	return rc;
}

static const struct file_operations vdsp_fops = {
	.owner  = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = vdsp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vdsp_ioctl,
#endif
	.poll = vdsp_poll,
	.open = vdsp_open,
	.release = vdsp_close,
};

void vdsp_panic_notify(void)
{
	struct vdsp_file *vdsp_file;
	struct vdsp *drvdata = g_drvdata;
	struct vdsp_notify_info e_info;

	pr_info("vdsp panic ========== .\n");
	/*deinit watchdog to avoid watchdog bite */
	vdsp_wdt_hw_deinit();

	if (!drvdata) {
		pr_err("drvdata is NULL.\n");
		return;
	}

	atomic_set(&drvdata->is_crash, 1);
	e_info.type = VDSP_EVENT_PANIC;
	spin_lock(&drvdata->flock);
	if (list_empty_careful(&drvdata->file_list))
		goto out;

	list_for_each_entry(vdsp_file, &drvdata->file_list, list) {
		vdsp_event_queue(vdsp_file->e_ctrl, &e_info);
		wake_up_interruptible(&vdsp_file->waitq);
	}

out:
	spin_unlock(&drvdata->flock);
}
EXPORT_SYMBOL_GPL(vdsp_panic_notify);

int vdsp_kernel_commit_sync(struct vdsp_request *rq)
{
	struct vdsp *drvdata = g_drvdata;
	struct vdsp_comm *queue = NULL;
	long rc;

	if (!drvdata || !rq) {
		pr_err("drvdata:%p or rq:%p are NULL.\n", drvdata, rq);
		return -EINVAL;
	}

	queue = drvdata->queue;
	if (!queue) {
		pr_err("drvdata queue is NULL.\n");
		return -EINVAL;
	}

	mutex_lock(&queue->lock);

	if (drvdata->ssr_info->subsys_dev->track.state != SUBSYS_RUNNING) {
		pr_err("vdsp is power off or crashed.\n");
		rc = -ENODEV;
		goto out;
	}

	vdsp_fill_hw_request(queue->comm, rq);

	vdsp_send_device_irq(drvdata);
	if (rq->request_type == VDSP_RUNTIME_SET_CLOSE) {
		rc = 0;
		goto out;
	}

	rc = vdsp_complete_cmd_irq(drvdata, queue,
			vdsp_cmd_complete);
	/* copy back inline data */
	if (rc == 0) {
		rc = vdsp_complete_hw_request(queue->comm, rq);
	} else {
		pr_err("vdsp command timeout.\n");
		rc = -ENODEV;
		goto out;
	}

out:
	mutex_unlock(&queue->lock);
	return rc;
}
EXPORT_SYMBOL(vdsp_kernel_commit_sync);

static int vdsp_set_statistic(int enable)
{
	int rc = -EINVAL;
	struct vdsp_runtime_request_t runtime_rq;
	struct vdsp_runtime_response_t *response;
	struct vdsp_request rq;

	VDBG("enter.\n");
	memset(&runtime_rq, 0, sizeof(runtime_rq));
	if (enable > 0)
		runtime_rq.stats_payload.set_stats_enable = 1;
	else
		runtime_rq.stats_payload.set_stats_enable = 0;
	runtime_rq.type = VDSP_RUNTIME_SET_STATS;
	runtime_rq.stats_payload.methods = STATS_METHOD_DETAILS;
	runtime_rq.stats_payload.mode = STATS_MODE_LOOP;
	runtime_rq.stats_payload.interval = 1000; //1s

	memset(&rq, 0, sizeof(rq));
	memcpy(rq.nsid, (void *)RUNTIME_NSID, VDSP_CMD_NAMESPACE_ID_SIZE);
	memcpy(rq.in_data, &runtime_rq, sizeof(runtime_rq));
	rq.ioctl_queue.in_data_size = sizeof(runtime_rq);
	rq.ioctl_queue.out_data_size = sizeof(struct vdsp_runtime_response_t);
	rq.ioctl_queue.flags = VDSP_QUEUE_FLAG_NSID;

	rc = vdsp_kernel_commit_sync(&rq);
	if (rc) {
		pr_err("submit command to vdsp failed.\n");
		return -EFAULT;
	}

	response = (struct vdsp_runtime_response_t *)rq.out_data;
	if (response->result) {
		pr_err("set stat parameters failed.\n");
		return -EFAULT;
	}

	VDBG("exit.\n");
	return rc;
}

static ssize_t vdsp_loading_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct vdsp_ioctl_params loading;

	if (vdsp_get_loading(&loading) < 0) {
		sprintf(buf, "get loading failed\n");
		pr_err("get loading failed.\n");
		return -EFAULT;
	}

	return sprintf(buf, "vdsp core loading: %d%% .\n", loading.loading);
}

static DEVICE_ATTR_RO(vdsp_loading);

static ssize_t vdsp_state_store(struct device *dev,
		struct device_attribute *attr,
		const char *buffer, size_t size)
{

	int rc = -EINVAL;
	int val;

	VDBG("enter.\n");
	rc = kstrtouint(buffer, 10, &val);
	if (rc)
		return -EINVAL;

	rc = vdsp_set_statistic(val);
	if (rc < 0) {
		pr_err("set statistics failed.\n");
		return -EFAULT;
	}

	VDBG("exit.\n");
	return size;
}

static DEVICE_ATTR_WO(vdsp_state);

static struct attribute *vdsp_dev_attrs[] = {
	&dev_attr_vdsp_state.attr,
	&dev_attr_vdsp_loading.attr,
	NULL
};

static const struct attribute_group vdsp_dev_attr_group = {
	.attrs = vdsp_dev_attrs
};

static const struct attribute_group *vdsp_dev_attr_groups[] = {
	&vdsp_dev_attr_group,
	NULL
};

static int log_event_notify(struct notifier_block *self,
			unsigned long action, void *data)
{
	struct vdsp_request *rq = (struct vdsp_request *)data;
	int rc = -EINVAL;

	if (action == LOG_EVENT_SETTING) {
		rc = vdsp_kernel_commit_sync(rq);
		if (rc) {
			pr_err("submit command to vdsp failed.\n");
			return NOTIFY_BAD;
		}
	}
	return NOTIFY_OK;
}

struct notifier_block log_event_notifier = {
	.notifier_call = log_event_notify,
};

static int vdsp_set_close(void)
{
	int rc = -EINVAL;
	struct vdsp_runtime_request_t runtime_rq;
	struct vdsp_request rq;

	VDBG("enter.\n");
	memset(&runtime_rq, 0, sizeof(runtime_rq));

	runtime_rq.type = VDSP_RUNTIME_SET_CLOSE;

	memset(&rq, 0, sizeof(rq));
	memcpy(rq.nsid, (void *)RUNTIME_NSID, VDSP_CMD_NAMESPACE_ID_SIZE);
	memcpy(rq.in_data, &runtime_rq, sizeof(runtime_rq));
	rq.ioctl_queue.in_data_size = sizeof(runtime_rq);
	rq.ioctl_queue.out_data_size = sizeof(struct vdsp_runtime_response_t);
	rq.ioctl_queue.flags = VDSP_QUEUE_FLAG_NSID;
	rq.request_type = VDSP_RUNTIME_SET_CLOSE;

	rc = vdsp_kernel_commit_sync(&rq);
	if (rc) {
		pr_err("submit command to vdsp failed.\n");
		return -EFAULT;
	}

	VDBG("exit.\n");
	return rc;
}

int vdsp_runtime_suspend(struct device *dev)
{
	struct vdsp *drvdata = dev_get_drvdata(dev);
	int rc = 0;

	VDBG("enter.\n");

	if (drvdata->status == VDSP_POWER_OFF)
		return rc;

	rc = vdsp_wdt_hw_deinit();
	if (rc < 0)
		pr_err("%s deinit vdsp wdt failed.\n", __func__);

	vdsp_set_close();

	if (vdsp_is_active(drvdata) != 0)
		pr_err("vdsp status is active or error.\n");

	vdsp_halt_dsp(drvdata);
	vdsp_reset_dsp(drvdata);
	vdsp_disable_dsp(drvdata);
	vdsp_log_read_last();
	drvdata->status = VDSP_POWER_OFF;
	memset(&drvdata->params, 0x0, sizeof(drvdata->params));
	vdsp_ssr_set_status(drvdata->ssr_info, SUBSYS_CLOSED);

	VDBG("exit.\n");

	return rc;
}
EXPORT_SYMBOL_GPL(vdsp_runtime_suspend);

void send_irq_to_vdsp(struct device *dev, int irq_id)
{
	struct vdsp *drvdata = dev_get_drvdata(dev);

	if (drvdata->hw_ops->send_irq)
		drvdata->hw_ops->send_irq(drvdata->hw_arg, irq_id);
}
EXPORT_SYMBOL_GPL(send_irq_to_vdsp);

int vdsp_runtime_resume(struct device *dev)
{
	struct vdsp *drvdata = dev_get_drvdata(dev);
	int rc = 0;
	int i;

	VDBG("enter.\n");
	for (i = 0; i < drvdata->n_queues; ++i)
		mutex_lock(&drvdata->queue[i].lock);

	vdsp_update_rtc(drvdata);
	rc = vdsp_enable_dsp(drvdata);
	if (rc < 0) {
		pr_err("couldn't enable VDSP\n");
		goto out;
	}

	vdsp_dvfs_set(drvdata, drvdata->init_coreclk);
	vdsp_config_dsp(drvdata);
	rc = vdsp_boot_firmware(drvdata);
	if (rc < 0) {
		vdsp_disable_dsp(drvdata);
		pr_err("vdsp boot firmware failed.\n");
		goto out;
	}

	drvdata->status = VDSP_POWER_ON;
	vdsp_ssr_set_status(drvdata->ssr_info, SUBSYS_RUNNING);
	atomic_set(&drvdata->is_crash, 0);

	/* init wdt3 */
	rc = vdsp_wdt_hw_init();
	if (rc < 0) {
		pr_err("vdsp wdt init failed.\n");
		goto out;
	}

out:
	for (i = 0; i < drvdata->n_queues; ++i)
		mutex_unlock(&drvdata->queue[i].lock);

	VDBG("rc:%d.\n", rc);
	return rc;
}
EXPORT_SYMBOL_GPL(vdsp_runtime_resume);

static int compare_queue_priority(const void *a, const void *b)
{
	const void * const *ppa = a;
	const void * const *ppb = b;
	const struct vdsp_comm *pa = *ppa, *pb = *ppb;

	if (pa->priority == pb->priority)
		return 0;
	else
		return pa->priority < pb->priority ? -1 : 1;
}

int vdsp_get_status(void)
{
	if (g_drvdata)
		return g_drvdata->status;
	else
		return VDSP_POWER_OFF;
}
EXPORT_SYMBOL(vdsp_get_status);

int vdsp_init(struct platform_device *pdev,
		const struct vdsp_hw_ops *hw_ops,
		void *hw_arg,
		struct vdsp_extern_module *module,
		struct vdsp_mem_region *mem_region)
{
	int rc;
	struct vdsp *drvdata;
	struct device *dev = &pdev->dev;
	int i;
	struct vdsp_mem_t *comm_info;

	if (!module || !mem_region) {
		pr_err("Invalid parameters.\n");
		return -EINVAL;
	}

	drvdata = devm_kzalloc(dev, sizeof(struct vdsp), GFP_KERNEL);
	if (!drvdata) {
		//pr_err("allocate drvdata failed.\n");
		return -ENOMEM;
	}

	drvdata->dev = dev;
	drvdata->hw_ops = hw_ops;
	drvdata->hw_arg = hw_arg;
	platform_set_drvdata(pdev, drvdata);

	comm_info = &(mem_region->vdsp_mem_info[VDSP_SMD_SYS_MEM]);
	drvdata->comm = comm_info->baseaddr;
	drvdata->comm_phys = comm_info->phys_addr;
	rc = device_property_read_u32_array(drvdata->dev, "queue-priority",
					NULL, 0);
	if (rc > 0) {
		drvdata->n_queues = rc;
		drvdata->queue_priority = devm_kzalloc(drvdata->dev,
					rc * sizeof(u32), GFP_KERNEL);
		if (drvdata->queue_priority == NULL) {
			//pr_err("allocate queue_priority failed.\n");
			return -ENOMEM;
		}
		rc = device_property_read_u32_array(drvdata->dev,
						"queue-priority",
						drvdata->queue_priority,
						drvdata->n_queues);
		if (rc < 0) {
			pr_err("parse queue_priority failed.\n");
			return -EINVAL;
		}
		VDBG("multiqueue (%d) configuration, queue priorities:\n", drvdata->n_queues);
		for (i = 0; i < drvdata->n_queues; ++i)
			VDBG("queue_priority[%d]:%d\n", i, drvdata->queue_priority[i]);
	} else {
		drvdata->n_queues = 1;
	}
	drvdata->queue = devm_kzalloc(drvdata->dev,
				drvdata->n_queues * sizeof(*drvdata->queue),
				GFP_KERNEL);
	drvdata->queue_ordered = devm_kzalloc(drvdata->dev,
				drvdata->n_queues * sizeof(*drvdata->queue_ordered),
				GFP_KERNEL);
	if (drvdata->queue == NULL ||
		drvdata->queue_ordered == NULL) {
		pr_err("allocate queue failed.\n");
		return -ENOMEM;
	}

	for (i = 0; i < drvdata->n_queues; ++i) {
		mutex_init(&drvdata->queue[i].lock);
		drvdata->queue[i].comm = drvdata->comm + VDSP_CMD_STRIDE * i;
		init_completion(&drvdata->queue[i].completion);
		if (drvdata->queue_priority)
			drvdata->queue[i].priority = drvdata->queue_priority[i];
		drvdata->queue_ordered[i] = drvdata->queue + i;
	}
	sort(drvdata->queue_ordered, drvdata->n_queues, sizeof(*drvdata->queue_ordered),
		compare_queue_priority, NULL);
	if (drvdata->n_queues > 1) {
		VDBG("SW -> HW queue priority mapping:\n");
		for (i = 0; i < drvdata->n_queues; ++i)
			VDBG("i:%d -> priority:%d\n", i, drvdata->queue_ordered[i]->priority);
	}

	if (module->smd_irq_id) {
		rc = devm_request_irq(dev, module->smd_irq_id, vdsp_irq_handler,
				0, "vdsp_smd", drvdata);
		if (rc < 0) {
			pr_err("request smd irq:%d failed\n", module->smd_irq_id);
			return rc;
		}
	}

	if (module->rtc_irq_id) {
		rc = devm_request_irq(dev, module->rtc_irq_id, vdsp_rtc_handler,
				0, "vdsp_rtc", drvdata);
		if (rc < 0) {
			pr_err("request rtc irq:%d failed\n", module->rtc_irq_id);
			return rc;
		}
	}

	if (module->is_support_smmu) {
		rc = vdsp_smmu_mem_region_map_init(dev, mem_region);
		if (rc < 0) {
			pr_err("smmu map memory failed.\n");
			return rc;
		}
	}

	rc = vdsp_loader_init(pdev, &(mem_region->vdsp_mem_info[VDSP_CODE_BACKUP_MEM]));
	if (rc) {
		pr_err("init image loader failed.\n");
		goto err_smmu_unmap;
	}

	blocking_notifier_chain_register(&vdsp_log_notify_list, &log_event_notifier);

	vdsp_log_init(&(mem_region->vdsp_mem_info[VDSP_LOG_SYS_MEM]),
					module->log_params);

	raw_notifier_chain_register(&ssr_notifier_list, &ssr_event_notifier);

	rc = vdsp_ssr_init(pdev, &drvdata->ssr_info, module,
			&(mem_region->vdsp_mem_info[VDSP_SYSDUMP_MEM]));
	if (rc) {
		pr_err("init ssr failed.\n");
		goto err_put_loader;
	}

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		rc = vdsp_runtime_resume(dev);
		if (rc)
			goto err_pm_disable;
	}

	drvdata->miscdev = (struct miscdevice){
		.minor = MISC_DYNAMIC_MINOR,
		.name = devm_kstrdup(dev, VDSP_CDEV_NAME, GFP_KERNEL),
		.nodename = devm_kstrdup(dev, VDSP_CDEV_NAME, GFP_KERNEL),
		.fops = &vdsp_fops,
		.groups = vdsp_dev_attr_groups,
	};

	rc = misc_register(&drvdata->miscdev);
	if (rc < 0) {
		pr_err("register char device failed.\n");
		goto err_pm_disable;
	}

	mutex_init(&drvdata->bw_mutex);
	mutex_init(&drvdata->buf_mutex);
	mutex_init(&drvdata->restart_mutex);
	INIT_LIST_HEAD(&drvdata->smmu_buf_list);
	INIT_LIST_HEAD(&drvdata->file_list);
	spin_lock_init(&drvdata->flock);
	atomic_set(&drvdata->opened, 0);
	atomic_set(&drvdata->log_inited, 0);
	atomic_set(&drvdata->is_crash, 0);

	memcpy(&drvdata->mem_region, mem_region, sizeof(struct vdsp_mem_region));
	memcpy(&drvdata->module, module, sizeof(struct vdsp_mem_region));

	((struct vdsp_hw *)hw_arg)->vdsp = drvdata;
	g_drvdata = drvdata;
	pr_info("vdsp init successfully.\n");
	return 0;

err_pm_disable:
	pm_runtime_disable(dev);
	vdsp_ssr_uninit(drvdata->ssr_info);
err_put_loader:
	vdsp_loader_uninit();
err_smmu_unmap:
	if (module->is_support_smmu)
		vdsp_smmu_mem_region_map_deinit(dev, mem_region);

	return rc;
}
EXPORT_SYMBOL_GPL(vdsp_init);

int vdsp_deinit(struct platform_device *pdev)
{
	struct vdsp *drvdata = platform_get_drvdata(pdev);

	pr_info("in\n");
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(drvdata->dev))
		vdsp_runtime_suspend(drvdata->dev);

	if (drvdata->module.is_support_smmu)
		vdsp_smmu_mem_region_map_deinit(drvdata->dev, &drvdata->mem_region);

	misc_deregister(&drvdata->miscdev);
	blocking_notifier_chain_unregister(&vdsp_log_notify_list, &log_event_notifier);
	vdsp_loader_uninit();
	raw_notifier_chain_unregister(&ssr_notifier_list, &ssr_event_notifier);
	vdsp_ssr_uninit(drvdata->ssr_info);

	mutex_destroy(&drvdata->restart_mutex);
	mutex_destroy(&drvdata->bw_mutex);
	mutex_destroy(&drvdata->buf_mutex);

	devm_kfree(&pdev->dev, drvdata->queue);
	devm_kfree(&pdev->dev, drvdata->queue_ordered);
	devm_kfree(&pdev->dev, drvdata->queue_priority);
	devm_kfree(&pdev->dev, drvdata);
	g_drvdata = NULL;
	pr_info("out\n");

	return 0;
}
EXPORT_SYMBOL_GPL(vdsp_deinit);

MODULE_LICENSE("GPL");
