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
//#define DEBUG

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/debugfs.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <sound/tlv.h>
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
#include <linux/kthread.h>
#include <linux/slimbus/slimbus.h>
#include <linux/of_slimbus.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "slim_api.h"
#include <dsp/apr_audio-v2.h>

struct slim_device sdev_test;
static struct slim_device *g_sdev;

#define JLQ_DAI_SLIM_API_SND_HW_PARAMS  1   /*for normal version, hw params by snd, set 1 */

/*device dependency*/
#define JLQ_DAI_SLIM_NAME  "jlq-dai-slim"
#define JLQ_SLIM_DEV_NAME  "masterslim-gd"

/*DPort*/
typedef enum {
    DPORT_NUM_0 = 0,
    DPORT_NUM_1 = 1,
    DPORT_NUM_2 = 2,
    DPORT_NUM_3 = 3,
    DPORT_MAX_NUM
} DPORT_INDEX;

/*master Port Map*/
#define MASTER_SB_PGD_PORT_RX_SCO			DPORT_NUM_3     //slave dport0
#define MASTER_SB_PGD_PORT_RX1_FM			DPORT_NUM_1     //slave dport1
#define MASTER_SB_PGD_PORT_RX2_FM			DPORT_NUM_2     //slave dport2
#define MASTER_SB_PGD_PORT_TX_SCO			DPORT_NUM_0     //slave dport16

#define MASTER_SB_PGD_PORT_RX_NUM			3
#define MASTER_SB_PGD_PORT_TX_NUM			1


/*master channels*/
enum MASTER_SB_CHS_INDEX {
    MASTER_CHS_ID_0 = 0,   /*M_BT_SCO_SLIM_RX*/
    MASTER_CHS_ID_1,       /*M_FM_SLIM_RX*/
    MASTER_CHS_ID_2,       /*M_FM_SLIM_RX*/
    MASTER_CHS_ID_3,       /*M_BT_SCO_SLIM_TX*/
    MASTER_CHS_NUMBER
};

/*master DAI, according with BE cpu dai*/
#define SLIMBUS_7_RX		0x400e
#define SLIMBUS_7_TX		0x400f
#define SLIMBUS_8_RX		0x4010
#define SLIMBUS_8_TX		0x4011

enum {
    MASTER_FM_SLIM_TX = SLIMBUS_8_TX,
    MASTER_BT_SCO_SLIM_TX = SLIMBUS_7_TX,
    MASTER_BT_SCO_SLIM_RX = SLIMBUS_7_RX,

    MASTER_SLIM_NUM_CODEC_DAIS = 3
};

#define SLIM_DAI_RATES (SNDRV_PCM_RATE_48000 | \
			SNDRV_PCM_RATE_8000 | \
			SNDRV_PCM_RATE_16000 | \
			SNDRV_PCM_RATE_96000 | \
			SNDRV_PCM_RATE_192000 | \
			SNDRV_PCM_RATE_384000)

#define SLIM_DAI_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			  SNDRV_PCM_FMTBIT_S24_LE | \
			  SNDRV_PCM_FMTBIT_S32_LE)

#define DAI_STATE_INITIALIZED (0x01 << 0)
#define DAI_STATE_PREPARED (0x01 << 1)
#define DAI_STATE_RUNNING (0x01 << 2)

#define SET_DAI_STATE(status, state) \
	(status |= state)

#define CLR_DAI_STATE(status, state) \
	(status = status & (~state))


struct jlq_slim_dai_data {
    unsigned int dai_id;
    u16 *chan_h;   /*ch handle*/
    u16 *sh_ch;    /*ch*/
    u16 grph;
    u32 rate;
    u16 bits;
    u16 ch_cnt;
    u8 status;
    struct snd_soc_dai_driver *dai_drv;
    struct slim_port_cfg port_cfg;
    struct slim_device *sdev;
    u32 ph[2];    /*Port Handle, fm tx group will use ph[0] and ph[1]*/
};

struct jlq_dai_slim_drv_data {
    struct slim_device *sdev;
    u16 num_dais;
    struct jlq_slim_dai_data slim_dai_data[MASTER_SLIM_NUM_CODEC_DAIS];
};

struct jlq_slim_dai_data *jlq_slim_get_dai_data(
    struct jlq_dai_slim_drv_data *drv_data,
    int dai_id)
{
    struct jlq_slim_dai_data *dai_data_t;
    int i;

    for (i = 0; i < drv_data->num_dais; i++) {
        dai_data_t = &drv_data->slim_dai_data[i];
        if (dai_data_t->dai_id == dai_id)
            return dai_data_t;
    }

    pr_info(LOGTAG"%s: no dai data found for dai_id %d\n",
            __func__, dai_id);
    return NULL;
}

int jlq_dai_slim_startup(struct snd_pcm_substream *substream,
                         struct snd_soc_dai *dai)
{
    int ret = 0;

    pr_info(LOGTAG"%s, substream = %s  stream = %d dai->name = %s",
            __func__, substream->name, substream->stream, dai->name);

    return ret;
}
EXPORT_SYMBOL_GPL(jlq_dai_slim_startup);

int jlq_dai_slim_hw_params(
    struct snd_pcm_substream *substream,
    struct snd_pcm_hw_params *params,
    struct snd_soc_dai *dai)
{
    struct jlq_dai_slim_drv_data *drv_data;
    struct jlq_slim_dai_data *dai_data;
    int rc = 0;
    int dai_id;
    (void)substream;

    pr_info(LOGTAG"%s, substream = %s, stream = %d, dai->name = %s",
            __func__, substream->name, substream->stream, dai->name);

    //for debug.
    pr_info(LOGTAG"%s, dai->rate = %d, dai->channels = %d, dai->sample_bits = %d",
            __func__, dai->rate, dai->channels, dai->sample_bits);

    pr_info(LOGTAG"%s, params_rate(params) = %d, params_channels(params) = %d, params_format(params) = %d",
            __func__, params_rate(params), params_channels(params), params_format(params));

    if(!g_sdev) {
        pr_err(LOGTAG"%s: g_sdev not inited %d\n");
        return -EINVAL;
    }
    drv_data = dev_get_drvdata(&g_sdev->dev);

    dai_id = dai->id;
    if(dai_id < 0) {
        pr_err(LOGTAG"%s: invalid dai_id %d\n", __func__, dai_id);
        return -EINVAL;
    }

    dai_data = jlq_slim_get_dai_data(drv_data, dai_id);
    if (!dai_data) {
        pr_err(LOGTAG"%s: Invalid dai_data for dai_id %d\n",
               __func__, dai_id);
        rc = -EINVAL;
        goto done;
    }

#if JLQ_DAI_SLIM_API_SND_HW_PARAMS  /*dai use this*/
    if (!dai_data->ch_cnt || dai_data->ch_cnt != params_channels(params)) {
        pr_err(LOGTAG"%s: invalid ch_cnt %d %d\n",
               __func__, dai_data->ch_cnt, params_channels(params));
        rc = -EINVAL;
        goto done;
    }

    dai_data->rate = params_rate(params);
    dai_data->port_cfg.port_opts = SLIM_OPT_NONE;
    if (dai_data->rate >= SNDRV_PCM_RATE_48000)
        dai_data->port_cfg.watermark = 16;
    else
        dai_data->port_cfg.watermark = 8;

    switch (params_format(params)) {
    case SNDRV_PCM_FORMAT_S16_LE:
        dai_data->bits = 16;
        break;
    case SNDRV_PCM_FORMAT_S24_LE:
        dai_data->bits = 24;
        break;
    case SNDRV_PCM_FORMAT_S32_LE:
        dai_data->bits = 32;
        break;
    default:
        pr_err(LOGTAG"%s: invalid format %d\n", __func__,
               params_format(params));
        rc = -EINVAL;
        goto done;
    }

#else
    switch (dai_id) {
    case MASTER_FM_SLIM_TX:
        dai_data->rate = 48000;
        dai_data->ch_cnt = 2;
        break;
    case MASTER_BT_SCO_SLIM_TX:
        dai_data->rate = 8000;
        dai_data->ch_cnt = 1;
        break;
    case MASTER_BT_SCO_SLIM_RX:
        dai_data->rate = 8000;
        dai_data->ch_cnt = 1;
        break;

    default:
        pr_err(LOGTAG"dai->id is invalid:%d", dai_id);
        return -EINVAL;
    }

    dai_data->port_cfg.watermark = 16;
    dai_data->bits = 16;
#endif

    pr_info(LOGTAG"%s: ch_cnt=%u rate=%u, bit_width = %u\n",
            __func__, dai_data->ch_cnt, dai_data->rate,
            dai_data->bits);
done:
    return rc;
}
EXPORT_SYMBOL_GPL(jlq_dai_slim_hw_params);


int jlq_dai_slim_set_channel_map(struct snd_soc_dai *dai,
                                 unsigned int tx_num, unsigned int *tx_slot,
                                 unsigned int rx_num, unsigned int *rx_slot)
{
    struct jlq_dai_slim_drv_data *drv_data;
    struct jlq_slim_dai_data *dai_data;
    struct snd_soc_dai_driver *dai_drv;
    int dai_id;
    u8 i = 0;

    pr_info(LOGTAG"%s(dai id:%d tx_num=%u, rx_num=%u)\n", __func__, dai->id, tx_num, rx_num);

    if(!g_sdev) {
        pr_err(LOGTAG"%s: g_sdev not inited %d\n");
        return -EINVAL;
    }
    drv_data = dev_get_drvdata(&g_sdev->dev);

    dai_id = dai->id;
    if(dai_id < 0) {
        pr_err(LOGTAG"%s: invalid dai_id %d\n", __func__, dai_id);
        return -EINVAL;
    }

    dai_data = jlq_slim_get_dai_data(drv_data, dai_id);
    if (!dai_data) {
        pr_err(LOGTAG"%s: Invalid dai_data for dai_id %d\n",
               __func__, dai_id);
        return -EINVAL;
    }

    dai_drv = dai_data->dai_drv;

    /*master slim tx ch*/
    if (tx_num > dai_drv->playback.channels_max) {
        pr_err(LOGTAG"%s: tx_num %u max out master port cnt(playback.channels_max:%d)\n",
               __func__, tx_num, dai_drv->playback.channels_max);
        return -EINVAL;
    }

    for (i = 0; i < tx_num; i++) {
        dai_data->sh_ch[i] = tx_slot[i];
        pr_info(LOGTAG"%s: tx_slot %d \n", __func__, tx_slot[i]);
    }

    /*master slim rx ch*/
    if (rx_num > dai_drv->capture.channels_max) {
        pr_err(LOGTAG"%s: rx_num %u max out master port cnt(capture.channels_max:%d)\n",
               __func__, rx_num, dai_drv->capture.channels_max);
        return -EINVAL;
    }

    for (i = 0; i < rx_num; i++) {
        dai_data->sh_ch[i] = rx_slot[i];
        pr_info(LOGTAG"%s: rx_slot %d\n", __func__, rx_slot[i]);
    }

    dai_data->ch_cnt = tx_num + rx_num;
    return 0;
}
EXPORT_SYMBOL_GPL(jlq_dai_slim_set_channel_map);

#define SLIM_PORT_HDL(la, f, p) ((la)<<24 | (f) << 16 | (p))
#define SLIM_LA_MANAGER		0xFF

static int jlq_slim_alloc_mgrports(struct slim_device *sb, enum slim_port_req req,
                                   int nports, u32 *rh, int hsz, int dai_id)
{
    int i, j;
    int ret = -EINVAL;
    int nphysp = nports;
    struct slim_controller *ctrl = sb->ctrl;

    pr_info(LOGTAG"%s(dai id: %d)\n", __func__, dai_id);

    if (!rh || !ctrl)
        return -EINVAL;
    if (req == SLIM_REQ_HALF_DUP)
        nphysp *= 2;
    if (hsz / sizeof(u32) < nphysp)
        return -EINVAL;
    mutex_lock(&ctrl->m_ctrl);

    /* PGD Port Map */
    i = 0;
    switch(dai_id) {
    case MASTER_FM_SLIM_TX:
        i = MASTER_SB_PGD_PORT_RX1_FM;
        break;
    case MASTER_BT_SCO_SLIM_TX:
        i = MASTER_SB_PGD_PORT_RX_SCO;
        break;
    case MASTER_BT_SCO_SLIM_RX:
        i = MASTER_SB_PGD_PORT_TX_SCO;
        break;
    }

    for (/*i = 0*/; i < ctrl->nports; i++) {
        bool multiok = true;

        if (ctrl->ports[i].state != SLIM_P_FREE)
            continue;
        /* Start half duplex channel at even port */
        if (req == SLIM_REQ_HALF_DUP && (i % 2))
            continue;
        /* Allocate ports contiguously for multi-ch */
        if (ctrl->nports < (i + nphysp)) {
            i = ctrl->nports;
            break;
        }
        if (req == SLIM_REQ_MULTI_CH) {
            multiok = true;
            for (j = i; j < i + nphysp; j++) {
                if (ctrl->ports[j].state != SLIM_P_FREE) {
                    multiok = false;
                    break;
                }
            }
            if (!multiok)
                continue;
        }

        break;
    }
    if (i >= ctrl->nports) {
        ret = -EDQUOT;
        goto alloc_err;
    }
    ret = 0;
    for (j = i; j < i + nphysp; j++) {
        ctrl->ports[j].state = SLIM_P_UNCFG;
        ctrl->ports[j].req = req;
        /*
        if (req == SLIM_REQ_HALF_DUP && (j % 2))
        	ctrl->ports[j].flow = SLIM_SINK;
        else
        	ctrl->ports[j].flow = SLIM_SRC;
        */
        switch(dai_id) {
        case MASTER_FM_SLIM_TX:
            ctrl->ports[j].flow = SLIM_SINK;
            break;
        case MASTER_BT_SCO_SLIM_TX:
            ctrl->ports[j].flow = SLIM_SINK;
            break;
        case MASTER_BT_SCO_SLIM_RX:
            ctrl->ports[j].flow = SLIM_SRC;
            break;
        }

        if (ctrl->alloc_port)
            ret = ctrl->alloc_port(ctrl, j);
        if (ret) {
            for (; j >= i; j--)
                ctrl->ports[j].state = SLIM_P_FREE;
            goto alloc_err;
        }
        //*rh = SLIM_PORT_HDL(SLIM_LA_MANAGER, 0, j);
        *rh = SLIM_PORT_HDL(sb->laddr, ctrl->ports[j].flow, j);
        pr_info(LOGTAG"%s succ, dai id: %d, port: %d, porth: %.8x\n", __func__, dai_id, j, *rh);
        rh++;
    }
alloc_err:
    mutex_unlock(&ctrl->m_ctrl);
    return ret;
}

int jlq_dai_slim_prepare(struct snd_pcm_substream *substream,
                         struct snd_soc_dai *dai)
{
    struct jlq_dai_slim_drv_data *drv_data;
    struct jlq_slim_dai_data *dai_data = NULL;
    struct slim_ch prop;
    int rc;
    uint32_t rates;
    uint8_t rxport, grp = false, nchan = 1;
    u8 i, j;
    int dai_id;
    (void)substream;

    pr_info(LOGTAG"%s, substream = %s  stream = %d dai->name = %s",
            __func__, substream->name, substream->stream, dai->name);

    if(!g_sdev) {
        pr_err(LOGTAG"%s: g_sdev not inited %d\n");
        return -EINVAL;
    }
    drv_data = dev_get_drvdata(&g_sdev->dev);

    dai_id = dai->id;
    if(dai_id < 0) {
        pr_err(LOGTAG"%s: invalid dai_id %d\n", __func__, dai_id);
        return -EINVAL;
    }

    dai_data = jlq_slim_get_dai_data(drv_data, dai_id);
    if (!dai_data) {
        pr_err(LOGTAG"%s: Invalid dai_data for dai %d\n",
               __func__, dai_id);
        return -EINVAL;
    }

    pr_info(LOGTAG"%s: dai id (%d) has state 0x%x\n",
            __func__, dai_id, dai_data->status);

    if (!(dai_data->status & DAI_STATE_INITIALIZED)) {
        pr_err(LOGTAG"%s: dai id (%d) has invalid state 0x%x\n",
               __func__, dai_id, dai_data->status);
        return -EINVAL;
    }

    if (!(dai_data->status & DAI_STATE_PREPARED)) {
        switch (dai_id) {
        case MASTER_FM_SLIM_TX:
            grp = true;
            nchan = 2;
            rxport = 1;
            break;
        case MASTER_BT_SCO_SLIM_TX:
            rxport = 1;
            break;
        case MASTER_BT_SCO_SLIM_RX:
            rxport = 0;
            break;
        default:
            pr_err(LOGTAG"dai->id is invalid:%d", dai_id);
            return -EINVAL;
        }

        for (i = 0; i < dai_data->ch_cnt; i++) {
            rc = slim_query_ch(drv_data->sdev, dai_data->sh_ch[i],
                               &dai_data->chan_h[i]);
            if (rc) {
                pr_err(LOGTAG"%s:query chan handle failed rc %d\n",
                       __func__, rc);
                goto error_chan_query;
            }

            pr_info(LOGTAG"%s query_ch ok(ch: %d, chan_h: %d)\n", __func__, dai_data->sh_ch[i], dai_data->chan_h[i]);
        }

        /* Define the channel with below parameters */
        rates = dai_data->rate;
        prop.prot =  ((rates == 44100) || (rates == 88200)) ?
                     SLIM_PUSH : SLIM_AUTO_ISO;
        prop.baser = ((rates == 44100) || (rates == 88200)) ?
                     SLIM_RATE_11025HZ : SLIM_RATE_4000HZ;
        prop.dataf = ((rates == 48000) || (rates == 44100) ||
                      (rates == 88200) || (rates == 96000)) ?
                     SLIM_CH_DATAF_NOT_DEFINED : SLIM_CH_DATAF_LPCM_AUDIO;

        /* for feedback channel, PCM bit should not be set */
        //prop.dataf = SLIM_CH_DATAF_NOT_DEFINED;  /*FM 48K NOT_DEFINED, SCO 8k/16k LPCM*/

        prop.auxf = SLIM_CH_AUXF_NOT_APPLICABLE;
        prop.ratem = ((rates == 44100) || (rates == 88200)) ?
                     (rates / 11025) : (rates / 4000);
        prop.sampleszbits = 16;  //dai_data->bits;

        /*define ch*/
        rc = slim_define_ch(drv_data->sdev, &prop, dai_data->chan_h,
                            dai_data->ch_cnt, grp, &dai_data->grph);

        if (rc) {
            pr_err(LOGTAG"%s:define chan failed rc %d\n",
                   __func__, rc);
            goto error_define_chan;
        }

        /*alloc port*/
        rc = jlq_slim_alloc_mgrports(drv_data->sdev,
                                     SLIM_REQ_DEFAULT, dai_data->ch_cnt,
                                     dai_data->ph,
                                     sizeof(dai_data->ph), dai_id);
        if (rc < 0) {
            pr_err(LOGTAG"%s:alloc mgrport failed rc %d\n",
                   __func__, rc);
            goto error_mgrports;
        }

        rc = slim_config_mgrports(drv_data->sdev, dai_data->ph,
                                  dai_data->ch_cnt,
                                  &(dai_data->port_cfg));
        if (rc < 0) {
            pr_err(LOGTAG"%s: config mgrport failed rc %d\n",
                   __func__, rc);
            goto error_mgrports;
        }

        pr_info(LOGTAG"%s config_mgrports ok\n", __func__);

        /* Mark stream status as prepared */
        SET_DAI_STATE(dai_data->status, DAI_STATE_PREPARED);

    }

    if (!(dai_data->status & DAI_STATE_RUNNING)) {
        if (rxport) {
            for (i = 0; i < dai_data->ch_cnt; i++) {
                rc = slim_connect_sink(drv_data->sdev,
                                       &dai_data->ph[i], 1,
                                       dai_data->chan_h[i]);
                if (rc < 0) {
                    pr_err(LOGTAG"%s: slim_connect_sink failed, porth: %.8x, chan_h: %.8x, err = %d\n",
                           __func__, dai_data->ph[i], dai_data->chan_h[i], rc);
                    goto err_connect;
                }

                pr_info(LOGTAG"%s slim_connect_sink ok(ph: %.8x, ch_h: %d)\n", __func__, dai_data->ph[i], dai_data->chan_h[i]);
            }
        } else {
            for (i = 0; i < dai_data->ch_cnt; i++) {
                rc = slim_connect_src(drv_data->sdev, dai_data->ph[i],
                                      dai_data->chan_h[i]);
                if (rc < 0) {
                    pr_err(LOGTAG"%s: slim_connect_sink failed, porth: %.8x, chan_h: %.8x, err = %d\n",
                           __func__, dai_data->ph[i], dai_data->chan_h[i], rc);
                    goto err_connect;
                }

                pr_info(LOGTAG"%s slim_connect_src ok(ph: %.8x, ch_h: %d)\n", __func__, dai_data->ph[i], dai_data->chan_h[i]);
            }
        }

        /*enable ch*/
        rc = slim_control_ch(drv_data->sdev,
                             (grp ? dai_data->grph : * (dai_data->chan_h)),
                             SLIM_CH_ACTIVATE, true);
        if (rc < 0) {
            pr_err(LOGTAG"%s: SLIM_CH_ACTIVATE failed, err = %d\n",
                   __func__, rc);
            goto err_control;
        }

        pr_info(LOGTAG"%s slim_control_ch SLIM_CH_ACTIVATE ok\n", __func__);

        /* Mark dai status as running */
        SET_DAI_STATE(dai_data->status, DAI_STATE_RUNNING);
    }

    pr_info(LOGTAG"%s succ\n", __func__);

    return rc;

error_mgrports:
err_connect:
err_control:
    slim_dealloc_mgrports(drv_data->sdev,
                          dai_data->ph, dai_data->ch_cnt);

error_define_chan:
error_chan_query:
    for (j = 0; j < i; j++)
        slim_dealloc_ch(drv_data->sdev, dai_data->chan_h[j]);
    return rc;
}
EXPORT_SYMBOL_GPL(jlq_dai_slim_prepare);


void jlq_dai_slim_shutdown(struct snd_pcm_substream *substream,
                           struct snd_soc_dai *dai)
{
    struct jlq_dai_slim_drv_data *drv_data;
    struct jlq_slim_dai_data *dai_data;
    int i, rc = 0;
    int dai_id;
    uint8_t rxport, grp = false, nchan = 1;
    (void)substream;

    pr_info(LOGTAG"%s, substream = %s  stream = %d dai->name = %s",
            __func__, substream->name, substream->stream, dai->name);

    if(!g_sdev) {
        pr_err(LOGTAG"%s: g_sdev not inited %d\n");
        return;
    }
    drv_data = dev_get_drvdata(&g_sdev->dev);

    dai_id = dai->id;
    if(dai_id < 0) {
        pr_err(LOGTAG"%s: invalid dai_id %d\n", __func__, dai_id);
        return;
    }
    dai_data = jlq_slim_get_dai_data(drv_data, dai_id);

    switch (dai_id) {
    case MASTER_FM_SLIM_TX:
        grp = true;
        nchan = 2;
        rxport = 1;
        break;
    case MASTER_BT_SCO_SLIM_TX:
        rxport = 1;
        break;
    case MASTER_BT_SCO_SLIM_RX:
        rxport = 0;
        break;

    default:
        pr_info(LOGTAG"dai->id is invalid:%d", dai_id);
        return;
    }

    pr_info(LOGTAG"%s: dai id (%d) has state 0x%x\n",
            __func__, dai_id, dai_data->status);

    if ((dai_data->status & DAI_STATE_RUNNING)) {
        rc = slim_control_ch(drv_data->sdev,
                             (grp ? dai_data->grph : * (dai_data->chan_h)),
                             SLIM_CH_REMOVE, true);
        if (rc < 0) {
            pr_info(LOGTAG"%s: SLIM_CH_REMOVE failed, err = %d\n",
                    __func__, rc);
            goto err;
        }

        /*disconnect port and demalloc mgrports*/
        for (i = 0; i < dai_data->ch_cnt; i++) {
            rc = slim_disconnect_ports(drv_data->sdev, &dai_data->ph[i], 1);
            if (rc < 0) {
                pr_err(LOGTAG"%s: slim_disconnect_ports failed, porth: %.8x, err = %d\n",
                       __func__, dai_data->ph[i], rc);
                goto err;
            }

            /* clear running state for the dai */
            CLR_DAI_STATE(dai_data->status, DAI_STATE_RUNNING);
        }
    }

    if ((dai_data->status & DAI_STATE_PREPARED)) {
        for (i = 0; i < dai_data->ch_cnt; i++) {
            rc = slim_dealloc_mgrports(drv_data->sdev, &dai_data->ph[i], 1);
            if (rc < 0) {
                pr_err(LOGTAG"%s: slim_dealloc_mgrports failed, err = %d\n",
                       __func__, rc);
                goto err;
            }

            rc = slim_dealloc_ch(drv_data->sdev, dai_data->chan_h[i]);
            if (rc < 0) {
                pr_err(LOGTAG"%s: dealloc_ch failed, err = %d\n",
                       __func__, rc);
                goto err;
            }
        }

        /* clear prepared state for the dai */
        CLR_DAI_STATE(dai_data->status, DAI_STATE_PREPARED);
    }
    return;
err:
    pr_info(LOGTAG"%s: there are error!\n", __func__);
    return;
}
EXPORT_SYMBOL_GPL(jlq_dai_slim_shutdown);


static int jlq_slim_comp_probe(struct snd_soc_component *codec)
{
    pr_info("jlq_slim_comp_probe, codec->name: %s\n", codec->name);

    return 0;
}

static const struct snd_soc_component_driver jlq_dai_slim_component = {
    .name       = "jlq-dai-slim-cmpnt",
    .probe	= jlq_slim_comp_probe,
};

static struct snd_soc_dai_ops jlq_dai_slim_ops = {
    .startup    = jlq_dai_slim_startup,
    .prepare    = jlq_dai_slim_prepare,
    .hw_params  = jlq_dai_slim_hw_params,
    .shutdown   = jlq_dai_slim_shutdown,
    .set_channel_map = jlq_dai_slim_set_channel_map,
};

static struct snd_soc_dai_driver jlq_slim_dais[] = {
    {	/* FM Audio data multiple channel  : FM -> qdsp */
        .name = "master_fm_slim_tx",
        .id = MASTER_FM_SLIM_TX,
        .capture = {
            .stream_name = "MASTER FM TX Capture",
            .aif_name = "MASTER_SLIMBUS_8_TX",
            .rates = SNDRV_PCM_RATE_48000, /* 48 KHz */
            .formats = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits */
            .rate_max = 48000,
            .rate_min = 48000,
            .channels_min = 1,
            .channels_max = 2,
        },
        .ops = &jlq_dai_slim_ops,
    },
    {	/* Bluetooth SCO voice uplink: bt -> modem */
        .name = "master_bt_sco_slim_tx",
        .id = MASTER_BT_SCO_SLIM_TX,
        .capture = {
            .stream_name = "MASTER SCO TX Capture",
            /* 8 KHz or 16 KHz */
            .rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000
            | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000
            | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000,
            .formats = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits */
            .rate_max = 96000,
            .rate_min = 8000,
            .channels_min = 1,
            .channels_max = 1,
        },
        .ops = &jlq_dai_slim_ops,
    },
    {	/* Bluetooth SCO voice downlink: modem -> bt */
        .name = "master_bt_sco_slim_rx",
        .id = MASTER_BT_SCO_SLIM_RX,
        .playback = {
            .stream_name = "MASTER SCO RX Playback",
            /* 8/16/44.1/48/88.2/96 Khz */
            .rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000
            | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000
            | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000,
            .formats = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits */
            .rate_max = 96000,
            .rate_min = 8000,
            .channels_min = 1,
            .channels_max = 1,
        },
        .ops = &jlq_dai_slim_ops,
    },
};

static int jlq_dai_slim_populate_dai_data(struct device *dev,
        struct jlq_dai_slim_drv_data *drv_data)
{
    struct snd_soc_dai_driver *dai_drv;
    struct jlq_slim_dai_data *dai_data_t;
    u8 num_ch;
    int i, j, rc;

    pr_info(LOGTAG"%s\n", __func__);

    for (i = 0; i < drv_data->num_dais; i++) {
        num_ch = 0;
        dai_drv = &jlq_slim_dais[i];
        num_ch += dai_drv->capture.channels_max;
        num_ch += dai_drv->playback.channels_max;

        dai_data_t = &drv_data->slim_dai_data[i];
        dai_data_t->dai_drv = dai_drv;
        dai_data_t->dai_id = dai_drv->id;
        dai_data_t->sdev = drv_data->sdev;

        SET_DAI_STATE(dai_data_t->status,
                      DAI_STATE_INITIALIZED);

        dai_data_t->chan_h = devm_kzalloc(dev,
                                          sizeof(u16) * num_ch,
                                          GFP_KERNEL);

        if (!dai_data_t->chan_h) {
            pr_err(LOGTAG"%s: DAI ID %d, Failed to alloc channel handles\n",
                   __func__, i);
            rc = -ENOMEM;
            goto err_mem_alloc;
        }

        dai_data_t->sh_ch = devm_kzalloc(dev,
                                         sizeof(u16) * num_ch,
                                         GFP_KERNEL);

        if (!dai_data_t->sh_ch) {
            pr_err(LOGTAG"%s: DAI ID %d, Failed to alloc sh_ch\n",
                   __func__, i);
            rc = -ENOMEM;
            goto err_mem_alloc;
        }
    }
    return 0;

err_mem_alloc:
    for (j = 0; j < i; j++) {
        dai_data_t = &drv_data->slim_dai_data[i];

        devm_kfree(dev, dai_data_t->chan_h);
        dai_data_t->chan_h = NULL;

        devm_kfree(dev, dai_data_t->sh_ch);
        dai_data_t->sh_ch = NULL;
    }
    return rc;
}


static void jlq_dai_slim_remove_dai_data(
    struct device *dev,
    struct jlq_dai_slim_drv_data *drv_data)
{
    int i;
    struct jlq_slim_dai_data *dai_data_t;

    pr_info(LOGTAG"%s\n", __func__);

    for (i = 0; i < drv_data->num_dais; i++) {
        dai_data_t = &drv_data->slim_dai_data[i];

        kfree(dai_data_t->chan_h);
        dai_data_t->chan_h = NULL;
        kfree(dai_data_t->sh_ch);
        dai_data_t->sh_ch = NULL;
    }
}


static int jlq_dai_slim_dev_probe(struct slim_device *sdev)
{
    int rc;

    struct jlq_dai_slim_drv_data *drv_data;
    struct device *dev = &sdev->dev;
    g_sdev = sdev;
/*
    if(master_slimbus_state_get() != MASTER_SLIM_STAT_INITED) {
        pr_err(LOGTAG"defering %s, master_slimbus NOT INITED\n", __func__);
        return -EPROBE_DEFER;
    }
*/
    pr_info(LOGTAG"jlq_dai_slim_dev_probe(from asoc), sdev->name: %s, sdev->laddr: %d\n", sdev->name, sdev->laddr);

    drv_data = devm_kzalloc(dev, sizeof(*drv_data),	GFP_KERNEL);
    if (!drv_data) {
        rc = -ENOMEM;
        goto err_ret;
    }

    drv_data->sdev = sdev;
    drv_data->num_dais = MASTER_SLIM_NUM_CODEC_DAIS;

    rc = jlq_dai_slim_populate_dai_data(dev, drv_data);
    if (rc) {
        pr_err(LOGTAG"%s: failed to setup dai_data, err = %d\n",
               __func__, rc);
        goto err_populate_dai;
    }

    dev_set_drvdata(dev, drv_data);

    pr_info(LOGTAG"jlq_dai_slim_dev_probe, succ\n");

    return rc;

err_populate_dai:
    devm_kfree(dev, drv_data);

err_ret:
    pr_info(LOGTAG"jlq_dai_slim_dev_probe(from asoc), fail\n");
    return rc;
}

static int jlq_dai_slim_dev_remove(struct slim_device *sdev)
{
    jlq_dai_slim_remove_dai_data(&sdev->dev, sdev->dev.driver_data);

    g_sdev = NULL;
    return 0;
}

static const struct slim_device_id jlq_dai_slim_dt_match[] = {
    { JLQ_SLIM_DEV_NAME, 0 },
    {}
};

static struct slim_driver jlq_dai_slim_driver = {
    .driver = {
        .name = JLQ_SLIM_DEV_NAME,
        .owner = THIS_MODULE,
    },
    .probe = jlq_dai_slim_dev_probe,
    .remove = jlq_dai_slim_dev_remove,
    .id_table = jlq_dai_slim_dt_match,
};

int __init jlq_dai_slim_init(void)
{
    int rc;

    pr_info(LOGTAG"jlq_dai_slim_init(from asoc)\n");

    rc = slim_driver_register(&jlq_dai_slim_driver);
    if (rc)
        pr_err(LOGTAG"%s: failed to register with slimbus driver rc = %d",
               __func__, rc);

    return rc;
}

void __exit jlq_dai_slim_exit(void)
{
    slim_driver_unregister(&jlq_dai_slim_driver);
}


module_init(jlq_dai_slim_init);     /*maybe be called in the audio-kernel/asoc/platform_init.c*/
module_exit(jlq_dai_slim_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("JLQ Slimbus Master DAI driver");


