// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/platform/jlq/bridge/bridge.c
 *
 * Copyright (c)2019-2021   JLQ Technology Co.,Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <soc/jlq/jr510/jlq-bridge.h>
#include <dt-bindings/jlq/jr510/irqs.h>

#define	AP_INTR_SRC_BITS		32
#define TOP_MAILBOX_CM4F_SINTR_SET          (0x48)
#define TOP_MAILBOX_CM4F_NINTR_SET          (0x60)
#define TOP_MAILBOX_ADSP_NINTR_SET          (0x340)

static LIST_HEAD(bridge_ch_list);
static DEFINE_MUTEX(bridge_ch_creation_mutex);
unsigned int bridge_ch_num;
static void __iomem *bridge_irq_trig_cm4_sec_addr;
static void __iomem *bridge_irq_trig_cm4_nosec_addr;
static void __iomem *bridge_irq_trig_adsp_addr;

static struct list_head rcvbuf_list;
/*-------------------------------------------------------------------------*/

/*
 * buf_init
 *
 * Allocate a circular buffer and all associated memory.
 */
static int buf_init(struct bridge_buf *bf, char *buf_base, unsigned int size,
		    u32 put_offset, u32 get_offset, u32 unit)
{

	if (buf_base == NULL || bf == NULL) {
		BRIDGE_ERR("buf_base:%p, bf:%p\n", buf_base, bf);
		return -EINVAL;
	}

	bf->buf_buf = buf_base;
	bf->buf_size = size;
	bf->buf_put = bf->buf_buf + put_offset * unit;
	bf->buf_get = bf->buf_buf + get_offset * unit;

	return 0;
}
static int info_init(union Bridge_Pro pro, struct bridge_info *info, u32 unit)
{
	struct buf_lbridge_head *phead =
				(struct buf_lbridge_head *)(info->membase);

	info->pdata = (char *)(info->membase)
				+ sizeof(struct buf_lbridge_head);
	info->length = info->memlength
				- sizeof(struct buf_lbridge_head);

	return buf_init(&info->circular_buf, info->pdata, info->length,
				phead->SndDataOffset, phead->RcvDataOffset,
				unit);
}
/*
 * buf_data_avail
 *
 * Return the number of bytes of data written into the circular
 * buffer.
 */
static unsigned int buf_data_avail(struct bridge_buf *bf, u32 unit)
{
	return ((bf->buf_size + bf->buf_put - bf->buf_get) % bf->buf_size)
		/ unit;
}

/*
 * buf_space_avail
 *
 * Return the number of bytes of space available in the circular
 * buffer.
 */
static unsigned int buf_space_avail(struct bridge_buf *bf, u32 unit)
{
	return ((bf->buf_size + bf->buf_get - bf->buf_put - unit)
		% bf->buf_size) / unit;
}

/*
 * user_buf_put_data
 *
 * Copy data from a user buffer and put it into the circular buffer.
 * Restrict to the amount of space available.
 *
 * Return the number of bytes copied.
 */
static ssize_t
user_buf_put_data(struct bridge_channel *drvdata, struct bridge_buf *bf,
				const char __user *buf, unsigned int count)
{
	unsigned int len = 0;
#ifdef CONFIG_BRIDGE_DEBUG_LOOP_TEST
	struct bridge_rxdata *data = NULL, *n = NULL;
#endif

	if (bf->buf_put >= bf->buf_buf + bf->buf_size)
		panic("%s:line=%d\n", __func__, __LINE__);

	len  = buf_space_avail(bf, drvdata->unit);
	if (count > len) {
		if (drvdata->dbg_flag == true)
			BRIDGE_INFO("%s no space want %d avail %d!\n",
					drvdata->name, count, len);
		return -ENOMEM;
	}

	len = bf->buf_buf + bf->buf_size - bf->buf_put;
	if (count > len) {
		memcpy_toio(bf->buf_put, buf, len);
		memcpy_toio(bf->buf_buf, buf+len, count - len);
		bf->buf_put = bf->buf_buf + count - len;
	} else {
		memcpy_toio(bf->buf_put, buf, count);
		if (count < len)
			bf->buf_put += count;
		else /* count == len */
			bf->buf_put = bf->buf_buf;
	}

	if (bf->buf_put >= bf->buf_buf + bf->buf_size)
		panic("%s:line=%d\n", __func__, __LINE__);

	return count;
}

/*
 * user_buf_get_data
 *
 * Get data from the circular buffer and copy to the given user  buffer.
 * Restrict to the amount of data available.
 *
 * Return the number of bytes copied.
 */
static ssize_t
user_buf_get_data(struct bridge_channel *drvdata, struct bridge_buf *bf,
		  char __user *buf, unsigned int count)
{
	unsigned int len = 0;

	len = buf_data_avail(bf, drvdata->unit);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = bf->buf_buf + bf->buf_size - bf->buf_get;
	if (count > len) {
		memcpy_fromio(buf, bf->buf_get, len);
		memcpy_fromio(buf+len, bf->buf_buf, count - len);
		bf->buf_get = bf->buf_buf + count - len;
	} else {
		memcpy_fromio(buf, bf->buf_get, count);
		if (count < len)
			bf->buf_get += count;
		else /* count == len */
			bf->buf_get = bf->buf_buf;
	}

	return count;
}

u64 get_clock(void)
{
	return local_clock();
}

static inline u32 get_rcv_offset(union Bridge_Pro pro,
				 struct buf_lbridge_head *plhead,
				 struct buf_hbridge_head *phhead)
{
	return plhead->RcvDataOffset;
}
static inline u32 get_snd_offset(union Bridge_Pro *pro,
				 struct buf_lbridge_head *plhead,
				 struct buf_hbridge_head *phhead)
{
	return plhead->SndDataOffset;
}
static inline u32 get_snd_irq_count(union Bridge_Pro *pro,
				    struct buf_lbridge_head *plhead,
				    struct buf_hbridge_head *phhead)
{
	return plhead->SndIrqCount;
}
static inline void rcv_irq_count_add(struct bridge_channel *drvdata,
				     struct buf_lbridge_head *plhead,
				     struct buf_hbridge_head *phhead)
{
	plhead->RcvIrqCount++;
	drvdata->rx_int_dinfo.count = plhead->SndDataOffset;
	drvdata->rx_int_dinfo.t = get_clock();
}

static inline u32 get_snd_count(union Bridge_Pro *pro,
				struct buf_lbridge_head *plhead,
				struct buf_hbridge_head *phhead)
{
	return plhead->SndIrqCount;
}

static void bridge_irq_en(unsigned int irq)
{
	enable_irq(irq);
}

static void bridge_irq_dis(unsigned int irq)
{
	disable_irq_nosync(irq);
}

static irqreturn_t bridge_rx_handler(int irq, void *dev_id)
{
	struct bridge_channel *drvdata = dev_id;
	struct buf_lbridge_head *plhead =
			(struct buf_lbridge_head *)drvdata->rxinfo.membase;
	struct buf_hbridge_head *phhead =
			(struct buf_hbridge_head *)drvdata->rxinfo.membase;

	drvdata->rxinfo.circular_buf.buf_put =
			get_snd_offset(&drvdata->property, plhead, phhead)
			* drvdata->unit + drvdata->rxinfo.pdata;
	rcv_irq_count_add(drvdata, plhead, phhead);
	if (drvdata->rxhandle)
		drvdata->rxhandle(drvdata);
	else
		wake_up_interruptible(&drvdata->rxwait_queue);

	return IRQ_HANDLED;
}

static int bridge_open(struct bridge_channel *drvdata)
{
	int status = 0;

	if (drvdata->is_open) {
		BRIDGE_INFO("%s must be reopened.\n", drvdata->name);
		status = -EBUSY;
		goto out;
	}

	status = info_init(drvdata->property, &drvdata->txinfo, drvdata->unit);
	if (status) {
		BRIDGE_ERR("%s txinfo init err.\n", drvdata->name);
		goto out;
	}

	status = info_init(drvdata->property, &drvdata->rxinfo, drvdata->unit);
	if (status) {
		BRIDGE_ERR("%s rxinfo init err.\n", drvdata->name);
		goto out;
	}

	status = request_irq(drvdata->rxirq, bridge_rx_handler, 0,
			     drvdata->name, drvdata);
	if (status) {
		BRIDGE_ERR("request rxirq failed %d\n", status);
		status = -ENXIO;
	}
	irq_set_irq_wake(drvdata->rxirq, 1);

	drvdata->dbg_flag = true;
	drvdata->is_open = 1;
	if (drvdata->f_open == 0)
		drvdata->f_open = 1;
	BRIDGE_PRT("open %s ok.\n", drvdata->name);

out:
	return status;
}

static inline bool check_snd_len(u32 len, struct bridge_channel *drvdata)
{
	return (len > drvdata->txinfo.length);
}

static bool test_rcvbusy(struct bridge_channel *drvdata)
{
	return false;
}

static ssize_t bridge_send_rawdata(struct bridge_channel *drvdata,
				   const char __user *buf, size_t count)
{
	struct buf_lbridge_head *plhead =
			(struct buf_lbridge_head *)drvdata->txinfo.membase;
	ssize_t status = -EINVAL;
	u32 timeout = drvdata->tx_timeout;
	static unsigned int __iomem *membase;
	unsigned long flags;

	while (1) {
		if (plhead->RcvDataOffset >
			drvdata->txinfo.circular_buf.buf_size) {
			BRIDGE_INFO("%s %s invalid RcvDataOffset 0x%x!\n",
				drvdata->name, __func__, plhead->RcvDataOffset);
			return status;
		}

		spin_lock_irqsave(&drvdata->wr_lock, flags);
		drvdata->txinfo.circular_buf.buf_get = plhead->RcvDataOffset
							+ drvdata->txinfo.pdata;
		status = user_buf_put_data(drvdata,
					   &drvdata->txinfo.circular_buf,
					   buf, count);
		if (unlikely(status < 0)) {
			spin_unlock_irqrestore(&drvdata->wr_lock, flags);
			if (drvdata->dbg_flag == true) {
				BRIDGE_INFO(
					"%s buf put ret:%x rcvoffset:%x sndoffset:%x\n",
					drvdata->name, (int)status,
					plhead->RcvDataOffset,
					plhead->SndDataOffset);
				drvdata->dbg_flag = false;
			}
			if (timeout / BRIDGE_SND_WAIT_TIME) {
				writel(drvdata->txirq, drvdata->irq_trig);
			/*trigger irq to ZSP0 and it will trigger tx ack irq*/
				plhead->SndIrqCount++;
				msleep(BRIDGE_SND_WAIT_TIME);
				timeout = timeout - BRIDGE_SND_WAIT_TIME;
				continue;
			} else {
				goto out;
			}
		} else {
			plhead->SndDataOffset =
					drvdata->txinfo.circular_buf.buf_put
					- drvdata->txinfo.pdata;
			spin_unlock_irqrestore(&drvdata->wr_lock, flags);
			break;
		}
	}

	/* trigger irq to CP and it will trigger tx ack irq */
	writel(drvdata->txirq, drvdata->irq_trig);

	plhead->SndIrqCount++;

	if (plhead->SndIrqCount == 1) {
		mdelay(1);
		do {
			if (unlikely(plhead->SndDataOffset
			    != plhead->RcvDataOffset)) {
				BRIDGE_INFO("%s wait Rcv get data!\n",
					    drvdata->name);
				if (drvdata->dbg_info.len) {
					membase =
					     ioremap(drvdata->dbg_info.addr, 8);
					if (!membase) {
						BRIDGE_ERR("%s:", __func__);
						BRIDGE_ERR("ioremap failed\n");
						return -EIO;
					}
					BRIDGE_INFO("%s dbg addr 0x%x,",
						    drvdata->name,
						    drvdata->dbg_info.addr);
					BRIDGE_INFO("value 0x%x\n",
						    readl(membase));
					iounmap(membase);
				} else
					BRIDGE_INFO("no dbg info!\n");
				if (timeout / BRIDGE_SND_WAITRDY_TIME) {
					msleep(BRIDGE_SND_WAITRDY_TIME);
					timeout = timeout
						  - BRIDGE_SND_WAITRDY_TIME;
				}
			} else
				break;
		} while (timeout / BRIDGE_SND_WAITRDY_TIME);
	}
	plhead->SndDataCount += count;
	if (drvdata->dbg_flag == false) {
		BRIDGE_INFO("%s send change ok count:%x\n",
				drvdata->name, (u32)count);
		drvdata->dbg_flag = true;
	}
out:
	return status;
}

static ssize_t bridge_send(struct bridge_channel *drvdata,
			   const char __user *buf, size_t count,
			   bool isuser, bool is_last)
{
	ssize_t status = -EINVAL;

	if (check_para(drvdata) != 0)
		goto out;

	if (check_snd_len(count, drvdata)) {
		BRIDGE_INFO("%s:too many send data count:%d\n",
			    drvdata->name, (int)count);
		goto out;
	}

	if (count == 0) {
		BRIDGE_INFO("%s:count = 0\n", drvdata->name);
		status = count;
		goto out;
	}

	status = bridge_send_rawdata(drvdata, buf, count);

	drvdata->tx_dinfo.t = get_clock();
	drvdata->tx_dinfo.count = count;
out:
	return status;

}

static ssize_t bridge_rcv_rawdata(struct bridge_channel *drvdata,
				  char __user *buf, size_t count)
{
	ssize_t status = -EINVAL;
	struct buf_lbridge_head *plhead =
			(struct buf_lbridge_head *)drvdata->rxinfo.membase;

retry:
	status = user_buf_get_data(drvdata, &drvdata->rxinfo.circular_buf,
				   buf, count);
	if (status == 0) {
		if (drvdata->notify)
			return status;
		status = wait_event_interruptible_timeout(drvdata->rxwait_queue,
			  (buf_data_avail(&drvdata->rxinfo.circular_buf,
					  drvdata->unit) != 0),
			  1*HZ);
		if (status == -ERESTARTSYS)
			return status;
		disable_irq(drvdata->rxirq);
		drvdata->rxinfo.circular_buf.buf_put = plhead->SndDataOffset
						       + drvdata->rxinfo.pdata;
		enable_irq(drvdata->rxirq);
		goto retry;
	}
	plhead->RcvDataCount += status;
	plhead->RcvDataOffset = drvdata->rxinfo.circular_buf.buf_get
				- drvdata->rxinfo.pdata;

	return status;
}

static int bridge_close(struct bridge_channel *drvdata)
{
	irq_set_irq_wake(drvdata->rxirq, 0);
	free_irq(drvdata->rxirq, drvdata);
	drvdata->is_open = 0;
	BRIDGE_PRT("close %s .\n", drvdata->name);

	return 0;
}

static unsigned int bridge_poll(struct bridge_channel *drvdata)
{
	unsigned int mask = POLLHUP;

	if (buf_data_avail(&drvdata->rxinfo.circular_buf, drvdata->unit) != 0)
		mask |= (POLLIN | POLLRDNORM);

	return mask;
}

static ssize_t bridge_info_show(struct bridge_channel *drvdata, char *buf)
{
	struct buf_lbridge_head *rxphead =
		(struct buf_lbridge_head *)drvdata->rxinfo.membase;
	struct buf_lbridge_head *txphead =
		(struct buf_lbridge_head *)drvdata->txinfo.membase;
	u64 rxint_t = 0, rx_t = 0, tx_t = 0;
	unsigned long rxint_nt = 0, rx_nt = 0, tx_nt = 0;
	struct timespec ts;
	struct rtc_time tm;

	rxint_t = drvdata->rx_int_dinfo.t;
	rxint_nt = do_div(rxint_t, 1000000000);
	rx_t = drvdata->rx_dinfo.t;
	rx_nt = do_div(rx_t, 1000000000);
	tx_t = drvdata->tx_dinfo.t;
	tx_nt = do_div(tx_t, 1000000000);

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);

	return sprintf(buf, "========================================\n"
		"%s information:(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n"
		"rxirq=%d txirq=%d isopen=%d tx_buf_len=0x%x rx_buf_len=0x%x\n"
		"\n"
		"ap send to cp 0x%x byte(s) data.write data offset 0x%x\n"
		"ap trigger cp read irq %d times\n"
		"cp receive 0x%x byte(s) data.read data offset 0x%x\n"
		"cp ack irq %d times\n"
		"\n"
		"cp send to ap 0x%x byte(s) data.write data offset 0x%x\n"
		"cp trigger ap read irq %d times\n"
		"ap recive 0x%x byte(s) data.read data offset 0x%x\n"
		"ap ack irq %d times\n"
		"debug info:\n"
		"rx last int at [%5lu.%06lu], updata offset 0x%x\n"
		"rx last return at [%5lu.%06lu], count 0x%x\n"
		"tx last return at [%5lu.%06lu], count 0x%x\n"
		"===================================================\n",
		drvdata->name, tm.tm_year + 1900, tm.tm_mon + 1,
		tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec,
		drvdata->rxirq, drvdata->txirq, drvdata->is_open,
		drvdata->txinfo.memlength, drvdata->rxinfo.memlength,
		txphead->SndDataCount, txphead->SndDataOffset,
		txphead->SndIrqCount,
		txphead->RcvDataCount, txphead->RcvDataOffset,
		txphead->RcvIrqCount,
		rxphead->SndDataCount, rxphead->SndDataOffset,
		rxphead->SndIrqCount,
		rxphead->RcvDataCount, rxphead->RcvDataOffset,
		rxphead->RcvIrqCount,
		(unsigned long)rxint_t, rxint_nt/1000,
		drvdata->rx_int_dinfo.count,
		(unsigned long)rx_t, rx_nt/1000,
		drvdata->rx_dinfo.count,
		(unsigned long)tx_t, tx_nt/1000,
		drvdata->tx_dinfo.count
		);
}

static ssize_t bridge_reg_show(struct bridge_channel *drvdata, char *buf)
{
	return sprintf(buf, "================================================\n"
		"To Add the reg value print!\n"
		"===================================================\n"
		);
}

static int bridge_driver_init(struct bridge_channel *drvdata,
			      struct jlq_bridge_info *bridge_info)
{
	drvdata->is_open = 0;
	drvdata->baseaddr = bridge_info->baseaddr;
	drvdata->top_mbox_base = bridge_info->top_mbox_base;
	drvdata->irq_trig = bridge_info->irq_trig;
	drvdata->tx_pa_max = bridge_info->tx_pa_max;
	drvdata->type = bridge_info->type;
	drvdata->property = bridge_info->property;
	drvdata->rxirq = bridge_info->irq_base + bridge_info->rx_id;
	drvdata->txirq = bridge_info->tx_id;
	sprintf(drvdata->name, "%s", bridge_info->name);

	drvdata->txinfo.membase = ioremap_nocache((size_t)(bridge_info->tx_addr
						   + bridge_info->baseaddr),
						   bridge_info->tx_len);
	drvdata->txinfo.memlength = bridge_info->tx_len;

	drvdata->rxinfo.membase = ioremap_nocache((size_t)(bridge_info->rx_addr
						  + bridge_info->baseaddr),
						  bridge_info->rx_len);
	drvdata->rxinfo.memlength = bridge_info->rx_len;

	memset_io(drvdata->txinfo.membase, 0, drvdata->txinfo.memlength);
	memset_io(drvdata->rxinfo.membase, 0, drvdata->rxinfo.memlength);

	memcpy(&drvdata->dbg_info, &bridge_info->dbg_info, sizeof(struct info));
	drvdata->unit = NO_FRAME_UNIT;
	mutex_init(&drvdata->write_sem);
	mutex_init(&drvdata->read_sem);
	mutex_init(&drvdata->op_mutex);
	spin_lock_init(&drvdata->wr_lock);
	init_waitqueue_head(&drvdata->rxwait_queue);

	return 0;
}
static int bridge_driver_uninit(struct bridge_channel *drvdata)
{
	iounmap(drvdata->txinfo.membase);
	iounmap(drvdata->rxinfo.membase);
	kfree(drvdata->rx_save_buf);
	return 0;
}

static int bridge_save_rxbuf(struct bridge_channel *drvdata)
{
	if (drvdata->f_open && drvdata->rx_save_buf)
		memcpy(drvdata->rx_save_buf, drvdata->rxinfo.pdata,
		       drvdata->rxinfo.length);
	return 0;
}

static int bridge_restore_rxbuf(struct bridge_channel *drvdata)
{
	if (drvdata->f_open && drvdata->rx_save_buf)
		memcpy(drvdata->rxinfo.pdata, drvdata->rx_save_buf,
		       drvdata->rxinfo.length);
	return 0;
}

static const struct bridge_operations bridge_ops = {
	.drvinit = bridge_driver_init,
	.drvuninit = bridge_driver_uninit,
	.rxsavebuf = bridge_save_rxbuf,
	.rxstorebuf = bridge_restore_rxbuf,
	.open = bridge_open,
	.close = bridge_close,
	.poll = bridge_poll,
	.xmit = bridge_send,
	.recv_raw = bridge_rcv_rawdata,
	.recv_pkt = NULL,
	.irq_en = bridge_irq_en,
	.irq_dis = bridge_irq_dis,
	.set_fc = NULL,
	.clr_fc = NULL,
	.info_show = bridge_info_show,
	.reg_show = bridge_reg_show,
	.test_rcvbusy = test_rcvbusy,
};
const struct bridge_operations *get_bridge_ops(void)
{
	return &bridge_ops;
}

static int bridge_rcvbuf_alloc(struct bridge_channel *drvdata,
	struct bridge_rxdata *rxbuf, size_t size)
{
	if (list_empty(&rcvbuf_list)) {
		rxbuf->vData = dma_alloc_coherent(drvdata->misc_dev.parent,
				size, &rxbuf->pData, GFP_KERNEL);
		if (!rxbuf->vData) {
			BRIDGE_ERR("%s rcvbuf_alloc 0x%zx err.\n",
				drvdata->name, size);
			return -ENOMEM;
		}
	} else {
		struct bridge_rxdata *rxdata;

		rxdata = list_first_entry(&rcvbuf_list,
					struct bridge_rxdata, list);
		list_del(&rxdata->list);
		rxbuf->vData = rxdata->vData;
		rxbuf->pData = rxdata->pData;
		list_add_tail(&rxdata->list, &drvdata->rxdata_list);
	}
	rxbuf->frame.Start = (u32)rxbuf->pData;

	return 0;
}

static void bridge_rcvbuf_free(struct bridge_channel *drvdata,
		struct bridge_rxdata *rxbuf)
{
	if (rxbuf->vData)
		dma_free_coherent(drvdata->misc_dev.parent,
			drvdata->rxinfo.pkt_size, rxbuf->vData,
			rxbuf->pData);
}

static irqreturn_t bridge_rx_irq(struct bridge_channel *drvdata)
{
	if (drvdata->notify) {
		drvdata->notify(drvdata->priv, BG_EV_RX);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}
static irqreturn_t bridge_ctl_irq(struct bridge_channel *drvdata)
{
	return IRQ_NONE;
}

ssize_t bridge_write(struct bridge_channel *ch, const char *buf,
				size_t count)
{
	struct bridge_channel *drvdata = ch;
	ssize_t status = -EINVAL;

	if (drvdata && drvdata->ops && drvdata->ops->xmit) {
		mutex_lock(&drvdata->write_sem);
		status = drvdata->ops->xmit(drvdata, buf, count, true, true);
		mutex_unlock(&drvdata->write_sem);
	} else
		BRIDGE_ERR("write err: drvdata=%p, ops=%p, xmit=%p\n",
			drvdata, drvdata->ops, drvdata->ops->xmit);

	return status;
}
EXPORT_SYMBOL(bridge_write);

ssize_t bridge_write_from_irq(struct bridge_channel *ch, const char *buf,
				size_t count)
{
	struct bridge_channel *drvdata = ch;
	ssize_t status = -EINVAL;

	if (drvdata && drvdata->ops && drvdata->ops->xmit)
		status = drvdata->ops->xmit(drvdata, buf, count, true, true);
	else
		BRIDGE_ERR("write err: drvdata=%p, ops=%p, xmit=%p\n",
			drvdata, drvdata->ops, drvdata->ops->xmit);

	return status;
}
EXPORT_SYMBOL(bridge_write_from_irq);

ssize_t bridge_read(struct bridge_channel *ch, char *buf, size_t count)
{
	ssize_t status = -EINVAL;
	struct bridge_channel *drvdata = ch;

	mutex_lock(&drvdata->read_sem);

	if (check_para(drvdata) != 0)
		goto out;

	status = drvdata->ops->recv_raw(drvdata, buf, count);

	drvdata->rx_dinfo.t = get_clock();
	drvdata->rx_dinfo.count = status;
out:
	mutex_unlock(&drvdata->read_sem);
	return status;
}
EXPORT_SYMBOL(bridge_read);

ssize_t bridge_read_from_notify(struct bridge_channel *ch, char *buf, size_t count)
{
	ssize_t status = -EINVAL;
	struct bridge_channel *drvdata = ch;

	if (check_para(drvdata) != 0)
		goto out;

	status = drvdata->ops->recv_raw(drvdata, buf, count);

	drvdata->rx_dinfo.t = get_clock();
	drvdata->rx_dinfo.count = status;
out:
	return status;
}
EXPORT_SYMBOL(bridge_read_from_notify);

int bridge_name_open(const char *name, uint32_t edge,
			struct bridge_channel **_ch,
			void *priv, void (*notify)(void *, unsigned int))
{
	struct bridge_channel *drvdata = NULL;
	int status = -EINVAL;
	unsigned int ch_num = 0;

	mutex_lock(&bridge_ch_creation_mutex);
	list_for_each_entry(drvdata, &bridge_ch_list, list) {
		if (!strcmp(name, drvdata->name)) {
			if (drvdata->is_open) {
				mutex_unlock(&bridge_ch_creation_mutex);
				return status;
			}
			break;
		}
		ch_num++;
	}
	mutex_unlock(&bridge_ch_creation_mutex);

	if (ch_num == bridge_ch_num)
		return status;

	mutex_lock(&drvdata->op_mutex);
	if (drvdata && drvdata->ops && drvdata->ops->open)
		status = drvdata->ops->open(drvdata);
	else
		BRIDGE_ERR("open err: drvdata=%p, ops=%p, open=%p\n",
			drvdata, drvdata->ops, drvdata->ops->open);

	drvdata->notify = notify;
	drvdata->priv = priv;
	*_ch = drvdata;
	mutex_unlock(&drvdata->op_mutex);

	return status;
}
EXPORT_SYMBOL(bridge_name_open);

int bridge_release(struct bridge_channel *ch)
{
	struct bridge_channel *drvdata = ch;
	int status = -EINVAL;

	mutex_lock(&drvdata->op_mutex);
	if (drvdata && drvdata->ops && drvdata->ops->close)
		status = drvdata->ops->close(drvdata);
	else {
		BRIDGE_ERR("release err: drvdata=%p, ops=%p, close=%p\n",
			drvdata, drvdata->ops, drvdata->ops->close);
	}
	mutex_unlock(&drvdata->op_mutex);
	return status;
}
EXPORT_SYMBOL(bridge_release);

ssize_t bridge_flush_for_cp_pd(struct bridge_channel *ch)
{
	struct bridge_channel *drvdata = ch;
	ssize_t status = -EINVAL;

	pr_crit("bridge: %s\n", __func__);

	/* clear share buffer */
	memset_io(drvdata->txinfo.membase, 0, drvdata->txinfo.memlength);
	memset_io(drvdata->rxinfo.membase, 0, drvdata->rxinfo.memlength);

	/* init control buffer */
	status = info_init(drvdata->property, &drvdata->txinfo, drvdata->unit);
	status = info_init(drvdata->property, &drvdata->rxinfo, drvdata->unit);

	/* clear the txirq status */
	writel(0xffff, drvdata->irq_trig + 0xC);

	return status;
}

int bridge_name_flush_for_cp_pd(const char *name)
{
	struct bridge_channel *drvdata = NULL;
	int status = -EINVAL;

	mutex_lock(&bridge_ch_creation_mutex);
	list_for_each_entry(drvdata, &bridge_ch_list, list) {
		if (!strcmp(name, drvdata->name)) {
			if (drvdata->is_open) {
				bridge_flush_for_cp_pd(drvdata);
				mutex_unlock(&bridge_ch_creation_mutex);
				return 0;
			}
		}
	}
	mutex_unlock(&bridge_ch_creation_mutex);
	return status;
}
EXPORT_SYMBOL(bridge_name_flush_for_cp_pd);

static ssize_t info_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t info_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct bridge_channel *drvdata = NULL;
	int ret;

	drvdata = dev_get_drvdata(dev);
	if (drvdata && drvdata->ops && drvdata->ops->info_show)
		ret = drvdata->ops->info_show(drvdata, buf);
	else {
		BRIDGE_ERR("%s err: drvdata=%p, ops=%p, %s=%p\n", __func__,
			   drvdata, drvdata->ops, __func__,
			   drvdata->ops->info_show);
		return -EINVAL;
	}
	return ret;
}

static ssize_t reg_status_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t reg_status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct bridge_channel *drvdata = NULL;
	int ret = 0;

	drvdata = dev_get_drvdata(dev);
	if (drvdata && drvdata->ops && drvdata->ops->reg_show)
		ret = drvdata->ops->reg_show(drvdata, buf);
	else {
		BRIDGE_ERR("reg_show err: drvdata=%p, ops=%p, info_show=%p\n",
			drvdata, drvdata->ops, drvdata->ops->reg_show);
		return -EINVAL;
	}
	return ret;
}

static DEVICE_ATTR_RW(info);
static DEVICE_ATTR_RW(reg_status);

static int bridge_attr_init(struct device *dev)
{
	int retval = 0;
	struct bridge_channel *drvdata = dev->driver_data;

	drvdata->recv_list = kmalloc(sizeof(*drvdata->recv_list), GFP_KERNEL);
	if (!drvdata->recv_list) {
		BRIDGE_ERR("%s could not allocate list_head\n", __func__);
		return -ENOMEM;
	}

	retval = device_create_file(dev, &dev_attr_info);
	if (retval < 0) {
		BRIDGE_ERR("err_create_attr_irq_num: %x\n", retval);
		goto err_create_attr_irq_num;
	}

	retval = device_create_file(dev, &dev_attr_reg_status);
	if (retval < 0) {
		BRIDGE_ERR("err_create_attr_irq_status: %x\n", retval);
		goto err_create_attr_irq_status;
	}
	return retval;
err_create_attr_irq_status:
	device_remove_file(dev, &dev_attr_info);
err_create_attr_irq_num:
	return retval;

}
static int bridge_attr_uninit(struct bridge_channel *drvdata)
{
	device_remove_file(drvdata->misc_dev.this_device,
			   &dev_attr_info);
	device_remove_file(drvdata->misc_dev.this_device,
			   &dev_attr_reg_status);

	return 0;
}

static int jlq_bridge_parse_dt(struct platform_device *pdev,
				struct jlq_bridge_info *bridge_info)
{
	struct resource *r;
	char *key;
	struct device_node *np = pdev->dev.of_node;
	const char *ch_name;
	int ret;

	ch_name = of_get_property(np, "jlq,bridge-name", NULL);
	strcpy(bridge_info->name, ch_name);

	key = "baseaddr";
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!r)
		goto missing_key;
	bridge_info->baseaddr = r->start;

	key = "rx_addr";
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!r)
		goto missing_key;
	bridge_info->rx_addr = r->start;
	bridge_info->rx_len = resource_size(r);

	key = "tx_addr";
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!r)
		goto missing_key;
	bridge_info->tx_addr = r->start;
	bridge_info->tx_len = resource_size(r);
	bridge_info->tx_pa_max = bridge_info->tx_len;

	key = "jlq,bridge-edge";
	ret = of_property_read_u32(np, key, &bridge_info->edge);
	if (ret)
		goto missing_key;

	switch (bridge_info->edge) {
	case TO_CM4:
		if (IS_ERR_OR_NULL(bridge_irq_trig_cm4_nosec_addr))
			bridge_irq_trig_cm4_nosec_addr =
				devm_platform_ioremap_resource(pdev, 1);
		bridge_info->irq_trig = bridge_irq_trig_cm4_nosec_addr;
		break;
	case TO_CM4_LPM:
		if (IS_ERR_OR_NULL(bridge_irq_trig_cm4_sec_addr))
			bridge_irq_trig_cm4_sec_addr =
				devm_platform_ioremap_resource(pdev, 1);
		bridge_info->irq_trig = bridge_irq_trig_cm4_sec_addr;
		break;
	case TO_ADSP:
		if (IS_ERR_OR_NULL(bridge_irq_trig_adsp_addr))
			bridge_irq_trig_adsp_addr =
				devm_platform_ioremap_resource(pdev, 1);
		bridge_info->irq_trig = bridge_irq_trig_adsp_addr;
		break;
	default:
		bridge_info->irq_trig = NULL;
	}

	bridge_info->irq_base = 0;
	bridge_info->rx_id = of_irq_get_byname(np, "rx_irq");
	bridge_info->rx_ctl_id = of_irq_get_byname(np, "rx_ctl_id");

	key = "jlq,bridge-tx-irq";
	ret = of_property_read_u32(np, key, &bridge_info->tx_id);
	if (ret)
		goto missing_key;

	bridge_info->dev = &pdev->dev;
	memset(&bridge_info->dbg_info, 0, sizeof(struct info));

	return 0;

missing_key:
	pr_err("%s: missing key: %s", __func__, key);
	return -ENODEV;
}

static int jlq_bridge_probe(struct platform_device *pdev)
{
	int retval = 0;
	struct jlq_bridge_info *bridge_info = NULL;
	struct bridge_channel *drvdata = NULL;

	BRIDGE_INFO("%s in!\n", __func__);

	bridge_info = kzalloc(sizeof(struct jlq_bridge_info), GFP_KERNEL);
	if (!bridge_info) {
		BRIDGE_ERR("out of memory\n");
		retval = -ENOMEM;
		goto out;
	}
	retval = jlq_bridge_parse_dt(pdev, bridge_info);
	if (retval) {
		BRIDGE_ERR("parse bridge info error!\n");
		goto bridge_info_err;
	}

	drvdata = kzalloc(sizeof(struct bridge_channel), GFP_KERNEL);
	if (!drvdata) {
		BRIDGE_ERR("out of memory\n");
		retval = -ENOMEM;
		goto bridge_info_err;
	}
	dev_set_drvdata(&pdev->dev, (void *)drvdata);
	drvdata->dev = &pdev->dev;

	drvdata->f_open = 0;
	drvdata->ops = get_bridge_ops();

	if (!drvdata->ops) {
		BRIDGE_ERR("drvdata ops is null\n");
		goto driver_init_err;
	}
	if (!drvdata->ops->drvinit) {
		BRIDGE_ERR("drvdata ops drvinit = %p\n", drvdata->ops->drvinit);
		goto driver_init_err;
	}

	retval = drvdata->ops->drvinit(drvdata, bridge_info);
	if (retval < 0)
		goto driver_init_err;

	if (drvdata->property.pro) {
		drvdata->alloc_rcvbuf = bridge_rcvbuf_alloc;
		drvdata->free_rcvbuf = bridge_rcvbuf_free;
	}
	drvdata->rxhandle = bridge_rx_irq;
	drvdata->ctlhandle = bridge_ctl_irq;

	list_add_tail(&drvdata->list, &bridge_ch_list);
	bridge_ch_num++;

	bridge_attr_init(&pdev->dev);

	kfree(bridge_info);

	BRIDGE_INFO("%s channel probe ok!\n", drvdata->name);
	return 0;

driver_init_err:
	kfree(drvdata);
bridge_info_err:
	kfree(bridge_info);
out:
	return retval;
}
static int __exit jlq_bridge_remove(struct platform_device *pdev)
{
	struct bridge_channel *drvdata = NULL;

	drvdata = dev_get_drvdata(&pdev->dev);
	bridge_attr_uninit(drvdata);
	BRIDGE_PRT("devices driver remove.\n");
	return 0;
}

static const struct of_device_id jlq_bridge_match_table[] = {
	{ .compatible = "jlq,bridge", },
	{}
};
static struct platform_driver jlq_bridge_driver = {
	.probe = jlq_bridge_probe,
	.remove = __exit_p(jlq_bridge_remove),
	.driver = {
		.name = "jlq-bridge",
		.of_match_table = jlq_bridge_match_table,
	},
};

static int __init jlq_bridge_init(void)
{
	int rc;

	bridge_ch_num = 0;

	INIT_LIST_HEAD(&bridge_ch_list);
	rc = platform_driver_register(&jlq_bridge_driver);
	if (rc) {
		BRIDGE_ERR("%s: bridge driver register failed %d\n",
			__func__, rc);
		return rc;
	}
	return 0;
}

static void __exit jlq_bridge_exit(void)
{
	platform_driver_unregister(&jlq_bridge_driver);
}
#ifdef MODULE
module_init(jlq_bridge_init);
#else
arch_initcall(jlq_bridge_init);
#endif

module_exit(jlq_bridge_exit);

MODULE_DESCRIPTION("Bridge driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("JLQ:bridge");
MODULE_SOFTDEP("pre: mailbox-irq-JR510");
