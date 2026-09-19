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
#include <linux/delay.h>
#include <linux/slimbus/slimbus.h>
#include <linux/of_slimbus.h>
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
#include "slim_api.h"

/*master slimbus state*/
static int master_slim_state = MASTER_SLIM_STAT_UNINIT;


void __iomem *SBRegBase = NULL;
void __iomem *SBCtrlBase = NULL;


/*CSMI generic configuration*/
MTDR_Config mtdiConfig = {
    //.regBase = MTDR_REGS_BASE,
    .snifferMode = 0,
    .enableFramer = 1,
    .enableDevice = 1,
    .retryLimit = 3,
    .reportAtEvent = 0,
    .disableHardwareCrcCalculation = 0,
    .limitReports = 0,
    .eaProductId = MTDR_ProductId,
    .eaInstanceValue = MTDR_InstanceValue,
    .eaInterfaceId = MTDR_InterfaceId,
    .eaGenericId = MTDR_GenericId,
    .eaFramerId = MTDR_FramerId,
    .enumerateDevices = 1,
};

/*Framer device configuration*/
MTDR_FramerConfig mtdiFramerConfig = {
    .rootFrequenciesSupported = 0xFFFF, //510, 24.576, 0x01  //0xFFFF,
    .quality = MTDR_FQ_LOW_JITTER,
    .pauseAtRootFrequencyChange = 0,
};

/*Generic device configuration*/
MTDR_GenericDeviceConfig mtdiDeviceConfig = {
    .presenceRatesSupported = 0xFFFFFF,
    .transportProtocolIsochronous = 1,
    .transportProtocolPushed = 1,
    .transportProtocolPulled = 1,
    .sinkStartLevel = 0,
    .dataPortClockPrescaler = 2,
    .cportClockDivider = 0,
    .referenceClockSelector = MTDR_RC_CLOCK_GEAR_6,  /*510 don't care this*/
    .dmaTresholdSource = 32,
    .dmaTresholdSink = 0, //0x0f,    /*510's dataport total depth is 64*/
};

/*bus configuration*/
MTDR_BusProperties mtdiBus = {
    .rootFrequency = MTDR_RF_1,
    .clockGear = MTDR_CG_9,
    .subframeMode = MTDR_SM_3_CSW_24_SL,  //MTDR_SM_3_CSW_32_SL,
};

/*channels configuration*/
enum MTDR_CHS_INDEX {
    MTDR_CHS_ID_0 = 0,  /*M_BT_SCO_SLIM_RX*/
    MTDR_CHS_ID_1,  /*M_FM_SLIM_RX*/
    MTDR_CHS_ID_2,  /*M_FM_SLIM_RX*/
    MTDR_CHS_ID_3,  /*M_BT_SCO_SLIM_TX*/
    MTDR_CHS_ID_4,  /*master loopback*/
    MTDR_CHS_ID_5,  /*master/slave loopback*/
    MTDR_CHS_ID_6,  /*master/slave loopback*/
    MTDR_CHS_NUMBER
};

MTDR_ChannelProperties mtdiChannles[MTDR_CHS_NUMBER] = {
    /*M_BT_SCO_SLIM_RX*/
    [MTDR_CHS_ID_0] = {
        //.channelNumber			 = ,
        .segmentLength			 = 6,
        .segmentDistribution	 = 0x208,
        .transportProtocol		 = MTDR_TP_PUSHED,
        .frequencyLockedBit 	 = 0,
        .presenceRate			 = MTDR_PR_4K,
        .auxiliaryBitFormat 	 = MTDR_AF_NOT_APPLICABLE,
        .channelLink			 = 0,
        .dataType				 = MTDR_DF_NOT_INDICATED,
        .dataLength 			 = 4,
        .active 				 = 0,
    },

    /*M_FM_SLIM_RX*/
    [MTDR_CHS_ID_1] = {
        .segmentLength           = 4,
        .segmentDistribution     = 0x208,
        .transportProtocol       = MTDR_TP_ISOCHRONOUS,
        .frequencyLockedBit      = 0,
        .presenceRate            = MTDR_PR_48K,
        .auxiliaryBitFormat      = MTDR_AF_NOT_APPLICABLE,
        .channelLink             = 0,
        .dataType                = MTDR_DF_NOT_INDICATED,
        .dataLength              = 4,
        .active                  = 0,
    },

    /*M_FM_SLIM_RX*/
    [MTDR_CHS_ID_2] = {
        .segmentLength           = 4,
        .segmentDistribution     = 0x208,
        .transportProtocol       = MTDR_TP_ISOCHRONOUS,
        .frequencyLockedBit      = 0,
        .presenceRate            = MTDR_PR_48K,
        .auxiliaryBitFormat      = MTDR_AF_NOT_APPLICABLE,
        .channelLink             = 0,
        .dataType                = MTDR_DF_NOT_INDICATED,
        .dataLength              = 4,
        .active                  = 0,
    },

    /*M_BT_SCO_SLIM_TX*/
    [MTDR_CHS_ID_3] = {
        .segmentLength			 = 6,
        .segmentDistribution	 = 0x208,
        .transportProtocol		 = MTDR_TP_PUSHED,
        .frequencyLockedBit 	 = 0,
        .presenceRate			 = MTDR_PR_4K,
        .auxiliaryBitFormat 	 = MTDR_AF_NOT_APPLICABLE,
        .channelLink			 = 0,
        .dataType				 = MTDR_DF_NOT_INDICATED,
        .dataLength 			 = 4,
        .active 				 = 0,
    },

#if MASTER_LOOPBACK_TEST
    /*master loopback test*/
    [MTDR_CHS_ID_4] = {
        .segmentLength           = 4,
        .segmentDistribution     = 0x208,
        .transportProtocol       = MTDR_TP_ISOCHRONOUS,
        .frequencyLockedBit      = 0,
        .presenceRate            = MTDR_PR_48K,
        .auxiliaryBitFormat      = MTDR_AF_NOT_APPLICABLE,
        .channelLink             = 0,
        .dataType                = MTDR_DF_NOT_INDICATED,
        .dataLength              = 4,
        .active                  = 0,
    },
#endif

#if MASTER_SLAVE_LOOPBACK_TEST
    [MTDR_CHS_ID_5] = {
        .segmentLength			 = 6,
        .segmentDistribution	 = 0x208,
        .transportProtocol		 = MTDR_TP_PUSHED,
        .frequencyLockedBit 	 = 0,
        .presenceRate			 = MTDR_PR_4K,
        .auxiliaryBitFormat 	 = MTDR_AF_NOT_APPLICABLE,
        .channelLink			 = 0,
        .dataType				 = MTDR_DF_NOT_INDICATED,
        .dataLength 			 = 4,
        .active 				 = 0,
    },

    [MTDR_CHS_ID_6] = {
        .segmentLength			 = 6,
        .segmentDistribution	 = 0x208,
        .transportProtocol		 = MTDR_TP_PUSHED,
        .frequencyLockedBit 	 = 0,
        .presenceRate			 = MTDR_PR_4K,
        .auxiliaryBitFormat 	 = MTDR_AF_NOT_APPLICABLE,
        .channelLink			 = 0,
        .dataType				 = MTDR_DF_NOT_INDICATED,
        .dataLength 			 = 4,
        .active 				 = 0,
    },
#endif

};


struct mslim_ch {
    int id;
    char *name;
    uint32_t port_hdl;	/* slimbus port handler */
    uint16_t port;		/* slimbus port number */

    uint8_t ch;		/* slimbus channel number */
    uint16_t ch_hdl;	/* slimbus channel handler */
    uint16_t grph;	/* slimbus group channel handler */
};

struct mslim {
    struct device *dev;
    struct slim_device *slim_pgd;
    struct slim_device *slim_ifd;
    struct mutex io_lock;
    struct mutex xfer_lock;
    uint8_t enabled;

    uint32_t num_rx_port;
    uint32_t num_tx_port;
    uint32_t sample_rate;

    struct mslim_ch *rx_chs;
    struct mslim_ch *tx_chs;

    int (*vendor_init)(struct mslim *mslim);
    int (*vendor_port_en)(struct mslim *mslim, uint8_t port_num, uint8_t rxport, uint8_t enable);
};

struct mslim_ch_para {
    int id;
    uint16_t port;
    uint8_t rxport;
    MTDR_ChannelProperties *prop;
};

/* according by btfm defines */
enum {
    M_FM_SLIM_RX = 0,
    M_BT_SCO_SLIM_RX,
    M_BT_SCO_SLIM_TX,
#if MASTER_LOOPBACK_TEST
    M_LOOPBACK_RX,
    M_LOOPBACK_TX,
#endif
#if MASTER_SLAVE_LOOPBACK_TEST
    M_S_LOOPBACK_RX,
    M_S_LOOPBACK_TX,
#endif

    M_SLIM_NUM_CODEC_DAIS
};

/* Slimbus Port defines - This should be redefined in specific device file */
#define M_SLIM_PGD_PORT_LAST			0xFF

/* PGD Port Map */
#define M_SB_PGD_PORT_RX_SCO			DPORT_NUM_3     //slave dport0
#define M_SB_PGD_PORT_RX1_FM			DPORT_NUM_1     //slave dport1
#define M_SB_PGD_PORT_RX2_FM			DPORT_NUM_2     //slave dport2
#define M_SB_PGD_PORT_TX_SCO			DPORT_NUM_0     //slave dport16
#if MASTER_LOOPBACK_TEST
#define M_SB_PGD_PORT_RX_LOOPBACK       DPORT_NUM_3
#define M_SB_PGD_PORT_TX_LOOPBACK       DPORT_NUM_0
#endif
#if MASTER_SLAVE_LOOPBACK_TEST
#define M_S_DPORT_RX_LOOPBACK           DPORT_NUM_3
#define M_S_DPORT_TX_LOOPBACK           DPORT_NUM_0
#endif

#define MASTER_SB_PGD_PORT_RX_NUM			3
#define MASTER_SB_PGD_PORT_TX_NUM			1


/* btfm ch, SLAVE (WCN3950) Port assignment */
static struct mslim_ch master_rxport[] = {
    {
        .id = M_BT_SCO_SLIM_RX, .name = "SCO_Rx",
        .port = M_SB_PGD_PORT_RX_SCO
    },
    {
        .id = M_FM_SLIM_RX, .name = "FM_Rx1",
        .port = M_SB_PGD_PORT_RX1_FM
    },
    {
        .id = M_FM_SLIM_RX, .name = "FM_Rx2",
        .port = M_SB_PGD_PORT_RX2_FM
    },

#if MASTER_LOOPBACK_TEST
    {
        .id = M_LOOPBACK_RX, .name = "LOOPBACK_Rx",
        .port = M_SB_PGD_PORT_RX_LOOPBACK
    },
#endif

#if MASTER_SLAVE_LOOPBACK_TEST
    {
        .id = M_S_LOOPBACK_RX, .name = "M_S_LOOPBACK_RX",
        .port = M_S_DPORT_RX_LOOPBACK
    },
#endif


    {
        .id = M_SLIM_NUM_CODEC_DAIS, .name = "",
        .port = M_SLIM_PGD_PORT_LAST},
};

static struct mslim_ch master_txport[] = {
    {
        .id = M_BT_SCO_SLIM_TX, .name = "SCO_Tx",
        .port = M_SB_PGD_PORT_TX_SCO
    },

#if MASTER_LOOPBACK_TEST
    {
        .id = M_LOOPBACK_TX, .name = "LOOPBACK_Tx",
        .port = M_SB_PGD_PORT_TX_LOOPBACK
    },
#endif

#if MASTER_SLAVE_LOOPBACK_TEST
    {
        .id = M_S_LOOPBACK_TX, .name = "M_S_LOOPBACK_TX",
        .port = M_S_DPORT_TX_LOOPBACK
    },
#endif


    {
        .id = M_SLIM_NUM_CODEC_DAIS, .name = "",
        .port = M_SLIM_PGD_PORT_LAST},
};


static struct mslim_ch_para master_channel_paras[] = {
    {
        .id = M_BT_SCO_SLIM_RX,
        .port = M_SB_PGD_PORT_RX_SCO,
        .rxport = 1,
        .prop = &mtdiChannles[0]
    },
    {
        .id = M_FM_SLIM_RX,
        .port = M_SB_PGD_PORT_RX1_FM,
        .rxport = 1,
        .prop = &mtdiChannles[1]
    },
    {
        .id = M_FM_SLIM_RX,
        .port = M_SB_PGD_PORT_RX2_FM,
        .rxport = 1,
        .prop = &mtdiChannles[2]
    },
    {
        .id = M_BT_SCO_SLIM_TX,
        .port = M_SB_PGD_PORT_TX_SCO,
        .rxport = 0,
        .prop = &mtdiChannles[3]
    },

#if MASTER_LOOPBACK_TEST
    {
        .id = M_LOOPBACK_RX,
        .port = M_SB_PGD_PORT_RX_LOOPBACK,
        .rxport = 1,
        .prop = &mtdiChannles[4]
    },
    {
        .id = M_LOOPBACK_TX,
        .port = M_SB_PGD_PORT_TX_LOOPBACK,
        .rxport = 0,
        .prop = &mtdiChannles[4]
    },
#endif

#if MASTER_SLAVE_LOOPBACK_TEST
    {
        .id = M_S_LOOPBACK_RX,
        .port = M_SB_PGD_PORT_RX_LOOPBACK,
        .rxport = 1,
        .prop = &mtdiChannles[5]
    },
    {
        .id = M_S_LOOPBACK_TX,
        .port = M_SB_PGD_PORT_TX_LOOPBACK,
        .rxport = 0,
        .prop = &mtdiChannles[6]
    },

#endif

    {
        .id = M_SLIM_NUM_CODEC_DAIS,
    },
};

#define SLIM_MASTER_RXPORT (&master_rxport[0])
#define SLIM_MASTER_TXPORT (&master_txport[0])

/*sound slot, ->map to btfm ch, ->map to slimbus ch. from bengal_tdm_snd_hw_params*/
#if MASTER_SLAVE_NORMAL_VERSION
static unsigned int snd_slot_offset[8] = {157, 159, 160, 161};
static unsigned int *rx_slot = &snd_slot_offset[1];
static unsigned int *tx_slot = &snd_slot_offset[0];
#define MASTER_SB_CHS_RX_NUM			3
#define MASTER_SB_CHS_TX_NUM			1
#endif

#if MASTER_SLAVE_NORMAL_TEST  /*normal test with slave*/
static unsigned int snd_slot_offset[8] = {0, 4, 8, 12, 16, 20, 24, 28};
static unsigned int *rx_slot = &snd_slot_offset[1];
static unsigned int *tx_slot = &snd_slot_offset[5];
#define MASTER_SB_CHS_RX_NUM			3
#define MASTER_SB_CHS_TX_NUM			1
#endif

#if MASTER_LOOPBACK_TEST  /*for master loopback test*/
static unsigned int snd_slot_offset[16] = {0, 4, 8, 12, 16, 20, 16, 24, 28};
static unsigned int *rx_slot = &snd_slot_offset[1];
static unsigned int *tx_slot = &snd_slot_offset[5];
#define MASTER_SB_CHS_RX_NUM			4
#define MASTER_SB_CHS_TX_NUM			2
#endif

#if MASTER_SLAVE_LOOPBACK_TEST
static unsigned int snd_slot_offset[16] = {24, 28};
static unsigned int *rx_slot = &snd_slot_offset[0];
static unsigned int *tx_slot = &snd_slot_offset[1];
#define MASTER_SB_CHS_RX_NUM			4
#define MASTER_SB_CHS_TX_NUM			2
#endif


/*delay*/
void m_slimbus_delay(uint32_t cycles)
{
    uint32_t i;
    for (i = 0; i < cycles; i++) {
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
        __asm__ volatile("nop");
    }
}

void m_slimbus_SBBase_set(void __iomem		 *_SBRegBase, void __iomem *_SBCtrlBase)
{
    SBRegBase = _SBRegBase;
    SBCtrlBase = _SBCtrlBase;
}

void m_slimbus_enable(void __iomem *ctrl_regbase)
{
    if(IS_ERR_OR_NULL(ctrl_regbase))
        return;

    //writel(0x07, ctrl_regbase+SLIMBUS_CLK_CTRL-0x100);    //enable sb clk

    PR_INFO(LOGTAG"dereset slimbus\n");
    writel(0x1f, ctrl_regbase + SLIMBUS_RST_CTRL - 0x100); //de reset sb

    PR_INFO(LOGTAG"setting slimbus mid\n");
    writel(1 << 16 | MTDR_Manufacturer_ID, ctrl_regbase + SLIMBUS_CTRL - 0x100); //set primary framer and mid

    /*if there is some problems, config AHB to 2'b00, now ingore this*/
    //writel(0x00, ctrl_regbase+SLIMBUS_AHB_CTRL-0x100);
}

void m_MTDR_regBase_set(uintptr_t base)
{
    mtdiConfig.regBase = base;
}

int m_slimbus_start(void)
{
    int ret;

    PR_INFO(LOGTAG"m_slimbus_start\n");

    ret = m_MTDR_Init();
    if(ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_Init fail %u\n", ret);
        return -1;
    }

#if DEBUG_CHECK_INFO
    /*check CONFIG_MODE CONFIG_EA CONFIG_EA2 INT_EN*/
    PR_INFO(LOGTAG"CONFIG_MODE: %x\n", readl(SBRegBase + SB_CONFIG_MODE));
    PR_INFO(LOGTAG"CONFIG_EA: %x\n", readl(SBRegBase + SB_CONFIG_EA));
    PR_INFO(LOGTAG"CONFIG_EA2: %x\n", readl(SBRegBase + SB_CONFIG_EA2));
    PR_INFO(LOGTAG"INT_EN: %x\n", readl(SBRegBase + SB_INT_EN));
#endif

    /*check manager, framer and generic enable status*/
    //mdelay(10);
    if((readl(SBRegBase + SB_CONFIG_MODE) & 0xff) != 0x1a) {
        pr_err(LOGTAG"manager, framer and generic enable status error\n");
        return -1;
    }

    ret = m_MTDR_SetFramerConfig();
    if (ret != MTDR_RET_EOK) {
        pr_err(LOGTAG"SetFramerConfig function failed with result: %u\n", ret);
        return ret;
    }
#if DEBUG_CHECK_INFO
    /*check CONFIG_FR*/
    PR_INFO(LOGTAG"CONFIG_FR: %x\n", readl(SBRegBase + SB_CONFIG_FR));
#endif

    ret = m_MTDR_SetGenericDeviceConfig();
    if (ret != MTDR_RET_EOK) {
        pr_err(LOGTAG"SetGenericDeviceConfig function failed with result: %u\n", ret);
        return ret;
    }
#if DEBUG_CHECK_INFO
    /*CONFIG_PR_TP CONFIG_CPORT CONFIG_DPORT CONFIG_THR*/
    PR_INFO(LOGTAG"CONFIG_PR_TP: %x\n", readl(SBRegBase + SB_CONFIG_PR_TP));
    PR_INFO(LOGTAG"CONFIG_CPORT: %x\n", readl(SBRegBase + SB_CONFIG_CPORT));
    PR_INFO(LOGTAG"CONFIG_DPORT: %x\n", readl(SBRegBase + SB_CONFIG_DPORT));
    PR_INFO(LOGTAG"CONFIG_THR: %x\n", readl(SBRegBase + SB_CONFIG_THR));
#endif

    /* Start the Driver and enable Manager and its interrupts */
    ret = m_MTDR_Start();
    if (ret != MTDR_RET_EOK) {
        pr_err(LOGTAG"Start function failed with result: %u\n", ret);
        return ret;
    }

    pr_info(LOGTAG"m_slimbus_start ok\n");
    return ret;
}

void m_slimbus_enableRCFG_INT(void)
{
    MTDR_Config *config = &mtdiConfig;
    MTDR_Instance *instance;
    uint32_t reg;

    instance = (MTDR_Instance *) m_MTDR_pDrvIns_get();
    instance->registerBase = config->regBase;
    instance->registers = (MTDR_Registers *) config->regBase;

    reg = MTDR_ReadReg(INTERRUPTS.INT_EN);

    /*enable rcfg int for rcfg done*/
    INTERRUPTS__INT_EN__RCFG_INT_EN__SET(reg);

    MTDR_WriteReg(INTERRUPTS.INT_EN, reg);
}


void m_slimbus_DataPortInterruptsHandler(uint8_t dataPortNumber, MTDR_DataPortInterrupt dataPortInterrupt)
{
    MTDR_DataPortStatus portstatus;
    MTDR_GenericDeviceConfig deviceConfig;

    uint16_t dmaTresholdSink;

    /*get dataport status*/
    m_MTDR_GetDataPortStatus(dataPortNumber, &portstatus);

    /*get THR*/
    m_MTDR_GetGenericDeviceConfig(&deviceConfig);
    dmaTresholdSink = deviceConfig.dmaTresholdSink;

    //PR_INFO(LOGTAG"MTDR_DataPortInterruptsHandler(int: %d), port: %d, portstatus: %d %d, dmaTresholdSink: %d\n",
    //            dataPortInterrupt, dataPortNumber, portstatus.active, portstatus.sink, dmaTresholdSink);

    /*sink dport, rx*/
    if(dataPortInterrupt & MTDR_DP_INT_DMA) {
        if(portstatus.active && portstatus.sink) {
            /*rx data, now here nothing.*/
            PR_INFO(LOGTAG"MTDR_DP_INT_DMA\n");
        }
    }

    /*other int*/
    if(dataPortInterrupt & MTDR_DP_INT_ACT) {
        PR_INFO(LOGTAG"MTDR_DP_INT_ACT\n");
    }

    if(dataPortInterrupt & MTDR_DP_INT_CON) {
        PR_INFO(LOGTAG"MTDR_DP_INT_CON\n");
    }

    if(dataPortInterrupt & MTDR_DP_INT_CHAN) {
        PR_INFO(LOGTAG"MTDR_DP_INT_CHAN\n");
    }

    if(dataPortInterrupt & MTDR_DP_INT_OVF) {
        PR_INFO(LOGTAG"MTDR_DP_INT_OVF\n");
    }

    if(dataPortInterrupt & MTDR_DP_INT_UND) {
        PR_INFO(LOGTAG"MTDR_DP_INT_UND\n");
    }

    return;
}

int m_slimbus_dPort_init(void)
{
    m_MTDR_SetPresenceRateGeneration(M_SB_PGD_PORT_RX_SCO, 1);  //enable presence
    m_MTDR_ClearDataPortFifo(M_SB_PGD_PORT_RX_SCO);  //clear dport fifo
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_RX_SCO, 0x00);  //disable dport int
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_RX_SCO, 0x07);  //enalbe dport act/con/chan int

    m_MTDR_SetPresenceRateGeneration(M_SB_PGD_PORT_RX1_FM, 1);
    m_MTDR_ClearDataPortFifo(M_SB_PGD_PORT_RX1_FM);
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_RX1_FM, 0x00);
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_RX1_FM, 0x07);

    m_MTDR_SetPresenceRateGeneration(M_SB_PGD_PORT_RX2_FM, 1);
    m_MTDR_ClearDataPortFifo(M_SB_PGD_PORT_RX2_FM);
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_RX2_FM, 0x00);
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_RX2_FM, 0x07);

    m_MTDR_SetPresenceRateGeneration(M_SB_PGD_PORT_TX_SCO, 1);
    m_MTDR_ClearDataPortFifo(M_SB_PGD_PORT_TX_SCO);
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_TX_SCO, 0x00);
    m_MTDR_SetDataPortInterrupts(M_SB_PGD_PORT_TX_SCO, 0x07);

    return 0;
}

/*reconfig slimbus operation properties, if bus is NULL, use default mtdiBus*/
int m_slimbus_reconfigBus(MTDR_BusProperties *bus)
{
    bool sendingFinished;
    MTDR_SubframeMode subframeMode;
    MTDR_RootFrequency rootFrequency;
    MTDR_ClockGear clockGear;
    MTDR_BusProperties *busProp;

    PR_INFO(LOGTAG"m_MTDR_reconfigBus\n");

    if(!bus)
        busProp = &mtdiBus;
    else
        busProp = bus;

    m_MTDR_MsgBeginReconfiguration();
    m_MTDR_MsgNextClockGear(busProp->clockGear);
    m_MTDR_MsgNextSubframeMode(busProp->subframeMode);
    m_MTDR_MsgNextRootFrequency(busProp->rootFrequency);
    m_MTDR_MsgReconfigureNow();

    /*Wait until all messages are sent*/
    do {
        m_MTDR_GetStatusMessages(&sendingFinished);

#if DEBUG_CHECK_INFO
        PR_INFO(LOGTAG"COMMAND_STATUS.STATE: %x\n", readl(SBRegBase + SB_STATE));
#endif

#if DEBUG_ONLY
        break;  //debug only!
#endif

    } while (!sendingFinished);

    mdelay(200);

    /*check reconfig result*/
    m_MTDR_GetStatusSlimbus(&subframeMode, &clockGear, &rootFrequency);

    if ((subframeMode != busProp->subframeMode)
        || (rootFrequency != busProp->rootFrequency)
        || (clockGear != busProp->clockGear)) {
        PR_INFO(LOGTAG"reconfigureBus error!\n");
        PR_INFO(LOGTAG"subframeMode:%d, busProp->subframeMode:%d\n", subframeMode, busProp->subframeMode);
        PR_INFO(LOGTAG"rootFrequency:%d, busProp->rootFrequency:%d\n", rootFrequency, busProp->rootFrequency);
        PR_INFO(LOGTAG"clockGear:%d, busProp->clockGear:%d\n", clockGear, busProp->clockGear);
        return -1;
    }

    PR_INFO(LOGTAG"m_MTDR_reconfigBus, succ\n");

    return 0;
}

int m_slimbus_connectSource(uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber)
{
    uint32_t ret;
    bool sendingFinished;

    PR_INFO(LOGTAG"m_slimbus_connectSource(la(%x), port(%d), Channel(%d))\n", destinationLa, portNumber, channelNumber);

    ret = m_MTDR_MsgConnectSource(destinationLa, portNumber, channelNumber);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_slimbus_connectSource failed: %u\n", ret);
        goto exit;
    }

    /* Wait until all messages are sent */
    do {
        m_MTDR_GetStatusMessages(&sendingFinished);
    } while (!sendingFinished);

exit:
    return ret;
}


int m_slimbus_connectSink(uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber)
{
    uint32_t ret;
    bool sendingFinished;

    PR_INFO(LOGTAG"m_slimbus_connectSink(la(%x), port(%d), Channel(%d))\n", destinationLa, portNumber, channelNumber);

    ret = m_MTDR_MsgConnectSink(destinationLa, portNumber, channelNumber);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgConnectSink failed: %u\n", ret);
        goto exit;
    }

    /* Wait until all messages are sent */
    do {
        m_MTDR_GetStatusMessages(&sendingFinished);
    } while (!sendingFinished);

exit:
    return ret;
}


/* Configure Channels */
int m_slimbus_configureChannel(MTDR_ChannelProperties *channel)
{
    uint32_t ret;
    bool sendingFinished;

    if(!channel)
        return -1;

    PR_INFO(LOGTAG"m_slimbus_configureChannel\n");

    ret = m_MTDR_MsgBeginReconfiguration();
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgBeginReconfiguration failed: %u\n", ret);
        goto exit;
    }

    ret = m_MTDR_MsgNextDefineChannel(
              channel->channelNumber, channel->transportProtocol,
              channel->segmentDistribution, channel->segmentLength);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgNextDefineChannel failed: %u\n", ret);
        goto exit;
    }

    ret = m_MTDR_MsgNextDefineContent(
              channel->channelNumber, channel->frequencyLockedBit,
              channel->presenceRate, channel->auxiliaryBitFormat, channel->dataType,
              channel->channelLink, channel->dataLength);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgNextDefineContent failed: %u\n", ret);
        goto exit;
    }

    ret = m_MTDR_MsgReconfigureNow();
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgReconfigureNow failed: %u\n", ret);
        goto exit;
    }

    /* Wait until all messages are sent */
    do {
        m_MTDR_GetStatusMessages(&sendingFinished);
    } while (!sendingFinished);

exit:
    return ret;
}

int m_slimbus_activeChannel(uint8_t channelNumber)
{
    uint32_t ret;
    bool sendingFinished;

    /* Configure and activate Channels */
    PR_INFO(LOGTAG"m_slimbus_activeChannel(%d)\n", channelNumber);

    ret = m_MTDR_MsgBeginReconfiguration();;
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgBeginReconfiguration failed: %u\n", ret);
        goto exit;
    }

    ret = m_MTDR_MsgNextActivateChannel(channelNumber);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgNextActivateChannel failed: %u\n", ret);
        goto exit;
    }

    ret = m_MTDR_MsgReconfigureNow();
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"m_MTDR_MsgReconfigureNow failed: %u\n", ret);
        goto exit;
    }

    /* Wait until all messages are sent */
    do {
        m_MTDR_GetStatusMessages(&sendingFinished);;
    } while (!sendingFinished);

exit:
    return ret;
}


void m_slim_devices_get(struct slim_device **sbdev_master_frd,
                        struct slim_device **sbdev_master_ifd, struct slim_device **sbdev_master_gd)
{
    struct jlq_slim_ctrl *dev;

    struct slim_controller *ctrl = NULL;
    struct slim_device *sbdev = NULL;
    u8 e_len = 6;

    struct list_head *pos, *next;

    PR_INFO(LOGTAG"m_slim_devices_get\n");

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
            if(*sbdev_master_frd)
                *sbdev_master_frd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, master_ifd_ea, e_len) == 0) {
            if(*sbdev_master_ifd)
                *sbdev_master_ifd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        } else if(memcmp(sbdev->e_addr, master_gd_ea, e_len) == 0) {
            if(*sbdev_master_gd)
                *sbdev_master_gd = sbdev;
            PR_INFO(LOGTAG"found device, name: %s, la: %2x\n", sbdev->name, sbdev->laddr);
        }

        else {
            //nothing.
        }
    }
}


int m_slim_alloc_port(struct mslim *mslim)
{
    int ret = -EINVAL, i;

    struct mslim_ch *rx_chs;
    struct mslim_ch *tx_chs;

    if (!mslim)
        return ret;

    PR_INFO(LOGTAG"m_slim_alloc_port\n");

    rx_chs = mslim->rx_chs;
    tx_chs = mslim->tx_chs;

    if (!rx_chs || !tx_chs)
        return ret;

    PR_INFO(LOGTAG"Master\tRx: id\tname\tport\thdl\t\tch\tch_hdl");
    for (i = 0 ; (rx_chs->port != M_SLIM_PGD_PORT_LAST) &&
         (i < M_SLIM_NUM_CODEC_DAIS); i++, rx_chs++) {

        /* Get Rx port handler from slimbus driver based
         * on port number
         */
        ret = slim_get_slaveport(mslim->slim_pgd->laddr,
                                 rx_chs->port, &rx_chs->port_hdl, SLIM_SINK);
        if (ret < 0) {
            PR_INFO(LOGTAG"master port failure port#%d - ret[%d]",
                    rx_chs->port, SLIM_SINK);
            return ret;
        }
        PR_INFO(LOGTAG"Master\t    %d\t%s\t%d\t%x\t%d\t%x", rx_chs->id,
                rx_chs->name, rx_chs->port, rx_chs->port_hdl,
                rx_chs->ch, rx_chs->ch_hdl);
    }

    PR_INFO(LOGTAG"Master\tTx: id\tname\tport\thdl\t\tch\tch_hdl");
    for (i = 0; (tx_chs->port != M_SLIM_PGD_PORT_LAST) &&
         (i < M_SLIM_NUM_CODEC_DAIS); i++, tx_chs++) {

        /* Get Tx port handler from slimbus driver based
         * on port number
         */
        ret = slim_get_slaveport(mslim->slim_pgd->laddr,
                                 tx_chs->port, &tx_chs->port_hdl, SLIM_SRC);
        if (ret < 0) {
            PR_INFO(LOGTAG"master port failure port#%d - ret[%d]",
                    tx_chs->port, SLIM_SRC);
            return ret;
        }
        PR_INFO(LOGTAG"Master\t    %d\t%s\t%d\t%x\t%d\t%x", tx_chs->id,
                tx_chs->name, tx_chs->port, tx_chs->port_hdl,
                tx_chs->ch, tx_chs->ch_hdl);
    }
    return ret;
}

static int m_slim_dai_set_channel_map(struct mslim *mslim,
                                      unsigned int tx_num, unsigned int *tx_slot,
                                      unsigned int rx_num, unsigned int *rx_slot)
{
    int ret = -EINVAL, i;

    struct mslim_ch *rx_chs;
    struct mslim_ch *tx_chs;

    if (!mslim)
        return ret;

    PR_INFO(LOGTAG"m_slim_dai_set_channel_map\n");

    rx_chs = mslim->rx_chs;
    tx_chs = mslim->tx_chs;

    if (!rx_chs || !tx_chs)
        return ret;

    PR_INFO(LOGTAG"Master\tRx: id\tname\tport\thdl\tch\tch_hdl");
    for (i = 0; (rx_chs->port != M_SLIM_PGD_PORT_LAST) && (i < rx_num);
         i++, rx_chs++) {
        /* Set Rx Channel number from machine driver and
         * get channel handler from slimbus driver
         */
        rx_chs->ch = *(uint8_t *)(rx_slot + i);
        ret = slim_query_ch(mslim->slim_pgd, rx_chs->ch,
                            &rx_chs->ch_hdl);
        if (ret < 0) {
            PR_INFO(LOGTAG"slim_query_ch failure ch#%d - ret[%d]",
                    rx_chs->ch, ret);
            goto error;
        }
        PR_INFO(LOGTAG"Master\t    %d\t%s\t%d\t%x\t%d\t%x", rx_chs->id,
                rx_chs->name, rx_chs->port, rx_chs->port_hdl,
                rx_chs->ch, rx_chs->ch_hdl);
    }

    PR_INFO(LOGTAG"Master\tTx: id\tname\tport\thdl\tch\tch_hdl");
    for (i = 0; (tx_chs->port != M_SLIM_PGD_PORT_LAST) && (i < tx_num);
         i++, tx_chs++) {
        /* Set Tx Channel number from machine driver and
         * get channel handler from slimbus driver
         */
        tx_chs->ch = *(uint8_t *)(tx_slot + i);
        ret = slim_query_ch(mslim->slim_pgd, tx_chs->ch,
                            &tx_chs->ch_hdl);
        if (ret < 0) {
            PR_INFO(LOGTAG"slim_query_ch failure ch#%d - ret[%d]",
                    tx_chs->ch, ret);
            goto error;
        }
        PR_INFO(LOGTAG"Master\t    %d\t%s\t%d\t%x\t%d\t%x", tx_chs->id,
                tx_chs->name, tx_chs->port, tx_chs->port_hdl,
                tx_chs->ch, tx_chs->ch_hdl);
    }

error:
    return ret;
}

int m_slim_enable_ch(struct mslim *mslim, struct mslim_ch *ch,
                     uint8_t rxport, uint32_t rates, uint8_t grp, uint8_t nchan)
{
    int ret, i;
    struct slim_ch prop;
    struct mslim_ch *chan = ch;
    uint16_t ch_h[2];

    if (!mslim || !ch)
        return -EINVAL;

    PR_INFO(LOGTAG"m_slim_enable_ch, port: %d ch: %d", ch->port, ch->ch);

    /* Define the channel with below parameters */
    prop.prot =  ((rates == 44100) || (rates == 88200)) ?
                 SLIM_PUSH : SLIM_AUTO_ISO;
    prop.baser = ((rates == 44100) || (rates == 88200)) ?
                 SLIM_RATE_11025HZ : SLIM_RATE_4000HZ;
    prop.dataf = ((rates == 48000) || (rates == 44100) ||
                  (rates == 88200) || (rates == 96000)) ?
                 SLIM_CH_DATAF_NOT_DEFINED : SLIM_CH_DATAF_LPCM_AUDIO;

    /* for feedback channel, PCM bit should not be set */
    //PR_INFO(LOGTAG"port open for feedback ch, not setting PCM bit");
    prop.dataf = SLIM_CH_DATAF_NOT_DEFINED;

    prop.auxf = SLIM_CH_AUXF_NOT_APPLICABLE;
    prop.ratem = ((rates == 44100) || (rates == 88200)) ?
                 (rates / 11025) : (rates / 4000);
    prop.sampleszbits = 16;

    ch_h[0] = ch->ch_hdl;
    ch_h[1] = (grp) ? (ch + 1)->ch_hdl : 0;

    PR_INFO(LOGTAG"channel define - prot:%d, dataf:%d, auxf:%d",
            prop.prot, prop.dataf, prop.auxf);
    PR_INFO(LOGTAG"channel define - rates:%d, baser:%d, ratem:%d",
            rates, prop.baser, prop.ratem);

    ret = slim_define_ch(mslim->slim_pgd, &prop, ch_h, nchan, grp,
                         &ch->grph);
    if (ret < 0) {
        PR_INFO(LOGTAG"slim_define_ch failed ret[%d]", ret);
        goto error;
    }

    for (i = 0; i < nchan; i++, ch++) {
        /* Enable port through registration setting */
        if (mslim->vendor_port_en) {
            ret = mslim->vendor_port_en(mslim, ch->port,
                                        rxport, 1);
            if (ret < 0) {
                PR_INFO(LOGTAG"vendor_port_en failed ret[%d]",
                        ret);
                goto error;
            }
        }

        if (rxport) {
            PR_INFO(LOGTAG"slim_connect_sink(port: %d, ch: %d)",
                    ch->port, ch->ch);
            /* Connect Port with channel given by Machine driver*/
            ret = slim_connect_sink(mslim->slim_pgd,
                                    &ch->port_hdl, 1, ch->ch_hdl);
            if (ret < 0) {
                PR_INFO(LOGTAG"slim_connect_sink failed ret[%d]",
                        ret);
                goto remove_channel;
            }

        } else {
            PR_INFO(LOGTAG"slim_connect_src(port: %d, ch: %d)",
                    ch->port, ch->ch);
            /* Connect Port with channel given by Machine driver*/
            ret = slim_connect_src(mslim->slim_pgd, ch->port_hdl,
                                   ch->ch_hdl);
            if (ret < 0) {
                PR_INFO(LOGTAG"slim_connect_src failed ret[%d]",
                        ret);
                goto remove_channel;
            }
        }
    }

    /* Activate the channel immediately */
    PR_INFO(LOGTAG
            "port: %d, ch: %d, grp: %d, ch->grph: 0x%x, ch_hdl: 0x%x",
            chan->port, chan->ch, grp, chan->grph, chan->ch_hdl);
    ret = slim_control_ch(mslim->slim_pgd, (grp ? chan->grph :
                                            chan->ch_hdl), SLIM_CH_ACTIVATE, true);
    if (ret < 0) {
        PR_INFO(LOGTAG"slim_control_ch failed ret[%d]", ret);
        goto remove_channel;
    }

error:
    return ret;

remove_channel:
    /* Remove the channel immediately*/
    ret = slim_control_ch(mslim->slim_pgd, (grp ? ch->grph : ch->ch_hdl),
                          SLIM_CH_REMOVE, true);
    if (ret < 0)
        PR_INFO(LOGTAG"slim_control_ch failed ret[%d]", ret);

    return ret;
}

#if MASTER_SLIM_DAI_SND_UT
static int m_slim_dai_prepare(struct mslim *mslim, int soc_dai_id, unsigned int dai_rate)
{
    int i, ret = -EINVAL;

    struct mslim_ch *ch;
    uint8_t rxport, grp = false, nchan = 1;

    PR_INFO(LOGTAG"m_slim_dai_prepare, dai->id: %d, dai->rate: %d", soc_dai_id, dai_rate);

    /* save sample rate */
    mslim->sample_rate = dai_rate;

    switch (soc_dai_id) {
    case M_FM_SLIM_RX:
        grp = true;
        nchan = 2;
        ch = mslim->rx_chs;
        rxport = 1;
        break;
    case M_BT_SCO_SLIM_RX:
        ch = mslim->rx_chs;
        rxport = 1;
        break;
    case M_BT_SCO_SLIM_TX:
        ch = mslim->tx_chs;
        rxport = 0;
        break;
#if MASTER_LOOPBACK_TEST
    case M_LOOPBACK_RX:
        ch = mslim->rx_chs;
        rxport = 1;
        break;
    case M_LOOPBACK_TX:
        ch = mslim->tx_chs;
        rxport = 0;
        break;
#endif

    case M_SLIM_NUM_CODEC_DAIS:
    default:
        PR_INFO(LOGTAG"dai->id is invalid:%d", soc_dai_id);
        return ret;
    }

    /* Search for dai->id matched port handler */
    for (i = 0; (i < M_SLIM_NUM_CODEC_DAIS) &&
         (ch->id != M_SLIM_NUM_CODEC_DAIS) &&
         (ch->id != soc_dai_id); ch++, i++)
        ;

    if ((ch->port == M_SLIM_PGD_PORT_LAST) ||
        (ch->id == M_SLIM_NUM_CODEC_DAIS)) {
        PR_INFO(LOGTAG"ch is invalid!!");
        return ret;
    }

    ret = m_slim_enable_ch(mslim, ch, rxport, dai_rate, grp, nchan);

    return ret;
}
#endif

static int m_slim_channel_conn_cfg(struct mslim *mslim)
{
    struct mslim_ch *rx_chs;
    struct mslim_ch *tx_chs;
    struct mslim_ch_para *para = master_channel_paras;

    struct mslim_ch *chsp;
    int ret = -1, i, j;

    if (!mslim)
        return ret;

    PR_INFO(LOGTAG"m_slim_channel_conn_cfg\n");

    rx_chs = mslim->rx_chs;
    tx_chs = mslim->tx_chs;

    if (!rx_chs || !tx_chs)
        return ret;

    for(i = 0; (i < MTDR_CHS_NUMBER) && (para->id != M_SLIM_NUM_CODEC_DAIS); i++, para++) {
        if(para->rxport) {
            chsp = rx_chs;
            for(j = 0; j < MTDR_CHS_NUMBER; j++, chsp++) {
                if((para->id == chsp->id) && (para->port == chsp->port)) {
                    para->prop->channelNumber = chsp->ch;

                    /*dont need source here, connect source(TX) by bftm*/
                    para->prop->sinksNum = 1,
                                para->prop->sinks[0].logicalAddress = mslim->slim_pgd->laddr,
                                                    para->prop->sinks[0].portNumber	= (uint8_t)chsp->port,

                    //PR_INFO(LOGTAG"MasterCHL_Rx(id/port/channel): %d %d %d",para->id, para->port, para->prop->channelNumber);

                    m_slimbus_connectSink(para->prop->sinks[0].logicalAddress,
                                          para->prop->sinks[0].portNumber, para->prop->channelNumber);

#ifdef CHANNEL_DEFINE_CONTENT_BY_MASTER
                    m_slimbus_configureChannel(para->prop);
#endif
                    break;
                }
            }
        } else if(!para->rxport) {
            chsp = tx_chs;
            for(j = 0; j < MTDR_CHS_NUMBER; j++, chsp++) {
                if((para->id == chsp->id) && (para->port == chsp->port)) {
                    para->prop->channelNumber = chsp->ch;
                    /*dont need sink here, connect sink(RX) by bftm*/
                    para->prop->source.logicalAddress = mslim->slim_pgd->laddr,
                                para->prop->source.portNumber = (uint8_t)chsp->port,

                    //PR_INFO(LOGTAG"MasterCHL_Tx(id/port/channel): %d %d %d",para->id, para->port, para->prop->channelNumber);

                    m_slimbus_connectSource(para->prop->source.logicalAddress,
                                            para->prop->source.portNumber, para->prop->channelNumber);

#ifdef CHANNEL_DEFINE_CONTENT_BY_MASTER
                    m_slimbus_configureChannel(para->prop);
#endif
                    break;
                }
            }
        } else
            return ret;
    }

    return 0;
}


int m_slim_entry(void)
{
    int ret = -1;

    struct jlq_slim_ctrl *ctrl;
    struct slim_device *dev_master_frd;
    struct slim_device *dev_master_ifd;
    struct slim_device *dev_master_gd;

    struct mslim *m_slim;

    PR_INFO(LOGTAG"m_slim_entry\n");

    /*check version define*/
#if MASTER_SLAVE_NORMAL_VERSION  /*normal JR510 version*/
    PR_INFO(LOGTAG"MASTER_SLAVE_NORMAL_VERSION\n");
#endif
#if MASTER_SLAVE_NORMAL_TEST
    PR_INFO(LOGTAG"MASTER_SLAVE_NORMAL_TEST\n");
#endif
#if MASTER_LOOPBACK_TEST
    PR_INFO(LOGTAG"MASTER_LOOPBACK_TEST\n");
#endif
#if MASTER_SLAVE_LOOPBACK_TEST
    PR_INFO(LOGTAG"MASTER_SLAVE_LOOPBACK_TEST\n");
#endif


    /*slimbus start*/
    ret = m_slimbus_start();
    if(ret != 0) {
        PR_INFO(LOGTAG"m_slimbus_start fail %u\n", ret);
        return -1;
    }

    /*reconfig bus*/
    m_slimbus_reconfigBus(NULL);

    /*will wait for all devs discovery*/
    while(slim_devs_check() != SLIM_DEVS_CHECK_OK) {
        msleep(100);
    }

    slim_devs_list();

    ctrl = m_slimbus_slim_ctrl_get();
    if(!ctrl) {
        PR_INFO(LOGTAG"slim ctrl dev is NULL!\n");
        return ret;
    }

    m_slim_devices_get(&dev_master_frd, &dev_master_ifd, &dev_master_gd);

    m_slim = kzalloc(sizeof(struct mslim), GFP_KERNEL);
    if (m_slim == NULL) {
        PR_INFO(LOGTAG"allocation mslim fail\n");
        return -ENOMEM;
    }

    m_slim->slim_ifd = dev_master_ifd;
    m_slim->slim_pgd = dev_master_gd;
    m_slim->rx_chs = SLIM_MASTER_RXPORT;
    m_slim->tx_chs = SLIM_MASTER_TXPORT;

    /*master alloc port*/
    ret = m_slim_alloc_port(m_slim);
    if(ret < 0) {
        PR_INFO(LOGTAG"m_slim_alloc_port fail\n");
        goto fail;
    }

    /*master channel map, according to snd slot*/
    ret = m_slim_dai_set_channel_map(m_slim, MASTER_SB_CHS_TX_NUM, tx_slot, 0, NULL);
    if(ret < 0) {
        PR_INFO(LOGTAG"m_slim_dai_set_channel_map TX fail\n");
        goto fail;
    }
    ret = m_slim_dai_set_channel_map(m_slim, 0, NULL, MASTER_SB_CHS_RX_NUM, rx_slot);
    if(ret < 0) {
        PR_INFO(LOGTAG"m_slim_dai_set_channel_map RX fail\n");
        goto fail;
    }

    /*dport init*/
    m_slimbus_dPort_init();

    /*master enable ch, be done by btfm, maybe for verification only here.*/
    //m_slim_dai_prepare(m_slim, M_FM_SLIM_RX, 48000);
    //m_slim_dai_prepare(m_slim, M_BT_SCO_SLIM_RX, 8000);  //or 44100
    //m_slim_dai_prepare(m_slim, M_BT_SCO_SLIM_TX, 8000);  //44100

#if (MASTER_LOOPBACK_TEST & MASTER_LOOPBACK_TEST_WITH_CH_CONTENT)
    m_slim_dai_prepare(m_slim, M_LOOPBACK_RX, 8000);
    m_slim_dai_prepare(m_slim, M_LOOPBACK_TX, 8000);
#endif

#if (MASTER_SLAVE_NORMAL_VERSION | (MASTER_LOOPBACK_TEST & ~MASTER_LOOPBACK_TEST_WITH_CH_CONTENT))
    m_slim_channel_conn_cfg(m_slim);
#endif

#if (MASTER_LOOPBACK_TEST & ~MASTER_LOOPBACK_TEST_WITH_CH_CONTENT)
    ret = m_slimbus_activeChannel(mtdiChannles[4].channelNumber);
    if(ret != 0) {
        PR_INFO(LOGTAG"m_slimbus_start fail %u\n", ret);
        return -1;
    }
#endif

    /*enable RCFG int, slave use this*/
    m_slimbus_enableRCFG_INT();

    master_slim_state = MASTER_SLIM_STAT_INITED;

    PR_INFO(LOGTAG"m_slim_entry, succ, MASTER_SLIM_STAT_INITED\n");

    return 0;
fail:
    if(m_slim)
        kfree(m_slim);
    return -1;
}

int m_slim_entry_dai(void)
{
    int ret = -1;

    PR_INFO(LOGTAG"m_slim_entry_dai\n");

    /*slimbus start*/
    ret = m_slimbus_start();
    if(ret != 0) {
        PR_INFO(LOGTAG"m_slimbus_start fail %u\n", ret);
        return -1;
    }

    /*reconfig bus*/
    m_slimbus_reconfigBus(NULL);

    /*will wait for all devs discovery*/
    /*
    while(slim_devs_check() != SLIM_DEVS_CHECK_OK) {
        msleep(100);
    }
    */

    slim_devs_list();

    /*dport init*/
    m_slimbus_dPort_init();

    /*enable RCFG int, slave use this*/
    m_slimbus_enableRCFG_INT();

    master_slim_state = MASTER_SLIM_STAT_INITED;

    PR_INFO(LOGTAG"m_slim_entry_dai, succ, MASTER_SLIM_STAT_INITED\n");

    jlq_slim_frm_clk_disable();

    return 0;
}

int slim_master_thread(void *data)
{
    //struct jlq_slim_ctrl *dev = (struct jlq_slim_ctrl *)data;
    //struct completion *notify = &dev->slim_master_notify;

    PR_INFO(LOGTAG"slim_master_thread started");

#if MASTER_SLIM_DAI_SND
    m_slim_entry_dai();
#else
    m_slim_entry();
#endif
    return 0;
}


int master_slimbus_state_get(void)
{
    return master_slim_state;
}
EXPORT_SYMBOL(master_slimbus_state_get);

MODULE_LICENSE("GPL");

