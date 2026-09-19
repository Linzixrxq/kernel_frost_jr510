// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <sound/asound.h>
#include <dsp/msm-dts-srs-tm-config.h>
#include <dsp/apr_audio-v2.h>
#include <dsp/q6adm-v2.h>
#include <dsp/q6audio-v2.h>
#include <dsp/q6afe-v2.h>
#include <dsp/q6core.h>
#include <dsp/audio_cal_utils.h>
#include <dsp/q6common.h>
#include <ipc/apr.h>
#include "adsp_err.h"

#define TIMEOUT_MS 1000

#define RESET_COPP_ID 99
#define INVALID_COPP_ID 0xFF
/* Used for inband payload copy, max size is 4k */
/* 3 is to account for module, instance & param ID in payload */
#define ADM_GET_PARAMETER_LENGTH (4096 - APR_HDR_SIZE - 3 * sizeof(uint32_t))

#define ULL_SUPPORTED_BITS_PER_SAMPLE 16
#define ULL_SUPPORTED_SAMPLE_RATE 48000

#ifndef CONFIG_DOLBY_DAP
#undef DOLBY_ADM_COPP_TOPOLOGY_ID
#define DOLBY_ADM_COPP_TOPOLOGY_ID 0xFFFFFFFE
#endif

#ifndef CONFIG_DOLBY_DS2
#undef DS2_ADM_COPP_TOPOLOGY_ID
#define DS2_ADM_COPP_TOPOLOGY_ID 0xFFFFFFFF
#endif

#define SESSION_TYPE_RX 0

/* ENUM for adm_status */
enum adm_cal_status {
	ADM_STATUS_CALIBRATION_REQUIRED = 0,
	ADM_STATUS_MAX,
};

struct adm_ctl {
	void *apr;
	atomic_t matrix_map_stat;
	wait_queue_head_t matrix_map_wait;
	wait_queue_head_t adm_wait;
};

static struct adm_ctl			this_adm;

struct adm_multi_ch_map {
	bool set_channel_map;
	char channel_mapping[PCM_FORMAT_MAX_NUM_CHANNEL_V8];
};

#define ADM_MCH_MAP_IDX_PLAYBACK 0
#define ADM_MCH_MAP_IDX_REC 1

int adm_validate_and_get_port_index(int port_id)
{
	int index;
	int ret;

	ret = q6audio_validate_port(port_id);
	if (ret < 0) {
		pr_err("%s: port validation failed id 0x%x ret %d\n",
			__func__, port_id, ret);
		return -EINVAL;
	}

	index = afe_get_port_index(port_id);
	if (index < 0 || index >= AFE_MAX_PORTS) {
		pr_err("%s: Invalid port idx %d port_id 0x%x\n",
			__func__, index,
			port_id);
		return -EINVAL;
	}
	pr_debug("%s: port_idx- %d\n", __func__, index);
	return index;
}
EXPORT_SYMBOL(adm_validate_and_get_port_index);


static void adm_reset_data(void)
{
	apr_reset(this_adm.apr);

	this_adm.apr = NULL;
}

static int32_t adm_callback(struct apr_client_data *data, void *priv)
{
	uint32_t *payload;

	if (data == NULL) {
		pr_err("%s: data parameter is null\n", __func__);
		return -EINVAL;
	}

	payload = data->payload;

	if (data->opcode == RESET_EVENTS) {
		pr_debug("%s: Reset event is received: %d %d apr[%p]\n",
			__func__,
			data->reset_event, data->reset_proc, this_adm.apr);
		//if (this_adm.apr)
		//	adm_reset_data();
		return 0;
	}

	if (data->payload_size >= sizeof(uint32_t)) {
		if (data->opcode == APR_BASIC_RSP_RESULT) {
			pr_debug("%s: APR_BASIC_RSP_RESULT payload id 0x%X\n",
				     __func__, payload[0]);
			
			if (payload[1] != 0) {
				pr_err("%s: cmd = 0x%x returned error status = 0x%x\n",
					__func__, payload[0], payload[1]);
			}

			switch (payload[0]) {

			case ADM_CMD_MATRIX_MAP_ROUTINGS_V5:
			case ADM_STREAM_FM_OPEN:
			case ADM_STREAM_FM_RUN:
			case ADM_STREAM_FM_CLOSE:
			case ADM_STREAM_FM_PAUSE:
			case ADM_IVOLUME_CMD_FM_SET_STEP:
			case ADM_CMD_AUDIO_CAL_UPDATED:
            case ADM_CMD_ENABLE_EFFECT:
				pr_debug("%s: adm callback received,wake up.\n",__func__);
				atomic_set(&this_adm.matrix_map_stat,
					payload[1]);
				wake_up(&this_adm.matrix_map_wait);
				break;
			default:
				pr_err("%s: Unknown Cmd: 0x%x\n", __func__,
								payload[0]);
				break;
			}
		}
	}
	return 0;
}

static int __adm_open_fm(struct asm_pcm_cfg_param cfg_param)
{
	struct fm_stream_cmd_open	open;
	int ret = 0;
	
	open.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	open.hdr.pkt_size = sizeof(open);
	open.hdr.src_svc = APR_SVC_ADM;
	open.hdr.src_domain = APR_DOMAIN_APPS;
	open.hdr.src_port = 0; /* Ignored */;
	open.hdr.dest_svc = APR_SVC_ADM;
	open.hdr.dest_domain = APR_DOMAIN_ADSP;
	open.hdr.dest_port = 0; /* Ignored */;
	open.hdr.token = 0;
	open.hdr.opcode = ADM_STREAM_FM_OPEN;
	open.cfg_param = cfg_param;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&open);
	if (ret < 0) {
		pr_err("%s: fm open failed ret %d\n",__func__,ret);
		ret = -EINVAL;
		goto fail_cmd;
	}
#if 1
	ret = wait_event_timeout(this_adm.matrix_map_wait,
				atomic_read(&this_adm.matrix_map_stat) >= 0,
				msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: timeout. waited for open fm\n",__func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
			adsp_err_get_err_str(atomic_read(
			&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
				atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}
#endif
fail_cmd:
	return ret;
}

int adm_open_fm(uint16_t bits_per_sample,uint32_t rate, uint32_t channels)
{
    struct asm_pcm_cfg_param pcm_cfg;

	pcm_cfg.sample_rate = rate;
	pcm_cfg.channel_num = channels;
	pcm_cfg.bit_width   = bits_per_sample;
	pcm_cfg.interleave = 0;

	pr_info("%s: rate(%d) channels(%d) bits(%d).\n",
		__func__, pcm_cfg.sample_rate, pcm_cfg.channel_num,
		pcm_cfg.bit_width);
	
	return __adm_open_fm(pcm_cfg);
}
EXPORT_SYMBOL(adm_open_fm);

int adm_fm_run(void)
{
	struct fm_stream_cmd_run	run;
	int ret = 0;

	pr_debug("%s\n",__func__);
	
	run.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	run.hdr.pkt_size = sizeof(run);
	run.hdr.src_svc = APR_SVC_ADM;
	run.hdr.src_domain = APR_DOMAIN_APPS;
	run.hdr.src_port = 0; /* Ignored */;
	run.hdr.dest_svc = APR_SVC_ADM;
	run.hdr.dest_domain = APR_DOMAIN_ADSP;
	run.hdr.dest_port = 0; /* Ignored */;
	run.hdr.token = 0;
	run.hdr.opcode = ADM_STREAM_FM_RUN;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&run);
	if (ret < 0) {
		pr_err("%s: fm run failed ret %d\n",__func__,ret);
		ret = -EINVAL;
		goto fail_cmd;
	}
#if 1
	ret = wait_event_timeout(this_adm.matrix_map_wait,
				atomic_read(&this_adm.matrix_map_stat) >= 0,
				msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: timeout. waited for run fm\n",__func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
			adsp_err_get_err_str(atomic_read(
			&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
				atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}
#endif
fail_cmd:
	return ret;
}
EXPORT_SYMBOL(adm_fm_run);

int adm_fm_pause(void)
{
	struct fm_stream_cmd_pause	pause;
	int ret = 0;

	pr_info("%s\n",__func__);

	pause.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	pause.hdr.pkt_size = sizeof(pause);
	pause.hdr.src_svc = APR_SVC_ADM;
	pause.hdr.src_domain = APR_DOMAIN_APPS;
	pause.hdr.src_port = 0; /* Ignored */;
	pause.hdr.dest_svc = APR_SVC_ADM;
	pause.hdr.dest_domain = APR_DOMAIN_ADSP;
	pause.hdr.dest_port = 0; /* Ignored */;
	pause.hdr.token = 0;
	pause.hdr.opcode = ADM_STREAM_FM_PAUSE;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&pause);
	if (ret < 0) {
		pr_err("%s: fm pause failed ret %d\n",__func__,ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(this_adm.matrix_map_wait,
				atomic_read(&this_adm.matrix_map_stat) >= 0,
				msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: timeout. waited for pause fm\n",__func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
			adsp_err_get_err_str(atomic_read(
			&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
				atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}

fail_cmd:
	return ret;
}
EXPORT_SYMBOL(adm_fm_pause);

int adm_fm_close(void)
{
	struct fm_stream_cmd_close	close;
	int ret = 0;

	pr_info("%s\n",__func__);
	
	close.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	close.hdr.pkt_size = sizeof(close);
	close.hdr.src_svc = APR_SVC_ADM;
	close.hdr.src_domain = APR_DOMAIN_APPS;
	close.hdr.src_port = 0; /* Ignored */;
	close.hdr.dest_svc = APR_SVC_ADM;
	close.hdr.dest_domain = APR_DOMAIN_ADSP;
	close.hdr.dest_port = 0; /* Ignored */;
	close.hdr.token = 0;
	close.hdr.opcode = ADM_STREAM_FM_CLOSE;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&close);
	if (ret < 0) {
		pr_err("%s: fm close failed ret %d\n",__func__,ret);
		ret = -EINVAL;
		goto fail_cmd;
	}
#if 1
	ret = wait_event_timeout(this_adm.matrix_map_wait,
				atomic_read(&this_adm.matrix_map_stat) >= 0,
				msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: timeout. waited for close fm\n",__func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
			adsp_err_get_err_str(atomic_read(
			&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
				atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}
#endif
fail_cmd:
	return ret;
}
EXPORT_SYMBOL(adm_fm_close);

int adm_caldata_update(void)
{
	struct audiocal_adsp_update	updated;
	int ret = 0;

	pr_debug("%s\n",__func__);

	updated.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	updated.hdr.pkt_size = sizeof(updated);
	updated.hdr.src_svc = APR_SVC_ADM;
	updated.hdr.src_domain = APR_DOMAIN_APPS;
	updated.hdr.src_port = 0; /* Ignored */;
	updated.hdr.dest_svc = APR_SVC_ADM;
	updated.hdr.dest_domain = APR_DOMAIN_ADSP;
	updated.hdr.dest_port = 0; /* Ignored */;
	updated.hdr.token = 0;
	updated.hdr.opcode = ADM_CMD_AUDIO_CAL_UPDATED;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&updated);
	if (ret < 0) {
		pr_err("%s: fm run failed ret %d\n",__func__,ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(this_adm.matrix_map_wait,
							atomic_read(&this_adm.matrix_map_stat) >= 0,
							msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: timeout.\n",__func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
				adsp_err_get_err_str(atomic_read(
				&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
					atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}

fail_cmd:
	return ret;
}
EXPORT_SYMBOL(adm_caldata_update);

/**
 * adm_matrix_map -
 *        command to send ADM matrix map for ADM copp list
 *
 * @path: direction or ADM path type
 * @payload_map: have info of session id and associated copp_idx/num_copps
 * @perf_mode: performance mode like LL/ULL/..
 * @passthr_mode: flag to indicate passthrough mode
 *
 * Returns 0 on success or error on failure
 */
int adm_matrix_map(struct route_payload *payload_map)
{
	struct adm_cmd_matrix_map_routings_v5	route;
	int ret = 0;
	
	route.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	route.hdr.pkt_size = sizeof(route);
	route.hdr.src_svc = APR_SVC_ADM;
	route.hdr.src_domain = APR_DOMAIN_APPS;
	route.hdr.src_port = 0; /* Ignored */;
	route.hdr.dest_svc = APR_SVC_ADM;
	route.hdr.dest_domain = APR_DOMAIN_ADSP;
	route.hdr.dest_port = 0; /* Ignored */;
	route.hdr.token = 0;
	route.hdr.opcode = ADM_CMD_MATRIX_MAP_ROUTINGS_V5;
	route.bedai_id = payload_map->bdai_id;
	route.fedai_id = payload_map->fdai_id;
	route.connect = payload_map->connect;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&route);
	if (ret < 0) {
		pr_err("%s: routing for fedai_id %d failed ret %d\n",
			__func__, route.fedai_id, ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(this_adm.matrix_map_wait,
				atomic_read(&this_adm.matrix_map_stat) >= 0,
				msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: routing for syream failed\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
			adsp_err_get_err_str(atomic_read(
			&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
				atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}
fail_cmd:
	return ret;
}
EXPORT_SYMBOL(adm_matrix_map);


int adm_set_fm_volume(u16 volume)
{
	struct fm_volume_set_step_cmd	vol;
	int ret = 0;
	
	pr_info("%s volume %d\n",__func__,volume);
	
	vol.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	vol.hdr.pkt_size = sizeof(vol);
	vol.hdr.src_svc = APR_SVC_ADM;
	vol.hdr.src_domain = APR_DOMAIN_APPS;
	vol.hdr.src_port = 0; /* Ignored */;
	vol.hdr.dest_svc = APR_SVC_ADM;
	vol.hdr.dest_domain = APR_DOMAIN_ADSP;
	vol.hdr.dest_port = 0; /* Ignored */;
	vol.hdr.token = 0;
	vol.hdr.opcode = ADM_IVOLUME_CMD_FM_SET_STEP;
	vol.vol_step.value = volume;
	vol.vol_step.direction = FM_IVOLUME_DIRECTION_RX;
	vol.vol_step.ramp_duration_ms = FM_IVOLUME_RAMP_MS;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&vol);
	if (ret < 0) {
		pr_err("%s: set fm volume failed ret %d\n",__func__,ret);
		ret = -EINVAL;
		goto fail_cmd;
	}
#if 1
	ret = wait_event_timeout(this_adm.matrix_map_wait,
				atomic_read(&this_adm.matrix_map_stat) >= 0,
				msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: timeout. waited for set fm volume\n",__func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
			adsp_err_get_err_str(atomic_read(
			&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
				atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}
#endif
fail_cmd:
	return ret;
}
EXPORT_SYMBOL(adm_set_fm_volume);


int adm_pack_and_set_one_pp_param(int port_id, int copp_idx,
				  struct param_hdr_v3 param_hdr, u8 *param_data)
{
	u8 *packed_data = NULL;
	u32 total_size = 0;
	int ret = 0;

	total_size = sizeof(union param_hdrs) + param_hdr.param_size;
	packed_data = kzalloc(total_size, GFP_KERNEL);
	if (!packed_data)
		return -ENOMEM;

	ret = q6common_pack_pp_params(packed_data, &param_hdr, param_data,
				      &total_size);
	if (ret) {
		pr_err("%s: Failed to pack parameter data, error %d\n",
		       __func__, ret);
		goto done;
	}

	ret = adm_set_pp_params(port_id, copp_idx, NULL, packed_data,
				total_size);
	if (ret)
		pr_err("%s: Failed to set parameter data, error %d\n", __func__,
		       ret);
done:
	kfree(packed_data);
	return ret;
}
EXPORT_SYMBOL(adm_pack_and_set_one_pp_param);

/**
 * adm_set_volume -
 *        command to set volume on ADM copp
 *
 * @port_id: Port ID number
 * @copp_idx: copp index assigned
 * @volume: gain value to set
 *
 * Returns 0 on success or error on failure
 */
int adm_set_volume(int port_id, int copp_idx, int volume)
{
	struct audproc_volume_ctrl_master_gain audproc_vol;
	struct param_hdr_v3 param_hdr;
	int rc  = 0;

	pr_info("%s: port_id %d, volume %d\n", __func__, port_id, volume);

	memset(&audproc_vol, 0, sizeof(audproc_vol));
	memset(&param_hdr, 0, sizeof(param_hdr));
	param_hdr.module_id = AUDPROC_MODULE_ID_VOL_CTRL;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = AUDPROC_PARAM_ID_VOL_CTRL_MASTER_GAIN;
	param_hdr.param_size = sizeof(audproc_vol);

	audproc_vol.master_gain = volume;

	rc = adm_pack_and_set_one_pp_param(port_id, copp_idx, param_hdr,
					   (uint8_t *) &audproc_vol);
	if (rc)
		pr_err("%s: Failed to set volume, err %d\n", __func__, rc);

	return rc;
}
EXPORT_SYMBOL(adm_set_volume);

/**
 * adm_set_softvolume -
 *        command to set softvolume
 *
 * @port_id: Port ID number
 * @copp_idx: copp index assigned
 * @softvol_param: Params to set for softvolume
 *
 * Returns 0 on success or error on failure
 */
int adm_set_softvolume(int port_id, int copp_idx,
			struct audproc_softvolume_params *softvol_param)
{
	struct audproc_soft_step_volume_params audproc_softvol;
	struct param_hdr_v3 param_hdr;
	int rc  = 0;

	pr_debug("%s: period %d step %d curve %d\n", __func__,
		 softvol_param->period, softvol_param->step,
		 softvol_param->rampingcurve);

	memset(&audproc_softvol, 0, sizeof(audproc_softvol));
	memset(&param_hdr, 0, sizeof(param_hdr));
	param_hdr.module_id = AUDPROC_MODULE_ID_VOL_CTRL;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = AUDPROC_PARAM_ID_SOFT_VOL_STEPPING_PARAMETERS;
	param_hdr.param_size = sizeof(audproc_softvol);

	audproc_softvol.period = softvol_param->period;
	audproc_softvol.step = softvol_param->step;
	audproc_softvol.ramping_curve = softvol_param->rampingcurve;

	pr_debug("%s: period %d, step %d, curve %d\n", __func__,
		 audproc_softvol.period, audproc_softvol.step,
		 audproc_softvol.ramping_curve);

	rc = adm_pack_and_set_one_pp_param(port_id, copp_idx, param_hdr,
					   (uint8_t *) &audproc_softvol);
	if (rc)
		pr_err("%s: Failed to set soft volume, err %d\n", __func__, rc);

	return rc;
}
EXPORT_SYMBOL(adm_set_softvolume);

/**
 * adm_set_mic_gain -
 *        command to set MIC gain
 *
 * @port_id: Port ID number
 * @copp_idx: copp index assigned
 * @volume: gain value to set
 *
 * Returns 0 on success or error on failure
 */
int adm_set_mic_gain(int port_id, int copp_idx, int volume)
{
	struct admx_mic_gain mic_gain_params;
	struct param_hdr_v3 param_hdr;
	int rc = 0;

	pr_debug("%s: Setting mic gain to %d at port_id 0x%x\n", __func__,
		 volume, port_id);

	memset(&mic_gain_params, 0, sizeof(mic_gain_params));
	memset(&param_hdr, 0, sizeof(param_hdr));
	param_hdr.module_id = ADM_MODULE_IDX_MIC_GAIN_CTRL;
	param_hdr.instance_id = INSTANCE_ID_0;
	param_hdr.param_id = ADM_PARAM_IDX_MIC_GAIN;
	param_hdr.param_size = sizeof(mic_gain_params);

	mic_gain_params.tx_mic_gain = volume;

	rc = adm_pack_and_set_one_pp_param(port_id, copp_idx, param_hdr,
					   (uint8_t *) &mic_gain_params);
	if (rc)
		pr_err("%s: Failed to set mic gain, err %d\n", __func__, rc);

	return rc;
}
EXPORT_SYMBOL(adm_set_mic_gain);

int adm_effect_enable(int effect_type,int effect_config,int enable)
{
	struct adm_cmd_effect_enable	effect_enable;
	int ret = 0;

	effect_enable.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	effect_enable.hdr.pkt_size = sizeof(effect_enable);
	effect_enable.hdr.src_svc = APR_SVC_ADM;
	effect_enable.hdr.src_domain = APR_DOMAIN_APPS;
	effect_enable.hdr.src_port = 0; /* Ignored */;
	effect_enable.hdr.dest_svc = APR_SVC_ADM;
	effect_enable.hdr.dest_domain = APR_DOMAIN_ADSP;
	effect_enable.hdr.dest_port = 0; /* Ignored */;
	effect_enable.hdr.token = 0;
	effect_enable.hdr.opcode = ADM_CMD_ENABLE_EFFECT;
	effect_enable.effect_type = effect_type;
	effect_enable.effect_config = effect_config;
	effect_enable.enable = enable;

	atomic_set(&this_adm.matrix_map_stat, -1);

	ret = apr_send_pkt(this_adm.apr, (uint32_t *)&effect_enable);
	if (ret < 0) {
		pr_err("%s: apr_send_pkt failed ret %d\n",__func__,ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(this_adm.matrix_map_wait,
				atomic_read(&this_adm.matrix_map_stat) >= 0,
				msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_err("%s: config failed\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	} else if (atomic_read(&this_adm.matrix_map_stat) > 0) {
		pr_err("%s: DSP returned error[%s]\n", __func__,
			adsp_err_get_err_str(atomic_read(
			&this_adm.matrix_map_stat)));
		ret = adsp_err_get_lnx_err_code(
				atomic_read(&this_adm.matrix_map_stat));
		goto fail_cmd;
	}
fail_cmd:
	return ret;
}
EXPORT_SYMBOL(adm_effect_enable);

int __init adm_init(void)
{
	init_waitqueue_head(&this_adm.matrix_map_wait);
	init_waitqueue_head(&this_adm.adm_wait);

	if (this_adm.apr == NULL) {
		this_adm.apr = apr_register("ADSP", "ADM", adm_callback,
						0xFFFFFFFF, &this_adm);
		if (this_adm.apr == NULL) {
			pr_err("%s: Unable to register ADM\n", __func__);
			return -ENODEV;
		}
		rtac_set_adm_handle(this_adm.apr);
	}

	return 0;
}

void adm_exit(void)
{
	if (this_adm.apr)
		adm_reset_data();
}
