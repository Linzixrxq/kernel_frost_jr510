// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <dsp/msm_audio_ion.h>
#include <dsp/apr_audio-v2.h>
#include <dsp/audio_cal_utils.h>
#include <dsp/q6afe-v2.h>
#include <dsp/q6audio-v2.h>
#include <dsp/q6common.h>
#include <dsp/q6core.h>
#include <dsp/msm-audio-event-notify.h>
#include <ipc/apr_tal.h>
#include "adsp_err.h"

#define WAKELOCK_TIMEOUT	5000
#define AFE_CLK_TOKEN	1024

struct wlock {
	struct wakeup_source ws;
};

//static struct wlock wl;

struct afe_ctl {
	void *apr;
	atomic_t state;
	atomic_t status;
	atomic_t clk_state;
	atomic_t clk_status;
	wait_queue_head_t wait[AFE_MAX_PORTS];
	wait_queue_head_t wait_wakeup;
	wait_queue_head_t clk_wait;
	struct task_struct *task;
	wait_queue_head_t lpass_core_hw_wait;
	uint32_t lpass_hw_core_client_hdl[AFE_LPASS_CORE_HW_VOTE_MAX];
	void (*tx_cb)(uint32_t opcode,
		uint32_t token, uint32_t *payload, void *priv);
	void (*rx_cb)(uint32_t opcode,
		uint32_t token, uint32_t *payload, void *priv);
	void *tx_private_data;
	void *rx_private_data;
	uint32_t mmap_handle;

	void (*pri_spdif_tx_cb)(uint32_t opcode,
		uint32_t token, uint32_t *payload, void *priv);
	void (*sec_spdif_tx_cb)(uint32_t opcode,
		uint32_t token, uint32_t *payload, void *priv);
	void *pri_spdif_tx_private_data;
	void *sec_spdif_tx_private_data;
	int pri_spdif_config_change;
	int sec_spdif_config_change;

	u16 dtmf_gen_rx_portid;
	struct audio_cal_info_spk_prot_cfg	prot_cfg;
	struct afe_spkr_prot_calib_get_resp	calib_data;
	struct audio_cal_info_sp_th_vi_ftm_cfg	th_ftm_cfg;
	struct audio_cal_info_sp_th_vi_v_vali_cfg	v_vali_cfg;
	struct audio_cal_info_sp_ex_vi_ftm_cfg	ex_ftm_cfg;
	struct afe_sp_th_vi_get_param_resp	th_vi_resp;
	struct afe_sp_th_vi_v_vali_get_param_resp	th_vi_v_vali_resp;
	struct afe_sp_ex_vi_get_param_resp	ex_vi_resp;
	struct afe_sp_rx_tmax_xmax_logging_resp	xt_logging_resp;
	struct afe_av_dev_drift_get_param_resp	av_dev_drift_resp;
	struct afe_doa_tracking_mon_get_param_resp	doa_tracking_mon_resp;
	int vi_tx_port;
	int vi_rx_port;
	uint32_t afe_sample_rates[AFE_MAX_PORTS];
	struct aanc_data aanc_info;
	struct mutex afe_cmd_lock;
	struct mutex afe_apr_lock;
	struct mutex afe_clk_lock;
	int dev_acdb_id[AFE_MAX_PORTS];
	routing_cb rt_cb;
	struct audio_uevent_data *uevent_data;
	/* cal info for AFE */
	struct afe_fw_info *fw_data;
	u32 island_mode[AFE_MAX_PORTS];
	struct vad_config vad_cfg[AFE_MAX_PORTS];
	struct notifier_block event_notifier;
	/* FTM spk params */
	uint32_t initial_cal;
	uint32_t v_vali_flag;
	int vr_dev_cnt;
};

static unsigned long afe_configured_cmd;

static struct afe_ctl this_afe;

#define TIMEOUT_MS 1000
#define Q6AFE_MAX_VOLUME 0x3FFF

#define SIZEOF_CFG_CMD(y) \
		(sizeof(struct apr_hdr) + sizeof(u16) + (sizeof(struct y)))

static int afe_aud_event_notify(struct notifier_block *self,
				unsigned long action, void *data)
{
	switch (action) {
	case SWR_WAKE_IRQ_REGISTER:
		afe_send_cmd_wakeup_register(data, true);
		break;
	case SWR_WAKE_IRQ_DEREGISTER:
		afe_send_cmd_wakeup_register(data, false);
		break;
	default:
		pr_err("%s: invalid event type: %lu\n", __func__, action);
		return -EINVAL;
	}

	return 0;
}

static bool afe_token_is_valid(uint32_t token)
{
	if (token >= AFE_MAX_PORTS) {
		pr_err("%s: token %d is invalid.\n", __func__, token);
		return false;
	}
	return true;
}

static int32_t afe_callback(struct apr_client_data *data, void *priv)
{
	if (!data) {
		pr_err("%s: Invalid param data\n", __func__);
		return -EINVAL;
	}

	if (data->opcode == RESET_EVENTS) {
		pr_debug("%s: reset event = %d %d apr[%p]\n",
			__func__,
			data->reset_event, data->reset_proc, this_afe.apr);
		return 0;
	}

    if (data->payload_size) {
		uint32_t *payload;

		payload = data->payload;
		if (data->opcode == APR_BASIC_RSP_RESULT) {
			if (data->payload_size < (2 * sizeof(uint32_t))) {
				pr_err("%s: Error: size %d is less than expected\n",
					__func__, data->payload_size);
				return -EINVAL;
			}
			pr_debug("%s:opcode = 0x%X cmd = 0x%X status = 0x%X token=%d\n",
				__func__, data->opcode,
				payload[0], payload[1], data->token);
			/* payload[1] contains the error status for response */
			if (payload[1] != 0) {
				if(data->token == AFE_CLK_TOKEN)
					atomic_set(&this_afe.clk_status, payload[1]);
				else
					atomic_set(&this_afe.status, payload[1]);
				pr_err("%s: cmd = 0x%x returned error = 0x%x\n",
					__func__, payload[0], payload[1]);
			}
			switch (payload[0]) {
			case AFE_PORT_CMD_SET_PARAM_V2:
			case AFE_PORT_CMD_SET_PARAM_V3:
				if (rtac_make_afe_callback(payload,
							   data->payload_size))
					return 0;
			case AFE_PORT_CMD_DEVICE_OPEN:
			case AFE_PORT_CMD_DEVICE_STOP:
			case AFE_PORT_CMD_DEVICE_START:
			case AFE_PORT_CMD_DEVICE_CLOSE:
			case AFE_SVC_CMD_SET_PARAM:
			case AFE_SVC_CMD_SET_PARAM_V2:
				if(data->token == AFE_CLK_TOKEN) {
					atomic_set(&this_afe.clk_state, 0);
					wake_up(&this_afe.clk_wait);
				} else {
					atomic_set(&this_afe.state, 0);
					if (afe_token_is_valid(data->token))
						wake_up(&this_afe.wait[data->token]);
					else
						return -EINVAL;
				}
				break;
			case AFE_PORT_CMD_GET_PARAM_V2:
			case AFE_PORT_CMD_GET_PARAM_V3:
				/*
				 * Should only come here if there is an APR
				 * error or malformed APR packet. Otherwise
				 * response will be returned as
				 * AFE_PORT_CMDRSP_GET_PARAM_V2/3
				 */
				pr_debug("%s: AFE Get Param opcode 0x%x token 0x%x src %d dest %d\n",
					__func__, data->opcode, data->token,
					data->src_port, data->dest_port);
				if (payload[1] != 0) {
					pr_err("%s: AFE Get Param failed with error %d\n",
					       __func__, payload[1]);
					if (rtac_make_afe_callback(
						    payload,
						    data->payload_size))
						return 0;
				}
				atomic_set(&this_afe.state, payload[1]);
				if (afe_token_is_valid(data->token))
					wake_up(&this_afe.wait[data->token]);
				else
					return -EINVAL;
				break;
			default:
				pr_err("%s: Unknown cmd 0x%X\n", __func__,
						payload[0]);
				break;
			}
		}

	}
	return 0;
}

/**
 * afe_get_port_type -
 *        Retrieve AFE port type whether RX or TX
 *
 * @port_id: AFE Port ID number
 *
 * Returns RX/TX type.
 */
int afe_get_port_type(u16 port_id)
{
	int ret = MSM_AFE_PORT_TYPE_RX;

	switch (port_id) {
	case VOICE_RECORD_RX:
	case VOICE_RECORD_TX:
		ret = MSM_AFE_PORT_TYPE_TX;
		break;
	case VOICE_PLAYBACK_TX:
	case VOICE2_PLAYBACK_TX:
		ret = MSM_AFE_PORT_TYPE_RX;
		break;
	default:
		/* Odd numbered ports are TX and Rx are Even numbered */
		if (port_id & 0x1)
			ret = MSM_AFE_PORT_TYPE_TX;
		else
			ret = MSM_AFE_PORT_TYPE_RX;
		break;
	}

	return ret;
}
EXPORT_SYMBOL(afe_get_port_type);

int afe_sizeof_cfg_cmd(u16 port_id)
{
	int ret_size;

	switch (port_id) {
	case PRIMARY_I2S_RX:
	case PRIMARY_I2S_TX:
	case SECONDARY_I2S_RX:
	case SECONDARY_I2S_TX:
	case MI2S_RX:
	case MI2S_TX:
	case AFE_PORT_ID_PRIMARY_MI2S_RX:
	case AFE_PORT_ID_PRIMARY_MI2S_TX:
	case AFE_PORT_ID_QUATERNARY_MI2S_RX:
	case AFE_PORT_ID_QUATERNARY_MI2S_TX:
	case AFE_PORT_ID_QUINARY_MI2S_RX:
	case AFE_PORT_ID_QUINARY_MI2S_TX:
	case AFE_PORT_ID_SENARY_MI2S_RX:
	case AFE_PORT_ID_SENARY_MI2S_TX:
		ret_size = SIZEOF_CFG_CMD(afe_param_id_i2s_cfg);
		break;
	case AFE_PORT_ID_PRIMARY_META_MI2S_RX:
	case AFE_PORT_ID_SECONDARY_META_MI2S_RX:
		ret_size = SIZEOF_CFG_CMD(afe_param_id_meta_i2s_cfg);
		break;
	case HDMI_RX:
	case DISPLAY_PORT_RX:
		ret_size =
		SIZEOF_CFG_CMD(afe_param_id_hdmi_multi_chan_audio_cfg);
		break;
	case AFE_PORT_ID_PRIMARY_SPDIF_RX:
	case AFE_PORT_ID_PRIMARY_SPDIF_TX:
	case AFE_PORT_ID_SECONDARY_SPDIF_RX:
	case AFE_PORT_ID_SECONDARY_SPDIF_TX:
		ret_size =
		SIZEOF_CFG_CMD(afe_param_id_spdif_cfg_v2);
		break;
	case SLIMBUS_0_RX:
	case SLIMBUS_0_TX:
	case SLIMBUS_1_RX:
	case SLIMBUS_1_TX:
	case SLIMBUS_2_RX:
	case SLIMBUS_2_TX:
	case SLIMBUS_3_RX:
	case SLIMBUS_3_TX:
	case SLIMBUS_4_RX:
	case SLIMBUS_4_TX:
	case SLIMBUS_5_RX:
	case SLIMBUS_5_TX:
	case SLIMBUS_6_RX:
	case SLIMBUS_6_TX:
	case SLIMBUS_7_RX:
	case SLIMBUS_7_TX:
	case SLIMBUS_8_RX:
	case SLIMBUS_8_TX:
	case SLIMBUS_9_RX:
	case SLIMBUS_9_TX:
		ret_size = SIZEOF_CFG_CMD(afe_param_id_slimbus_cfg);
		break;
	case VOICE_PLAYBACK_TX:
	case VOICE2_PLAYBACK_TX:
	case VOICE_RECORD_RX:
	case VOICE_RECORD_TX:
		ret_size = SIZEOF_CFG_CMD(afe_param_id_pseudo_port_cfg);
		break;
	case RT_PROXY_PORT_001_RX:
	case RT_PROXY_PORT_001_TX:
		ret_size = SIZEOF_CFG_CMD(afe_param_id_rt_proxy_port_cfg);
		break;
	case AFE_PORT_ID_USB_RX:
	case AFE_PORT_ID_USB_TX:
		ret_size = SIZEOF_CFG_CMD(afe_param_id_usb_audio_cfg);
		break;
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_1:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_2:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_2:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_0:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_0:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_1:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_1:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_2:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_2:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_3:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_3:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_4:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_4:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_5:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_5:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_6:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_7:
		ret_size = SIZEOF_CFG_CMD(afe_param_id_cdc_dma_cfg_t);
		break;
	case AFE_PORT_ID_PRIMARY_PCM_RX:
	case AFE_PORT_ID_PRIMARY_PCM_TX:
	case AFE_PORT_ID_SECONDARY_PCM_RX:
	case AFE_PORT_ID_SECONDARY_PCM_TX:
	case AFE_PORT_ID_TERTIARY_PCM_RX:
	case AFE_PORT_ID_TERTIARY_PCM_TX:
	case AFE_PORT_ID_QUATERNARY_PCM_RX:
	case AFE_PORT_ID_QUATERNARY_PCM_TX:
	case AFE_PORT_ID_QUINARY_PCM_RX:
	case AFE_PORT_ID_QUINARY_PCM_TX:
	case AFE_PORT_ID_SENARY_PCM_RX:
	case AFE_PORT_ID_SENARY_PCM_TX:
	default:
		pr_debug("%s: default case 0x%x\n", __func__, port_id);
		ret_size = SIZEOF_CFG_CMD(afe_param_id_pcm_cfg);
		break;
	}
	return ret_size;
}

/**
 * afe_q6_interface_prepare -
 *        wrapper API to check Q6 AFE registered to APR otherwise registers
 *
 * Returns 0 on success or error on failure.
 */
int afe_q6_interface_prepare(void)
{
	int ret = 0;

	pr_debug("%s:\n", __func__);

	if (this_afe.apr == NULL) {
		this_afe.apr = apr_register("ADSP", "AFE", afe_callback,
			0xFFFFFFFF, &this_afe);
		if (this_afe.apr == NULL) {
			pr_err("%s: Unable to register AFE\n", __func__);
			ret = -ENETRESET;
		}
		rtac_set_afe_handle(this_afe.apr);
		this_afe.vr_dev_cnt = 0;
	}
	return ret;
}
EXPORT_SYMBOL(afe_q6_interface_prepare);

/*
 * afe_apr_send_pkt : returns 0 on success, negative otherwise.
 */
static int afe_apr_send_pkt(void *data, wait_queue_head_t *wait)
{
	int ret;

	mutex_lock(&this_afe.afe_apr_lock);
	if (wait)
		atomic_set(&this_afe.state, 1);
	atomic_set(&this_afe.status, 0);
	ret = apr_send_pkt(this_afe.apr, data);
	if (ret > 0) {
		if (wait) {
			ret = wait_event_timeout(*wait,
					(atomic_read(&this_afe.state) == 0),
					msecs_to_jiffies(2 * TIMEOUT_MS));
			if (!ret) {
				pr_err_ratelimited("%s: request timedout\n",
					__func__);
				ret = -ETIMEDOUT;
				trace_printk("%s: wait for ADSP response timed out\n",
					__func__);
			} else if (atomic_read(&this_afe.status) > 0) {
				pr_err("%s: DSP returned error[%s]\n", __func__,
					adsp_err_get_err_str(atomic_read(
					&this_afe.status)));
				ret = adsp_err_get_lnx_err_code(
						atomic_read(&this_afe.status));
			} else {
				ret = 0;
			}
		} else {
			ret = 0;
		}
	} else if (ret == 0) {
		pr_err("%s: packet not transmitted\n", __func__);
		/* apr_send_pkt can return 0 when nothing is transmitted */
		ret = -EINVAL;
	}

	pr_debug("%s: leave %d\n", __func__, ret);
	mutex_unlock(&this_afe.afe_apr_lock);
	return ret;
}
/*
 * afe_apr_send_clk_pkt : returns 0 on success, negative otherwise.
 */
static int afe_apr_send_clk_pkt(void *data, wait_queue_head_t *wait)
{
	int ret;

	if (wait)
		atomic_set(&this_afe.clk_state, 1);
	atomic_set(&this_afe.clk_status, 0);
	ret = apr_send_pkt(this_afe.apr, data);
	if (ret > 0) {
		if (wait) {
			ret = wait_event_timeout(*wait,
					(atomic_read(&this_afe.clk_state) == 0),
					msecs_to_jiffies(2 * TIMEOUT_MS));
			if (!ret) {
				pr_err("%s: timeout\n", __func__);
				ret = -ETIMEDOUT;
			} else if (atomic_read(&this_afe.clk_status) > 0) {
				pr_err("%s: DSP returned error[%s]\n", __func__,
					adsp_err_get_err_str(atomic_read(
					&this_afe.clk_status)));
				ret = adsp_err_get_lnx_err_code(
						atomic_read(&this_afe.clk_status));
			} else {
				ret = 0;
			}
		} else {
			ret = 0;
		}
	} else if (ret == 0) {
		pr_err("%s: packet not transmitted\n", __func__);
		/* apr_send_pkt can return 0 when nothing is transmitted */
		ret = -EINVAL;
	}

	pr_debug("%s: leave %d\n", __func__, ret);
	return ret;
}

/* This function shouldn't be called directly. Instead call q6afe_set_params. */
static int q6afe_set_params_v2(u16 port_id, int index,
			       struct mem_mapping_hdr *mem_hdr,
			       u8 *packed_param_data, u32 packed_data_size)
{
	struct afe_port_cmd_set_param_v2 *set_param = NULL;
	uint32_t size = sizeof(struct afe_port_cmd_set_param_v2);
	int rc = 0;

	pr_err("%s: \n", __func__);

	if (packed_param_data != NULL)
		size += packed_data_size;
	set_param = kzalloc(size, GFP_KERNEL);
	if (set_param == NULL)
		return -ENOMEM;

	set_param->apr_hdr.hdr_field =
		APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD, APR_HDR_LEN(APR_HDR_SIZE),
			      APR_PKT_VER);
	set_param->apr_hdr.pkt_size = size;
	set_param->apr_hdr.src_port = 0;
	set_param->apr_hdr.dest_port = 0;
	set_param->apr_hdr.src_svc = APR_SVC_AFE;
	set_param->apr_hdr.src_domain = APR_DOMAIN_APPS;
	set_param->apr_hdr.dest_svc = APR_SVC_AFE;
	set_param->apr_hdr.dest_domain = APR_DOMAIN_ADSP;

	set_param->apr_hdr.token = index;
	set_param->apr_hdr.opcode = AFE_PORT_CMD_SET_PARAM_V2;
	set_param->port_id = port_id;
	if (packed_data_size > U16_MAX) {
		pr_err("%s: Invalid data size for set params V2 %d\n", __func__,
		       packed_data_size);
		rc = -EINVAL;
		goto done;
	}
	set_param->payload_size = packed_data_size;
	if (mem_hdr != NULL) {
		set_param->mem_hdr = *mem_hdr;
	} else if (packed_param_data != NULL) {
		memcpy(&set_param->param_data, packed_param_data,
		       packed_data_size);
	} else {
		pr_err("%s: Both memory header and param data are NULL\n",
		       __func__);
		rc = -EINVAL;
		goto done;
	}

	//rc = afe_apr_send_pkt(set_param, &this_afe.wait[index]);
	rc = afe_apr_send_pkt(set_param, 0);
done:
	kfree(set_param);
	return rc;
}

static int q6afe_set_params(u16 port_id, int index,
			    struct mem_mapping_hdr *mem_hdr,
			    u8 *packed_param_data, u32 packed_data_size)
{
	int ret = 0;

	ret = afe_q6_interface_prepare();
	if (ret != 0) {
		pr_err("%s: Q6 interface prepare failed %d\n", __func__, ret);
		return ret;
	}

	port_id = q6audio_get_port_id(port_id);
	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_err("%s: Not a valid port id = 0x%x ret %d\n", __func__,
		       port_id, ret);
		return -EINVAL;
	}

	if (index < 0 || index >= AFE_MAX_PORTS) {
		pr_err("%s: AFE port index[%d] invalid\n", __func__, index);
		return -EINVAL;
	}

	return q6afe_set_params_v2(port_id, index, mem_hdr,
				   packed_param_data, packed_data_size);
}

static int q6afe_pack_and_set_param_in_band(u16 port_id, int index,
					    struct param_hdr_v3 param_hdr,
					    u8 *param_data)
{
	u8 *packed_param_data = NULL;
	int packed_data_size = sizeof(union param_hdrs) + param_hdr.param_size;
	int ret;

	packed_param_data = kzalloc(packed_data_size, GFP_KERNEL);
	if (packed_param_data == NULL)
		return -ENOMEM;

	ret = q6common_pack_pp_params(packed_param_data, &param_hdr, param_data,
				      &packed_data_size);
	if (ret) {
		pr_err("%s: Failed to pack param header and data, error %d\n",
		       __func__, ret);
		goto fail_cmd;
	}

	ret = q6afe_set_params(port_id, index, NULL, packed_param_data,
			       packed_data_size);

fail_cmd:
	kfree(packed_param_data);
	return ret;
}

/*
 * This function shouldn't be called directly. Instead call
 * q6afe_svc_set_params.
 */
static int q6afe_svc_set_params_v1(int index, struct mem_mapping_hdr *mem_hdr,
				   u8 *packed_param_data, u32 packed_data_size)
{
	struct afe_svc_cmd_set_param_v1 *svc_set_param = NULL;
	uint32_t size = sizeof(struct afe_svc_cmd_set_param_v1);
	int rc = 0;

	if (packed_param_data != NULL)
		size += packed_data_size;
	svc_set_param = kzalloc(size, GFP_KERNEL);
	if (svc_set_param == NULL)
		return -ENOMEM;

	svc_set_param->apr_hdr.hdr_field =
		APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD, APR_HDR_LEN(APR_HDR_SIZE),
			      APR_PKT_VER);
	svc_set_param->apr_hdr.pkt_size = size;
	svc_set_param->apr_hdr.src_port = 0;
	svc_set_param->apr_hdr.dest_port = 0;
	svc_set_param->apr_hdr.token = index;
	svc_set_param->apr_hdr.opcode = AFE_SVC_CMD_SET_PARAM;
	svc_set_param->payload_size = packed_data_size;

	if (mem_hdr != NULL) {
		/* Out of band case. */
		svc_set_param->mem_hdr = *mem_hdr;
	} else if (packed_param_data != NULL) {
		/* In band case. */
		memcpy(&svc_set_param->param_data, packed_param_data,
		       packed_data_size);
	} else {
		pr_err("%s: Both memory header and param data are NULL\n",
		       __func__);
		rc = -EINVAL;
		goto done;
	}

	rc = afe_apr_send_pkt(svc_set_param, &this_afe.wait[index]);
done:
	kfree(svc_set_param);
	return rc;
}

/*
 * This function shouldn't be called directly. Instead call
 * q6afe_svc_set_params.
 */
static int q6afe_svc_set_params_v2(int index, struct mem_mapping_hdr *mem_hdr,
				   u8 *packed_param_data, u32 packed_data_size)
{
	struct afe_svc_cmd_set_param_v2 *svc_set_param = NULL;
	uint16_t size = sizeof(struct afe_svc_cmd_set_param_v2);
	int rc = 0;

	if (packed_param_data != NULL)
		size += packed_data_size;
	svc_set_param = kzalloc(size, GFP_KERNEL);
	if (svc_set_param == NULL)
		return -ENOMEM;

	svc_set_param->apr_hdr.hdr_field =
		APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD, APR_HDR_LEN(APR_HDR_SIZE),
			      APR_PKT_VER);
	svc_set_param->apr_hdr.pkt_size = size;
	svc_set_param->apr_hdr.src_port = 0;
	svc_set_param->apr_hdr.dest_port = 0;
	svc_set_param->apr_hdr.token = index;
	svc_set_param->apr_hdr.opcode = AFE_SVC_CMD_SET_PARAM_V2;
	svc_set_param->payload_size = packed_data_size;

	if (mem_hdr != NULL) {
		/* Out of band case. */
		svc_set_param->mem_hdr = *mem_hdr;
	} else if (packed_param_data != NULL) {
		/* In band case. */
		memcpy(&svc_set_param->param_data, packed_param_data,
		       packed_data_size);
	} else {
		pr_err("%s: Both memory header and param data are NULL\n",
		       __func__);
		rc = -EINVAL;
		goto done;
	}

	rc = afe_apr_send_pkt(svc_set_param, &this_afe.wait[index]);
done:
	kfree(svc_set_param);
	return rc;
}

/*
 * This function shouldn't be called directly. Instead call
 * q6afe_clk_set_params.
 */
static int q6afe_clk_set_params_v1(int index, struct mem_mapping_hdr *mem_hdr,
				   u8 *packed_param_data, u32 packed_data_size)
{
	struct afe_svc_cmd_set_param_v1 *svc_set_param = NULL;
	uint32_t size = sizeof(struct afe_svc_cmd_set_param_v1);
	int rc = 0;

	if (packed_param_data != NULL)
		size += packed_data_size;
	svc_set_param = kzalloc(size, GFP_KERNEL);
	if (svc_set_param == NULL)
		return -ENOMEM;

	svc_set_param->apr_hdr.hdr_field =
		APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD, APR_HDR_LEN(APR_HDR_SIZE),
			      APR_PKT_VER);
	svc_set_param->apr_hdr.pkt_size = size;
	svc_set_param->apr_hdr.src_port = 0;
	svc_set_param->apr_hdr.dest_port = 0;
	svc_set_param->apr_hdr.token = AFE_CLK_TOKEN;
	svc_set_param->apr_hdr.opcode = AFE_SVC_CMD_SET_PARAM;
	svc_set_param->payload_size = packed_data_size;

	if (mem_hdr != NULL) {
		/* Out of band case. */
		svc_set_param->mem_hdr = *mem_hdr;
	} else if (packed_param_data != NULL) {
		/* In band case. */
		memcpy(&svc_set_param->param_data, packed_param_data,
		       packed_data_size);
	} else {
		pr_err("%s: Both memory header and param data are NULL\n",
		       __func__);
		rc = -EINVAL;
		goto done;
	}

	rc = afe_apr_send_clk_pkt(svc_set_param, &this_afe.clk_wait);
done:
	kfree(svc_set_param);
	return rc;
}

/*
 * This function shouldn't be called directly. Instead call
 * q6afe_clk_set_params.
 */
static int q6afe_clk_set_params_v2(int index, struct mem_mapping_hdr *mem_hdr,
				   u8 *packed_param_data, u32 packed_data_size)
{
	struct afe_svc_cmd_set_param_v2 *svc_set_param = NULL;
	uint16_t size = sizeof(struct afe_svc_cmd_set_param_v2);
	int rc = 0;

	if (packed_param_data != NULL)
		size += packed_data_size;
	svc_set_param = kzalloc(size, GFP_KERNEL);
	if (svc_set_param == NULL)
		return -ENOMEM;

	svc_set_param->apr_hdr.hdr_field =
		APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD, APR_HDR_LEN(APR_HDR_SIZE),
			      APR_PKT_VER);
	svc_set_param->apr_hdr.pkt_size = size;
	svc_set_param->apr_hdr.src_port = 0;
	svc_set_param->apr_hdr.dest_port = 0;
	svc_set_param->apr_hdr.token = AFE_CLK_TOKEN;
	svc_set_param->apr_hdr.opcode = AFE_SVC_CMD_SET_PARAM_V2;
	svc_set_param->payload_size = packed_data_size;

	if (mem_hdr != NULL) {
		/* Out of band case. */
		svc_set_param->mem_hdr = *mem_hdr;
	} else if (packed_param_data != NULL) {
		/* In band case. */
		memcpy(&svc_set_param->param_data, packed_param_data,
		       packed_data_size);
	} else {
		pr_err("%s: Both memory header and param data are NULL\n",
		       __func__);
		rc = -EINVAL;
		goto done;
	}

	rc = afe_apr_send_clk_pkt(svc_set_param, &this_afe.clk_wait);
done:
	kfree(svc_set_param);
	return rc;
}

static int q6afe_clk_set_params(int index, struct mem_mapping_hdr *mem_hdr,
				u8 *packed_param_data, u32 packed_data_size,
				bool is_iid_supported)
{
	int ret;

	ret = afe_q6_interface_prepare();
	if (ret != 0) {
		pr_err("%s: Q6 interface prepare failed %d\n", __func__, ret);
		return ret;
	}

	if (is_iid_supported)
		return q6afe_clk_set_params_v2(index, mem_hdr,
					       packed_param_data,
					       packed_data_size);
	else
		return q6afe_clk_set_params_v1(index, mem_hdr,
					       packed_param_data,
					       packed_data_size);
}

static int q6afe_svc_set_params(int index, struct mem_mapping_hdr *mem_hdr,
				u8 *packed_param_data, u32 packed_data_size,
				bool is_iid_supported)
{
	int ret;

	ret = afe_q6_interface_prepare();
	if (ret != 0) {
		pr_err("%s: Q6 interface prepare failed %d\n", __func__, ret);
		return ret;
	}

	if (is_iid_supported)
		return q6afe_svc_set_params_v2(index, mem_hdr,
					       packed_param_data,
					       packed_data_size);
	else
		return q6afe_svc_set_params_v1(index, mem_hdr,
					       packed_param_data,
					       packed_data_size);
}

static int q6afe_svc_pack_and_set_param_in_band(int index,
						struct param_hdr_v3 param_hdr,
						u8 *param_data)
{
	u8 *packed_param_data = NULL;
	u32 packed_data_size =
		sizeof(struct param_hdr_v3) + param_hdr.param_size;
	int ret = 0;
	bool is_iid_supported = q6common_is_instance_id_supported();

	packed_param_data = kzalloc(packed_data_size, GFP_KERNEL);
	if (!packed_param_data)
		return -ENOMEM;

	ret = q6common_pack_pp_params_v2(packed_param_data, &param_hdr,
					param_data, &packed_data_size,
					is_iid_supported);
	if (ret) {
		pr_err("%s: Failed to pack parameter header and data, error %d\n",
		       __func__, ret);
		goto done;
	}
	if (param_hdr.module_id == AFE_MODULE_CLOCK_SET)
		ret = q6afe_clk_set_params(index, NULL, packed_param_data,
					packed_data_size, is_iid_supported);
	else
		ret = q6afe_svc_set_params(index, NULL, packed_param_data,
					   packed_data_size, is_iid_supported);

done:
	kfree(packed_param_data);
	return ret;
}

static int afe_send_slimbus_slave_cfg(
	struct afe_param_cdc_slimbus_slave_cfg *sb_slave_cfg)
{
	struct param_hdr_v3 param_hdr;
	int ret;

	pr_debug("%s: enter\n", __func__);

	memset(&param_hdr, 0, sizeof(param_hdr));
	param_hdr.module_id = AFE_MODULE_CDC_DEV_CFG;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = AFE_PARAM_ID_CDC_SLIMBUS_SLAVE_CFG;
	param_hdr.param_size = sizeof(struct afe_param_cdc_slimbus_slave_cfg);

	ret = q6afe_svc_pack_and_set_param_in_band(IDX_GLOBAL_CFG, param_hdr,
						   (u8 *) sb_slave_cfg);
	if (ret)
		pr_err("%s: AFE_PARAM_ID_CDC_SLIMBUS_SLAVE_CFG failed %d\n",
		       __func__, ret);

	pr_debug("%s: leave %d\n", __func__, ret);
	return ret;
}

static int afe_send_codec_reg_page_config(
	struct afe_param_cdc_reg_page_cfg *cdc_reg_page_cfg)
{
	struct param_hdr_v3 param_hdr;
	int ret;

	memset(&param_hdr, 0, sizeof(param_hdr));
	param_hdr.module_id = AFE_MODULE_CDC_DEV_CFG;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = AFE_PARAM_ID_CDC_REG_PAGE_CFG;
	param_hdr.param_size = sizeof(struct afe_param_cdc_reg_page_cfg);

	ret = q6afe_svc_pack_and_set_param_in_band(IDX_GLOBAL_CFG, param_hdr,
						   (u8 *) cdc_reg_page_cfg);
	if (ret)
		pr_err("%s: AFE_PARAM_ID_CDC_REG_PAGE_CFG failed %d\n",
		       __func__, ret);

	return ret;
}

static int afe_send_codec_reg_config(
	struct afe_param_cdc_reg_cfg_data *cdc_reg_cfg)
{
	u8 *packed_param_data = NULL;
	u32 packed_data_size = 0;
	u32 single_param_size = 0;
	u32 max_data_size = 0;
	u32 max_single_param = 0;
	struct param_hdr_v3 param_hdr;
	int idx = 0;
	int ret = -EINVAL;
	bool is_iid_supported = q6common_is_instance_id_supported();

	memset(&param_hdr, 0, sizeof(param_hdr));
	max_single_param = sizeof(struct param_hdr_v3) +
			   sizeof(struct afe_param_cdc_reg_cfg);
	max_data_size = APR_MAX_BUF - sizeof(struct afe_svc_cmd_set_param_v2);
	packed_param_data = kzalloc(max_data_size, GFP_KERNEL);
	if (!packed_param_data)
		return -ENOMEM;

	/* param_hdr is the same for all params sent, set once at top */
	param_hdr.module_id = AFE_MODULE_CDC_DEV_CFG;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = AFE_PARAM_ID_CDC_REG_CFG;
	param_hdr.param_size = sizeof(struct afe_param_cdc_reg_cfg);

	while (idx < cdc_reg_cfg->num_registers) {
		memset(packed_param_data, 0, max_data_size);
		packed_data_size = 0;
		single_param_size = 0;

		while (packed_data_size + max_single_param < max_data_size &&
		       idx < cdc_reg_cfg->num_registers) {
			ret = q6common_pack_pp_params_v2(
				packed_param_data + packed_data_size,
				&param_hdr, (u8 *) &cdc_reg_cfg->reg_data[idx],
				&single_param_size, is_iid_supported);
			if (ret) {
				pr_err("%s: Failed to pack parameters with error %d\n",
				       __func__, ret);
				goto done;
			}
			packed_data_size += single_param_size;
			idx++;
		}

		ret = q6afe_svc_set_params(IDX_GLOBAL_CFG, NULL,
					   packed_param_data, packed_data_size,
					   is_iid_supported);
		if (ret) {
			pr_err("%s: AFE_PARAM_ID_CDC_REG_CFG failed %d\n",
				__func__, ret);
			break;
		}
	}
done:
	kfree(packed_param_data);
	return ret;
}

static int afe_init_cdc_reg_config(void)
{
	struct param_hdr_v3 param_hdr;
	int ret;

	pr_debug("%s: enter\n", __func__);
	memset(&param_hdr, 0, sizeof(param_hdr));
	param_hdr.module_id = AFE_MODULE_CDC_DEV_CFG;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = AFE_PARAM_ID_CDC_REG_CFG_INIT;

	ret = q6afe_svc_pack_and_set_param_in_band(IDX_GLOBAL_CFG, param_hdr,
						   NULL);
	if (ret)
		pr_err("%s: AFE_PARAM_ID_CDC_INIT_REG_CFG failed %d\n",
		       __func__, ret);

	return ret;
}

static int afe_send_slimbus_slave_port_cfg(
	struct afe_param_slimbus_slave_port_cfg *slim_slave_config, u16 port_id)
{
	struct param_hdr_v3 param_hdr;
	int ret;

	pr_debug("%s: enter, port_id =  0x%x\n", __func__, port_id);
	memset(&param_hdr, 0, sizeof(param_hdr));
	param_hdr.module_id = AFE_MODULE_HW_MAD;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.reserved = 0;
	param_hdr.param_id = AFE_PARAM_ID_SLIMBUS_SLAVE_PORT_CFG;
	param_hdr.param_size = sizeof(struct afe_param_slimbus_slave_port_cfg);

	ret = q6afe_pack_and_set_param_in_band(port_id,
					       q6audio_get_port_index(port_id),
					       param_hdr,
					       (u8 *) slim_slave_config);
	if (ret)
		pr_err("%s: AFE_PARAM_ID_SLIMBUS_SLAVE_PORT_CFG failed %d\n",
			__func__, ret);

	pr_debug("%s: leave %d\n", __func__, ret);
	return ret;
}

/**
 * afe_set_config -
 *        to configure AFE session with
 *        specified configuration for given config type
 *
 * @config_type: config type
 * @config_data: configuration to pass to AFE session
 * @arg: argument used in specific config types
 *
 * Returns 0 on success or error value on port start failure.
 */
int afe_set_config(enum afe_config_type config_type, void *config_data, int arg)
{
	int ret;

	pr_debug("%s: enter config_type %d\n", __func__, config_type);
	ret = afe_q6_interface_prepare();
	if (ret) {
		pr_err("%s: Q6 interface prepare failed %d\n", __func__, ret);
		return ret;
	}

	switch (config_type) {
	case AFE_SLIMBUS_SLAVE_CONFIG:
		ret = afe_send_slimbus_slave_cfg(config_data);
		if (!ret)
			ret = afe_init_cdc_reg_config();
		else
			pr_err("%s: Sending slimbus slave config failed %d\n",
			       __func__, ret);
		break;
	case AFE_CDC_REGISTERS_CONFIG:
		ret = afe_send_codec_reg_config(config_data);
		break;
	case AFE_SLIMBUS_SLAVE_PORT_CONFIG:
		ret = afe_send_slimbus_slave_port_cfg(config_data, arg);
		break;
	case AFE_CDC_CLIP_REGISTERS_CONFIG:
		ret = afe_send_codec_reg_config(config_data);
		break;
	case AFE_CDC_REGISTER_PAGE_CONFIG:
		ret = afe_send_codec_reg_page_config(config_data);
		break;
	default:
		pr_err("%s: unknown configuration type %d",
			__func__, config_type);
		ret = -EINVAL;
	}

	if (!ret)
		set_bit(config_type, &afe_configured_cmd);

	return ret;
}
EXPORT_SYMBOL(afe_set_config);

/*
 * afe_clear_config - If SSR happens ADSP loses AFE configs, let AFE driver know
 *		      about the state so client driver can wait until AFE is
 *		      reconfigured.
 */
void afe_clear_config(enum afe_config_type config)
{
	clear_bit(config, &afe_configured_cmd);
}
EXPORT_SYMBOL(afe_clear_config);

bool afe_has_config(enum afe_config_type config)
{
	return !!test_bit(config, &afe_configured_cmd);
}

inline static int afe_send_cmd_port_start(u16 port_id)
{
	struct afe_port_cmd_device_start start;
	int ret, index;

	pr_debug("%s: enter\n", __func__);
	index = q6audio_get_port_index(port_id);
	if (index < 0 || index >= AFE_MAX_PORTS) {
		pr_err("%s: AFE port index[%d] invalid!\n",
				__func__, index);
		return -EINVAL;
	}
	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_err("%s: port id: 0x%x ret %d\n", __func__, port_id, ret);
		return -EINVAL;
	}

	start.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					    APR_HDR_LEN(APR_HDR_SIZE),
					    APR_PKT_VER);
	start.hdr.pkt_size = sizeof(start);
	start.hdr.src_port = 0;
	start.hdr.dest_port = 0;
	start.hdr.src_svc = APR_SVC_AFE;
	start.hdr.src_domain = APR_DOMAIN_APPS;
	start.hdr.dest_svc = APR_SVC_AFE;
	start.hdr.dest_domain = APR_DOMAIN_ADSP;

	start.hdr.token = index;
	start.hdr.opcode = AFE_PORT_CMD_DEVICE_START;
	start.port_id = jlqaudio_get_dsp_be_id(port_id);
	pr_debug("%s: cmd device start opcode[0x%x] port id[0x%x]\n",
		 __func__, start.hdr.opcode, start.port_id);

	//ret = afe_apr_send_pkt(&start, &this_afe.wait[index]);
	ret = afe_apr_send_pkt(&start, 0);
	if (ret)
		pr_err("%s: AFE enable for port 0x%x failed %d\n", __func__,
		       port_id, ret);

	return ret;
}

/**
 * afe_set_routing_callback -
 *         Update callback function for routing
 *
 * @cb: callback function to update with
 *
 */
void afe_set_routing_callback(routing_cb cb)
{
	this_afe.rt_cb = cb;
}
EXPORT_SYMBOL(afe_set_routing_callback);

int afe_port_open(u16 port_id, struct afe_device_config *afe_config)
{

	struct afe_cmd_device_open open;
	int ret, index;

	index = q6audio_get_port_index(port_id);
	if (index < 0 || index >= AFE_MAX_PORTS) {
		pr_err("%s: AFE port index[%d] invalid!\n",
				__func__, index);
		return -EINVAL;
	}
	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_err("%s: port id: 0x%x ret %d\n", __func__, port_id, ret);
		return -EINVAL;
	}

	ret = afe_q6_interface_prepare();
	if (ret != 0) {
		pr_err("%s: Q6 interface prepare failed %d\n", __func__, ret);
		return ret;
	}

    mutex_lock(&this_afe.afe_cmd_lock);

	if(port_id == VOICE_RECORD_RX ||
	   port_id == VOICE_RECORD_TX){
	    if(this_afe.vr_dev_cnt > 0){
	        pr_info("%s: voice record device is already opened",__func__);
	        mutex_unlock(&this_afe.afe_cmd_lock);
	        return 0;
	    }
	    this_afe.vr_dev_cnt = 1;
	}

	open.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					    APR_HDR_LEN(APR_HDR_SIZE),
					    APR_PKT_VER);
	open.hdr.pkt_size = sizeof(open);
	open.hdr.src_port = 0;
	open.hdr.dest_port = 0;
	open.hdr.src_svc = APR_SVC_AFE;
	open.hdr.src_domain = APR_DOMAIN_APPS;
	open.hdr.dest_svc = APR_SVC_AFE;
	open.hdr.dest_domain = APR_DOMAIN_ADSP;

	open.hdr.token = index;
	open.hdr.opcode = AFE_PORT_CMD_DEVICE_OPEN;
	open.device_id = jlqaudio_get_dsp_be_id(port_id);
	open.channel_num  = (uint8_t)afe_config->channel_num;
	open.sample_bytes = (uint8_t)afe_config->bit_width/8;
	open.interleave   = (uint8_t)afe_config->interleave;
	open.sample_rate  = afe_config->sample_rate;

	pr_info("%s: device id[0x%x] port_id[0x%x], sample_rate[%d] channel[%d] sample_bytes[%d] interleave[%d]\n",
		 __func__,open.device_id,port_id,open.sample_rate,open.channel_num,open.sample_bytes,open.interleave);

	//ret = afe_apr_send_pkt(&start, &this_afe.wait[index]);
	ret = afe_apr_send_pkt(&open, 0);
	if (ret)
		pr_err("%s: AFE enable for port 0x%x failed %d\n", __func__,
		       port_id, ret);

	mutex_unlock(&this_afe.afe_cmd_lock);
	return ret;
}
EXPORT_SYMBOL(afe_port_open);

static int __afe_port_start(u16 port_id, union afe_port_config *afe_config,
			    u32 rate, u16 afe_in_channels, u16 afe_in_bit_width,
			    union afe_enc_config_data *enc_cfg,
			    u32 codec_format, u32 scrambler_mode, u32 mono_mode,
			    struct afe_dec_config *dec_cfg)
{
	int ret = 0;
//	int index = 0;

	if (!afe_config) {
		pr_err("%s: Error, no configuration data\n", __func__);
		ret = -EINVAL;
		return ret;
	}

	pr_debug("%s: port id: 0x%x  do nothing\n", __func__, port_id);
	return ret;

#if 0

	index = q6audio_get_port_index(port_id);
	if (index < 0 || index >= AFE_MAX_PORTS) {
		pr_err("%s: AFE port index[%d] invalid!\n",
				__func__, index);
		return -EINVAL;
	}
	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_err("%s: port id: 0x%x ret %d\n", __func__, port_id, ret);
		return -EINVAL;
	}

	ret = afe_q6_interface_prepare();
	if (ret != 0) {
		pr_err("%s: Q6 interface prepare failed %d\n", __func__, ret);
		return ret;
	}

	mutex_lock(&this_afe.afe_cmd_lock);
	
	ret = afe_send_cmd_port_start(port_id);

	mutex_unlock(&this_afe.afe_cmd_lock);

	return ret;
#endif

}

/**
 * afe_port_start - to configure AFE session with
 * specified port configuration
 *
 * @port_id: AFE port id number
 * @afe_config: port configutation
 * @rate: sampling rate of port
 *
 * Returns 0 on success or error value on port start failure.
 */
int afe_port_start(u16 port_id, union afe_port_config *afe_config,
		   u32 rate)
{
	return __afe_port_start(port_id, afe_config, rate,
				0, 0, NULL, ASM_MEDIA_FMT_NONE, 0, 0, NULL);
}
EXPORT_SYMBOL(afe_port_start);

/**
 * afe_port_start_v2 - to configure AFE session with
 * specified port configuration and encoder /decoder params
 *
 * @port_id: AFE port id number
 * @afe_config: port configutation
 * @rate: sampling rate of port
 * @enc_cfg: AFE enc configuration information to setup encoder
 * @afe_in_channels: AFE input channel configuration, this needs
 *  update only if input channel is differ from AFE output
 * @dec_cfg: AFE dec configuration information to set up decoder
 *
 * Returns 0 on success or error value on port start failure.
 */
int afe_port_start_v2(u16 port_id, union afe_port_config *afe_config,
		      u32 rate, u16 afe_in_channels, u16 afe_in_bit_width,
		      struct afe_enc_config *enc_cfg,
		      struct afe_dec_config *dec_cfg)
{
	int ret = 0;

	if (enc_cfg != NULL)
		ret = __afe_port_start(port_id, afe_config, rate,
					afe_in_channels, afe_in_bit_width,
					&enc_cfg->data, enc_cfg->format,
					enc_cfg->scrambler_mode,
					enc_cfg->mono_mode, dec_cfg);
	else if (dec_cfg != NULL)
		ret = __afe_port_start(port_id, afe_config, rate,
					afe_in_channels, afe_in_bit_width,
					NULL, dec_cfg->format, 0, 0, dec_cfg);

	return ret;
}
EXPORT_SYMBOL(afe_port_start_v2);

int afe_get_port_index(u16 port_id)
{
	switch (port_id) {
	case PRIMARY_I2S_RX: return IDX_PRIMARY_I2S_RX;
	case PRIMARY_I2S_TX: return IDX_PRIMARY_I2S_TX;
	case AFE_PORT_ID_PRIMARY_PCM_RX:
		return IDX_AFE_PORT_ID_PRIMARY_PCM_RX;
	case AFE_PORT_ID_PRIMARY_PCM_TX:
		return IDX_AFE_PORT_ID_PRIMARY_PCM_TX;
	case AFE_PORT_ID_SECONDARY_PCM_RX:
		return IDX_AFE_PORT_ID_SECONDARY_PCM_RX;
	case AFE_PORT_ID_SECONDARY_PCM_TX:
		return IDX_AFE_PORT_ID_SECONDARY_PCM_TX;
	case AFE_PORT_ID_TERTIARY_PCM_RX:
		return IDX_AFE_PORT_ID_TERTIARY_PCM_RX;
	case AFE_PORT_ID_TERTIARY_PCM_TX:
		return IDX_AFE_PORT_ID_TERTIARY_PCM_TX;
	case AFE_PORT_ID_QUATERNARY_PCM_RX:
		return IDX_AFE_PORT_ID_QUATERNARY_PCM_RX;
	case AFE_PORT_ID_QUATERNARY_PCM_TX:
		return IDX_AFE_PORT_ID_QUATERNARY_PCM_TX;
	case AFE_PORT_ID_QUINARY_PCM_RX:
		return IDX_AFE_PORT_ID_QUINARY_PCM_RX;
	case AFE_PORT_ID_QUINARY_PCM_TX:
		return IDX_AFE_PORT_ID_QUINARY_PCM_TX;
	case AFE_PORT_ID_SENARY_PCM_RX:
		return IDX_AFE_PORT_ID_SENARY_PCM_RX;
	case AFE_PORT_ID_SENARY_PCM_TX:
		return IDX_AFE_PORT_ID_SENARY_PCM_TX;
	case SECONDARY_I2S_RX: return IDX_SECONDARY_I2S_RX;
	case SECONDARY_I2S_TX: return IDX_SECONDARY_I2S_TX;
	case MI2S_RX: return IDX_MI2S_RX;
	case MI2S_TX: return IDX_MI2S_TX;
	case HDMI_RX: return IDX_HDMI_RX;
	case DISPLAY_PORT_RX: return IDX_DISPLAY_PORT_RX;
	case AFE_PORT_ID_PRIMARY_SPDIF_RX: return IDX_PRIMARY_SPDIF_RX;
	case AFE_PORT_ID_PRIMARY_SPDIF_TX: return IDX_PRIMARY_SPDIF_TX;
	case AFE_PORT_ID_SECONDARY_SPDIF_RX: return IDX_SECONDARY_SPDIF_RX;
	case AFE_PORT_ID_SECONDARY_SPDIF_TX: return IDX_SECONDARY_SPDIF_TX;
	case RSVD_2: return IDX_RSVD_2;
	case RSVD_3: return IDX_RSVD_3;
	case DIGI_MIC_TX: return IDX_DIGI_MIC_TX;
	case VOICE_RECORD_RX: return IDX_VOICE_RECORD_RX;
	case VOICE_RECORD_TX: return IDX_VOICE_RECORD_TX;
	case VOICE_PLAYBACK_TX: return IDX_VOICE_PLAYBACK_TX;
	case VOICE2_PLAYBACK_TX: return IDX_VOICE2_PLAYBACK_TX;
	case SLIMBUS_0_RX: return IDX_SLIMBUS_0_RX;
	case SLIMBUS_0_TX: return IDX_SLIMBUS_0_TX;
	case SLIMBUS_1_RX: return IDX_SLIMBUS_1_RX;
	case SLIMBUS_1_TX: return IDX_SLIMBUS_1_TX;
	case SLIMBUS_2_RX: return IDX_SLIMBUS_2_RX;
	case SLIMBUS_2_TX: return IDX_SLIMBUS_2_TX;
	case SLIMBUS_3_RX: return IDX_SLIMBUS_3_RX;
	case SLIMBUS_3_TX: return IDX_SLIMBUS_3_TX;
	case INT_BT_SCO_RX: return IDX_INT_BT_SCO_RX;
	case INT_BT_SCO_TX: return IDX_INT_BT_SCO_TX;
	case INT_BT_A2DP_RX: return IDX_INT_BT_A2DP_RX;
	case INT_FM_RX: return IDX_INT_FM_RX;
	case INT_FM_TX: return IDX_INT_FM_TX;
	case RT_PROXY_PORT_001_RX: return IDX_RT_PROXY_PORT_001_RX;
	case RT_PROXY_PORT_001_TX: return IDX_RT_PROXY_PORT_001_TX;
	case SLIMBUS_4_RX: return IDX_SLIMBUS_4_RX;
	case SLIMBUS_4_TX: return IDX_SLIMBUS_4_TX;
	case SLIMBUS_5_RX: return IDX_SLIMBUS_5_RX;
	case SLIMBUS_5_TX: return IDX_SLIMBUS_5_TX;
	case SLIMBUS_6_RX: return IDX_SLIMBUS_6_RX;
	case SLIMBUS_6_TX: return IDX_SLIMBUS_6_TX;
	case SLIMBUS_7_RX: return IDX_SLIMBUS_7_RX;
	case SLIMBUS_7_TX: return IDX_SLIMBUS_7_TX;
	case SLIMBUS_8_RX: return IDX_SLIMBUS_8_RX;
	case SLIMBUS_8_TX: return IDX_SLIMBUS_8_TX;
	case SLIMBUS_9_RX: return IDX_SLIMBUS_9_RX;
	case SLIMBUS_9_TX: return IDX_SLIMBUS_9_TX;
	case AFE_PORT_ID_USB_RX: return IDX_AFE_PORT_ID_USB_RX;
	case AFE_PORT_ID_USB_TX: return IDX_AFE_PORT_ID_USB_TX;
	case AFE_PORT_ID_PRIMARY_MI2S_RX:
		return IDX_AFE_PORT_ID_PRIMARY_MI2S_RX;
	case AFE_PORT_ID_PRIMARY_MI2S_TX:
		return IDX_AFE_PORT_ID_PRIMARY_MI2S_TX;
	case AFE_PORT_ID_QUATERNARY_MI2S_RX:
		return IDX_AFE_PORT_ID_QUATERNARY_MI2S_RX;
	case AFE_PORT_ID_QUATERNARY_MI2S_TX:
		return IDX_AFE_PORT_ID_QUATERNARY_MI2S_TX;
	case AFE_PORT_ID_SECONDARY_MI2S_RX:
		return IDX_AFE_PORT_ID_SECONDARY_MI2S_RX;
	case AFE_PORT_ID_SECONDARY_MI2S_TX:
		return IDX_AFE_PORT_ID_SECONDARY_MI2S_TX;
	case AFE_PORT_ID_TERTIARY_MI2S_RX:
		return IDX_AFE_PORT_ID_TERTIARY_MI2S_RX;
	case AFE_PORT_ID_TERTIARY_MI2S_TX:
		return IDX_AFE_PORT_ID_TERTIARY_MI2S_TX;
	case AFE_PORT_ID_SECONDARY_MI2S_RX_SD1:
		return IDX_AFE_PORT_ID_SECONDARY_MI2S_RX_SD1;
	case AFE_PORT_ID_QUINARY_MI2S_RX:
		return IDX_AFE_PORT_ID_QUINARY_MI2S_RX;
	case AFE_PORT_ID_QUINARY_MI2S_TX:
		return IDX_AFE_PORT_ID_QUINARY_MI2S_TX;
	case AFE_PORT_ID_SENARY_MI2S_RX:
		return IDX_AFE_PORT_ID_SENARY_MI2S_RX;
	case AFE_PORT_ID_SENARY_MI2S_TX:
		return IDX_AFE_PORT_ID_SENARY_MI2S_TX;
	case AFE_PORT_ID_PRIMARY_TDM_RX:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_0;
	case AFE_PORT_ID_PRIMARY_TDM_TX:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_0;
	case AFE_PORT_ID_PRIMARY_TDM_RX_1:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_1;
	case AFE_PORT_ID_PRIMARY_TDM_TX_1:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_1;
	case AFE_PORT_ID_PRIMARY_TDM_RX_2:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_2;
	case AFE_PORT_ID_PRIMARY_TDM_TX_2:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_2;
	case AFE_PORT_ID_PRIMARY_TDM_RX_3:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_3;
	case AFE_PORT_ID_PRIMARY_TDM_TX_3:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_3;
	case AFE_PORT_ID_PRIMARY_TDM_RX_4:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_4;
	case AFE_PORT_ID_PRIMARY_TDM_TX_4:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_4;
	case AFE_PORT_ID_PRIMARY_TDM_RX_5:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_5;
	case AFE_PORT_ID_PRIMARY_TDM_TX_5:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_5;
	case AFE_PORT_ID_PRIMARY_TDM_RX_6:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_6;
	case AFE_PORT_ID_PRIMARY_TDM_TX_6:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_6;
	case AFE_PORT_ID_PRIMARY_TDM_RX_7:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_RX_7;
	case AFE_PORT_ID_PRIMARY_TDM_TX_7:
		return IDX_AFE_PORT_ID_PRIMARY_TDM_TX_7;
	case AFE_PORT_ID_SECONDARY_TDM_RX:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_0;
	case AFE_PORT_ID_SECONDARY_TDM_TX:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_0;
	case AFE_PORT_ID_SECONDARY_TDM_RX_1:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_1;
	case AFE_PORT_ID_SECONDARY_TDM_TX_1:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_1;
	case AFE_PORT_ID_SECONDARY_TDM_RX_2:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_2;
	case AFE_PORT_ID_SECONDARY_TDM_TX_2:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_2;
	case AFE_PORT_ID_SECONDARY_TDM_RX_3:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_3;
	case AFE_PORT_ID_SECONDARY_TDM_TX_3:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_3;
	case AFE_PORT_ID_SECONDARY_TDM_RX_4:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_4;
	case AFE_PORT_ID_SECONDARY_TDM_TX_4:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_4;
	case AFE_PORT_ID_SECONDARY_TDM_RX_5:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_5;
	case AFE_PORT_ID_SECONDARY_TDM_TX_5:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_5;
	case AFE_PORT_ID_SECONDARY_TDM_RX_6:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_6;
	case AFE_PORT_ID_SECONDARY_TDM_TX_6:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_6;
	case AFE_PORT_ID_SECONDARY_TDM_RX_7:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_RX_7;
	case AFE_PORT_ID_SECONDARY_TDM_TX_7:
		return IDX_AFE_PORT_ID_SECONDARY_TDM_TX_7;
	case AFE_PORT_ID_TERTIARY_TDM_RX:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_0;
	case AFE_PORT_ID_TERTIARY_TDM_TX:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_0;
	case AFE_PORT_ID_TERTIARY_TDM_RX_1:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_1;
	case AFE_PORT_ID_TERTIARY_TDM_TX_1:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_1;
	case AFE_PORT_ID_TERTIARY_TDM_RX_2:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_2;
	case AFE_PORT_ID_TERTIARY_TDM_TX_2:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_2;
	case AFE_PORT_ID_TERTIARY_TDM_RX_3:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_3;
	case AFE_PORT_ID_TERTIARY_TDM_TX_3:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_3;
	case AFE_PORT_ID_TERTIARY_TDM_RX_4:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_4;
	case AFE_PORT_ID_TERTIARY_TDM_TX_4:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_4;
	case AFE_PORT_ID_TERTIARY_TDM_RX_5:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_5;
	case AFE_PORT_ID_TERTIARY_TDM_TX_5:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_5;
	case AFE_PORT_ID_TERTIARY_TDM_RX_6:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_6;
	case AFE_PORT_ID_TERTIARY_TDM_TX_6:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_6;
	case AFE_PORT_ID_TERTIARY_TDM_RX_7:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_RX_7;
	case AFE_PORT_ID_TERTIARY_TDM_TX_7:
		return IDX_AFE_PORT_ID_TERTIARY_TDM_TX_7;
	case AFE_PORT_ID_QUATERNARY_TDM_RX:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_0;
	case AFE_PORT_ID_QUATERNARY_TDM_TX:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_0;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_1:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_1;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_1:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_1;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_2:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_2;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_2:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_2;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_3:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_3;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_3:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_3;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_4:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_4;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_4:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_4;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_5:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_5;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_5:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_5;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_6:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_6;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_6:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_6;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_7:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_RX_7;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_7:
		return IDX_AFE_PORT_ID_QUATERNARY_TDM_TX_7;
	case AFE_PORT_ID_QUINARY_TDM_RX:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_0;
	case AFE_PORT_ID_QUINARY_TDM_TX:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_0;
	case AFE_PORT_ID_QUINARY_TDM_RX_1:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_1;
	case AFE_PORT_ID_QUINARY_TDM_TX_1:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_1;
	case AFE_PORT_ID_QUINARY_TDM_RX_2:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_2;
	case AFE_PORT_ID_QUINARY_TDM_TX_2:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_2;
	case AFE_PORT_ID_QUINARY_TDM_RX_3:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_3;
	case AFE_PORT_ID_QUINARY_TDM_TX_3:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_3;
	case AFE_PORT_ID_QUINARY_TDM_RX_4:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_4;
	case AFE_PORT_ID_QUINARY_TDM_TX_4:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_4;
	case AFE_PORT_ID_QUINARY_TDM_RX_5:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_5;
	case AFE_PORT_ID_QUINARY_TDM_TX_5:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_5;
	case AFE_PORT_ID_QUINARY_TDM_RX_6:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_6;
	case AFE_PORT_ID_QUINARY_TDM_TX_6:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_6;
	case AFE_PORT_ID_QUINARY_TDM_RX_7:
		return IDX_AFE_PORT_ID_QUINARY_TDM_RX_7;
	case AFE_PORT_ID_QUINARY_TDM_TX_7:
		return IDX_AFE_PORT_ID_QUINARY_TDM_TX_7;
	case AFE_PORT_ID_SENARY_TDM_RX:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_0;
	case AFE_PORT_ID_SENARY_TDM_TX:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_0;
	case AFE_PORT_ID_SENARY_TDM_RX_1:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_1;
	case AFE_PORT_ID_SENARY_TDM_TX_1:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_1;
	case AFE_PORT_ID_SENARY_TDM_RX_2:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_2;
	case AFE_PORT_ID_SENARY_TDM_TX_2:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_2;
	case AFE_PORT_ID_SENARY_TDM_RX_3:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_3;
	case AFE_PORT_ID_SENARY_TDM_TX_3:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_3;
	case AFE_PORT_ID_SENARY_TDM_RX_4:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_4;
	case AFE_PORT_ID_SENARY_TDM_TX_4:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_4;
	case AFE_PORT_ID_SENARY_TDM_RX_5:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_5;
	case AFE_PORT_ID_SENARY_TDM_TX_5:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_5;
	case AFE_PORT_ID_SENARY_TDM_RX_6:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_6;
	case AFE_PORT_ID_SENARY_TDM_TX_6:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_6;
	case AFE_PORT_ID_SENARY_TDM_RX_7:
		return IDX_AFE_PORT_ID_SENARY_TDM_RX_7;
	case AFE_PORT_ID_SENARY_TDM_TX_7:
		return IDX_AFE_PORT_ID_SENARY_TDM_TX_7;
	case AFE_PORT_ID_INT0_MI2S_RX:
		return IDX_AFE_PORT_ID_INT0_MI2S_RX;
	case AFE_PORT_ID_INT0_MI2S_TX:
		return IDX_AFE_PORT_ID_INT0_MI2S_TX;
	case AFE_PORT_ID_INT1_MI2S_RX:
		return IDX_AFE_PORT_ID_INT1_MI2S_RX;
	case AFE_PORT_ID_INT1_MI2S_TX:
		return IDX_AFE_PORT_ID_INT1_MI2S_TX;
	case AFE_PORT_ID_INT2_MI2S_RX:
		return IDX_AFE_PORT_ID_INT2_MI2S_RX;
	case AFE_PORT_ID_INT2_MI2S_TX:
		return IDX_AFE_PORT_ID_INT2_MI2S_TX;
	case AFE_PORT_ID_INT3_MI2S_RX:
		return IDX_AFE_PORT_ID_INT3_MI2S_RX;
	case AFE_PORT_ID_INT3_MI2S_TX:
		return IDX_AFE_PORT_ID_INT3_MI2S_TX;
	case AFE_PORT_ID_INT4_MI2S_RX:
		return IDX_AFE_PORT_ID_INT4_MI2S_RX;
	case AFE_PORT_ID_INT4_MI2S_TX:
		return IDX_AFE_PORT_ID_INT4_MI2S_TX;
	case AFE_PORT_ID_INT5_MI2S_RX:
		return IDX_AFE_PORT_ID_INT5_MI2S_RX;
	case AFE_PORT_ID_INT5_MI2S_TX:
		return IDX_AFE_PORT_ID_INT5_MI2S_TX;
	case AFE_PORT_ID_INT6_MI2S_RX:
		return IDX_AFE_PORT_ID_INT6_MI2S_RX;
	case AFE_PORT_ID_INT6_MI2S_TX:
		return IDX_AFE_PORT_ID_INT6_MI2S_TX;
	case AFE_PORT_ID_PRIMARY_META_MI2S_RX:
		return IDX_AFE_PORT_ID_PRIMARY_META_MI2S_RX;
	case AFE_PORT_ID_SECONDARY_META_MI2S_RX:
		return IDX_AFE_PORT_ID_SECONDARY_META_MI2S_RX;
	case AFE_PORT_ID_VA_CODEC_DMA_TX_0:
		return IDX_AFE_PORT_ID_VA_CODEC_DMA_TX_0;
	case AFE_PORT_ID_VA_CODEC_DMA_TX_1:
		return IDX_AFE_PORT_ID_VA_CODEC_DMA_TX_1;
	case AFE_PORT_ID_VA_CODEC_DMA_TX_2:
		return IDX_AFE_PORT_ID_VA_CODEC_DMA_TX_2;
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_0:
		return IDX_AFE_PORT_ID_WSA_CODEC_DMA_RX_0;
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_0:
		return IDX_AFE_PORT_ID_WSA_CODEC_DMA_TX_0;
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_1:
		return IDX_AFE_PORT_ID_WSA_CODEC_DMA_RX_1;
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_1:
		return IDX_AFE_PORT_ID_WSA_CODEC_DMA_TX_1;
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_2:
		return IDX_AFE_PORT_ID_WSA_CODEC_DMA_TX_2;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_0:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_0;
	case AFE_PORT_ID_TX_CODEC_DMA_TX_0:
		return IDX_AFE_PORT_ID_TX_CODEC_DMA_TX_0;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_1:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_1;
	case AFE_PORT_ID_TX_CODEC_DMA_TX_1:
		return IDX_AFE_PORT_ID_TX_CODEC_DMA_TX_1;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_2:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_2;
	case AFE_PORT_ID_TX_CODEC_DMA_TX_2:
		return IDX_AFE_PORT_ID_TX_CODEC_DMA_TX_2;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_3:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_3;
	case AFE_PORT_ID_TX_CODEC_DMA_TX_3:
		return IDX_AFE_PORT_ID_TX_CODEC_DMA_TX_3;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_4:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_4;
	case AFE_PORT_ID_TX_CODEC_DMA_TX_4:
		return IDX_AFE_PORT_ID_TX_CODEC_DMA_TX_4;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_5:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_5;
	case AFE_PORT_ID_TX_CODEC_DMA_TX_5:
		return IDX_AFE_PORT_ID_TX_CODEC_DMA_TX_5;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_6:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_6;
	case AFE_PORT_ID_RX_CODEC_DMA_RX_7:
		return IDX_AFE_PORT_ID_RX_CODEC_DMA_RX_7;
	case AFE_LOOPBACK_TX:
		return IDX_AFE_LOOPBACK_TX;
	default:
		pr_err("%s: port 0x%x\n", __func__, port_id);
		return -EINVAL;
	}
}

/**
 * afe_open -
 *         command to open AFE port
 *
 * @port_id: AFE port id
 * @afe_config: AFE port config to pass
 * @rate: sample rate
 *
 * Returns 0 on success or error on failure
 */
int afe_open(u16 port_id,
		union afe_port_config *afe_config, int rate)
{
	struct afe_port_cmd_device_start start;
	union afe_port_config port_cfg;
	struct param_hdr_v3 param_hdr;
	int ret = 0;
	int cfg_type;
	int index = 0;

	memset(&param_hdr, 0, sizeof(param_hdr));
	memset(&start, 0, sizeof(start));
	memset(&port_cfg, 0, sizeof(port_cfg));

	if (!afe_config) {
		pr_err("%s: Error, no configuration data\n", __func__);
		ret = -EINVAL;
		return ret;
	}

	pr_err("%s: port_id 0x%x rate %d\n", __func__, port_id, rate);

	index = q6audio_get_port_index(port_id);
	if (index < 0 || index >= AFE_MAX_PORTS) {
		pr_err("%s: AFE port index[%d] invalid!\n",
				__func__, index);
		return -EINVAL;
	}
	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_err("%s: Invalid port 0x%x ret %d", __func__, port_id, ret);
		return -EINVAL;
	}

	if ((port_id == RT_PROXY_DAI_001_RX) ||
		(port_id == RT_PROXY_DAI_002_TX)) {
		pr_err("%s: wrong port 0x%x\n", __func__, port_id);
		return -EINVAL;
	}
	if ((port_id == RT_PROXY_DAI_002_RX) ||
		(port_id == RT_PROXY_DAI_001_TX))
		port_id = VIRTUAL_ID_TO_PORTID(port_id);

	ret = afe_q6_interface_prepare();
	if (ret != 0) {
		pr_err("%s: Q6 interface prepare failed %d\n", __func__, ret);
		return -EINVAL;
	}

	if ((index >= 0) && (index < AFE_MAX_PORTS)) {
		this_afe.afe_sample_rates[index] = rate;

		if (this_afe.rt_cb)
			this_afe.dev_acdb_id[index] = this_afe.rt_cb(port_id);
	}

	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_err("%s: Failed : Invalid Port id = 0x%x ret %d\n",
			__func__, port_id, ret);
		return -EINVAL;
	}
	mutex_lock(&this_afe.afe_cmd_lock);

	switch (port_id) {
	case PRIMARY_I2S_RX:
	case PRIMARY_I2S_TX:
		cfg_type = AFE_PARAM_ID_I2S_CONFIG;
		break;
	case AFE_PORT_ID_PRIMARY_PCM_RX:
	case AFE_PORT_ID_PRIMARY_PCM_TX:
	case AFE_PORT_ID_SECONDARY_PCM_RX:
	case AFE_PORT_ID_SECONDARY_PCM_TX:
	case AFE_PORT_ID_TERTIARY_PCM_RX:
	case AFE_PORT_ID_TERTIARY_PCM_TX:
	case AFE_PORT_ID_QUATERNARY_PCM_RX:
	case AFE_PORT_ID_QUATERNARY_PCM_TX:
	case AFE_PORT_ID_QUINARY_PCM_RX:
	case AFE_PORT_ID_QUINARY_PCM_TX:
	case AFE_PORT_ID_SENARY_PCM_RX:
	case AFE_PORT_ID_SENARY_PCM_TX:
		cfg_type = AFE_PARAM_ID_PCM_CONFIG;
		break;
	case SECONDARY_I2S_RX:
	case SECONDARY_I2S_TX:
	case AFE_PORT_ID_PRIMARY_MI2S_RX:
	case AFE_PORT_ID_PRIMARY_MI2S_TX:
	case AFE_PORT_ID_QUATERNARY_MI2S_RX:
	case AFE_PORT_ID_QUATERNARY_MI2S_TX:
	case MI2S_RX:
	case MI2S_TX:
	case AFE_PORT_ID_QUINARY_MI2S_RX:
	case AFE_PORT_ID_QUINARY_MI2S_TX:
	case AFE_PORT_ID_SENARY_MI2S_RX:
	case AFE_PORT_ID_SENARY_MI2S_TX:
		cfg_type = AFE_PARAM_ID_I2S_CONFIG;
		break;
	case AFE_PORT_ID_PRIMARY_META_MI2S_RX:
	case AFE_PORT_ID_SECONDARY_META_MI2S_RX:
		cfg_type = AFE_PARAM_ID_META_I2S_CONFIG;
		break;
	case HDMI_RX:
	case DISPLAY_PORT_RX:
		cfg_type = AFE_PARAM_ID_HDMI_CONFIG;
		break;
	case AFE_PORT_ID_PRIMARY_SPDIF_RX:
	case AFE_PORT_ID_PRIMARY_SPDIF_TX:
	case AFE_PORT_ID_SECONDARY_SPDIF_RX:
	case AFE_PORT_ID_SECONDARY_SPDIF_TX:
		cfg_type = AFE_PARAM_ID_SPDIF_CONFIG;
		break;
	case SLIMBUS_0_RX:
	case SLIMBUS_0_TX:
	case SLIMBUS_1_RX:
	case SLIMBUS_1_TX:
	case SLIMBUS_2_RX:
	case SLIMBUS_2_TX:
	case SLIMBUS_3_RX:
	case SLIMBUS_3_TX:
	case SLIMBUS_4_RX:
	case SLIMBUS_4_TX:
	case SLIMBUS_5_RX:
	case SLIMBUS_6_RX:
	case SLIMBUS_6_TX:
	case SLIMBUS_7_RX:
	case SLIMBUS_7_TX:
	case SLIMBUS_8_RX:
	case SLIMBUS_8_TX:
	case SLIMBUS_9_RX:
	case SLIMBUS_9_TX:
		cfg_type = AFE_PARAM_ID_SLIMBUS_CONFIG;
		break;
	case AFE_PORT_ID_USB_RX:
	case AFE_PORT_ID_USB_TX:
		cfg_type = AFE_PARAM_ID_USB_AUDIO_CONFIG;
		break;
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_1:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_2:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_2:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_0:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_0:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_1:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_1:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_2:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_2:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_3:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_3:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_4:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_4:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_5:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_5:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_6:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_7:
		cfg_type = AFE_PARAM_ID_CODEC_DMA_CONFIG;
		break;
	default:
		pr_err("%s: Invalid port id 0x%x\n",
			__func__, port_id);
		ret = -EINVAL;
		goto fail_cmd;
	}

	param_hdr.module_id = AFE_MODULE_AUDIO_DEV_INTERFACE;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = cfg_type;
	param_hdr.param_size = sizeof(union afe_port_config);
	port_cfg = *afe_config;

	ret = q6afe_pack_and_set_param_in_band(port_id,
					       q6audio_get_port_index(port_id),
					       param_hdr, (u8 *) &port_cfg);
	if (ret) {
		pr_err("%s: AFE enable for port 0x%x opcode[0x%x]failed %d\n",
			__func__, port_id, cfg_type, ret);
		goto fail_cmd;
	}
	start.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	start.hdr.pkt_size = sizeof(start);
	start.hdr.src_port = 0;
	start.hdr.dest_port = 0;
	start.hdr.token = index;
	start.hdr.opcode = AFE_PORT_CMD_DEVICE_START;
	start.port_id = q6audio_get_port_id(port_id);
	pr_debug("%s: cmd device start opcode[0x%x] port id[0x%x]\n",
		__func__, start.hdr.opcode, start.port_id);

	ret = afe_apr_send_pkt(&start, &this_afe.wait[index]);
	if (ret) {
		pr_err("%s: AFE enable for port 0x%x failed %d\n", __func__,
				port_id, ret);
		goto fail_cmd;
	}

fail_cmd:
	mutex_unlock(&this_afe.afe_cmd_lock);
	return ret;
}
EXPORT_SYMBOL(afe_open);

/**
 * q6afe_audio_client_alloc -
 *         Assign new AFE audio client
 *
 * @priv: privata data to hold for audio client
 *
 * Returns ac pointer on success or NULL on failure
 */
struct afe_audio_client *q6afe_audio_client_alloc(void *priv)
{
	struct afe_audio_client *ac;
	int lcnt = 0;

	ac = kzalloc(sizeof(struct afe_audio_client), GFP_KERNEL);
	if (!ac)
		return NULL;

	ac->priv = priv;

	init_waitqueue_head(&ac->cmd_wait);
	INIT_LIST_HEAD(&ac->port[0].mem_map_handle);
	INIT_LIST_HEAD(&ac->port[1].mem_map_handle);
	pr_debug("%s: mem_map_handle list init'ed\n", __func__);
	mutex_init(&ac->cmd_lock);
	for (lcnt = 0; lcnt <= OUT; lcnt++) {
		mutex_init(&ac->port[lcnt].lock);
		spin_lock_init(&ac->port[lcnt].dsp_lock);
	}
	atomic_set(&ac->cmd_state, 0);

	return ac;
}
EXPORT_SYMBOL(q6afe_audio_client_alloc);

/**
 * q6afe_audio_client_buf_alloc_contiguous -
 *         Allocate contiguous shared buffers
 *
 * @dir: RX or TX direction of AFE port
 * @ac: AFE audio client handle
 * @bufsz: size of each shared buffer
 * @bufcnt: number of buffers
 *
 * Returns 0 on success or error on failure
 */
int q6afe_audio_client_buf_alloc_contiguous(unsigned int dir,
			struct afe_audio_client *ac,
			unsigned int bufsz,
			unsigned int bufcnt)
{
	int cnt = 0;
	int rc = 0;
	struct afe_audio_buffer *buf;
	size_t len;

	if (!(ac) || ((dir != IN) && (dir != OUT))) {
		pr_err("%s: ac %pK dir %d\n", __func__, ac, dir);
		return -EINVAL;
	}

	pr_debug("%s: bufsz[%d]bufcnt[%d]\n",
			__func__,
			bufsz, bufcnt);

	if (ac->port[dir].buf) {
		pr_debug("%s: buffer already allocated\n", __func__);
		return 0;
	}
	mutex_lock(&ac->cmd_lock);
	buf = kzalloc(((sizeof(struct afe_audio_buffer))*bufcnt),
			GFP_KERNEL);

	if (!buf) {
		pr_err("%s: null buf\n", __func__);
		mutex_unlock(&ac->cmd_lock);
		goto fail;
	}

	ac->port[dir].buf = buf;

	rc = msm_audio_ion_alloc(&buf[0].dma_buf,
				bufsz * bufcnt,
				&buf[0].phys, &len,
				&buf[0].data);
	if (rc) {
		pr_err("%s: audio ION alloc failed, rc = %d\n",
			__func__, rc);
		mutex_unlock(&ac->cmd_lock);
		goto fail;
	}

	buf[0].used = dir ^ 1;
	buf[0].size = bufsz;
	buf[0].actual_size = bufsz;
	cnt = 1;
	while (cnt < bufcnt) {
		if (bufsz > 0) {
			buf[cnt].data =  buf[0].data + (cnt * bufsz);
			buf[cnt].phys =  buf[0].phys + (cnt * bufsz);
			if (!buf[cnt].data) {
				pr_err("%s: Buf alloc failed\n",
							__func__);
				mutex_unlock(&ac->cmd_lock);
				goto fail;
			}
			buf[cnt].used = dir ^ 1;
			buf[cnt].size = bufsz;
			buf[cnt].actual_size = bufsz;
/*
			pr_debug("%s:  data[%pK]phys[%pK][%pK]\n", __func__,
				   buf[cnt].data,
				   &buf[cnt].phys,
				   &buf[cnt].phys);
*/
		}
		cnt++;
	}
	ac->port[dir].max_buf_cnt = cnt;
	mutex_unlock(&ac->cmd_lock);
	return 0;
fail:
	pr_err("%s: jump fail\n", __func__);
	q6afe_audio_client_buf_free_contiguous(dir, ac);
	return -EINVAL;
}
EXPORT_SYMBOL(q6afe_audio_client_buf_alloc_contiguous);

/**
 * q6afe_audio_client_buf_free_contiguous -
 *         frees the shared contiguous memory
 *
 * @dir: RX or TX direction of port
 * @ac: AFE audio client handle
 *
 */
int q6afe_audio_client_buf_free_contiguous(unsigned int dir,
			struct afe_audio_client *ac)
{
	struct afe_audio_port_data *port;
	int cnt = 0;

	mutex_lock(&ac->cmd_lock);
	port = &ac->port[dir];
	if (!port->buf) {
		pr_err("%s: buf is null\n", __func__);
		mutex_unlock(&ac->cmd_lock);
		return 0;
	}
	cnt = port->max_buf_cnt - 1;

	if (port->buf[0].data) {
/*
		pr_debug("%s: data[%pK], phys[%pK], dma_buf[%pK]\n",
			__func__,
			port->buf[0].data,
			&port->buf[0].phys,
			port->buf[0].dma_buf);
*/
		msm_audio_ion_free(port->buf[0].dma_buf);
		port->buf[0].dma_buf = NULL;
	}

	while (cnt >= 0) {
		port->buf[cnt].data = NULL;
		port->buf[cnt].phys = 0;
		cnt--;
	}
	port->max_buf_cnt = 0;
	kfree(port->buf);
	port->buf = NULL;
	mutex_unlock(&ac->cmd_lock);
	return 0;
}
EXPORT_SYMBOL(q6afe_audio_client_buf_free_contiguous);

/**
 * q6afe_audio_client_free -
 *         frees the audio client from AFE
 *
 * @ac: AFE audio client handle
 *
 */
void q6afe_audio_client_free(struct afe_audio_client *ac)
{
	int loopcnt;
	struct afe_audio_port_data *port;

	if (!ac) {
		pr_err("%s: audio client is NULL\n", __func__);
		return;
	}
	for (loopcnt = 0; loopcnt <= OUT; loopcnt++) {
		port = &ac->port[loopcnt];
		if (!port->buf)
			continue;
		pr_debug("%s: loopcnt = %d\n", __func__, loopcnt);
		q6afe_audio_client_buf_free_contiguous(loopcnt, ac);
	}
	kfree(ac);
}
EXPORT_SYMBOL(q6afe_audio_client_free);

int afe_validate_port(u16 port_id)
{
	int ret;

	switch (port_id) {
	case PRIMARY_I2S_RX:
	case PRIMARY_I2S_TX:
	case AFE_PORT_ID_PRIMARY_PCM_RX:
	case AFE_PORT_ID_PRIMARY_PCM_TX:
	case AFE_PORT_ID_SECONDARY_PCM_RX:
	case AFE_PORT_ID_SECONDARY_PCM_TX:
	case AFE_PORT_ID_TERTIARY_PCM_RX:
	case AFE_PORT_ID_TERTIARY_PCM_TX:
	case AFE_PORT_ID_QUATERNARY_PCM_RX:
	case AFE_PORT_ID_QUATERNARY_PCM_TX:
	case AFE_PORT_ID_QUINARY_PCM_RX:
	case AFE_PORT_ID_QUINARY_PCM_TX:
	case AFE_PORT_ID_SENARY_PCM_RX:
	case AFE_PORT_ID_SENARY_PCM_TX:
	case SECONDARY_I2S_RX:
	case SECONDARY_I2S_TX:
	case MI2S_RX:
	case MI2S_TX:
	case HDMI_RX:
	case DISPLAY_PORT_RX:
	case AFE_PORT_ID_PRIMARY_SPDIF_RX:
	case AFE_PORT_ID_PRIMARY_SPDIF_TX:
	case AFE_PORT_ID_SECONDARY_SPDIF_RX:
	case AFE_PORT_ID_SECONDARY_SPDIF_TX:
	case RSVD_2:
	case RSVD_3:
	case DIGI_MIC_TX:
	case VOICE_RECORD_RX:
	case VOICE_RECORD_TX:
	case VOICE_PLAYBACK_TX:
	case VOICE2_PLAYBACK_TX:
	case SLIMBUS_0_RX:
	case SLIMBUS_0_TX:
	case SLIMBUS_1_RX:
	case SLIMBUS_1_TX:
	case SLIMBUS_2_RX:
	case SLIMBUS_2_TX:
	case SLIMBUS_3_RX:
	case INT_BT_SCO_RX:
	case INT_BT_SCO_TX:
	case INT_BT_A2DP_RX:
	case INT_FM_RX:
	case INT_FM_TX:
	case RT_PROXY_PORT_001_RX:
	case RT_PROXY_PORT_001_TX:
	case SLIMBUS_4_RX:
	case SLIMBUS_4_TX:
	case SLIMBUS_5_RX:
	case SLIMBUS_6_RX:
	case SLIMBUS_6_TX:
	case SLIMBUS_7_RX:
	case SLIMBUS_7_TX:
	case SLIMBUS_8_RX:
	case SLIMBUS_8_TX:
	case SLIMBUS_9_RX:
	case SLIMBUS_9_TX:
	case AFE_PORT_ID_USB_RX:
	case AFE_PORT_ID_USB_TX:
	case AFE_PORT_ID_PRIMARY_MI2S_RX:
	case AFE_PORT_ID_PRIMARY_MI2S_TX:
	case AFE_PORT_ID_SECONDARY_MI2S_RX:
	case AFE_PORT_ID_SECONDARY_MI2S_TX:
	case AFE_PORT_ID_QUATERNARY_MI2S_RX:
	case AFE_PORT_ID_QUATERNARY_MI2S_TX:
	case AFE_PORT_ID_TERTIARY_MI2S_RX:
	case AFE_PORT_ID_TERTIARY_MI2S_TX:
	case AFE_PORT_ID_QUINARY_MI2S_RX:
	case AFE_PORT_ID_QUINARY_MI2S_TX:
	case AFE_PORT_ID_SENARY_MI2S_RX:
	case AFE_PORT_ID_SENARY_MI2S_TX:
	case AFE_PORT_ID_PRIMARY_META_MI2S_RX:
	case AFE_PORT_ID_SECONDARY_META_MI2S_RX:
	case AFE_PORT_ID_PRIMARY_TDM_RX:
	case AFE_PORT_ID_PRIMARY_TDM_TX:
	case AFE_PORT_ID_PRIMARY_TDM_RX_1:
	case AFE_PORT_ID_PRIMARY_TDM_TX_1:
	case AFE_PORT_ID_PRIMARY_TDM_RX_2:
	case AFE_PORT_ID_PRIMARY_TDM_TX_2:
	case AFE_PORT_ID_PRIMARY_TDM_RX_3:
	case AFE_PORT_ID_PRIMARY_TDM_TX_3:
	case AFE_PORT_ID_PRIMARY_TDM_RX_4:
	case AFE_PORT_ID_PRIMARY_TDM_TX_4:
	case AFE_PORT_ID_PRIMARY_TDM_RX_5:
	case AFE_PORT_ID_PRIMARY_TDM_TX_5:
	case AFE_PORT_ID_PRIMARY_TDM_RX_6:
	case AFE_PORT_ID_PRIMARY_TDM_TX_6:
	case AFE_PORT_ID_PRIMARY_TDM_RX_7:
	case AFE_PORT_ID_PRIMARY_TDM_TX_7:
	case AFE_PORT_ID_SECONDARY_TDM_RX:
	case AFE_PORT_ID_SECONDARY_TDM_TX:
	case AFE_PORT_ID_SECONDARY_TDM_RX_1:
	case AFE_PORT_ID_SECONDARY_TDM_TX_1:
	case AFE_PORT_ID_SECONDARY_TDM_RX_2:
	case AFE_PORT_ID_SECONDARY_TDM_TX_2:
	case AFE_PORT_ID_SECONDARY_TDM_RX_3:
	case AFE_PORT_ID_SECONDARY_TDM_TX_3:
	case AFE_PORT_ID_SECONDARY_TDM_RX_4:
	case AFE_PORT_ID_SECONDARY_TDM_TX_4:
	case AFE_PORT_ID_SECONDARY_TDM_RX_5:
	case AFE_PORT_ID_SECONDARY_TDM_TX_5:
	case AFE_PORT_ID_SECONDARY_TDM_RX_6:
	case AFE_PORT_ID_SECONDARY_TDM_TX_6:
	case AFE_PORT_ID_SECONDARY_TDM_RX_7:
	case AFE_PORT_ID_SECONDARY_TDM_TX_7:
	case AFE_PORT_ID_TERTIARY_TDM_RX:
	case AFE_PORT_ID_TERTIARY_TDM_TX:
	case AFE_PORT_ID_TERTIARY_TDM_RX_1:
	case AFE_PORT_ID_TERTIARY_TDM_TX_1:
	case AFE_PORT_ID_TERTIARY_TDM_RX_2:
	case AFE_PORT_ID_TERTIARY_TDM_TX_2:
	case AFE_PORT_ID_TERTIARY_TDM_RX_3:
	case AFE_PORT_ID_TERTIARY_TDM_TX_3:
	case AFE_PORT_ID_TERTIARY_TDM_RX_4:
	case AFE_PORT_ID_TERTIARY_TDM_TX_4:
	case AFE_PORT_ID_TERTIARY_TDM_RX_5:
	case AFE_PORT_ID_TERTIARY_TDM_TX_5:
	case AFE_PORT_ID_TERTIARY_TDM_RX_6:
	case AFE_PORT_ID_TERTIARY_TDM_TX_6:
	case AFE_PORT_ID_TERTIARY_TDM_RX_7:
	case AFE_PORT_ID_TERTIARY_TDM_TX_7:
	case AFE_PORT_ID_QUATERNARY_TDM_RX:
	case AFE_PORT_ID_QUATERNARY_TDM_TX:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_1:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_1:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_2:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_2:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_3:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_3:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_4:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_4:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_5:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_5:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_6:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_6:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_7:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_7:
	case AFE_PORT_ID_QUINARY_TDM_RX:
	case AFE_PORT_ID_QUINARY_TDM_TX:
	case AFE_PORT_ID_QUINARY_TDM_RX_1:
	case AFE_PORT_ID_QUINARY_TDM_TX_1:
	case AFE_PORT_ID_QUINARY_TDM_RX_2:
	case AFE_PORT_ID_QUINARY_TDM_TX_2:
	case AFE_PORT_ID_QUINARY_TDM_RX_3:
	case AFE_PORT_ID_QUINARY_TDM_TX_3:
	case AFE_PORT_ID_QUINARY_TDM_RX_4:
	case AFE_PORT_ID_QUINARY_TDM_TX_4:
	case AFE_PORT_ID_QUINARY_TDM_RX_5:
	case AFE_PORT_ID_QUINARY_TDM_TX_5:
	case AFE_PORT_ID_QUINARY_TDM_RX_6:
	case AFE_PORT_ID_QUINARY_TDM_TX_6:
	case AFE_PORT_ID_QUINARY_TDM_RX_7:
	case AFE_PORT_ID_QUINARY_TDM_TX_7:
	case AFE_PORT_ID_SENARY_TDM_RX:
	case AFE_PORT_ID_SENARY_TDM_TX:
	case AFE_PORT_ID_SENARY_TDM_RX_1:
	case AFE_PORT_ID_SENARY_TDM_TX_1:
	case AFE_PORT_ID_SENARY_TDM_RX_2:
	case AFE_PORT_ID_SENARY_TDM_TX_2:
	case AFE_PORT_ID_SENARY_TDM_RX_3:
	case AFE_PORT_ID_SENARY_TDM_TX_3:
	case AFE_PORT_ID_SENARY_TDM_RX_4:
	case AFE_PORT_ID_SENARY_TDM_TX_4:
	case AFE_PORT_ID_SENARY_TDM_RX_5:
	case AFE_PORT_ID_SENARY_TDM_TX_5:
	case AFE_PORT_ID_SENARY_TDM_RX_6:
	case AFE_PORT_ID_SENARY_TDM_TX_6:
	case AFE_PORT_ID_SENARY_TDM_RX_7:
	case AFE_PORT_ID_SENARY_TDM_TX_7:
	case AFE_PORT_ID_INT0_MI2S_RX:
	case AFE_PORT_ID_INT1_MI2S_RX:
	case AFE_PORT_ID_INT2_MI2S_RX:
	case AFE_PORT_ID_INT3_MI2S_RX:
	case AFE_PORT_ID_INT4_MI2S_RX:
	case AFE_PORT_ID_INT5_MI2S_RX:
	case AFE_PORT_ID_INT6_MI2S_RX:
	case AFE_PORT_ID_INT0_MI2S_TX:
	case AFE_PORT_ID_INT1_MI2S_TX:
	case AFE_PORT_ID_INT2_MI2S_TX:
	case AFE_PORT_ID_INT3_MI2S_TX:
	case AFE_PORT_ID_INT4_MI2S_TX:
	case AFE_PORT_ID_INT5_MI2S_TX:
	case AFE_PORT_ID_INT6_MI2S_TX:
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_1:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_2:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_2:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_0:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_0:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_1:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_1:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_2:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_2:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_3:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_3:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_4:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_4:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_5:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_5:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_6:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_7:
	{
		ret = 0;
		break;
	}

	default:
		pr_err("%s: default ret 0x%x\n", __func__, port_id);
		ret = -EINVAL;
	}

	return ret;
}

int afe_convert_virtual_to_portid(u16 port_id)
{
	int ret;

	/*
	 * if port_id is virtual, convert to physical..
	 * if port_id is already physical, return physical
	 */
	if (afe_validate_port(port_id) < 0) {
		if (port_id == RT_PROXY_DAI_001_RX ||
		    port_id == RT_PROXY_DAI_001_TX ||
		    port_id == RT_PROXY_DAI_002_RX ||
		    port_id == RT_PROXY_DAI_002_TX) {
			ret = VIRTUAL_ID_TO_PORTID(port_id);
		} else {
			pr_err("%s: wrong port 0x%x\n",
				__func__, port_id);
			ret = -EINVAL;
		}
	} else
		ret = port_id;

	return ret;
}
int afe_port_stop_nowait(int port_id)
{
	struct afe_port_cmd_device_stop stop;
	int ret = 0;

	if (this_afe.apr == NULL) {
		pr_err("%s: AFE is already closed\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}
	pr_debug("%s: port_id = 0x%x\n", __func__, port_id);
	port_id = q6audio_convert_virtual_to_portid(port_id);

	stop.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	stop.hdr.pkt_size = sizeof(stop);
	stop.hdr.src_port = 0;
	stop.hdr.dest_port = 0;
	stop.hdr.token = 0;
	stop.hdr.opcode = AFE_PORT_CMD_DEVICE_STOP;
	stop.port_id = port_id;
	stop.reserved = 0;

	ret = afe_apr_send_pkt(&stop, NULL);
	if (ret)
		pr_err("%s: AFE close failed %d\n", __func__, ret);

fail_cmd:
	return ret;

}

int afe_close(int port_id)
{
    pr_debug("%s: close be[%d] action delayed to platform driver\n",__func__,port_id);
    return 0;
}
EXPORT_SYMBOL(afe_close);

/**
 * afe_close - command to close AFE port
 *
 * @port_id: AFE port id
 *
 * Returns 0 on success, appropriate error code otherwise
 */
int afe_close_port(int port_id)
{
	struct afe_port_cmd_device_close close;
	int ret = 0;
	int index = 0;
	
	mutex_lock(&this_afe.afe_cmd_lock);

	if (this_afe.apr == NULL) {
		pr_err("%s: AFE is already closed\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	port_id = q6audio_convert_virtual_to_portid(port_id);
	index = q6audio_get_port_index(port_id);
	if (index < 0 || index >= AFE_MAX_PORTS) {
		pr_err("%s: AFE port index[%d] invalid!\n",
				__func__, index);
		goto fail_cmd;
	}
	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_warn("%s: Not a valid port id 0x%x ret %d\n",
			__func__, port_id, ret);
		goto fail_cmd;
	}

	if(port_id == VOICE_RECORD_RX ||
	   port_id == VOICE_RECORD_TX){
	    if(this_afe.vr_dev_cnt <= 0){
	        pr_info("%s: voice record device is already closed",__func__);
	        mutex_unlock(&this_afe.afe_cmd_lock);
	        return 0;
	    }
	    this_afe.vr_dev_cnt = 0;
	}

	close.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	close.hdr.pkt_size = sizeof(close);
	close.hdr.src_port = 0;
	close.hdr.dest_port = 0;
	close.hdr.src_svc = APR_SVC_AFE;
	close.hdr.src_domain = APR_DOMAIN_APPS;
	close.hdr.dest_svc = APR_SVC_AFE;
	close.hdr.dest_domain = APR_DOMAIN_ADSP;

	close.hdr.token = index;
	close.hdr.opcode = AFE_PORT_CMD_DEVICE_CLOSE;
	close.device_id = jlqaudio_get_dsp_be_id(port_id);
	
	pr_info("%s: device id = 0x%x\n", __func__, close.device_id);

	ret = afe_apr_send_pkt(&close, 0);
	if (ret)
		pr_err("%s: AFE close failed %d\n", __func__, ret);

fail_cmd:
	mutex_unlock(&this_afe.afe_cmd_lock);
	return ret;
}
EXPORT_SYMBOL(afe_close_port);

static void afe_release_uevent_data(struct kobject *kobj)
{
	struct audio_uevent_data *data = container_of(kobj,
		struct audio_uevent_data, kobj);

	kfree(data);
}

int __init afe_init(void)
{
	int i = 0;

	atomic_set(&this_afe.state, 0);
	atomic_set(&this_afe.status, 0);
	atomic_set(&this_afe.clk_state, 0);
	atomic_set(&this_afe.clk_status, 0);
	this_afe.apr = NULL;
	this_afe.dtmf_gen_rx_portid = -1;
	this_afe.mmap_handle = 0;
	this_afe.vi_tx_port = -1;
	this_afe.vi_rx_port = -1;
	for (i = 0; i < AFE_LPASS_CORE_HW_VOTE_MAX; i++)
		this_afe.lpass_hw_core_client_hdl[i] = 0;
	this_afe.prot_cfg.mode = MSM_SPKR_PROT_DISABLED;
	this_afe.th_ftm_cfg.mode = MSM_SPKR_PROT_DISABLED;
	this_afe.ex_ftm_cfg.mode = MSM_SPKR_PROT_DISABLED;
	mutex_init(&this_afe.afe_cmd_lock);
	mutex_init(&this_afe.afe_apr_lock);
	mutex_init(&this_afe.afe_clk_lock);
	for (i = 0; i < AFE_MAX_PORTS; i++) {
		this_afe.afe_sample_rates[i] = 0;
		this_afe.dev_acdb_id[i] = 0;
		this_afe.island_mode[i] = 0;
		this_afe.vad_cfg[i].is_enable = 0;
		this_afe.vad_cfg[i].pre_roll = 0;
		init_waitqueue_head(&this_afe.wait[i]);
	}
	init_waitqueue_head(&this_afe.wait_wakeup);
	init_waitqueue_head(&this_afe.lpass_core_hw_wait);
	init_waitqueue_head(&this_afe.clk_wait);
	//wakeup_source_init(&wl.ws, "spkr-prot");

	this_afe.uevent_data = kzalloc(sizeof(*(this_afe.uevent_data)), GFP_KERNEL);
	if (!this_afe.uevent_data)
		return -ENOMEM;

	/*
	 * Set release function to cleanup memory related to kobject
	 * before initializing the kobject.
	 */
	this_afe.uevent_data->ktype.release = afe_release_uevent_data;
	q6core_init_uevent_data(this_afe.uevent_data, "q6afe_uevent");

	this_afe.event_notifier.notifier_call  = afe_aud_event_notify;
	msm_aud_evt_blocking_register_client(&this_afe.event_notifier);

	return 0;
}

void afe_exit(void)
{
	if (this_afe.apr) {
		apr_reset(this_afe.apr);
		atomic_set(&this_afe.state, 0);
		this_afe.apr = NULL;
		rtac_set_afe_handle(this_afe.apr);
	}

	q6core_destroy_uevent_data(this_afe.uevent_data);

	mutex_destroy(&this_afe.afe_cmd_lock);
	mutex_destroy(&this_afe.afe_apr_lock);
	mutex_destroy(&this_afe.afe_clk_lock);
	//wakeup_source_trash(&wl.ws);
}

