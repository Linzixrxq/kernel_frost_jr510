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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/debugfs.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/slimbus/slimbus.h>
#include <linux/kthread.h>


#include "slim_inc.h"
#include "slim_mtdr_if.h"
#include "slim_chk.h"
#include "slim_regs.h"
#include "slim_mtdr.h"
#include "slim_master.h"
#include "slim_slave.h"
#include "slim_devs.h"

#define SLAVE_SLIM_BTFM_UT  1   /*sound dai api ut*/

#define LOGTAG "[slim][btfm_api]"


/*verify slimbus API for BTFM requirement.*/

#define BTFMSLIM_DBG(fmt, arg...)  pr_info(LOGTAG"%s: " fmt "\n", __func__, ## arg)
#define BTFMSLIM_INFO(fmt, arg...) pr_info(LOGTAG"%s: " fmt "\n", __func__, ## arg)
#define BTFMSLIM_ERR(fmt, arg...)  pr_info(LOGTAG"%s: " fmt "\n", __func__, ## arg)


/*dts probe string*/
#define SLIM_SLAVE_COMPATIBLE_STR	"btfmslim_slave"

/* Specific defines for slave slimbus device */
#define SLAVE_SLIM_REG_OFFSET		0x0800
#define SLIM_SLAVE_REG_OFFSET		SLAVE_SLIM_REG_OFFSET

/* Misc defines */
#define SLIM_SLAVE_RW_MAX_TRIES		3
#define SLIM_SLAVE_PRESENT_TIMEOUT	100

#define PGD	1
#define IFD	0


/* PGD Port Map, same as jr510 wcn3950 */
/*
#define SLAVE_SB_PGD_PORT_RX_NUM			1
#define SLAVE_SB_PGD_PORT_TX_NUM			3

#define SLAVE_SB_PGD_PORT_TX_SCO			0
#define SLAVE_SB_PGD_PORT_TX1_FM			1
#define SLAVE_SB_PGD_PORT_TX2_FM			2
#define SLAVE_SB_PGD_PORT_RX_SCO			16
*/

/* Codec driver defines */
enum {
    BTFM_FM_SLIM_TX = 0,
    BTFM_BT_SCO_SLIM_TX,
    BTFM_BT_SCO_A2DP_SLIM_RX,
    BTFM_BT_SPLIT_A2DP_SLIM_RX,
    BTFM_SLIM_NUM_CODEC_DAIS
};

/* Slimbus Port defines - This should be redefined in specific device file */
#define BTFM_SLIM_PGD_PORT_LAST				0xFF

/* Registers Address */
#define SLAVE_SB_COMP_TEST			    0x00000000
#define SLAVE_SB_SLAVE_HW_REV_MSB		0x00000001
#define SLAVE_SB_SLAVE_HW_REV_LSB		0x00000002
#define SLAVE_SB_DEBUG_FEATURES			0x00000005
#define SLAVE_SB_INTF_INT_EN			0x00000010
#define SLAVE_SB_INTF_INT_STATUS		0x00000011
#define SLAVE_SB_INTF_INT_CLR			0x00000012
#define SLAVE_SB_FRM_CFG				0x00000013
#define SLAVE_SB_FRM_STATUS			    0x00000014
#define SLAVE_SB_FRM_INT_EN			    0x00000015
#define SLAVE_SB_FRM_INT_STATUS			0x00000016
#define SLAVE_SB_FRM_INT_CLR			0x00000017
#define SLAVE_SB_FRM_WAKEUP			    0x00000018
#define SLAVE_SB_FRM_CLKCTL_DONE		0x00000019
#define SLAVE_SB_FRM_IE_STATUS			0x0000001A
#define SLAVE_SB_FRM_VE_STATUS			0x0000001B
#define SLAVE_SB_PGD_TX_CFG_STATUS		0x00000020
#define SLAVE_SB_PGD_RX_CFG_STATUS		0x00000021
#define SLAVE_SB_PGD_DEV_INT_EN			0x00000022
#define SLAVE_SB_PGD_DEV_INT_STATUS		0x00000023
#define SLAVE_SB_PGD_DEV_INT_CLR			0x00000024
#define SLAVE_SB_PGD_PORT_INT_EN_RX_0		0x00000030
#define SLAVE_SB_PGD_PORT_INT_EN_RX_1		0x00000031
#define SLAVE_SB_PGD_PORT_INT_EN_TX_0		0x00000032
#define SLAVE_SB_PGD_PORT_INT_EN_TX_1		0x00000033
#define SLAVE_SB_PGD_PORT_INT_STATUS_RX_0	0x00000034
#define SLAVE_SB_PGD_PORT_INT_STATUS_RX_1	0x00000035
#define SLAVE_SB_PGD_PORT_INT_STATUS_TX_0	0x00000036
#define SLAVE_SB_PGD_PORT_INT_STATUS_TX_1	0x00000037
#define SLAVE_SB_PGD_PORT_INT_CLR_RX_0		0x00000038
#define SLAVE_SB_PGD_PORT_INT_CLR_RX_1		0x00000039
#define SLAVE_SB_PGD_PORT_INT_CLR_TX_0		0x0000003A
#define SLAVE_SB_PGD_PORT_INT_CLR_TX_1		0x0000003B
#define SLAVE_SB_PGD_PORT_RX_CFGN(n)		(0x00000040 + n)
#define SLAVE_SB_PGD_PORT_TX_CFGN(n)		(0x00000050 + n)
#define SLAVE_SB_PGD_PORT_INT_RX_SOURCEN(n)	(0x00000060 + n)
#define SLAVE_SB_PGD_PORT_INT_TX_SOURCEN(n)	(0x00000070 + n)
#define SLAVE_SB_PGD_PORT_RX_STATUSN(n)		(0x00000080 + n)
#define SLAVE_SB_PGD_PORT_TX_STATUSN(n)		(0x00000090 + n)
#define SLAVE_SB_PGD_TX_PORTn_MULTI_CHNL_0(n)	(0x00000100 + 0x4*n)
#define SLAVE_SB_PGD_TX_PORTn_MULTI_CHNL_1(n)	(0x00000101 + 0x4*n)
#define SLAVE_SB_PGD_RX_PORTn_MULTI_CHNL_0(n)	(0x00000180 + 0x4*n)
#define SLAVE_SB_PGD_RX_PORTn_MULTI_CHNL_1(n)	(0x00000181 + 0x4*n)
#define SLAVE_SB_PGD_PORT_TX_OR_UR_CFGN(n)	(0x000001F0 + n)

/* Register Bit Setting */
#define SLAVE_ENABLE_OVERRUN_AUTO_RECOVERY	(0x1 << 1)
#define SLAVE_ENABLE_UNDERRUN_AUTO_RECOVERY	(0x1 << 0)
#define SLAVE_SB_PGD_PORT_ENABLE			(0x1 << 0)
#define SLAVE_SB_PGD_PORT_DISABLE		(0x0 << 0)
#define SLAVE_SB_PGD_PORT_WM_L1			(0x1 << 1)
#define SLAVE_SB_PGD_PORT_WM_L2			(0x2 << 1)
#define SLAVE_SB_PGD_PORT_WM_L3			(0x3 << 1)
#define SLAVE_SB_PGD_PORT_WM_L8			(0x8 << 1)
#define SLAVE_SB_PGD_PORT_WM_LB			(0xB << 1)


/* PGD Port Map */
#define SLAVE_SB_PGD_PORT_TX_SCO			0
#define SLAVE_SB_PGD_PORT_TX1_FM			1
#define SLAVE_SB_PGD_PORT_TX2_FM			2
#define CHRKVER3_SB_PGD_PORT_TX1_FM			4
#define CHRKVER3_SB_PGD_PORT_TX2_FM			5
#define SLAVE_SB_PGD_PORT_RX_SCO			16
#define SLAVE_SB_PGD_PORT_RX_A2P			17


struct btfmslim_ch {
    int id;
    char *name;
    uint32_t port_hdl;	/* slimbus port handler */
    uint16_t port;		/* slimbus port number */

    uint8_t ch;		/* slimbus channel number */
    uint16_t ch_hdl;	/* slimbus channel handler */
    uint16_t grph;	/* slimbus group channel handler */
};

struct btfmslim {
    struct device *dev;
    struct slim_device *slim_pgd;
    struct slim_device slim_ifd;
    struct mutex io_lock;
    struct mutex xfer_lock;
    uint8_t enabled;

    uint32_t num_rx_port;
    uint32_t num_tx_port;
    uint32_t sample_rate;

    struct btfmslim_ch *rx_chs;
    struct btfmslim_ch *tx_chs;

    int (*vendor_init)(struct btfmslim *btfmslim);
    int (*vendor_port_en)(struct btfmslim *btfmslim, uint8_t port_num,
                          uint8_t rxport, uint8_t enable);
};

/* btfm ch, SLAVE (WCN3950) Port assignment */
static struct btfmslim_ch slave_rxport[] = {
    {
        .id = BTFM_BT_SCO_A2DP_SLIM_RX, .name = "SCO_A2P_Rx",
        .port = SLAVE_SB_PGD_PORT_RX_SCO
    },

    {
        .id = BTFM_SLIM_NUM_CODEC_DAIS, .name = "",
        .port = BTFM_SLIM_PGD_PORT_LAST
    },
};

static struct btfmslim_ch slave_txport[] = {
    {
        .id = BTFM_BT_SCO_SLIM_TX, .name = "SCO_Tx",
        .port = SLAVE_SB_PGD_PORT_TX_SCO
    },
    {
        .id = BTFM_FM_SLIM_TX, .name = "FM_Tx1",
        .port = SLAVE_SB_PGD_PORT_TX1_FM
    },
    {
        .id = BTFM_FM_SLIM_TX, .name = "FM_Tx2",
        .port = SLAVE_SB_PGD_PORT_TX2_FM
    },

    {
        .id = BTFM_SLIM_NUM_CODEC_DAIS, .name = "",
        .port = BTFM_SLIM_PGD_PORT_LAST
    },
};

#define SLIM_SLAVE_RXPORT (&slave_rxport[0])
#define SLIM_SLAVE_TXPORT (&slave_txport[0])

/*sound slot, ->map to btfm ch, ->map to slimbus ch. from bengal_tdm_snd_hw_params*/
#if MASTER_SLAVE_NORMAL_VERSION
static unsigned int snd_slot_offset[8] = {157, 159, 160, 161};
static unsigned int *rx_slot = &snd_slot_offset[0];
static unsigned int *tx_slot = &snd_slot_offset[1];
#define SLAVE_SB_PGD_PORT_RX_NUM			1
#define SLAVE_SB_PGD_PORT_TX_NUM			3
#endif

#if MASTER_SLAVE_NORMAL_TEST  /*normal test with slave*/
static unsigned int snd_slot_offset[8] = {0, 4, 8, 12, 16, 20, 24, 28};
static unsigned int *tx_slot = &snd_slot_offset[1];
static unsigned int *rx_slot = &snd_slot_offset[5];
#define SLAVE_SB_PGD_PORT_RX_NUM			1
#define SLAVE_SB_PGD_PORT_TX_NUM			3
#endif


static int btfm_slim_get_dt_info(struct btfmslim *btfmslim)
{
    int ret = 0;
    struct slim_device *slim = btfmslim->slim_pgd;
    struct slim_device *slim_ifd = &btfmslim->slim_ifd;
    struct property *prop;

    if (!slim || !slim_ifd)
        return -EINVAL;

    pr_info(LOGTAG"btfm_slim_get_dt_info\n");

    if (slim->dev.of_node) {
        pr_info(LOGTAG"Platform data from device tree (%s)",
                slim->name);
        ret = of_property_read_string(slim->dev.of_node,
                                      "qcom,btfm-slim-ifd", &slim_ifd->name);
        if (ret) {
            pr_info(LOGTAG"Looking up %s property in node %s failed",
                    "qcom,btfm-slim-ifd",
                    slim->dev.of_node->full_name);
            return -ENODEV;
        }
        pr_info(LOGTAG"qcom,btfm-slim-ifd (%s)", slim_ifd->name);

        prop = of_find_property(slim->dev.of_node,
                                "qcom,btfm-slim-ifd-elemental-addr", NULL);
        if (!prop) {
            pr_info(LOGTAG"Looking up %s property in node %s failed",
                    "qcom,btfm-slim-ifd-elemental-addr",
                    slim->dev.of_node->full_name);
            return -ENODEV;
        } else if (prop->length != 6) {
            pr_info(LOGTAG
                    "invalid codec slim ifd addr. addr length= %d",
                    prop->length);
            return -ENODEV;
        }
        memcpy(slim_ifd->e_addr, prop->value, 6);
        pr_info(LOGTAG
                "PGD Enum Addr(from dts): %.02x:%.02x:%.02x:%.02x:%.02x:%.02x",
                slim->e_addr[0], slim->e_addr[1], slim->e_addr[2],
                slim->e_addr[3], slim->e_addr[4], slim->e_addr[5]);
        pr_info(LOGTAG
                "IFD Enum Addr(from dts): %.02x:%.02x:%.02x:%.02x:%.02x:%.02x",
                slim_ifd->e_addr[0], slim_ifd->e_addr[1],
                slim_ifd->e_addr[2], slim_ifd->e_addr[3],
                slim_ifd->e_addr[4], slim_ifd->e_addr[5]);

        //slim_ifd->dev.of_node = slim->dev.of_node;
    } else {
        pr_info(LOGTAG"Platform data is not valid");
    }

    return ret;
}

static int btfm_slim_write(struct btfmslim *btfmslim,
                    uint16_t reg, int bytes, void *src, uint8_t pgd)
{
    int ret, i;
    struct slim_ele_access msg;
    int slim_write_tries = SLIM_SLAVE_RW_MAX_TRIES;

    BTFMSLIM_DBG("Write to %s", pgd ? "PGD" : "IFD");
    msg.start_offset = SLIM_SLAVE_REG_OFFSET + reg;
    msg.num_bytes = bytes;
    msg.comp = NULL;

    for ( ; slim_write_tries != 0; slim_write_tries--) {
        mutex_lock(&btfmslim->xfer_lock);
        ret = slim_change_val_element(pgd ? btfmslim->slim_pgd :
                                      &btfmslim->slim_ifd, &msg, src, bytes);
        mutex_unlock(&btfmslim->xfer_lock);
        if (ret == 0)
            break;
        usleep_range(5000, 5100);
    }

    if (ret) {
        BTFMSLIM_ERR("failed (%d)", ret);
        return ret;
    }

    for (i = 0; i < bytes; i++)
        BTFMSLIM_DBG("Write 0x%02x to reg 0x%x", ((uint8_t *)src)[i],
                     reg + i);
    return 0;
}

static int btfm_slim_write_pgd(struct btfmslim *btfmslim,
                        uint16_t reg, int bytes, void *src)
{
    return btfm_slim_write(btfmslim, reg, bytes, src, PGD);
}

static int btfm_slim_write_inf(struct btfmslim *btfmslim,
                        uint16_t reg, int bytes, void *src)
{
    return btfm_slim_write(btfmslim, reg, bytes, src, IFD);
}

static int btfm_slim_read(struct btfmslim *btfmslim, unsigned short reg,
                   int bytes, void *dest, uint8_t pgd)
{
    int ret, i;
    struct slim_ele_access msg;
    int slim_read_tries = SLIM_SLAVE_RW_MAX_TRIES;

    BTFMSLIM_DBG("Read from %s", pgd ? "PGD" : "IFD");
    msg.start_offset = SLIM_SLAVE_REG_OFFSET + reg;
    msg.num_bytes = bytes;
    msg.comp = NULL;

    for ( ; slim_read_tries != 0; slim_read_tries--) {
        mutex_lock(&btfmslim->xfer_lock);
        ret = slim_request_val_element(pgd ? btfmslim->slim_pgd :
                                       &btfmslim->slim_ifd, &msg, dest, bytes);
        mutex_unlock(&btfmslim->xfer_lock);
        if (ret == 0)
            break;
        usleep_range(5000, 5100);
    }

    if (ret)
        BTFMSLIM_ERR("failed (%d)", ret);

    for (i = 0; i < bytes; i++)
        BTFMSLIM_DBG("Read 0x%02x from reg 0x%x", ((uint8_t *)dest)[i],
                     reg + i);

    return 0;
}

static int btfm_slim_read_pgd(struct btfmslim *btfmslim,
                       uint16_t reg, int bytes, void *dest)
{
    return btfm_slim_read(btfmslim, reg, bytes, dest, PGD);
}

static int btfm_slim_read_inf(struct btfmslim *btfmslim,
                       uint16_t reg, int bytes, void *dest)
{
    return btfm_slim_read(btfmslim, reg, bytes, dest, IFD);
}

static int btfm_slim_slave_hw_init(struct btfmslim *btfmslim)
{
    int ret = 0;
    uint8_t reg_val;
    uint16_t reg;

    BTFMSLIM_DBG("");

    if (!btfmslim)
        return -EINVAL;

    /* Get SB_SLAVE_HW_REV_MSB value*/
    reg = SLAVE_SB_SLAVE_HW_REV_MSB;
    ret = btfm_slim_read(btfmslim, reg,  1, &reg_val, IFD);
    if (ret) {
        BTFMSLIM_ERR("failed to read (%d) reg 0x%x", ret, reg);
        goto error;
    }
    BTFMSLIM_DBG("Major Rev: 0x%x, Minor Rev: 0x%x",
                 (reg_val & 0xF0) >> 4, (reg_val & 0x0F));

    /* Get SB_SLAVE_HW_REV_LSB value*/
    reg = SLAVE_SB_SLAVE_HW_REV_LSB;
    ret = btfm_slim_read(btfmslim, reg,  1, &reg_val, IFD);
    if (ret) {
        BTFMSLIM_ERR("failed to read (%d) reg 0x%x", ret, reg);
        goto error;
    }
    BTFMSLIM_DBG("Step Rev: 0x%x", reg_val);

error:
    return ret;
}

static inline int is_fm_port(uint8_t port_num)
{
    if (port_num == SLAVE_SB_PGD_PORT_TX1_FM ||
        port_num == CHRKVER3_SB_PGD_PORT_TX1_FM ||
        port_num == CHRKVER3_SB_PGD_PORT_TX2_FM ||
        port_num == SLAVE_SB_PGD_PORT_TX2_FM)
        return 1;
    else
        return 0;
}

//#define CHECK_PORT_ENABLE_VE
static int btfm_slim_slave_enable_port(struct btfmslim *btfmslim, uint8_t port_num,
                                uint8_t rxport, uint8_t enable)
{
    int ret = 0;
    uint8_t reg_val = 0, en;
    uint8_t rxport_num = 0;
    uint16_t reg;

    BTFMSLIM_DBG("port(%d) enable(%d)", port_num, enable);
    if (rxport) {
        BTFMSLIM_DBG("sample rate is %d", btfmslim->sample_rate);
        /*if (enable &&
        	btfmslim->sample_rate != 44100 &&
        	btfmslim->sample_rate != 88200) {
        	BTFMSLIM_DBG("setting multichannel bit");*/
        /* For SCO Rx, A2DP Rx other than 44.1 and 88.2Khz */
        if (enable) {
            if (port_num < 24) {
                rxport_num = port_num - 16;
                reg_val = 0x01 << rxport_num;
                reg = SLAVE_SB_PGD_RX_PORTn_MULTI_CHNL_0(
                          rxport_num);
            } else {
                rxport_num = port_num - 24;
                reg_val = 0x01 << rxport_num;
                reg = SLAVE_SB_PGD_RX_PORTn_MULTI_CHNL_1(
                          rxport_num);
            }

            BTFMSLIM_DBG("writing reg_val (%d) to reg(%x)",
                         reg_val, reg);
            ret = btfm_slim_write(btfmslim, reg, 1, &reg_val, IFD);
            if (ret) {
                BTFMSLIM_ERR("failed to write (%d) reg 0x%x",
                             ret, reg);
                goto error;
            }
#ifdef CHECK_PORT_ENABLE_VE
            else {
                uint8_t reg_value = 0;
                btfm_slim_read_inf(btfmslim, reg, 1, &reg_value);
                BTFMSLIM_DBG("read reg_val (%d) from reg(%x)",	reg_value, reg);
            }
#endif

        }
        /* Port enable */
        reg = SLAVE_SB_PGD_PORT_RX_CFGN(port_num - 0x10);
        goto enable_disable_rxport;
    }
    if (!enable)
        goto enable_disable_txport;

    /* txport */
    /* Multiple Channel Setting */
    if (is_fm_port(port_num)) {
        if (port_num == CHRKVER3_SB_PGD_PORT_TX1_FM)
            reg_val = (0x1 << CHRKVER3_SB_PGD_PORT_TX1_FM);
        else if (port_num == CHRKVER3_SB_PGD_PORT_TX2_FM)
            reg_val = (0x1 << CHRKVER3_SB_PGD_PORT_TX2_FM);
        else
            reg_val = (0x1 << SLAVE_SB_PGD_PORT_TX1_FM) |
                      (0x1 << SLAVE_SB_PGD_PORT_TX2_FM);
        reg = SLAVE_SB_PGD_TX_PORTn_MULTI_CHNL_0(port_num);
        BTFMSLIM_INFO("writing reg_val (%d) to reg(%x)", reg_val, reg);
        ret = btfm_slim_write(btfmslim, reg, 1, &reg_val, IFD);
        if (ret) {
            BTFMSLIM_ERR("failed to write (%d) reg 0x%x", ret, reg);
            goto error;
        }
#ifdef CHECK_PORT_ENABLE_VE
        else {
            uint8_t reg_value = 0;
            btfm_slim_read_inf(btfmslim, reg, 1, &reg_value);
            BTFMSLIM_DBG("read reg_val (%d) from reg(%x)",  reg_value, reg);
        }
#endif

    } else if (port_num == SLAVE_SB_PGD_PORT_TX_SCO) {
        /* SCO Tx */
        reg_val = 0x1 << SLAVE_SB_PGD_PORT_TX_SCO;
        reg = SLAVE_SB_PGD_TX_PORTn_MULTI_CHNL_0(port_num);
        BTFMSLIM_DBG("writing reg_val (%d) to reg(%x)",
                     reg_val, reg);
        ret = btfm_slim_write(btfmslim, reg, 1, &reg_val, IFD);
        if (ret) {
            BTFMSLIM_ERR("failed to write (%d) reg 0x%x",
                         ret, reg);
            goto error;
        }
#ifdef CHECK_PORT_ENABLE_VE
        else {
            uint8_t reg_value = 0;
            btfm_slim_read_inf(btfmslim, reg, 1, &reg_value);
            BTFMSLIM_DBG("read reg_val (%d) from reg(%x)",  reg_value, reg);
        }
#endif

    }

    /* Enable Tx port hw auto recovery for underrun or overrun error */
    reg_val = (SLAVE_ENABLE_OVERRUN_AUTO_RECOVERY |
               SLAVE_ENABLE_UNDERRUN_AUTO_RECOVERY);
    reg = SLAVE_SB_PGD_PORT_TX_OR_UR_CFGN(port_num);
    BTFMSLIM_DBG("writing reg_val (%d) to reg(%x)",
                 reg_val, reg);
    ret = btfm_slim_write(btfmslim, reg, 1, &reg_val, IFD);
    if (ret) {
        BTFMSLIM_ERR("failed to write (%d) reg 0x%x", ret, reg);
        goto error;
    }
#ifdef CHECK_PORT_ENABLE_VE
    else {
        uint8_t reg_value = 0;
        btfm_slim_read_inf(btfmslim, reg, 1, &reg_value);
        BTFMSLIM_DBG("read reg_val (%d) from reg(%x)",  reg_value, reg);
    }
#endif

enable_disable_txport:
    /* Port enable */
    reg = SLAVE_SB_PGD_PORT_TX_CFGN(port_num);

enable_disable_rxport:
    if (enable)
        en = SLAVE_SB_PGD_PORT_ENABLE;
    else
        en = SLAVE_SB_PGD_PORT_DISABLE;

    if (is_fm_port(port_num))
        reg_val = en | SLAVE_SB_PGD_PORT_WM_L8;
    else if (port_num == SLAVE_SB_PGD_PORT_TX_SCO)
        reg_val = enable ? en | SLAVE_SB_PGD_PORT_WM_L1 : en;
    else
        reg_val = enable ? en | SLAVE_SB_PGD_PORT_WM_LB : en;

    if (enable && port_num == SLAVE_SB_PGD_PORT_TX_SCO)
        BTFMSLIM_INFO("programming SCO Tx with reg_val %d to reg 0x%x",
                      reg_val, reg);

    BTFMSLIM_DBG("writing reg_val (%d) to reg(%x)",
                 reg_val, reg);

    ret = btfm_slim_write(btfmslim, reg, 1, &reg_val, IFD);
    if (ret)
        BTFMSLIM_ERR("failed to write (%d) reg 0x%x", ret, reg);
#ifdef CHECK_PORT_ENABLE_VE
    else {
        uint8_t reg_value = 0;
        btfm_slim_read_inf(btfmslim, reg, 1, &reg_value);
        BTFMSLIM_DBG("read reg_val (%d) from reg(%x)",  reg_value, reg);
    }
#endif

error:
    return ret;
}

static int btfm_slim_enable_ch(struct btfmslim *btfmslim, struct btfmslim_ch *ch,
                        uint8_t rxport, uint32_t rates, uint8_t grp, uint8_t nchan)
{
    int ret, i;
    struct slim_ch prop;
    struct btfmslim_ch *chan = ch;
    uint16_t ch_h[2];

    if (!btfmslim || !ch)
        return -EINVAL;

    BTFMSLIM_DBG("port: %d ch: %d", ch->port, ch->ch);

    /* Define the channel with below parameters */
    prop.prot =  ((rates == 44100) || (rates == 88200)) ?
                 SLIM_PUSH : SLIM_AUTO_ISO;
    prop.baser = ((rates == 44100) || (rates == 88200)) ?
                 SLIM_RATE_11025HZ : SLIM_RATE_4000HZ;
    prop.dataf = ((rates == 48000) || (rates == 44100) ||
                  (rates == 88200) || (rates == 96000)) ?
                 SLIM_CH_DATAF_NOT_DEFINED : SLIM_CH_DATAF_LPCM_AUDIO;

    /* for feedback channel, PCM bit should not be set */
    //if (btfm_feedback_ch_setting) {
    BTFMSLIM_DBG("port open for feedback ch, not setting PCM bit");
    prop.dataf = SLIM_CH_DATAF_NOT_DEFINED;
    /* reset so that next port open sets the data format properly */
    //btfm_feedback_ch_setting = 0;
    //}
    prop.auxf = SLIM_CH_AUXF_NOT_APPLICABLE;
    prop.ratem = ((rates == 44100) || (rates == 88200)) ?
                 (rates / 11025) : (rates / 4000);
    prop.sampleszbits = 16;

    ch_h[0] = ch->ch_hdl;
    ch_h[1] = (grp) ? (ch + 1)->ch_hdl : 0;

    BTFMSLIM_INFO("channel define - prot:%d, dataf:%d, auxf:%d",
                  prop.prot, prop.dataf, prop.auxf);
    BTFMSLIM_INFO("channel define - rates:%d, baser:%d, ratem:%d",
                  rates, prop.baser, prop.ratem);

    ret = slim_define_ch(btfmslim->slim_pgd, &prop, ch_h, nchan, grp,
                         &ch->grph);
    if (ret < 0) {
        BTFMSLIM_ERR("slim_define_ch failed ret[%d]", ret);
        goto error;
    }

    for (i = 0; i < nchan; i++, ch++) {
        /* Enable port through registration setting */
        if (btfmslim->vendor_port_en) {
            ret = btfmslim->vendor_port_en(btfmslim, ch->port,
                                           rxport, 1);
            if (ret < 0) {
                BTFMSLIM_ERR("vendor_port_en failed ret[%d]",
                             ret);
                goto error;
            }
        }

        if (rxport) {
            BTFMSLIM_INFO("slim_connect_sink(port: %d, ch: %d)",
                          ch->port, ch->ch);
            /* Connect Port with channel given by Machine driver*/
            ret = slim_connect_sink(btfmslim->slim_pgd,
                                    &ch->port_hdl, 1, ch->ch_hdl);
            if (ret < 0) {
                BTFMSLIM_ERR("slim_connect_sink failed ret[%d]",
                             ret);
                goto remove_channel;
            }

        } else {
            BTFMSLIM_INFO("slim_connect_src(port: %d, ch: %d)",
                          ch->port, ch->ch);
            /* Connect Port with channel given by Machine driver*/
            ret = slim_connect_src(btfmslim->slim_pgd, ch->port_hdl,
                                   ch->ch_hdl);
            if (ret < 0) {
                BTFMSLIM_ERR("slim_connect_src failed ret[%d]",
                             ret);
                goto remove_channel;
            }
        }
    }

    /* Activate the channel immediately */
    BTFMSLIM_INFO(
        "port: %d, ch: %d, grp: %d, ch->grph: 0x%x, ch_hdl: 0x%x",
        chan->port, chan->ch, grp, chan->grph, chan->ch_hdl);
    ret = slim_control_ch(btfmslim->slim_pgd, (grp ? chan->grph :
                          chan->ch_hdl), SLIM_CH_ACTIVATE, true);
    if (ret < 0) {
        BTFMSLIM_ERR("slim_control_ch failed ret[%d]", ret);
        goto remove_channel;
    }

error:
    return ret;

remove_channel:
    /* Remove the channel immediately*/
    ret = slim_control_ch(btfmslim->slim_pgd, (grp ? ch->grph : ch->ch_hdl),
                          SLIM_CH_REMOVE, true);
    if (ret < 0)
        BTFMSLIM_ERR("slim_control_ch failed ret[%d]", ret);

    return ret;
}

static int btfm_slim_disable_ch(struct btfmslim *btfmslim, struct btfmslim_ch *ch,
                         uint8_t rxport, uint8_t grp, uint8_t nchan)
{
    int ret, i;

    if (!btfmslim || !ch)
        return -EINVAL;

    BTFMSLIM_INFO("port:%d, grp: %d, ch->grph:0x%x, ch->ch_hdl:0x%x ",
                  ch->port, grp, ch->grph, ch->ch_hdl);

    /* For 44.1/88.2 Khz A2DP Rx, disconnect the port first */
    if (rxport &&
        (btfmslim->sample_rate == 44100 ||
         btfmslim->sample_rate == 88200)) {
        BTFMSLIM_DBG("disconnecting the ports, removing the channel");
        ret = slim_disconnect_ports(btfmslim->slim_pgd,
                                    &ch->port_hdl, 1);
        if (ret < 0) {
            BTFMSLIM_ERR("slim_disconnect_ports failed ret[%d]",
                         ret);
        }
    }

    /* Remove the channel immediately*/
    ret = slim_control_ch(btfmslim->slim_pgd, (grp ? ch->grph : ch->ch_hdl),
                          SLIM_CH_REMOVE, true);
    if (ret < 0) {
        BTFMSLIM_ERR("slim_control_ch failed ret[%d]", ret);
        if (btfmslim->sample_rate != 44100 &&
            btfmslim->sample_rate != 88200) {
            ret = slim_disconnect_ports(btfmslim->slim_pgd,
                                        &ch->port_hdl, 1);
            if (ret < 0) {
                BTFMSLIM_ERR("disconnect_ports failed ret[%d]",
                             ret);
                goto error;
            }
        }
    }

    /* Disable port through registration setting */
    for (i = 0; i < nchan; i++, ch++) {
        if (btfmslim->vendor_port_en) {
            ret = btfmslim->vendor_port_en(btfmslim, ch->port,
                                           rxport, 0);
            if (ret < 0) {
                BTFMSLIM_ERR("vendor_port_en failed ret[%d]",
                             ret);
                break;
            }
        }
    }
error:
    return ret;
}

static int btfm_slim_get_logical_addr(struct slim_device *slim)
{
    int ret = 0;
    const unsigned long timeout = jiffies +
                                  msecs_to_jiffies(SLIM_SLAVE_PRESENT_TIMEOUT);

    do {
        ret = slim_get_logical_addr(slim, slim->e_addr,
                                    ARRAY_SIZE(slim->e_addr), &slim->laddr);
        if (!ret)  {
            BTFMSLIM_DBG("Assigned l-addr: 0x%x", slim->laddr);
            break;
        }
        /* Give SLIMBUS time to report present and be ready. */
        usleep_range(1000, 1100);
        BTFMSLIM_DBG("retyring get logical addr");
    } while (time_before(jiffies, timeout));

    return ret;
}

static int btfm_slim_alloc_port(struct btfmslim *btfmslim)
{
    int ret = -EINVAL, i;

    struct btfmslim_ch *rx_chs;
    struct btfmslim_ch *tx_chs;

    if (!btfmslim)
        return ret;

    pr_info(LOGTAG"btfm_slim_alloc_port\n");

    rx_chs = btfmslim->rx_chs;
    tx_chs = btfmslim->tx_chs;

    if (!rx_chs || !tx_chs)
        return ret;

    pr_info(LOGTAG"Rx: id\tname\tport\thdl\tch\tch_hdl");
    for (i = 0 ; (rx_chs->port != BTFM_SLIM_PGD_PORT_LAST) &&
         (i < BTFM_SLIM_NUM_CODEC_DAIS); i++, rx_chs++) {

        /* Get Rx port handler from slimbus driver based
         * on port number
         */
        ret = slim_get_slaveport(btfmslim->slim_pgd->laddr,
                                 rx_chs->port, &rx_chs->port_hdl, SLIM_SINK);
        if (ret < 0) {
            pr_info(LOGTAG"slave port failure port#%d - ret[%d]",
                    rx_chs->port, SLIM_SINK);
            return ret;
        }
        pr_info(LOGTAG"    %d\t%s\t%d\t%x\t%d\t%x", rx_chs->id,
                rx_chs->name, rx_chs->port, rx_chs->port_hdl,
                rx_chs->ch, rx_chs->ch_hdl);
    }

    pr_info(LOGTAG"Tx: id\tname\tport\thdl\tch\tch_hdl");
    for (i = 0; (tx_chs->port != BTFM_SLIM_PGD_PORT_LAST) &&
         (i < BTFM_SLIM_NUM_CODEC_DAIS); i++, tx_chs++) {

        /* Get Tx port handler from slimbus driver based
         * on port number
         */
        ret = slim_get_slaveport(btfmslim->slim_pgd->laddr,
                                 tx_chs->port, &tx_chs->port_hdl, SLIM_SRC);
        if (ret < 0) {
            pr_info(LOGTAG"slave port failure port#%d - ret[%d]",
                    tx_chs->port, SLIM_SRC);
            return ret;
        }
        pr_info(LOGTAG"    %d\t%s\t%d\t%x\t%d\t%x", tx_chs->id,
                tx_chs->name, tx_chs->port, tx_chs->port_hdl,
                tx_chs->ch, tx_chs->ch_hdl);
    }
    return ret;
}

static int btfm_slim_hw_init(struct btfmslim *btfmslim)
{
    int ret;

    struct slim_device *slim = btfmslim->slim_pgd;
    struct slim_device *slim_ifd = &btfmslim->slim_ifd;

    BTFMSLIM_DBG("");
    if (!btfmslim)
        return -EINVAL;

    if (btfmslim->enabled) {
        BTFMSLIM_DBG("Already enabled");
        return 0;
    }
    mutex_lock(&btfmslim->io_lock);
    BTFMSLIM_INFO(
        "PGD Enum Addr: %.02x:%.02x:%.02x:%.02x:%.02x: %.02x",
        slim->e_addr[0], slim->e_addr[1], slim->e_addr[2],
        slim->e_addr[3], slim->e_addr[4], slim->e_addr[5]);
    BTFMSLIM_INFO(
        "IFD Enum Addr: %.02x:%.02x:%.02x:%.02x:%.02x: %.02x",
        slim_ifd->e_addr[0], slim_ifd->e_addr[1],
        slim_ifd->e_addr[2], slim_ifd->e_addr[3],
        slim_ifd->e_addr[4], slim_ifd->e_addr[5]);

    /* Assign Logical Address for PGD (Ported Generic Device)
     * enumeration address
     */
    ret = btfm_slim_get_logical_addr(btfmslim->slim_pgd);
    if (ret) {
        BTFMSLIM_ERR("failed to get slimbus %s logical address: %d",
                     btfmslim->slim_pgd->name, ret);
        goto error;
    }

    /* Assign Logical Address for Ported Generic Device
     * enumeration address
     */
    ret = btfm_slim_get_logical_addr(&btfmslim->slim_ifd);
    if (ret) {
        BTFMSLIM_ERR("failed to get slimbus %s logical address: %d",
                     btfmslim->slim_ifd.name, ret);
        goto error;
    }

    /* Allocate ports with logical address to get port handler from
     * slimbus driver
     */
    ret = btfm_slim_alloc_port(btfmslim);
    if (ret)
        goto error;

    /* Start vendor specific initialization and get port information */
    if (btfmslim->vendor_init)
        ret = btfmslim->vendor_init(btfmslim);

    /* Only when all registers read/write successfully, it set to
     * enabled status
     */
    btfmslim->enabled = 1;
error:
    mutex_unlock(&btfmslim->io_lock);
    return ret;
}

static int btfm_slim_hw_deinit(struct btfmslim *btfmslim)
{
    int ret = 0;

    if (!btfmslim)
        return -EINVAL;

    if (!btfmslim->enabled) {
        BTFMSLIM_DBG("Already disabled");
        return 0;
    }
    mutex_lock(&btfmslim->io_lock);
    btfmslim->enabled = 0;
    mutex_unlock(&btfmslim->io_lock);
    return ret;
}

static int btfm_slim_dai_prepare(struct btfmslim *btfmslim, int soc_dai_id, unsigned int dai_rate)
{
    int i, ret = -EINVAL;

    struct btfmslim_ch *ch;
    uint8_t rxport, grp = false, nchan = 1;

    BTFMSLIM_DBG("dai->id: %d, dai->rate: %d", soc_dai_id, dai_rate);

    /* save sample rate */
    btfmslim->sample_rate = dai_rate;

    switch (soc_dai_id) {
    case BTFM_FM_SLIM_TX:
        grp = true;
        nchan = 2;
        ch = btfmslim->tx_chs;
        rxport = 0;
        break;
    case BTFM_BT_SCO_SLIM_TX:
        ch = btfmslim->tx_chs;
        rxport = 0;
        break;
    case BTFM_BT_SCO_A2DP_SLIM_RX:
        //case BTFM_BT_SPLIT_A2DP_SLIM_RX:
        ch = btfmslim->rx_chs;
        rxport = 1;
        break;
    case BTFM_SLIM_NUM_CODEC_DAIS:
    default:
        BTFMSLIM_ERR("dai->id is invalid:%d", soc_dai_id);
        return ret;
    }

    /* Search for dai->id matched port handler */
    for (i = 0; (i < BTFM_SLIM_NUM_CODEC_DAIS) &&
         (ch->id != BTFM_SLIM_NUM_CODEC_DAIS) &&
         (ch->id != soc_dai_id); ch++, i++)
        ;

    if ((ch->port == BTFM_SLIM_PGD_PORT_LAST) ||
        (ch->id == BTFM_SLIM_NUM_CODEC_DAIS)) {
        BTFMSLIM_ERR("ch is invalid!!");
        return ret;
    }

    ret = btfm_slim_enable_ch(btfmslim, ch, rxport, dai_rate, grp, nchan);

    return ret;
}

/* This function will be called once during boot up */
static int btfm_slim_dai_set_channel_map(struct btfmslim *btfmslim,
        unsigned int tx_num, unsigned int *tx_slot,
        unsigned int rx_num, unsigned int *rx_slot)
{
    int ret = -EINVAL, i;

    struct btfmslim_ch *rx_chs;
    struct btfmslim_ch *tx_chs;

    if (!btfmslim)
        return ret;

    pr_info(LOGTAG"btfm_slim_dai_set_channel_map\n");

    rx_chs = btfmslim->rx_chs;
    tx_chs = btfmslim->tx_chs;

    if (!rx_chs || !tx_chs)
        return ret;

    pr_info(LOGTAG"Rx: id\tname\tport\thdl\tch\tch_hdl");
    for (i = 0; (rx_chs->port != BTFM_SLIM_PGD_PORT_LAST) && (i < rx_num);
         i++, rx_chs++) {
        /* Set Rx Channel number from machine driver and
         * get channel handler from slimbus driver
         */
        rx_chs->ch = *(uint8_t *)(rx_slot + i);
        ret = slim_query_ch(btfmslim->slim_pgd, rx_chs->ch,
                            &rx_chs->ch_hdl);
        if (ret < 0) {
            pr_info(LOGTAG"slim_query_ch failure ch#%d - ret[%d]",
                    rx_chs->ch, ret);
            goto error;
        }
        pr_info(LOGTAG"    %d\t%s\t%d\t%x\t%d\t%x", rx_chs->id,
                rx_chs->name, rx_chs->port, rx_chs->port_hdl,
                rx_chs->ch, rx_chs->ch_hdl);
    }

    pr_info(LOGTAG"Tx: id\tname\tport\thdl\tch\tch_hdl");
    for (i = 0; (tx_chs->port != BTFM_SLIM_PGD_PORT_LAST) && (i < tx_num);
         i++, tx_chs++) {
        /* Set Tx Channel number from machine driver and
         * get channel handler from slimbus driver
         */
        tx_chs->ch = *(uint8_t *)(tx_slot + i);
        ret = slim_query_ch(btfmslim->slim_pgd, tx_chs->ch,
                            &tx_chs->ch_hdl);
        if (ret < 0) {
            pr_info(LOGTAG"slim_query_ch failure ch#%d - ret[%d]",
                    tx_chs->ch, ret);
            goto error;
        }
        pr_info(LOGTAG"    %d\t%s\t%d\t%x\t%d\t%x", tx_chs->id,
                tx_chs->name, tx_chs->port, tx_chs->port_hdl,
                tx_chs->ch, tx_chs->ch_hdl);
    }

error:
    return ret;
}

static int btfm_slim_dai_get_channel_map(struct btfmslim *btfmslim, int dai_id,
        unsigned int *tx_num, unsigned int *tx_slot,
        unsigned int *rx_num, unsigned int *rx_slot)
{
    int i, ret = -EINVAL, *slot = NULL, j = 0, num = 1;

    struct btfmslim_ch *ch = NULL;

    if (!btfmslim)
        return ret;

    switch (dai_id) {
    case BTFM_FM_SLIM_TX:
        num = 2;
    case BTFM_BT_SCO_SLIM_TX:
        if (!tx_slot || !tx_num) {
            BTFMSLIM_ERR("Invalid tx_slot %p or tx_num %p",
                         tx_slot, tx_num);
            return -EINVAL;
        }
        ch = btfmslim->tx_chs;
        if (!ch)
            return -EINVAL;
        slot = tx_slot;
        *rx_slot = 0;
        *tx_num = num;
        *rx_num = 0;
        break;
    case BTFM_BT_SCO_A2DP_SLIM_RX:
        //case BTFM_BT_SPLIT_A2DP_SLIM_RX:
        if (!rx_slot || !rx_num) {
            BTFMSLIM_ERR("Invalid rx_slot %p or rx_num %p",
                         rx_slot, rx_num);
            return -EINVAL;
        }
        ch = btfmslim->rx_chs;
        if (!ch)
            return -EINVAL;
        slot = rx_slot;
        *tx_slot = 0;
        *tx_num = 0;
        *rx_num = num;
        break;
    default:
        BTFMSLIM_ERR("Unsupported DAI %d", dai_id);
        return -EINVAL;
    }

    do {
        if (!ch)
            return -EINVAL;
        for (i = 0; (i < BTFM_SLIM_NUM_CODEC_DAIS) && (ch->id !=
                BTFM_SLIM_NUM_CODEC_DAIS) && (ch->id != dai_id);
             ch++, i++)
            ;

        if (ch->id == BTFM_SLIM_NUM_CODEC_DAIS ||
            i == BTFM_SLIM_NUM_CODEC_DAIS) {
            BTFMSLIM_ERR(
                "No channel has been allocated for dai (%d)",
                dai_id);
            return -EINVAL;
        }
        if (!slot)
            return -EINVAL;
        *(slot + j) = ch->ch;
        BTFMSLIM_DBG("id:%d, port:%d, ch:%d, slot: %d", ch->id,
                     ch->port, ch->ch, *(slot + j));

        /* In case it has mulitiple channels */
        if (++j < num)
            ch++;
    } while (j < num);

    return 0;
}


static void btfm_slim_dai_shutdown(struct btfmslim *btfmslim, int soc_dai_id)
{
    int i;

    struct btfmslim_ch *ch;
    uint8_t rxport, grp = false, nchan = 1;

    BTFMSLIM_DBG("dai->id: %d", soc_dai_id);

    switch (soc_dai_id) {
    case BTFM_FM_SLIM_TX:
        grp = true;
        nchan = 2;
        ch = btfmslim->tx_chs;
        rxport = 0;
        break;
    case BTFM_BT_SCO_SLIM_TX:
        ch = btfmslim->tx_chs;
        rxport = 0;
        break;
    case BTFM_BT_SCO_A2DP_SLIM_RX:
        //case BTFM_BT_SPLIT_A2DP_SLIM_RX:
        ch = btfmslim->rx_chs;
        rxport = 1;
        break;
    case BTFM_SLIM_NUM_CODEC_DAIS:
    default:
        BTFMSLIM_ERR("dai->id is invalid:%d", soc_dai_id);
        return;
    }

    /* Search for dai->id matched port handler */
    for (i = 0; (i < BTFM_SLIM_NUM_CODEC_DAIS) &&
         (ch->id != BTFM_SLIM_NUM_CODEC_DAIS) &&
         (ch->id != soc_dai_id); ch++, i++)
        ;

    if ((ch->port == BTFM_SLIM_PGD_PORT_LAST) ||
        (ch->id == BTFM_SLIM_NUM_CODEC_DAIS)) {
        BTFMSLIM_ERR("ch is invalid!!");
        return;
    }

    btfm_slim_disable_ch(btfmslim, ch, rxport, grp, nchan);

}

static int btfm_slim_remove(struct slim_device *slim)
{
    struct btfmslim *btfm_slim = slim->dev.platform_data;

    pr_info(LOGTAG"btfm_slim_remove\n");

    mutex_destroy(&btfm_slim->io_lock);
    mutex_destroy(&btfm_slim->xfer_lock);

    BTFMSLIM_DBG("slim_remove_device() - btfm_slim->slim_ifd");
    slim_remove_device(&btfm_slim->slim_ifd);

    kfree(btfm_slim);

    BTFMSLIM_DBG("slim_remove_device() - btfm_slim->slim_pgd");
    slim_remove_device(slim);

    return 0;
}

extern int slim_devs_check_slave(void);

static int btfm_slim_ut(void *data)
{
    struct btfmslim *btfm_slim = (struct btfmslim *)data;

    pr_err(LOGTAG"btfm_slim_ut\n");

    /*will wait for all devs discovery*/
    while(slim_devs_check_slave() != SLIM_DEVS_CHECK_OK) {
        msleep(1000);
    }

    pr_err(LOGTAG"btfm_slim_ut start\n");

    /*btfm hw init*/
    btfm_slim_hw_init(btfm_slim);

    /*check*/
    slim_ctrl_devices_list();

    /*map channel*/
    btfm_slim_dai_set_channel_map(btfm_slim, SLAVE_SB_PGD_PORT_TX_NUM, tx_slot, 0, NULL);
    btfm_slim_dai_set_channel_map(btfm_slim, 0, NULL, SLAVE_SB_PGD_PORT_RX_NUM, rx_slot);

    /*enable ch*/
    btfm_slim_dai_prepare(btfm_slim, BTFM_FM_SLIM_TX, 48000);
    btfm_slim_dai_prepare(btfm_slim, BTFM_BT_SCO_SLIM_TX, 8000);  //or 44100
    btfm_slim_dai_prepare(btfm_slim, BTFM_BT_SCO_A2DP_SLIM_RX, 8000);  //44100

    /*disable ch, FM TX*/
    //btfm_slim_dai_shutdown(btfm_slim, BTFM_FM_SLIM_TX);
    //btfm_slim_dai_shutdown(btfm_slim, BTFM_BT_SCO_SLIM_TX);
    //btfm_slim_dai_shutdown(btfm_slim, BTFM_BT_SCO_A2DP_SLIM_RX);

    return 0;
}


#define SLIM_SLAVE_INIT btfm_slim_slave_hw_init
#define SLIM_SLAVE_PORT_EN btfm_slim_slave_enable_port

static int btfm_slim_probe(struct slim_device *slim)
{
    int ret = 0;
    struct btfmslim *btfm_slim;
    struct slim_device *slim_gd;
    struct slim_device *slim_ifd;

    pr_info(LOGTAG"btfm_slim_probe(from slim_btfm_api)\n");

    /*check version define*/
#if MASTER_SLAVE_NORMAL_VERSION  /*normal JR510 version*/
    pr_info(LOGTAG"MASTER_SLAVE_NORMAL_VERSION\n");
#endif
#if MASTER_SLAVE_NORMAL_TEST
    pr_info(LOGTAG"MASTER_SLAVE_NORMAL_TEST\n");
#endif
#if MASTER_LOOPBACK_TEST
    pr_info(LOGTAG"MASTER_LOOPBACK_TEST\n");
#endif
#if MASTER_SLAVE_LOOPBACK_TEST
    pr_info(LOGTAG"MASTER_SLAVE_LOOPBACK_TEST\n");
#endif

    if (!slim->ctrl)
        return -EINVAL;

    /* Allocation btfmslim data pointer */
    btfm_slim = kzalloc(sizeof(struct btfmslim), GFP_KERNEL);
    if (btfm_slim == NULL) {
        pr_info(LOGTAG"error, allocation failed");
        return -ENOMEM;
    }
    /* BTFM Slimbus driver control data configuration */
    btfm_slim->slim_pgd = slim;

    /* Assign vendor specific function */
    btfm_slim->rx_chs = SLIM_SLAVE_RXPORT;
    btfm_slim->tx_chs = SLIM_SLAVE_TXPORT;
    btfm_slim->vendor_init = SLIM_SLAVE_INIT;
    btfm_slim->vendor_port_en = SLIM_SLAVE_PORT_EN;

    /* Created Mutex for slimbus data transfer */
    mutex_init(&btfm_slim->io_lock);
    mutex_init(&btfm_slim->xfer_lock);

    /* Get Device tree node for Interface Device enumeration address */
    ret = btfm_slim_get_dt_info(btfm_slim);
    if (ret)
        goto dealloc;

    /* Add Interface Device for slimbus driver */
    ret = slim_add_device(btfm_slim->slim_pgd->ctrl, &btfm_slim->slim_ifd);
    if (ret) {
        pr_info(LOGTAG"error, adding SLIMBUS device failed");
        goto dealloc;
    }

    /* Platform driver data allocation */
    slim->dev.platform_data = btfm_slim;

    /* Driver specific data allocation */
    btfm_slim->dev = &slim->dev;

    /*slave slimbus device*/
    slim_gd = btfm_slim->slim_pgd;
    slim_ifd = &btfm_slim->slim_ifd;

    /*-------start verification---------*/
#if SLAVE_SLIM_BTFM_UT
    {
        struct task_struct  *btfm_slim_ut_thread_task;
        btfm_slim_ut_thread_task = kthread_run(btfm_slim_ut, btfm_slim,
                                              "btfm_slim_ut_thread");
        if (IS_ERR(btfm_slim_ut_thread_task)) {
            pr_info(LOGTAG"Failed to start btfm_slim_ut_thread_task\n");
        }
    }
#endif

#if 0

    /*btfm hw init*/
    btfm_slim_hw_init(btfm_slim);

    /*check*/
    slim_ctrl_devices_list();

#if (MASTER_SLAVE_NORMAL_VERSION | MASTER_SLAVE_NORMAL_TEST)
    /*map channel*/
    btfm_slim_dai_set_channel_map(btfm_slim, SLAVE_SB_PGD_PORT_TX_NUM, tx_slot, 0, NULL);
    btfm_slim_dai_set_channel_map(btfm_slim, 0, NULL, SLAVE_SB_PGD_PORT_RX_NUM, rx_slot);

    /*enable ch*/
    btfm_slim_dai_prepare(btfm_slim, BTFM_FM_SLIM_TX, 48000);
    btfm_slim_dai_prepare(btfm_slim, BTFM_BT_SCO_SLIM_TX, 8000);  //or 44100
    btfm_slim_dai_prepare(btfm_slim, BTFM_BT_SCO_A2DP_SLIM_RX, 8000);  //44100

    /*disable ch, FM TX*/
    //btfm_slim_dai_shutdown(btfm_slim, BTFM_FM_SLIM_TX);
    //btfm_slim_dai_shutdown(btfm_slim, BTFM_BT_SCO_SLIM_TX);
    //btfm_slim_dai_shutdown(btfm_slim, BTFM_BT_SCO_A2DP_SLIM_RX);
#endif

#endif


    return ret;

dealloc:
    slim_remove_device(&btfm_slim->slim_ifd);
    mutex_destroy(&btfm_slim->io_lock);
    mutex_destroy(&btfm_slim->xfer_lock);
    kfree(btfm_slim);
    return ret;
}


static const struct slim_device_id btfm_slim_id[] = {
    {SLIM_SLAVE_COMPATIBLE_STR, 0},
    {}
};

static struct slim_driver btfm_slim_driver = {
    .driver = {
        .name = "btfmslim-driver",
        .owner = THIS_MODULE,
    },
    .probe = btfm_slim_probe,
    .remove = btfm_slim_remove,
    .id_table = btfm_slim_id
};

static int __init btfm_slim_init(void)
{
    int ret;

    pr_info(LOGTAG"btfm_slim_init(from slim_btfm_api)\n");
    ret = slim_driver_register(&btfm_slim_driver);
    if (ret)
        pr_info(LOGTAG"Failed to register slimbus driver: %d", ret);
    return ret;
}

static void __exit btfm_slim_exit(void)
{
    pr_info(LOGTAG"btfm_slim_exit\n");
    slim_driver_unregister(&btfm_slim_driver);
}

#if SLAVE_SLIM_BTFM_UT
late_initcall(btfm_slim_init);
module_exit(btfm_slim_exit);
#endif

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BTFM Slimbus Slave driver");
