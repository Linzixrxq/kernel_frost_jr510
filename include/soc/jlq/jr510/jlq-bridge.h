/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/soc/jlq/ja310/jlq-bridge.h
 *
 * Copyright (c) 2019-2021   Lite-On Technology Co., Ltd
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
 */

#ifndef JLQ_BRIDGE_H
#define JLQ_BRIDGE_H
#include <linux/poll.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#define BRIDGE_NET_SHARED_NUM	(16)

enum Bridge_Type {
	LS_CHAR = 0,
	HS_IP_PACKET,
	HS_RAW_DATA,
	HS_IP_PACKET_SHARE
};

union Bridge_Pro {
	u32	pro;
	struct	{
		u32	flow_ctl:1;
		u32	packet:1;
		u32	share:4;
		u32	reserved:26;
	} pro_b;
};

struct info {
	u32	type;
	u32	addr;
	u32	len;
};

struct jlq_bridge_info {
	struct list_head	list;
	char			name[12];
	u32			edge;
	unsigned long		baseaddr;
	unsigned int		irq_base;
	void __iomem		*top_mbox_base;
	void __iomem		*irq_trig;
	u32			tx_pa_max;
	enum Bridge_Type	type;
	union Bridge_Pro	property;
	u32			rx_addr;
	u32			rx_len;
	u32			rx_id;
	u32			tx_addr;
	u32			tx_len;
	u32			tx_id;
	u32			rx_ctl_id;
	u32			tx_ctl_id;
	u32			rx_pkt_maxnum;
	u32			rx_pkt_maxsize;
	u32			tx_pkt_maxnum;
	u32			tx_pkt_maxsize;
	void			*dev;
	struct info		dbg_info;
};

/* circular buffer */
struct bridge_buf {
	unsigned int	buf_size;/*buf length*/
	char		*buf_buf;/*MEMORY start addr*/
	char		*buf_get;/*begin address for read*/
	char		*buf_put;/*begin address for write*/
};

struct buf_lbridge_head {
	u32		RcvDataOffset;/*begin addr for read*/
	u32		RcvIrqCount;
	u32		RcvDataCount;
	u32		RcvReserved;
	u32		SndDataOffset;/*begin addr for write*/
	u32		SndIrqCount;
	u32		SndDataCount;
	u32		SndReserved;
};

struct buf_hbridge_head {
	u32		RcvFrmOffset;/*read frame offset of receiver*/
	u32		RcvFrmIrqCount;/*frame irq count of receiver */
	u32		RcvFrmCount;/*frame count of receiver*/
	u32		RcvFCIrqCount;/*clear flow ctl irq count of receiver*/
	u32		SndFrmOffset;/*write frame offset of sender*/
	u32		SndFrmIrqCount;/*frame irq count of sender */
	u32		SndFrmCount;/*frame count of sender*/
	u32		SndFCIrqCount;
				/*received flow ctl clear irq count of sender*/
	u32		RcvNotBusy;/*flow ctl status of receiver*/
	u32		Reserved1;
	u32		Reserved2;
	u32		Reserved3;
};/*frame and flow ctl*/

struct bridge_frame {
	u32		Start;
	u32		Count;
};

struct bridge_rxdata {
	struct list_head	list;
	struct bridge_frame	frame;
	unsigned char		*vData;
	dma_addr_t		pData;
	void			*context;
};

struct bridge_info {
	void __iomem		*membase;
	u32			memlength;
	char			*pdata;/*data begin addr == buf_buf*/
	u32			length;/*data area length*/
	u32			pkt_num;
	u32			pkt_size;
	struct bridge_buf	circular_buf;
};

enum bridge_debug_cmd {
	start_dump_read_buf_cmd,
	start_dump_write_buf_cmd,
	start_write_to_read_cmd,
	start_read_to_write_cmd,
	stop_dump_read_buf_cmd,
	stop_dump_write_buf_cmd,
	stop_write_to_read_cmd,
	stop_read_to_write_cmd,
};

struct bridge_debug_info {
	u64 t;
	u32 count;
};

struct bridge_channel;
struct bridge_operations {
	int (*drvinit)(struct bridge_channel *drvdata,
		       struct jlq_bridge_info *bridge_info);
	int (*drvuninit)(struct bridge_channel *drvdata);
	int (*rxsavebuf)(struct bridge_channel *drvdata);
	int (*rxstorebuf)(struct bridge_channel *drvdata);
	int (*open)(struct bridge_channel *drvdata);
	int (*close)(struct bridge_channel *drvdata);
	unsigned int (*poll)(struct bridge_channel *drvdata);
	ssize_t (*xmit)(struct bridge_channel *drvdata, const char __user *buf,
			size_t count, bool isuser, bool is_last);
	ssize_t (*recv_raw)(struct bridge_channel *drvdata, char __user *buf,
			    size_t count);
	ssize_t (*recv_pkt)(struct bridge_channel *drvdata,
			    struct list_head **buf, size_t count, bool isuser);
	void (*irq_en)(unsigned int irq);
	void (*irq_dis)(unsigned int irq);
	int (*set_fc)(struct bridge_channel *drvdata);
	int (*clr_fc)(struct bridge_channel *drvdata);
	ssize_t (*info_show)(struct bridge_channel *drvdata, char *buf);
	ssize_t (*reg_show)(struct bridge_channel *drvdata, char *buf);
	bool (*test_rcvbusy)(struct bridge_channel *drvdata);
};
struct bridge_channel {
	struct list_head			list;
	char					name[12];
	u32					edge;
	enum Bridge_Type			type;
	union Bridge_Pro			property;
	struct device				*dev;
	struct miscdevice			misc_dev;
						/* misc device structure */
	struct net_device			*net_dev;
	struct mutex				read_sem;
	struct mutex				write_sem;
	struct mutex				op_mutex;
	spinlock_t				wr_lock;
	struct wakeup_source			*wakesrc;
	u32					rxirq;/*irq number for read*/
	u32					txirq;/*irq number for write*/
	u32					rxctl_irq;
	u32					txctl_irq;
	u32					is_open;
	u32					f_open;/*first open flag*/
	u32					unit;
	u32					tx_timeout;
	u32					tx_pa_max;
	unsigned long				baseaddr;
	void __iomem				*top_mbox_base;
	void __iomem				*irq_trig;
	wait_queue_head_t			rxwait_queue;
	wait_queue_head_t			txwait_queue;
	struct bridge_debug_info		rx_int_dinfo;
	struct bridge_debug_info		rx_dinfo;
	struct bridge_debug_info		tx_dinfo;
	struct bridge_debug_info		tx_ctl_dinfo;
	struct bridge_info			rxinfo;
	struct bridge_info			txinfo;
	struct list_head			rxbuf_list;
	struct list_head			rxdata_list;
	struct list_head			rx_list;
	struct info				dbg_info;
	bool					dbg_flag;
	char					*rx_save_buf;
	int (*alloc_rcvbuf)(struct bridge_channel *drvdata,
			    struct bridge_rxdata *rxbuf, size_t size);
	void (*free_rcvbuf)(struct bridge_channel *drvdata,
			    struct bridge_rxdata *rxbuf);
	irqreturn_t (*rxhandle)(struct bridge_channel *drvdata);
	irqreturn_t (*ctlhandle)(struct bridge_channel *drvdata);
	const struct bridge_operations *ops;
	struct list_head **recv_list;
	void *shared_priv[BRIDGE_NET_SHARED_NUM];
	u8	shared_id[BRIDGE_NET_SHARED_NUM];
	void (*notify)(void *priv, unsigned int flags);
	void *priv;
};

#define BG_EV_RX 1	//bridge rx event

#define GET_BRIDGE_TX_LEN	_IO('B', 1)
#define GET_BRIDGE_RX_LEN	_IO('B', 2)
#define SET_BRIDGE_FLOW_CTL	_IO('B', 3)
#define CLEAR_BRIDGE_FLOW_CTL	_IO('B', 4)

#define BRIDGE_BUF_COUNT	(10)
#define NO_FRAME_UNIT	(1)
#define BRIDGE_SND_WAIT_TIME	(50)
#define BRIDGE_SND_WAIT_RETRY_TIME    (5)
#define BRIDGE_SND_WAITRDY_TIME		(500)

#define JLQ_BRIDGE_DEBUG	1
#ifdef	JLQ_BRIDGE_DEBUG
#define BRIDGE_PRT(format, args...)	pr_debug("bridge: " format, ## args)
#define BRIDGE_INFO(format, args...)	pr_info("bridge: " format, ## args)
#define BRIDGE_ERR(format, args...)	pr_err("bridge: " format, ## args)
#else
#define BRIDGE_PRT(x...)  do {} while (0)
#define BRIDGE_INFO(x...)  do {} while (0)
#define BRIDGE_ERR(x...)  do {} while (0)
#endif

static inline int check_para(struct bridge_channel *drvdata)
{
	int status = 0;

	if (!drvdata) {
		BRIDGE_ERR("drvdata NULL!\n");
		status = -EFAULT;
	}

	if (!drvdata->is_open) {
		BRIDGE_ERR("%s:not open!\n", drvdata->name);
		status = -ENXIO;
	}
	return status;
}

extern int bridge_name_open(const char *name, uint32_t edge,
				struct bridge_channel **_ch,
				void *priv, void (*notify)(void *,
				unsigned int));
extern ssize_t bridge_read(struct bridge_channel *ch, char *buf, size_t count);
extern ssize_t bridge_read_from_notify(struct bridge_channel *ch, char *buf,
					size_t count);
extern ssize_t bridge_write(struct bridge_channel *ch, const char *buf,
				size_t count);
extern ssize_t bridge_write_from_irq(struct bridge_channel *ch, const char *buf,
				size_t count);
extern int bridge_release(struct bridge_channel *ch);
extern const struct bridge_operations *get_bridge_ops(void);
extern u64 get_clock(void);
extern int bridge_name_flush_for_cp_pd(const char *name);
#endif

