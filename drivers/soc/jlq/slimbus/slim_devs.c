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

#include <linux/slimbus/slimbus.h>
#include <linux/module.h>

#include "slim_inc.h"
#include "slim_mtdr_if.h"
#include "slim_chk.h"
#include "slim_regs.h"
#include "slim_mtdr.h"
#include "slim_master.h"
#include "slim_slave.h"
#include "slim_devs.h"
#include "slim_ctrl.h"


/*master frame eaddr*/
unsigned char master_frd_ea[] = {0x00, 0x01, 0x10, 0xa5, 0x45, 0x04};

/*master interface eaddr*/
unsigned char master_ifd_ea[] = {0x00, 0x00, 0x10, 0xa5, 0x45, 0x04};

/*master generic eaddr*/
unsigned char master_gd_ea[] = {0x00, 0x02, 0x10, 0xa5, 0x45, 0x04};

#ifdef WCD9306
/*slave interface eaddr, 0217 00E0*/
unsigned char slave_ifd_ea[] = {0x00, 0x00, 0xe0, 0x00, 0x17, 0x02};

/*slave generic eaddr, 0217 00E0*/
unsigned char slave_gd_ea[] = {0x00, 0x01, 0xe0, 0x00, 0x17, 0x02};

#endif

#ifdef WCN3950
/*slave interface eaddr, 0x0217 0220*/
unsigned char slave_ifd_ea[] = {0x00, 0x00, 0x20, 0x02, 0x17, 0x02};

/*slave generic eaddr, 0x0217 0220 01*/
unsigned char slave_gd_ea[] = {0x00, 0x01, 0x20, 0x02, 0x17, 0x02};
#endif

#define SLIM_EA_LEN         (6)
#define SLIM_DEVS_NUM_510   (5)
#define SLIM_DEVS_NUM       (SLIM_DEVS_NUM_510)     /*master and slave devices, now 3-for reserve only*/

#define SLIM_DEV_MASTER     (1)
#define SLIM_DEV_SLAVE      (0)
#define SLIM_DEV_VALID      (1)     /*1-valid, other-invalid*/

struct slim_dev {
    unsigned char class;
    unsigned char ea[SLIM_EA_LEN];
    unsigned char la;
    unsigned char ms;     /*master or slave*/
    unsigned char valid;
};

static struct slim_dev slim_devs[SLIM_DEVS_NUM];
static struct mutex	slim_devs_lock;

void slim_devs_init(void)
{
    mutex_init(&slim_devs_lock);
    memset(slim_devs, 0, SLIM_DEVS_NUM * sizeof(struct slim_dev));
}

void slim_devs_reset(void)
{
    mutex_lock(&slim_devs_lock);
    memset(slim_devs, 0, SLIM_DEVS_NUM * sizeof(struct slim_dev));
    mutex_unlock(&slim_devs_lock);
}

/*void slim_devs_save(MTDR_DeviceClass class, unsigned char *ea, unsigned char la)*/
void slim_devs_save_0(unsigned int class, unsigned char *ea, unsigned char la)
{
    int i;

    struct slim_dev *dev = &slim_devs[0];

    pr_info(LOGTAG"slim_devs_save(class:%.2x, ea:%.2x%.2x%.2x%.2x%.2x%.2x, la:%.2x)\n", class,
           ea[0], ea[1], ea[2], ea[3], ea[4], ea[5], la);

    mutex_lock(&slim_devs_lock);
    for(i = 0; i < SLIM_DEVS_NUM; i++) {
        if(dev->valid != SLIM_DEV_VALID) {
            dev->class = class;
            memcpy(dev->ea, ea, SLIM_EA_LEN);
            dev->la = la;
            if((ea[4] == (MTDR_Manufacturer_ID & 0xff)) &&  (ea[5] == (MTDR_Manufacturer_ID >> 8)))
                dev->ms = SLIM_DEV_MASTER;
            else
                dev->ms = SLIM_DEV_SLAVE;
            dev->valid = SLIM_DEV_VALID;
            break;
        }
        dev++;
    }
    mutex_unlock(&slim_devs_lock);
    return;
}

void slim_devs_save(unsigned int class, unsigned char *ea, unsigned char la)
{
    static int i = 0;
    int j;

    struct slim_dev *dev0 = &slim_devs[0];
    struct slim_dev *dev = &slim_devs[i];

    /*3950 multi REPORT_PRESENT problem*/
    for(j = 0; j < SLIM_DEVS_NUM; j++) {
        if(dev0->valid == SLIM_DEV_VALID) {
            if(!memcmp(ea, dev0->ea, SLIM_EA_LEN))
                return;
        }
        dev0++;
    }

    mutex_lock(&slim_devs_lock);

    if(dev->valid != SLIM_DEV_VALID) {
        dev->valid = SLIM_DEV_VALID;
        dev->class = class;
        memcpy(dev->ea, ea, SLIM_EA_LEN);
        dev->la = la;
        if((ea[4] == (MTDR_Manufacturer_ID & 0xff)) &&  (ea[5] == (MTDR_Manufacturer_ID >> 8)))
            dev->ms = SLIM_DEV_MASTER;
        else
            dev->ms = SLIM_DEV_SLAVE;

        pr_info(LOGTAG"slim_devs_save[%d](class:%.2x, ea:%.2x%.2x%.2x%.2x%.2x%.2x, la:%.2x)\n", i, class,
               ea[0], ea[1], ea[2], ea[3], ea[4], ea[5], la);
        i++;
    }

    mutex_unlock(&slim_devs_lock);
    return;
}


void slim_devs_list(void)
{
    int i;

    struct slim_dev *dev = &slim_devs[0];

    PR_INFO(LOGTAG"slim_devs_list\n");

    PR_INFO(LOGTAG"  class  ea    la ms valid\n");

    for(i = 0; i < SLIM_DEVS_NUM; i++) {
        if(dev->valid == SLIM_DEV_VALID) {
            PR_INFO(LOGTAG"  %.2x %.2x%.2x%.2x%.2x%.2x%.2x %.2x %x %x \n", dev->class,
                    dev->ea[0], dev->ea[1], dev->ea[2], dev->ea[3], dev->ea[4], dev->ea[5],
                    dev->la, dev->ms, dev->valid);
        }
        dev++;
    }
}

int slim_devs_check(void)
{
    int i;
    int valid_devs = 0;

    struct slim_dev *dev = &slim_devs[0];

    //PR_INFO(LOGTAG"slim_devs_check\n");

    mutex_lock(&slim_devs_lock);
    for(i = 0; i < SLIM_DEVS_NUM; i++) {
        if(dev->valid == SLIM_DEV_VALID) {
            if(dev->class == MTDR_DC_FRAMER
               && !memcmp(dev->ea, master_frd_ea, SLIM_EA_LEN))
                valid_devs++;
            else if(dev->class == MTDR_DC_INTERFACE
                    && (!memcmp(dev->ea, master_ifd_ea, SLIM_EA_LEN) || !memcmp(dev->ea, slave_ifd_ea, SLIM_EA_LEN)))
                valid_devs++;
            else if(dev->class == MTDR_DC_GENERIC
                    && (!memcmp(dev->ea, master_gd_ea, SLIM_EA_LEN) || !memcmp(dev->ea, slave_gd_ea, SLIM_EA_LEN)))
                valid_devs++;
            else
                PR_INFO(LOGTAG"slim_devs_check, not match\n");
        }
        dev++;
    }
    mutex_unlock(&slim_devs_lock);

    if(valid_devs == SLIM_DEVS_NUM_510) {
        PR_INFO(LOGTAG"slim_devs_check, pass\n");
        return SLIM_DEVS_CHECK_OK;
    }
    return 0;
}

int slim_devs_check_slave(void)
{
    int i;
    int valid_devs = 0;

    struct slim_dev *dev = &slim_devs[0];

    //PR_INFO(LOGTAG"slim_devs_check\n");

    mutex_lock(&slim_devs_lock);
    for(i = 0; i < SLIM_DEVS_NUM; i++) {
        PR_INFO(LOGTAG"slim_devs_check_slave[%d](class:%.2x, ea:%.2x%.2x%.2x%.2x%.2x%.2x, la:%.2x)\n", i, dev->class,
               dev->ea[0], dev->ea[1], dev->ea[2], dev->ea[3], dev->ea[4], dev->ea[5], dev->la);

        if(dev->valid == SLIM_DEV_VALID) {
            if(!memcmp(dev->ea, slave_ifd_ea, SLIM_EA_LEN)) {
                PR_INFO(LOGTAG"slim_devs_check_slave, slave_ifd ok\n");
                valid_devs++;
            } else if(!memcmp(dev->ea, slave_gd_ea, SLIM_EA_LEN)) {
                PR_INFO(LOGTAG"slim_devs_check_slave, slave_gd ok\n");
                valid_devs++;
            }
        }
        dev++;
    }
    mutex_unlock(&slim_devs_lock);

    PR_INFO(LOGTAG"slim_devs_check_slave, valid_devs: %d\n", valid_devs);

    if(valid_devs == 2) {
        PR_INFO(LOGTAG"slim_devs_check_slave, pass\n");
        return SLIM_DEVS_CHECK_OK;
    }
    return 0;
}



/*list all registered devices of ctrl*/
void slim_ctrl_devices_list(void)
{
    struct jlq_slim_ctrl *dev;

    struct slim_controller *ctrl = NULL;
    struct slim_device *sbdev = NULL,
                            *sbdev_master_gd = NULL, *sbdev_master_ifd = NULL, *sbdev_master_frd = NULL;

    u8 e_len = 6;

#if defined(WCD9306) || defined(WCN3950)
    struct slim_device *sbdev_slave_gd = NULL, *sbdev_slave_ifd = NULL;
#endif

    struct list_head *pos, *next;

    PR_INFO(LOGTAG"slim_t_ctrl_devices_list\n");

    dev = m_slimbus_slim_ctrl_get();
    if(!dev) {
        PR_INFO(LOGTAG"slim ctrl dev is NULL!\n");
        return;
    }

    ctrl = &dev->ctrl;

    /*lookup master device*/
    list_for_each_safe(pos, next, &ctrl->devs) {
        sbdev = list_entry(pos, struct slim_device, dev_list);
        if (memcmp(sbdev->e_addr, master_frd_ea, e_len) == 0) {
            sbdev_master_frd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %.2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, master_ifd_ea, e_len) == 0) {
            sbdev_master_ifd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %.2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, master_gd_ea, e_len) == 0) {
            sbdev_master_gd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %.2x\n", sbdev->name, sbdev->laddr);
        }
#if defined(WCD9306) || defined(WCN3950)
        else if(memcmp(sbdev->e_addr, slave_ifd_ea, e_len) == 0) {
            sbdev_slave_ifd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %.2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, slave_gd_ea, e_len) == 0) {
            sbdev_slave_gd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %.2x\n", sbdev->name, sbdev->laddr);
        }
#endif
        else {
            //nothing.
        }
    }
}
EXPORT_SYMBOL(slim_ctrl_devices_list);

MODULE_LICENSE("GPL");

