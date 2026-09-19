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

#define DEBUG
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slimbus/slimbus.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_slimbus.h>
#include <linux/msm-sps.h>

#include "slim_inc.h"
#include "slim_mtdr_if.h"
#include "slim_chk.h"
#include "slim_regs.h"
#include "slim_mtdr.h"
#include "slim_master.h"
#include "slim_slave.h"
#include "slim_devs.h"
#include "slim_ctrl.h"


/*t rx task*/
static struct task_struct *slim_t_rx_task;
static struct completion slim_t_rx_notify;
static u8 slim_t_rbuf[16];

static int slim_t_rx_thread(void *data)
{
    int ret;
    //struct jlq_slim_ctrl *dev = (struct jlq_slim_ctrl *)data;
    (void)data;

    pr_info(LOGTAG"slim_t_rx_thread");

    while (!kthread_should_stop()) {
        set_current_state(TASK_INTERRUPTIBLE);
        ret = wait_for_completion_interruptible(&slim_t_rx_notify);
        if (ret)
            pr_info(LOGTAG"rx thread wait error:%d\n", ret);

        pr_info(LOGTAG"recved data, slim_t_rbuf: %2x %2x\n", slim_t_rbuf[0], slim_t_rbuf[1]);

    }

    return 0;
}


void slim_t_main(struct jlq_slim_ctrl *dev)
{
    struct slim_controller *ctrl = NULL;
    struct slim_device *sbdev = NULL,
                            *sbdev_master_gd = NULL, *sbdev_master_ifd = NULL, *sbdev_master_frd = NULL;
    u8 laddr_master_gd, laddr_master_ifd, laddr_master_frd;
    u8 e_len = 6;

#if defined(WCD9306) || defined(WCN3950)
    struct slim_device *sbdev_slave_gd = NULL, *sbdev_slave_ifd = NULL;
    u8 laddr_slave_gd, laddr_slave_ifd;
#endif

    struct list_head *pos, *next;

    int ret;

    if(!dev)
        return;

    pr_info(LOGTAG"slim_t_main\n");

    ctrl = &dev->ctrl;

    /*lookup master device*/
    list_for_each_safe(pos, next, &ctrl->devs) {
        sbdev = list_entry(pos, struct slim_device, dev_list);
        if (memcmp(sbdev->e_addr, master_frd_ea, 6) == 0) {
            sbdev_master_frd = sbdev;
            pr_info(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, master_ifd_ea, 6) == 0) {
            sbdev_master_ifd = sbdev;
            pr_info(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, master_gd_ea, 6) == 0) {
            sbdev_master_gd = sbdev;
            pr_info(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        }
#if defined(WCD9306) || defined(WCN3950)
        else if(memcmp(sbdev->e_addr, slave_ifd_ea, 6) == 0) {
            sbdev_slave_ifd = sbdev;
            pr_info(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, slave_gd_ea, 6) == 0) {
            sbdev_slave_gd = sbdev;
            pr_info(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        }
#endif
        else {
            //nothing.
        }
    }

    /*t rx thread*/
    memset(slim_t_rbuf, 0, 16);
    init_completion(&slim_t_rx_notify);
    slim_t_rx_task = kthread_run(slim_t_rx_thread, dev, "slim_t_rx_task");

    /*API: slim_get_logical_addr*/
    ret = slim_get_logical_addr(sbdev_master_frd, master_frd_ea, e_len, &laddr_master_frd);
    if(ret == 0) {
        pr_info(LOGTAG"slim_get_logical_addr, sbdev_master_frd, la: %2x\n", laddr_master_frd);
    }

    ret = slim_get_logical_addr(sbdev_master_ifd, master_ifd_ea, e_len, &laddr_master_ifd);
    if(ret == 0) {
        pr_info(LOGTAG"slim_get_logical_addr, sbdev_master_ifd, la: %2x\n", laddr_master_ifd);
    }

    ret = slim_get_logical_addr(sbdev_master_gd, master_gd_ea, e_len, &laddr_master_gd);
    if(ret == 0) {
        pr_info(LOGTAG"slim_get_logical_addr, sbdev_master_gd, la: %2x\n", laddr_master_gd);
    }

#if defined(WCD9306) || defined(WCN3950)
    if(sbdev_slave_ifd) {
        ret = slim_get_logical_addr(sbdev_slave_ifd, slave_ifd_ea, e_len, &laddr_slave_ifd);
        if(ret == 0) {
            pr_info(LOGTAG"slim_get_logical_addr, sbdev_slave_ifd, la: %2x\n", laddr_slave_ifd);
        }
    }

    if(sbdev_slave_gd) {
        ret = slim_get_logical_addr(sbdev_slave_gd, slave_gd_ea, e_len, &laddr_slave_gd);
        if(ret == 0) {
            pr_info(LOGTAG"slim_get_logical_addr, sbdev_slave_gd, la: %2x\n", laddr_slave_gd);
        }
    }
#endif

    /*reconfig bus*/
    //m_slimbus_reconfigBus(NULL);

    if(sbdev_slave_ifd) {
        /*sync 1 byte, request*/
        {
            struct slim_ele_access msg;
            u8 rbuf[16];
            u8 len = 1;

            msg.start_offset = 0x100;  //0x800;
            msg.num_bytes = 1;
            msg.comp = NULL;  /*if sync, must init to be NULL, it's important!*/

            ret = slim_request_val_element(sbdev_slave_ifd, &msg, rbuf, len);
            if(ret == 0) {
                pr_info(LOGTAG"slim_request_val_element(slave sync 1 byte), rbuf: %2x\n", rbuf[0]);
            } else {
                pr_info(LOGTAG"slim_request_val_element(slave sync 1 byte), fail(%d)\n", ret);
            }
        }
    }

}

void slim_t_entry(void)
{
    struct jlq_slim_ctrl *dev;

    pr_info(LOGTAG"slim_t_entry\n");

    dev = m_slimbus_slim_ctrl_get();
    if(!dev)
        pr_info(LOGTAG"slim ctrl dev is NULL!\n");
    else
        slim_t_main(dev);
}

