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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_slimbus.h>
#include <linux/slimbus/slimbus.h>

#include <dt-bindings/jlq/jr510/offset-audio_sysctrl.h>

#include "slim_mtdr_if.h"
#include "slim_chk.h"
#include "slim_regs.h"
#include "slim_mtdr.h"
#include "slim_master.h"
#include "slim_slave.h"
#include "slim_devs.h"
#include "slim_ctrl.h"
#include "adsp_subsys.h"


#define JLQ_SLIM_NAME	"jlq_slim_ctrl"
#define SLIM_ROOT_FREQ  24576000  //19200000  //24576000


static struct jlq_slim_ctrl *slim_ctrl_dev = NULL;

int jlq_slim_rx_enqueue(struct jlq_slim_ctrl *dev, u32 *buf, u8 len)
{
    spin_lock(&dev->rx_lock);
    if ((dev->tail + 1) % JLQ_CONCUR_MSG == dev->head) {
        spin_unlock(&dev->rx_lock);
        dev_err(dev->dev, "RX QUEUE full!\n");
        return -EXFULL;
    }
    memcpy((u8 *)dev->rx_msgs[dev->tail], (u8 *)buf, len);
    dev->tail = (dev->tail + 1) % JLQ_CONCUR_MSG;
    spin_unlock(&dev->rx_lock);
    return 0;
}

int jlq_slim_rx_dequeue(struct jlq_slim_ctrl *dev, u8 *buf)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->rx_lock, flags);
    if (dev->tail == dev->head) {
        spin_unlock_irqrestore(&dev->rx_lock, flags);
        return -ENODATA;
    }
    memcpy(buf, (u8 *)dev->rx_msgs[dev->head], JLQ_SLIM_MSGQ_BUF_LEN);
    dev->head = (dev->head + 1) % JLQ_CONCUR_MSG;
    spin_unlock_irqrestore(&dev->rx_lock, flags);
    return 0;
}

static irqreturn_t jlq_slim_interrupt(int irq, void *d)
{
    struct jlq_slim_ctrl *dev = d;

    MTDR_Instance *instance;
    uint32_t reg, copy;
    bool dataPortInterrupt = false;
    uint8_t i, j;
    MTDR_DataPortInterrupt dataPortIrq;
    bool rxMsg_flag = false;

    instance = (MTDR_Instance *) m_MTDR_pDrvIns_get();
    if (!instance) {
        pr_info(LOGTAG"jlq_slim_interrupt, instance is NULL\n");
        return IRQ_NONE;
    }

    /* Check INT register */
    reg = MTDR_ReadReg(INTERRUPTS.INT);
    pr_info(LOGTAG"jlq_slim_interrupt, INT reg: %8x\n", reg);

    if (reg) { //Interrupt occurred in INT register
        if ((INTERRUPTS__INT__TX_INT__READ(reg))) { //MessagesSendingFinished
            //nothing.
        }
        if ((INTERRUPTS__INT__TX_ERR__READ(reg))) {  //MessageSendingFailed
            dev_err(dev->dev, "INT__TX_ERR\n");
            dev->err = -EIO;
        }
        if ((INTERRUPTS__INT__RCFG_INT__READ(reg))) {  //rcfg done
            dev_err(dev->dev, "RCFG_INT\n");
            complete(&dev->reconf);
        }

        if (INTERRUPTS__INT__RX_INT__READ(reg)) {
            uint8_t rxFifoData[MTDR_RX_FIFO_MSG_MAX_SIZE];
            uint8_t fifoSize = 0;
            uint32_t state_reg = 0;

            for (;;) {
                state_reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
                if (COMMAND_STATUS__STATE__RX_PULL__READ(state_reg))
                    continue;

                if (COMMAND_STATUS__STATE__RX_NOTEMPTY__READ(state_reg) == 0)
                    break;

                fifoSize = m_MTDR_FifoReceive(rxFifoData, MTDR_RX_FIFO_MSG_MAX_SIZE);

#if DEBUG_CHECK_DATA
                if ((fifoSize > 0) && (fifoSize <= MTDR_MESSAGE_MAX_LENGTH)) {
                    int i = 0;
                    uint8_t *p = rxFifoData;
                    pr_info(LOGTAG"[RX DATA](%d)BEGIN: [", fifoSize);
                    for(i = 0; i < fifoSize; i++)
                        pr_info("%x ", *p++);
                    pr_info("]END\n");
                }
#endif

                jlq_slim_rx_enqueue(dev, (uint32_t *)rxFifoData, fifoSize);

                rxMsg_flag = true;
            }
        }

        dataPortInterrupt = INTERRUPTS__INT__PORT_INT__READ(reg);

        /* Clear interrupts */
        MTDR_WriteReg(INTERRUPTS.INT, reg);

        /*
         * Guarantee that interrupt clear bit write goes through before
         * signalling completion/exiting ISR
         */
        mb();
        if(rxMsg_flag == true)
            complete(&dev->rx_msgq_notify);
    }

    if (dataPortInterrupt != 0) {

        for (i = 0; i < 16; i++) {  //for (i = 0; i < 1; i++) { , this shall be ok for 510, deman.
            reg = MTDR_ReadReg(PORT_INTERRUPTS.P_INT[i]);

            if (reg == 0) //If all bits in the register are low, then there is no interrupt
                continue;

            copy = reg;
            for (j = 0; j < 4; j++) {   //P_INT register has 4 the same 8 bit registers with interrupts
                dataPortIrq = 0;
                dataPortIrq |=  (PORT_INTERRUPTS__P_INT__P0_ACT_INT__READ(copy)) ? MTDR_DP_INT_ACT : 0;
                dataPortIrq |=  (PORT_INTERRUPTS__P_INT__P0_CON_INT__READ(copy)) ? MTDR_DP_INT_CON : 0;
                dataPortIrq |=  (PORT_INTERRUPTS__P_INT__P0_CHAN_INT__READ(copy)) ? MTDR_DP_INT_CHAN : 0;
                dataPortIrq |=  (PORT_INTERRUPTS__P_INT__P0_DMA_INT__READ(copy)) ? MTDR_DP_INT_DMA : 0;
                dataPortIrq |=  (PORT_INTERRUPTS__P_INT__P0_OVF_INT__READ(copy)) ? MTDR_DP_INT_OVF : 0;
                dataPortIrq |=  (PORT_INTERRUPTS__P_INT__P0_UND_INT__READ(copy)) ? MTDR_DP_INT_UND : 0;

                copy >>= 8;

                if (dataPortIrq != 0) {
                    m_slimbus_DataPortInterruptsHandler((i * 4 + j), dataPortIrq);
                }
            }

            MTDR_WriteReg(PORT_INTERRUPTS.P_INT[i], reg); // Clear contents of the port interrupt register
        }
    }

    //pr_info(LOGTAG"jlq_slim_interrupt, exit\n");

    return IRQ_HANDLED;
}

static int jlq_xfer_msg(struct slim_controller *ctrl, struct slim_msg_txn *txn)
{
    DECLARE_COMPLETION_ONSTACK(done);
    struct jlq_slim_ctrl *dev = slim_get_ctrldata(ctrl);
    //u8 la = txn->la;
    u8 mc = (u8)(txn->mc & 0xFF);
    uint32_t ret = 0;

    //pr_info(LOGTAG"jlq_xfer_msg, dev->ctrl.nports: %d, dev->reconf_busy: %d\n", dev->ctrl.nports, dev->reconf_busy);

    mutex_lock(&dev->tx_lock);

    /*last reconfiguration is done?*/
    if (txn->mt == SLIM_MSG_MT_CORE &&
        mc == SLIM_MSG_MC_BEGIN_RECONFIGURATION) {
        if (dev->reconf_busy) {
            wait_for_completion(&dev->reconf);
            dev->reconf_busy = false;
        }
    }

    /*request VE*/
    if ((txn->mt == SLIM_MSG_MT_CORE) && (mc == SLIM_MSG_MC_REQUEST_VALUE)) {

        ret = m_MTDR_MsgRequestValue(txn->la, txn->tid, (txn->ec >> 4) & 0xFFF, txn->ec & 0x07);
    }

    /*change VE*/
    else if ((txn->mt == SLIM_MSG_MT_CORE) && (mc == SLIM_MSG_MC_CHANGE_VALUE)) {

        ret = m_MTDR_MsgChangeValue(txn->la, (txn->ec >> 4) & 0xFFF, txn->ec & 0x07,
                                    (uint8_t *)txn->wbuf, m_slim_slicesize_get(txn->ec & 0x07));
    }

    /*PORT connect/disconnect*/
    /*if (txn->mt == SLIM_MSG_MT_CORE && txn->la == 0xFF &&
    	(mc == SLIM_MSG_MC_CONNECT_SOURCE ||
    	 mc == SLIM_MSG_MC_CONNECT_SINK ||
    	 mc == SLIM_MSG_MC_DISCONNECT_PORT))
    	la = dev->pgdla;  //no this now, should be set when the MTDR_MESSAGE_CODE_REPORT_PRESENT, do later.
    */

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             (mc == SLIM_MSG_MC_CONNECT_SOURCE)) {
        ret = m_MTDR_MsgConnectSource(txn->la, *(txn->wbuf + 0) & 0x3f, *(txn->wbuf + 1));
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             (mc == SLIM_MSG_MC_CONNECT_SINK)) {
        ret = m_MTDR_MsgConnectSink(txn->la, *(txn->wbuf + 0) & 0x3f, *(txn->wbuf + 1));
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             (mc == SLIM_MSG_MC_DISCONNECT_PORT)) {
        ret = m_MTDR_MsgDisconnectPort(txn->la, *(txn->wbuf));
    }

    /*reconfiguration*/
    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_BEGIN_RECONFIGURATION) {
        dev->reconf_busy = true;
        ret = m_MTDR_MsgBeginReconfiguration();
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_NEXT_SUBFRAME_MODE) {
        /*do this by m_slimbus_reconfigBus*/
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_NEXT_CLOCK_GEAR) {
        /*do this by m_slimbus_reconfigBus*/
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_NEXT_DEFINE_CONTENT) {
        /*maybe do this by master*/
#ifndef CHANNEL_DEFINE_CONTENT_BY_MASTER
        bool frequencyLockedBit = txn->wbuf[1] >> 7;
        MTDR_PresenceRate presenceRate = txn->wbuf[1] & 0x7f;
        uint8_t ch;

        /*with wcn3950*/
        ch = txn->wbuf[0];
        if(ch == 160 || ch == 161) {
            presenceRate = MTDR_PR_48K;
        }
        else if(ch == 157 || ch == 159) {
            presenceRate = MTDR_PR_16K;
        }

        pr_crit(LOGTAG"presence rate(chl: %d, orig: %d, modified: %d\n", ch, txn->wbuf[1] & 0x7f, presenceRate);

        ret = m_MTDR_MsgNextDefineContent(txn->wbuf[0], frequencyLockedBit, presenceRate,
                                          txn->wbuf[2] >> 4, txn->wbuf[2] & 0x0f, (txn->wbuf[3] & 0x20) >> 5, txn->wbuf[3] & 0x1f);
#endif
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_NEXT_DEFINE_CHANNEL) {
#ifndef CHANNEL_DEFINE_CONTENT_BY_MASTER
        /*maybe do this by master*/
        enum slim_ch_proto	prot;
        MTDR_TransportProtocol tp;
        uint8_t ch;

        prot = txn->wbuf[2] >> 4;
        switch(prot) {
        case SLIM_AUTO_ISO:
        case SLIM_HARD_ISO:
            tp = MTDR_TP_ISOCHRONOUS;
            break;
        case SLIM_PUSH:
            tp = MTDR_TP_PUSHED;
            break;
        case SLIM_PULL:
            tp = MTDR_TP_PULLED;
            break;
        default: {
            ret = -1;
            pr_info(LOGTAG"SLIM_MSG_MC_NEXT_DEFINE_CHANNEL(prot error(%d)\n", prot);
        }
        }

        /*with wcn3950*/
        if(!ret) {
            ch = txn->wbuf[0];
            if(ch == 160) {
                m_MTDR_MsgNextSubframeMode(MTDR_SM_3_CSW_32_SL);
                m_MTDR_MsgNextClockGear(MTDR_CG_8);
                ret = m_MTDR_MsgNextDefineChannel(txn->wbuf[0], tp, 0xc38, 4);
            }
            else if(ch == 161) {
                m_MTDR_MsgNextSubframeMode(MTDR_SM_3_CSW_32_SL);
                m_MTDR_MsgNextClockGear(MTDR_CG_8);
                ret = m_MTDR_MsgNextDefineChannel(txn->wbuf[0], tp, 0xc3c, 4);
            }
            else if(ch == 157) {
                tp = MTDR_TP_ISOCHRONOUS;
                m_MTDR_MsgNextSubframeMode(MTDR_SM_3_CSW_24_SL);
                m_MTDR_MsgNextClockGear(MTDR_CG_6);
                ret = m_MTDR_MsgNextDefineChannel(txn->wbuf[0], tp, 0x808, 4);
            }
            else if(ch == 159) {
                tp = MTDR_TP_ISOCHRONOUS;
                m_MTDR_MsgNextSubframeMode(MTDR_SM_3_CSW_24_SL);
                m_MTDR_MsgNextClockGear(MTDR_CG_6);
                ret = m_MTDR_MsgNextDefineChannel(txn->wbuf[0], tp, 0x80c, 4);
            }
        }

        //if(!ret)
        //    ret = m_MTDR_MsgNextDefineChannel(txn->wbuf[0], tp, ((txn->wbuf[2] & 0x0f) << 8) | txn->wbuf[1], txn->wbuf[3] & 0x1f);
#endif
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_NEXT_ACTIVATE_CHANNEL) {
        ret = m_MTDR_MsgNextActivateChannel(*(txn->wbuf + 0));
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_NEXT_REMOVE_CHANNEL) {
        ret = m_MTDR_MsgNextRemoveChannel(*(txn->wbuf));
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_NEXT_DEACTIVATE_CHANNEL) {
        m_MTDR_MsgNextDeactivateChannel(*(txn->wbuf));
    }

    else if (txn->mt == SLIM_MSG_MT_CORE &&
             mc == SLIM_MSG_MC_RECONFIGURE_NOW) {
        ret = m_MTDR_MsgReconfigureNow();
        dev->reconf_busy = false;
    }

    else {
        //nothing.
    }

    mutex_unlock(&dev->tx_lock);

    return ret;
}


static int jlq_set_laddr(struct slim_controller *ctrl, const u8 *ea,
                         u8 elen, u8 laddr)
{
    struct jlq_slim_ctrl *dev = slim_get_ctrldata(ctrl);

    int ret;
    u64 ea_buf = 0;

    mutex_lock(&dev->tx_lock);

    ea_buf = ea[5];
    ea_buf = (ea_buf << 8) | ea[4];
    ea_buf = (ea_buf << 8) | ea[3];
    ea_buf = (ea_buf << 8) | ea[2];
    ea_buf = (ea_buf << 8) | ea[1];
    ea_buf = (ea_buf << 8) | ea[0];

    ret = m_MTDR_MsgAssignLogicalAddress(ea_buf, laddr);

    mutex_unlock(&dev->tx_lock);
    if (ret) {
        pr_err(LOGTAG"set LADDR:0x%x failed:ret:%d\n", laddr, ret);
    } else {
        pr_err(LOGTAG"jlq_set_laddr succ, ea: %2x %2x %2x %2x %2x %2x, la: %2x\n",
               ea[0], ea[1], ea[2], ea[3], ea[4], ea[5], laddr);
    }

    return ret;
}

/*
static void jlq_get_eaddr(u8 *e_addr, u32 *buffer)
{
    e_addr[0] = (buffer[1] >> 24) & 0xff;
    e_addr[1] = (buffer[1] >> 16) & 0xff;
    e_addr[2] = (buffer[1] >> 8) & 0xff;
    e_addr[3] = buffer[1] & 0xff;
    e_addr[4] = (buffer[0] >> 24) & 0xff;
    e_addr[5] = (buffer[0] >> 16) & 0xff;
}
*/

static int jlq_clk_pause_wakeup(struct slim_controller *ctrl)
{
    struct jlq_slim_ctrl *dev = slim_get_ctrldata(ctrl);

    enable_irq(dev->irq);
    clk_prepare_enable(dev->rclk);
    //writel_relaxed(1, dev->base + FRM_WAKEUP);
    /* Make sure framer wakeup write goes through before exiting function */
    mb();
    /*
     * Workaround: Currently, slave is reporting lost-sync messages
     * after slimbus comes out of clock pause.
     * Transaction with slave fail before slave reports that message
     * Give some time for that report to come
     * Slimbus wakes up in clock gear 10 at 24.576MHz. With each superframe
     * being 250 usecs, we wait for 20 superframes here to ensure
     * we get the message
     */
    usleep_range(4950, 5000);
    return 0;
}

static void jlq_slim_rxwq(struct jlq_slim_ctrl *dev)
{
    u8 rxFifoData[JLQ_SLIM_MSGQ_BUF_LEN];
    MTDR_Message rxMsg;

    int ret;

    while ((jlq_slim_rx_dequeue(dev, (u8 *)rxFifoData)) != -ENODATA) {
        if (m_MTDR_DecodeMessage(rxFifoData, JLQ_SLIM_MSGQ_BUF_LEN, &rxMsg) == MTDR_RET_EOK) {
#if DEBUG_CHECK_INFO
            {
                int i = 0;
                uint8_t *p = rxMsg.payload;

                pr_info(LOGTAG"RX_FIFO message BEGIN: {\n");
                if(rxMsg.arbitrationType == MTDR_AT_SHORT)
                    pr_info("srcAddr: %x\n", (uint8_t)(rxMsg.sourceAddress & 0xff));
                else if(rxMsg.arbitrationType == MTDR_AT_LONG)
                    pr_info("srcAddr: %4x %8x\n", (uint16_t)((rxMsg.sourceAddress >> 32) & 0xffff), (uint32_t)((rxMsg.sourceAddress) & 0xffffffff));

                if(rxMsg.destinationType == MTDR_DT_LOGICAL_ADDRESS)
                    pr_info("dstAddr: %x\n", (uint8_t)(rxMsg.destinationAddress & 0xff));
                else if(rxMsg.destinationType == MTDR_DT_ENUMERATION_ADDRESS)
                    pr_info("dstAddr: %4x %8x\n", (uint16_t)((rxMsg.destinationAddress >> 32) & 0xffff), (uint32_t)((rxMsg.destinationAddress) & 0xffffffff));

                pr_info("MT: %x\n", rxMsg.messageType);
                pr_info("MC: %x\n", rxMsg.messageCode);

                pr_info("PD: ");
                for(i = 0; i < rxMsg.payloadLength; i++)
                {
                    pr_info("%x ", *p++);
                }
                pr_info("}END\n");
            }
#endif

            switch (rxMsg.messageCode) {
            case MTDR_MESSAGE_CODE_REPORT_PRESENT: {        //Payload: 0 - Device Class code, 1 - Device Class Version
                u8 laddr;
                u8 ea_buf[6];
                ea_buf[0] = (u8)rxMsg.sourceAddress;
                ea_buf[1] = (u8)(rxMsg.sourceAddress >> 8);
                ea_buf[2] = (u8)(rxMsg.sourceAddress >> 16);
                ea_buf[3] = (u8)(rxMsg.sourceAddress >> 24);
                ea_buf[4] = (u8)(rxMsg.sourceAddress >> 32);
                ea_buf[5] = (u8)(rxMsg.sourceAddress >> 40);

                pr_info(LOGTAG"[REPORT_PRESENT] %.2x  %.2x%.2x%.2x%.2x%.2x%.2x\n", rxMsg.payload[0],
                        ea_buf[0], ea_buf[1], ea_buf[2], ea_buf[3], ea_buf[4], ea_buf[5]);

                ret = slim_assign_laddr(&dev->ctrl, ea_buf, 6, &laddr, false);
                if (ret)
                    pr_err("assign laddr failed, error:%d\n", ret);
                else
                    slim_devs_save(rxMsg.payload[0], ea_buf, laddr);


                /* Is this ported generic device? */
                /*if (!ret && e_addr[5] == ? &&
                	e_addr[4] == ? &&
                	e_addr[1] == ? &&
                	e_addr[2] != ?)
                	dev->pgdla = laddr;
                */
                /*
                if (!ret && !pm_runtime_enabled(dev->dev))
                	pm_runtime_enable(dev->dev);
                */

            }
            break;

            case MTDR_MESSAGE_CODE_REPORT_ABSENT:          //Payload: None
                //nothing.
                break;

            case MTDR_MESSAGE_CODE_REPLY_INFORMATION:      //Payload: 0 - Transaction ID, 1 => 16 - Information Slice
            case MTDR_MESSAGE_CODE_REPLY_VALUE:            //Payload: 0 - Transaction ID, 1 => 16 - Value Slice
                dev_dbg(dev->dev, "tid:%d, len:%d\n", rxMsg.payload[0], rxMsg.payloadLength - MTDR_TRANSACTION_ID_LENGTH);
                slim_msg_response(&dev->ctrl, &(rxMsg.payload[MTDR_TRANSACTION_ID_LENGTH]), rxMsg.payload[0], rxMsg.payloadLength - MTDR_TRANSACTION_ID_LENGTH);
                //pm_runtime_mark_last_busy(dev->dev);
                break;

            case MTDR_MESSAGE_CODE_REPORT_INFORMATION:     //Payload: 0 - Element Code [7:0], 1 - Element Code [15:8] 2 => 17 - Information Slice
                //nothing.
                break;

            default:
                dev_err(dev->dev, "unexpected message:mc:%x, mt:%x\n", rxMsg.messageCode, rxMsg.messageType);
                break;

            }
        } else
            dev_err(dev->dev, "m_MTDR_DecodeMessage error");

    }
}

static int jlq_slim_rx_msgq_thread(void *data)
{
    struct jlq_slim_ctrl *dev = (struct jlq_slim_ctrl *)data;
    struct completion *notify = &dev->rx_msgq_notify;
    int ret;

    dev_dbg(dev->dev, "rx thread started");

    while (!kthread_should_stop()) {
        set_current_state(TASK_INTERRUPTIBLE);
        ret = wait_for_completion_interruptible(notify);

        if (ret)
            dev_err(dev->dev, "rx thread wait error:%d\n", ret);

        /* irq notification per message */
        jlq_slim_rxwq(dev);
    }

    return 0;
}


struct jlq_slim_ctrl *m_slimbus_slim_ctrl_get(void)
{
    return slim_ctrl_dev;
}

static int jlq_slim_probe(struct platform_device *pdev)
{
    struct jlq_slim_ctrl *dev;
    int ret;

    struct resource		*slim_mem, *slim_io;
    struct resource		*irq;

    struct device_node *ctrl_node;
    void __iomem	*ctrl_regbase;

    unsigned long freq;

    pr_info(LOGTAG"jlq_slim_probe(JR510 slim ctrl)\n");

    if (adsp_state_get() != ADSP_STAT_POWERUP
        && adsp_state_get() != ADSP_STAT_LOADED
        && adsp_state_get() != ADSP_STAT_RUNNING) {
        dev_dbg(&pdev->dev, LOGTAG"defering %s, adsp NOT dereset or running\n", __func__);
        return -EPROBE_DEFER;
    }
    pr_info(LOGTAG"adsp is ready\n");


    slim_mem = platform_get_resource_byname(pdev, IORESOURCE_MEM,
                                            "slimbus_physical");
    if (!slim_mem) {
        dev_err(&pdev->dev, LOGTAG"no slimbus physical memory resource\n");
        return -ENODEV;
    }
    slim_io = request_mem_region(slim_mem->start, resource_size(slim_mem),
                                 pdev->name);
    if (!slim_io) {
        dev_err(&pdev->dev, LOGTAG"slimbus memory already claimed\n");
        return -EBUSY;
    }

    irq = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
                                       "slimbus_irq");
    if (!irq) {
        dev_err(&pdev->dev, LOGTAG"no slimbus IRQ resource\n");
        ret = -ENODEV;
        goto err_get_res_failed;
    }

    dev = kzalloc(sizeof(struct jlq_slim_ctrl), GFP_KERNEL);
    if (!dev) {
        ret = -ENOMEM;
        goto err_get_res_failed;
    }
    dev->wr_comp = kzalloc(sizeof(struct completion *) * JLQ_SLIM_TX_BUFS,
                           GFP_KERNEL);
    if (!dev->wr_comp)
        return -ENOMEM;
    dev->dev = &pdev->dev;
    platform_set_drvdata(pdev, dev);
    slim_set_ctrldata(&dev->ctrl, dev);
    dev->base = ioremap(slim_mem->start, resource_size(slim_mem));
    if (!dev->base) {
        dev_err(&pdev->dev, LOGTAG"IOremap failed\n");
        ret = -ENOMEM;
        goto err_ioremap_failed;
    }

    if (pdev->dev.of_node) {

        ret = of_property_read_u32(pdev->dev.of_node, "cell-index",
                                   &dev->ctrl.nr);
        if (ret) {
            dev_err(&pdev->dev, LOGTAG"Cell index unspecified:%d\n", ret);
            goto err_of_init_failed;
        }

    } else {
        dev->ctrl.nr = pdev->id;
    }
    dev->ctrl.nchans = JLQ_SLIM_NCHANS;
    dev->ctrl.nports = JLQ_SLIM_NPORTS;
    dev->ctrl.set_laddr = jlq_set_laddr;
    dev->ctrl.xfer_msg = jlq_xfer_msg;
    dev->ctrl.wakeup =  jlq_clk_pause_wakeup;
    dev->ctrl.alloc_port = NULL;
    dev->ctrl.dealloc_port = NULL;
    dev->ctrl.port_xfer = NULL;
    dev->ctrl.port_xfer_status = NULL;
    /* Reserve some messaging BW for satellite-apps driver communication */
    dev->ctrl.sched.pending_msgsl = 30;

    init_completion(&dev->rx_msgq_notify);
    init_completion(&dev->reconf);
    mutex_init(&dev->tx_lock);
    spin_lock_init(&dev->rx_lock);
    dev->reconf_busy = false;

    dev->irq = irq->start;

    /* Fire up the Rx message queue thread */
    dev->rx_msgq_thread = kthread_run(jlq_slim_rx_msgq_thread, dev,
                                      JLQ_SLIM_NAME "_rx_msgq_thread");
    if (IS_ERR(dev->rx_msgq_thread)) {
        ret = PTR_ERR(dev->rx_msgq_thread);
        dev_err(dev->dev, LOGTAG"Failed to start Rx message queue thread\n");
        goto err_thread_create_failed;
    }

    dev->framer.rootfreq = SLIM_ROOT_FREQ >> 1;  //SLIM_ROOT_FREQ >> 3;
    dev->framer.superfreq = dev->framer.rootfreq / SLIM_CL_PER_SUPERFRAME_DIV8;
    
    dev->ctrl.a_framer = &dev->framer;
    dev->ctrl.clkgear = SLIM_MAX_CLK_GEAR;  /*510, don't support CG10*/
    dev->ctrl.dev.parent = &pdev->dev;
    dev->ctrl.dev.of_node = pdev->dev.of_node;

    ret = request_threaded_irq(dev->irq, NULL, jlq_slim_interrupt,
                               IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
                               "jlq_slim_irq", dev);
    if (ret) {
        dev_err(&pdev->dev, LOGTAG"request IRQ failed\n");
        goto err_request_irq_failed;
    }

    pr_info(LOGTAG"slimbus irq request succ(%d)\n", dev->irq);


    /* Register with framework before enabling frame, clock */
    ret = slim_add_numbered_controller(&dev->ctrl);
    if (ret) {
        dev_err(dev->dev, LOGTAG"error adding controller\n");
        goto err_ctrl_failed;
    }

    /*enable slimbus clk*/
    /*hclk*/
    pr_info(LOGTAG"enable slimbus_hclk\n");
    dev->hclk = clk_get(dev->dev, "slimbus_hclk");
    if (IS_ERR(dev->hclk))
        pr_info(LOGTAG"check slimbus_hclk, error\n");
    else {
        freq = clk_get_rate(dev->hclk);
        pr_info(LOGTAG"check slimbus_hclk, %ld\n", freq);
    }
    clk_prepare_enable(dev->hclk);
    pr_info(LOGTAG"clk_prepare_enable(slimbus_hclk) ok\n");

    /*frm clk*/
    pr_info(LOGTAG"enable slimbus_frm_clk_i\n");
    dev->rclk = clk_get(dev->dev, "slimbus_frm_clk_i");
    if (IS_ERR(dev->rclk))
        pr_info(LOGTAG"check slimbus_frm_clk_i, error\n");
    else {
        freq = clk_get_rate(dev->rclk);
        pr_info(LOGTAG"check slimbus_frm_clk_i, %ld\n", freq);
    }
    clk_prepare_enable(dev->rclk);
    pr_info(LOGTAG"clk_prepare_enable(slimbus_frm_clk_i) ok\n");

    /*cport clk*/
    pr_info(LOGTAG"enable slimbus_cport_clk_i\n");
    dev->cclk = clk_get(dev->dev, "slimbus_cport_clk_i");
    if (IS_ERR(dev->cclk))
        pr_info(LOGTAG"check slimbus_cport_clk_i, error\n");
    else {
        freq = clk_get_rate(dev->cclk);
        pr_info(LOGTAG"check slimbus_cport_clk_i, %ld\n", freq);
    }
    clk_prepare_enable(dev->cclk);
    pr_info(LOGTAG"clk_prepare_enable(slimbus_cport_clk_i) ok\n");


    /*jr510 slimbus ctrl enable*/
    ctrl_node = of_find_compatible_node(NULL, NULL, "jlq,slimbus_ctrl");
    if (!ctrl_node) {
        pr_err(LOGTAG"jlq,slimbus_ctrl, No compatible node found\n");
        return -ENODEV;
    }

#if DEBUG_CHECK_INFO
    /*check info*/
    {
        struct resource res;

        if (of_address_to_resource(ctrl_node, 0, &res))
            return -ENODEV;

        pr_info(LOGTAG"slimbus ctrl_regbase, start[paddr]: 0x%x, end: 0x%x, name:%s\n",
                (unsigned int)res.start, (unsigned int)res.end, res.name);
    }
#endif

    ctrl_regbase = of_iomap(ctrl_node, 0);
    if (!ctrl_regbase) {
        pr_err(LOGTAG"jlq,slimbus_ctrl is NULL\n");
        return -ENODEV;
    }
    of_node_put(ctrl_node);

    /* Add devices registered with board-info now that controller is up */
    slim_ctrl_add_boarddevs(&dev->ctrl);

    if (pdev->dev.of_node)
        of_register_slim_devices(&dev->ctrl);
    else
        pr_info(LOGTAG"pdev->dev.of_node is null\n");

    /*slim ctrl dev*/
    slim_ctrl_dev = dev;

    pr_info(LOGTAG"JR510 SLIMBUS controller is up!\n");

    //pm_runtime_use_autosuspend(&pdev->dev);
    //pm_runtime_set_autosuspend_delay(&pdev->dev, JLQ_SLIM_AUTOSUSPEND);
    //pm_runtime_set_active(&pdev->dev);

#ifdef WCX_ENABLE
    wcx_enable();
#endif

    /*init slimbus regbase*/
    m_slimbus_SBBase_set(dev->base, ctrl_regbase);

    /*init mtdi regbase*/
    m_MTDR_regBase_set((uintptr_t)dev->base);

    /*jr510 slimbus dereset*/
    m_slimbus_enable(ctrl_regbase);
    pr_info(LOGTAG"dereset slimbus ok\n");

#if DEBUG_CHECK_INFO
    /*check info*/
    {
        unsigned int v32;

        pr_info(LOGTAG"try to r/w CONFIG_EA\n");

        /*CONFIG_MODE*/
        v32 = readl(dev->base + 0);
        pr_info(LOGTAG"slimbus CONFIG_MODE: 0x%.8x\n", v32);

        /*CONFIG_EA*/
        v32 = readl(dev->base + 4);
        pr_info(LOGTAG"slimbus CONFIG_EA: 0x%.8x\n", v32);
        v32 = (v32 & 0xffff0000) | MTDR_ProductId;
        writel(v32, dev->base + 4);
        pr_info(LOGTAG"slimbus CONFIG_EA(modified): 0x%.8x\n", v32);
    }
#endif

    pr_info(LOGTAG"jlq_slim_probe(slim ctrl) ok\n");

    /*start slimbus master*/
    slim_devs_init();
    init_completion(&dev->slim_master_notify);
    dev->slim_master_thread_task = kthread_run(slim_master_thread, dev,
                                   "slim_master_thread");
    if (IS_ERR(dev->slim_master_thread_task)) {
        ret = PTR_ERR(dev->slim_master_thread_task);
        dev_err(dev->dev, LOGTAG"Failed to start slim_master_thread_task\n");
        goto err_thread_create_failed;
    }

    return 0;

err_ctrl_failed:
err_request_irq_failed:
    kthread_stop(dev->rx_msgq_thread);
err_thread_create_failed:
    if (dev->hclk) {
        clk_disable_unprepare(dev->hclk);
        clk_put(dev->hclk);
    }
    if (dev->rclk) {
        clk_disable_unprepare(dev->rclk);
        clk_put(dev->rclk);
    }
    if (dev->cclk) {
        clk_disable_unprepare(dev->cclk);
        clk_put(dev->cclk);
    }
err_of_init_failed:
    iounmap(dev->base);
err_ioremap_failed:
    kfree(dev->wr_comp);
    kfree(dev);
err_get_res_failed:
    release_mem_region(slim_mem->start, resource_size(slim_mem));
    pr_info(LOGTAG"jlq_slim_probe fail\n");
    return ret;
}

static int jlq_slim_remove(struct platform_device *pdev)
{
    struct jlq_slim_ctrl *dev = platform_get_drvdata(pdev);

    struct resource *slim_mem;
    struct resource *slew_mem = dev->slew_mem;

    pm_runtime_disable(&pdev->dev);
    pm_runtime_set_suspended(&pdev->dev);
    free_irq(dev->irq, dev);
    slim_del_controller(&dev->ctrl);

    if (dev->rclk)
        clk_put(dev->rclk);
    if (dev->hclk)
        clk_put(dev->hclk);
    if (dev->cclk)
        clk_put(dev->cclk);

    kthread_stop(dev->rx_msgq_thread);

    iounmap(dev->base);
    kfree(dev->wr_comp);
    kfree(dev);

    if (slew_mem)
        release_mem_region(slew_mem->start, resource_size(slew_mem));
    slim_mem = platform_get_resource_byname(pdev, IORESOURCE_MEM,
                                            "slimbus_physical");
    if (slim_mem)
        release_mem_region(slim_mem->start, resource_size(slim_mem));
    return 0;
}


#ifdef CONFIG_PM
static int jlq_slim_runtime_idle(struct device *device)
{
    struct platform_device *pdev = to_platform_device(device);
    struct jlq_slim_ctrl *dev = platform_get_drvdata(pdev);

    if (dev->state == JLQ_CTRL_AWAKE)
        dev->state = JLQ_CTRL_IDLE;
    dev_dbg(device, "pm_runtime: idle...\n");
    pm_request_autosuspend(device);
    return -EAGAIN;
}

/*
 * If PM_RUNTIME is not defined, these 2 functions become helper
 * functions to be called from system suspend/resume. So they are not
 * inside ifdef CONFIG_PM_RUNTIME
 */
static int jlq_slim_runtime_suspend(struct device *device)
{
    struct platform_device *pdev = to_platform_device(device);
    struct jlq_slim_ctrl *dev = platform_get_drvdata(pdev);
    int ret;

    dev_dbg(device, "pm_runtime: suspending...\n");
    ret = slim_ctrl_clk_pause(&dev->ctrl, false, SLIM_CLK_UNSPECIFIED);
    if (ret) {
        dev_err(device, "clk pause not entered:%d\n", ret);
        dev->state = JLQ_CTRL_AWAKE;
    } else {
        dev->state = JLQ_CTRL_ASLEEP;
    }
    return ret;
}

static int jlq_slim_runtime_resume(struct device *device)
{
    struct platform_device *pdev = to_platform_device(device);
    struct jlq_slim_ctrl *dev = platform_get_drvdata(pdev);
    int ret = 0;

    dev_dbg(device, "pm_runtime: resuming...\n");
    if (dev->state == JLQ_CTRL_ASLEEP)
        ret = slim_ctrl_clk_pause(&dev->ctrl, true, 0);
    if (ret) {
        dev_err(device, "clk pause not exited:%d\n", ret);
        dev->state = JLQ_CTRL_ASLEEP;
    } else {
        dev->state = JLQ_CTRL_AWAKE;
    }
    return ret;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int jlq_slim_suspend(struct device *dev)
{
    int ret = -EBUSY;
    struct platform_device *pdev = to_platform_device(dev);
    struct jlq_slim_ctrl *cdev = platform_get_drvdata(pdev);

    if (!pm_runtime_enabled(dev) ||
        (!pm_runtime_suspended(dev) &&
         cdev->state == JLQ_CTRL_IDLE)) {
        dev_dbg(dev, "system suspend");
        ret = jlq_slim_runtime_suspend(dev);
        if (!ret) {
            if (cdev->hclk)
                clk_disable_unprepare(cdev->hclk);
        }
    }
    if (ret == -EBUSY) {
        /*
         * If the clock pause failed due to active channels, there is
         * a possibility that some audio stream is active during suspend
         * We dont want to return suspend failure in that case so that
         * display and relevant components can still go to suspend.
         * If there is some other error, then it should be passed-on
         * to system level suspend
         */
        ret = 0;
    }
    return ret;
}

static int jlq_slim_resume(struct device *dev)
{
    /* If runtime_pm is enabled, this resume shouldn't do anything */
    if (!pm_runtime_enabled(dev) || !pm_runtime_suspended(dev)) {
        struct platform_device *pdev = to_platform_device(dev);
        struct jlq_slim_ctrl *cdev = platform_get_drvdata(pdev);
        int ret;

        dev_dbg(dev, "system resume");
        if (cdev->hclk)
            clk_prepare_enable(cdev->hclk);
        ret = jlq_slim_runtime_resume(dev);
        if (!ret) {
            pm_runtime_mark_last_busy(dev);
            pm_request_autosuspend(dev);
        }
        return ret;

    }
    return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops jlq_slim_dev_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(
        jlq_slim_suspend,
        jlq_slim_resume
    )
    SET_RUNTIME_PM_OPS(
        jlq_slim_runtime_suspend,
        jlq_slim_runtime_resume,
        jlq_slim_runtime_idle
    )
};

static const struct of_device_id jlq_slim_dt_match[] = {
    {
        .compatible = "jlq,slim-controller",
    },
    {}
};

static struct platform_driver jlq_slim_driver = {
    .probe = jlq_slim_probe,
    .remove = jlq_slim_remove,
    .driver	= {
        .name = JLQ_SLIM_NAME,
        //.pm = &jlq_slim_dev_pm_ops,
        .of_match_table = jlq_slim_dt_match,
    },
};

static int jlq_slim_init(void)
{
    return platform_driver_register(&jlq_slim_driver);
}
device_initcall(jlq_slim_init);
//subsys_initcall(jlq_slim_init);


static void jlq_slim_exit(void)
{
    platform_driver_unregister(&jlq_slim_driver);
}
module_exit(jlq_slim_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("JLQ Slimbus controller");
MODULE_ALIAS("platform:jlq-slim");
