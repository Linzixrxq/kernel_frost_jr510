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

#ifndef __SLIM_MASTER_H__
#define __SLIM_MASTER_H__

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
#include <dt-bindings/jlq/jr510/offset-audio_sysctrl.h>

#include "slim_inc.h"
#include "slim_mtdr_if.h"
#include "slim_chk.h"
#include "slim_regs.h"


/*slimbus IP regs offset*/
/*Configuration*/
#define SB_CONFIG_MODE      0x0000  //0x00000400  R/W Configuration 每 Mode
#define SB_CONFIG_EA        0x0004  //0x0000( PRODUCT_ID ) R/W Configuration 每 Enumeration Address, part 1
#define SB_CONFIG_PR_TP     0x0008  //0x07FFFFFF  R/W Configuration 每 Generic device
#define SB_CONFIG_FR        0x000C  //0x0000FFFF  R/W Configuration 每 Framer device
#define SB_CONFIG_DPORT     0x0010  //0x00000600  R/W Configuration 每 Data Ports
#define SB_CONFIG_CPORT     0x0014  //0x00000000  R/W Configuration 每 Control Port
#define SB_CONFIG_EA2       0x0018  //0x00010200  R/W Configuration 每 Enumeration Address, part 2
#define SB_CONFIG_THR       0x001c  //0x003F0000  R/W Configuration 每 Data Port DMA thresholds

/*Command and status*/
#define SB_COMMAND          0x0020  //0x00000000  W  Command register
#define SB_STATE            0x0024  //0x00000000  R  State register
#define SB_IE_STATE         0x0028  //0x00000000  R  Information Element Status
#define SB_MCH_USAGE        0x002c  //0x05EE0000  R/W Message Channel Usage

/*Interrupt mask and status*/
#define SB_INT_EN           0x0038  //0x00000000  R/W Interrupt Enable
#define SB_INT              0x003C  //0x00000000  R/C Interrupt Status

/*Message FIFOs*/
#define SB_MC_FIFO          0x0040  //16 words 0x0040-0x007C RX_FIFO TX_FIFO

/*Port interrupt mask and status*/
#define SB_P_INT_EN         0x0080 //16 words 0x0080-0X00BC Port Interrupt Enable registers + Presence Rate Generation Enable
#define SB_P_INT            0x00C0 //16 words 0x00C0-0x00FC Port Interrupt Status registers

/*Port configuration state*/
#define SB_P_STATE_0        0x0100 //64 words 0x0100-0x02F8 Port (channel) configuration status registers, part 1
#define SB_P_STATE_1        0x0104 //64 words 0x0104-0x02FC Port (channel) configuration status registers, part 2

/*Port configuration state, Only in configuration when Data Ports FIFOs are accessible though AHB*/
#define SB_P_FIFO           0x1000 //64 x 16 words 0x1000-0x1FFC Data Port FIFOs.There is 64-byte space reserved for each data port.


/*SB Manager define*/
#define SBMGR_RX_FIFO_DEPTH         128


/*Enumeration Address*/
#define MTDR_Manufacturer_ID    0x0445  /*JLQ MI*/
#define MTDR_ProductId          0xA510  /*JR510 PC*/

/*DI*/
#define MTDR_InterfaceId        0x00  /*class: fd*/
#define MTDR_FramerId           0x01  /*class: fe*/
#define MTDR_GenericId          0x02  /*class: 00*/

/*IV*/
#define MTDR_InstanceValue      0x00    /*IV*/


/*
interface EA:   0445 a5100000
framer EA:      0445 a5100100
generic EA:     0445 a5100200
*/

/*Logic Address prefix*/
#define LA_PREFIX_FRAMER_DEVICE             0x20
#define LA_PREFIX_INTERFACE_DEVICE          0x40
#define LA_PREFIX_MASTER_GENERIC_DEVICE     0xA0   /*for jr510 slimbus IP's generic device*/
#define LA_PREFIX_SLAVE_GENERIC_DEVICE      0xB0   /*for qcom wcn39xx's generic device*/


extern void __iomem *SBRegBase;
extern void __iomem *SBCtrlBase;


/*dport address*/
//#define DPort0_FIFO_ADDR  (MTDR_REGS_BASE+0x1000)
//#define DPort1_FIFO_ADDR  (MTDR_REGS_BASE+0x1040)
//#define DPort2_FIFO_ADDR  (MTDR_REGS_BASE+0x1080)
//#define DPort3_FIFO_ADDR  (MTDR_REGS_BASE+0x10C0)
/*
struct slimbus_chip
{
	const char          *name;
	struct device		*dev;
	struct device_node  *of_node;
	void __iomem		*regbase;
	unsigned int		irq;
	void __iomem		*ctrl_regbase;
};
*/

/*slimbus operation properties*/
typedef struct {
    MTDR_RootFrequency rootFrequency;
    MTDR_ClockGear clockGear;
    MTDR_SubframeMode subframeMode;
} MTDR_BusProperties;

/*List of enumerated devices*/
#define MTDR_MAX_DEVICES     (8)
typedef struct {
    uint8_t devices;
    uint8_t framerCount;
    uint8_t genericCount;
    uint8_t interfaceCount;
    uint8_t managerCount;
    uint8_t framerLa[MTDR_MAX_DEVICES];
    uint8_t genericLa[MTDR_MAX_DEVICES];
    uint8_t interfaceLa[MTDR_MAX_DEVICES];
    uint8_t managerLa[MTDR_MAX_DEVICES];
} MTDR_EnumeratedDevices;


/*DPort*/
typedef enum {
    DPORT_NUM_0 = 0,
    DPORT_NUM_1 = 1,
    DPORT_NUM_2 = 2,
    DPORT_NUM_3 = 3,
    DPORT_MAX_NUM
} DPORT_INDEX;

/*Data Channel Endpoints*/
typedef struct {
    uint8_t logicalAddress;
    uint8_t portNumber;
} MTDR_DataEndpoint;

/*Data Channel properties*/
typedef struct {
    uint8_t channelNumber;
    MTDR_TransportProtocol transportProtocol;
    uint16_t segmentDistribution;
    uint8_t segmentLength;
    bool frequencyLockedBit;
    MTDR_PresenceRate presenceRate;
    MTDR_AuxFieldFormat auxiliaryBitFormat;
    MTDR_DataType dataType;
    bool channelLink;
    uint8_t dataLength;
    MTDR_DataEndpoint source;
    MTDR_DataEndpoint sinks[16];
    uint8_t sinksNum;
    bool active;
} MTDR_ChannelProperties;


extern MTDR_Config mtdiConfig;
extern MTDR_FramerConfig mtdiFramerConfig;
extern MTDR_GenericDeviceConfig mtdiDeviceConfig;
extern MTDR_BusProperties mtdiBus;

void m_slimbus_delay(uint32_t cycles);

void m_slimbus_SBBase_set(void __iomem		 *_SBRegBase, void __iomem *_SBCtrlBase);
void m_slimbus_enable(void __iomem *ctrl_regbase);

void m_MTDR_regBase_set(uintptr_t base);
int m_slimbus_start(void);

void m_slimbus_DataPortInterruptsHandler(uint8_t dataPortNumber, MTDR_DataPortInterrupt dataPortInterrupt);
int m_slimbus_reconfigBus(MTDR_BusProperties *bus);

int slim_master_thread(void *data);

int master_slimbus_state_get(void);


#endif
