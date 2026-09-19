/*
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

#ifndef SLIM_CTRL_H
#define SLIM_CTRL_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>

#define JLQ_CONCUR_MSG	        128     /*SBMGR_RX_FIFO_DEPTH*/
#define JLQ_SLIM_MSGQ_BUF_LEN	64      /*MTDR_RX_FIFO_MSG_MAX_SIZE*/ /* //40, Per spec.max 40 bytes per received message */
#define JLQ_SLIM_TX_BUFS		32
#define JLQ_SLIM_NPORTS			24
#define JLQ_SLIM_NCHANS			32
#define JLQ_SLIM_AUTOSUSPEND	(MSEC_PER_SEC / 10)

struct jlq_slim_endp {
    bool	connected;
    int		port_b;
};

enum jlq_ctrl_state {
    JLQ_CTRL_AWAKE,
    JLQ_CTRL_IDLE,
    JLQ_CTRL_ASLEEP,
    JLQ_CTRL_DOWN,
};

struct jlq_slim_ctrl {
    struct slim_controller  ctrl;
    struct slim_framer	framer;
    struct device		*dev;
    void __iomem		*base;
    struct resource		*lpass_mem;
    u32			        lpass_phy_base;
    void __iomem		*lpass_virt_base;
    bool			    lpass_mem_usage;
    struct resource		*slew_mem;
    u32			curr_bw;
    u8			msg_cnt;
    u32			tx_buf[10];
    u8			rx_msgs[JLQ_CONCUR_MSG][JLQ_SLIM_MSGQ_BUF_LEN];
    int			tx_tail;
    int			tx_head;
    spinlock_t	rx_lock;
    int			head;
    int			tail;
    int			irq;
    int			err;
    struct completion	    **wr_comp;
    struct jlq_slim_endp	*pipes;
    struct jlq_slim_endp	tx_msgq;
    struct jlq_slim_endp	rx_msgq;
    struct completion		rx_msgq_notify;
    struct task_struct		*rx_msgq_thread;
    struct clk			*rclk;  /*frm clk*/
    struct clk			*hclk;  /*hclk*/
    struct clk			*cclk;  /*cport clk*/
    struct mutex		tx_lock;
    struct mutex		ssr_lock;
    spinlock_t		    tx_buf_lock;
    u8					pgdla;
    int					port_nums;
    struct completion	reconf;
    bool				reconf_busy;
    bool				chan_active;
    enum 				jlq_ctrl_state	state;
    struct completion	ctrl_up;
    int			nsats;
    u32			ver;
    int			default_ipc_log_mask;
    int			ipc_log_mask;
    bool		sysfs_created;
    void		*ipc_slimbus_log;
    void		*ipc_slimbus_log_err;
    u32			current_rx_buf[10];
    int			current_count;
    atomic_t		ssr_in_progress;
    atomic_t		init_in_progress;

    struct completion	slim_master_notify;
    struct task_struct	*slim_master_thread_task;
};

struct jlq_slim_ctrl *m_slimbus_slim_ctrl_get(void);

#endif