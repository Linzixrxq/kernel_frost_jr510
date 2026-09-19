// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */


#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/timer.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>
#include <linux/msm_audio.h>

#include <linux/of_device.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>
#include <sound/devdep_params.h>
#include <dsp/msm_audio_ion.h>
#include <dsp/q6audio-v2.h>
#include <dsp/q6core.h>
#include <dsp/q6voice.h>
#include <dsp/q6asm-v2.h>

#include "msm-pcm-q6-v2.h"
#include "msm-pcm-routing-v2.h"
#include "msm-qti-pp-config.h"

#define DRV_NAME "msm-pcm-q6-v2"
#define TIMEOUT_MS	1000

enum stream_state {
	IDLE = 0,
	STOPPED,
	RUNNING,
};

static struct audio_locks the_locks;

#define PCM_MASTER_VOL_MAX_STEPS	0x2000
static const DECLARE_TLV_DB_LINEAR(msm_pcm_vol_gain, 0,
			PCM_MASTER_VOL_MAX_STEPS);


struct snd_msm_audio_voip_info {
    struct snd_pcm_substream *audioVoipTxSubstream;
    struct snd_pcm_substream *audioVoipRxSubstream;
};
struct snd_msm_audio_voip_info audioVoipInfo;


struct snd_msm {
	struct snd_card *card;
	struct snd_pcm *pcm;
};

#define CMD_EOS_MIN_TIMEOUT_LENGTH  50
#define CMD_EOS_TIMEOUT_MULTIPLIER  (HZ * 50)
#define MAX_PB_COPY_RETRIES         3

static struct snd_pcm_hardware msm_pcm_hardware_capture = {
	.info =                 (SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE |
				SNDRV_PCM_FMTBIT_S32_LE),
	.rates =                SNDRV_PCM_RATE_8000_384000,
	.rate_min =             8000,
	.rate_max =             384000,
	.channels_min =         1,
	.channels_max =         4,
	.buffer_bytes_max =     CAPTURE_MAX_NUM_PERIODS *
				CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min =	CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max =     CAPTURE_MAX_PERIOD_SIZE,
	.periods_min =          CAPTURE_MIN_NUM_PERIODS,
	.periods_max =          CAPTURE_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

static struct snd_pcm_hardware msm_pcm_hardware_playback = {
	.info =                 (SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE |
				SNDRV_PCM_FMTBIT_S32_LE),
	.rates =                SNDRV_PCM_RATE_8000_384000,
	.rate_min =             8000,
	.rate_max =             384000,
	.channels_min =         1,
	.channels_max =         8,
	.buffer_bytes_max =     PLAYBACK_MAX_NUM_PERIODS *
				PLAYBACK_MAX_PERIOD_SIZE,
	.period_bytes_min =	PLAYBACK_MIN_PERIOD_SIZE,
	.period_bytes_max =     PLAYBACK_MAX_PERIOD_SIZE,
	.periods_min =          PLAYBACK_MIN_NUM_PERIODS,
	.periods_max =          PLAYBACK_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

/* Conventional and unconventional sample rate supported */
static unsigned int supported_sample_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
	88200, 96000, 176400, 192000, 352800, 384000
};

static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(supported_sample_rates),
	.list = supported_sample_rates,
	.mask = 0,
};

static int msm_audio_voip_mute_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int mute_flag = ucontrol->value.integer.value[0];
	int ramp_duration = ucontrol->value.integer.value[1];

	struct snd_pcm_runtime *runtime = NULL;
	struct msm_audio *prtd = NULL;
	struct audio_client *audio_client;

	struct cvs_set_mute_cmd cvs_mute_cmd;

	if (audioVoipInfo.audioVoipTxSubstream == NULL) {
		pr_err(" %s: audio tx stream inactive.", __func__);
		ret = -EINVAL;
		goto done;
	}

	runtime = audioVoipInfo.audioVoipTxSubstream->runtime;
	if (runtime == NULL) {
		pr_err(" %s: audio tx stream runtime is null.", __func__);
		ret = -EINVAL;
		goto done;
	}
	prtd = runtime->private_data;
	audio_client = prtd->audio_client;

	pr_info("%s: mute_flag:%d\n", __func__, mute_flag);

	/* send mute/unmute to cvs */
	cvs_mute_cmd.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	cvs_mute_cmd.hdr.pkt_size = APR_PKT_SIZE(APR_HDR_SIZE,
					sizeof(cvs_mute_cmd) - APR_HDR_SIZE);
	cvs_mute_cmd.hdr.src_port = VOC_PATH_FULL;
	cvs_mute_cmd.hdr.dest_port = VOC_PATH_FULL;
	cvs_mute_cmd.hdr.src_svc = APR_SVC_ASM;
	cvs_mute_cmd.hdr.src_domain = APR_DOMAIN_APPS;
	cvs_mute_cmd.hdr.dest_svc = APR_SVC_ADSP_CVS;
	cvs_mute_cmd.hdr.dest_domain = APR_DOMAIN_ADSP;
	cvs_mute_cmd.hdr.token = 0;
	cvs_mute_cmd.hdr.opcode = VSS_IVOLUME_CMD_MUTE_V2;
	cvs_mute_cmd.cvs_set_mute.direction = VSS_IVOLUME_DIRECTION_TX;
	cvs_mute_cmd.cvs_set_mute.mute_flag = mute_flag;
	cvs_mute_cmd.cvs_set_mute.ramp_duration_ms = ramp_duration;

	ret = apr_send_pkt(audio_client->apr, (uint32_t *) &cvs_mute_cmd);
	if (ret < 0) {
		pr_err("%s: Error %d sending stream mute\n", __func__, ret);
		return -EINVAL;
	}
done:
	return ret;
}


static int msm_audio_voip_gain_set(int volume, int ramp_duration)
{
	int ret = 0;

	struct snd_pcm_runtime *runtime = NULL;
	struct msm_audio *prtd = NULL;
	struct audio_client *audio_client;

	struct cvp_set_rx_volume_step_cmd cvp_vol_step_cmd;

	if (audioVoipInfo.audioVoipRxSubstream == NULL) {
		pr_info(" %s: audio rx stream inactive.", __func__);
		ret = -EINVAL;
		goto done;
	}

	runtime = audioVoipInfo.audioVoipRxSubstream->runtime;
	if (runtime == NULL) {
		pr_err(" %s: audio rx stream runtime is null.", __func__);
		ret = -EINVAL;
		goto done;
	}
	prtd = runtime->private_data;
	audio_client = prtd->audio_client;

	if ((volume < 0) || (ramp_duration < 0)) {
		pr_err(" %s Invalid arguments", __func__);

		ret = -EINVAL;
		goto done;
	}

	pr_info("%s: volume: %d ramp_duration: %d\n", __func__, volume,
		ramp_duration);

	/* send volume to adsp */
	cvp_vol_step_cmd.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	cvp_vol_step_cmd.hdr.pkt_size = APR_PKT_SIZE(APR_HDR_SIZE,
				sizeof(cvp_vol_step_cmd) - APR_HDR_SIZE);
	cvp_vol_step_cmd.hdr.src_port = VOC_PATH_FULL;
	cvp_vol_step_cmd.hdr.dest_port = VOC_PATH_FULL;
	cvp_vol_step_cmd.hdr.src_svc = APR_SVC_ASM;;
	cvp_vol_step_cmd.hdr.src_domain = APR_DOMAIN_APPS;
	cvp_vol_step_cmd.hdr.dest_svc = APR_SVC_ADSP_CVP;
	cvp_vol_step_cmd.hdr.dest_domain = APR_DOMAIN_ADSP;

	cvp_vol_step_cmd.hdr.token = 0;
	cvp_vol_step_cmd.hdr.opcode = VSS_IVOLUME_CMD_SET_STEP;
	cvp_vol_step_cmd.cvp_set_vol_step.direction = VSS_IVOLUME_DIRECTION_RX;
	cvp_vol_step_cmd.cvp_set_vol_step.value = volume;
	cvp_vol_step_cmd.cvp_set_vol_step.ramp_duration_ms = ramp_duration;

	pr_info("%s step_value:%d, ramp_duration_ms:%d",
			__func__,
			cvp_vol_step_cmd.cvp_set_vol_step.value,
			cvp_vol_step_cmd.cvp_set_vol_step.ramp_duration_ms);

	ret = apr_send_pkt(audio_client->apr, (uint32_t *) &cvp_vol_step_cmd);
	if (ret < 0) {
		pr_err("Fail in sending VOIP RX VOL step\n");
		return -EINVAL;
	}
done:
	return ret;
}

static int msm_audio_voip_gain_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int volume = ucontrol->value.integer.value[0];
	int ramp_duration = ucontrol->value.integer.value[1];

	pr_info(" %s: volume=%d.", __func__, volume);

	if (audioVoipInfo.audioVoipRxSubstream == NULL) {
		pr_info(" %s: audio playback inactive currently.", __func__);
		ret = -EINVAL;
	} else {
		ret = msm_audio_voip_gain_set(volume, ramp_duration);
	}

	return ret;
}

static void event_handler(uint32_t opcode,
		uint32_t token, uint32_t *payload, void *priv)
{
	struct msm_audio *prtd = priv;
	struct snd_pcm_substream *substream = prtd->substream;
	uint32_t idx = 0;
	uint32_t size = 0;
	uint8_t buf_index;

	switch (opcode) {
	case ASM_DATA_EVENT_WRITE_DONE_V2: {
		pr_debug("ASM_DATA_EVENT_WRITE_DONE_V2\n");
		prtd->pcm_irq_pos += prtd->pcm_count;
		if (atomic_read(&prtd->start))
			snd_pcm_period_elapsed(substream);
		atomic_inc(&prtd->out_count);
		wake_up(&the_locks.write_wait);
		break;
	}
	case ASM_DATA_EVENT_RENDERED_EOS:
		pr_debug("ASM_DATA_EVENT_RENDERED_EOS\n");
		clear_bit(CMD_EOS, &prtd->cmd_pending);
	    
		wake_up(&the_locks.eos_wait);
		break;
	case ASM_DATA_EVENT_READ_DONE_V2: {
		pr_debug("ASM_DATA_EVENT_READ_DONE_V2\n");
		buf_index = q6asm_get_buf_index_from_token(token);
		if (buf_index >= CAPTURE_MAX_NUM_PERIODS) {
			pr_err("%s: buffer index %u is out of range.\n",
				__func__, buf_index);
			return;
		}
		pr_debug("%s: token=0x%08x buf_index=0x%08x size=%d\n",
			 __func__, token, buf_index, payload[2]);

		prtd->in_frame_info[buf_index].size = payload[2];
		
		/* assume data size = 0 during flushing */
		if (prtd->in_frame_info[buf_index].size) {
			prtd->pcm_irq_pos +=
				prtd->in_frame_info[buf_index].size;
			pr_debug("pcm_irq_pos=%d\n", prtd->pcm_irq_pos);
			if (atomic_read(&prtd->start))
				snd_pcm_period_elapsed(substream);
			if (atomic_read(&prtd->in_count) <= prtd->periods)
				atomic_inc(&prtd->in_count);
			wake_up(&the_locks.read_wait);
			if (prtd->mmap_flag &&
			    q6asm_is_cpu_buf_avail_nolock(OUT,
				prtd->audio_client,
				&size, &idx) &&
			    (substream->runtime->status->state ==
			     SNDRV_PCM_STATE_RUNNING))
				q6asm_read_nolock(prtd->audio_client);
		} else {
			pr_debug("%s: reclaim flushed buf in_count %x\n",
				__func__, atomic_read(&prtd->in_count));
			prtd->pcm_irq_pos += prtd->pcm_count;
			if (prtd->mmap_flag) {
				if (q6asm_is_cpu_buf_avail_nolock(OUT,
				    prtd->audio_client,
				    &size, &idx) &&
				    (substream->runtime->status->state ==
				    SNDRV_PCM_STATE_RUNNING))
					q6asm_read_nolock(prtd->audio_client);
			} else {
				atomic_inc(&prtd->in_count);
			}
			if (atomic_read(&prtd->in_count) == prtd->periods) {
				pr_info("%s: reclaimed all bufs\n", __func__);
				if (atomic_read(&prtd->start))
					snd_pcm_period_elapsed(substream);
				wake_up(&the_locks.read_wait);
			}
		}
		break;
	}
	case APR_BASIC_RSP_RESULT: {
		switch (payload[0]) {
		case ASM_SESSION_CMD_RUN_V2:
			pr_debug("ASM_SESSION_CMD_RUN_V2 ACK\n");
			if (substream->stream
				!= SNDRV_PCM_STREAM_PLAYBACK) {
				atomic_set(&prtd->start, 1);

				break;
			}
			if (prtd->mmap_flag) {
				pr_debug("%s:writing %d bytes of buffer to dsp\n",
					__func__,
					prtd->pcm_count);
				q6asm_write_nolock(prtd->audio_client,
					prtd->pcm_count,
					0, 0, NO_TIMESTAMP);
			} else {
				while (atomic_read(&prtd->out_needed)) {
					pr_debug("%s:writing %d bytes of buffer to dsp\n",
						__func__,
						prtd->pcm_count);
					q6asm_write_nolock(prtd->audio_client,
						prtd->pcm_count,
						0, 0, NO_TIMESTAMP);
					atomic_dec(&prtd->out_needed);
					//wake_up(&the_locks.write_wait);
				};
			}
			atomic_set(&prtd->start, 1);
			break;
		case ASM_STREAM_CMD_REGISTER_PP_EVENTS:
			pr_debug("%s: ASM_STREAM_CMD_REGISTER_PP_EVENTS:",
				__func__);
			break;
		default:
			pr_debug("%s:Payload = [0x%x]stat[0x%x]\n",
				__func__, payload[0], payload[1]);
			break;
		}
	}
	break;
	case RESET_EVENTS:
		pr_info("%s RESET_EVENTS\n", __func__);
		prtd->pcm_irq_pos += prtd->pcm_count;
		atomic_inc(&prtd->out_count);
		atomic_inc(&prtd->in_count);
		prtd->reset_event = true;
		if (atomic_read(&prtd->start))
			snd_pcm_period_elapsed(substream);
		wake_up(&the_locks.eos_wait);
		wake_up(&the_locks.write_wait);
		wake_up(&the_locks.read_wait);
		break;
	default:
		pr_debug("Not Supported Event opcode[0x%x]\n", opcode);
		break;
	}
}

static int msm_pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct snd_soc_component *component =
			snd_soc_rtdcom_lookup(soc_prtd, DRV_NAME);
	struct msm_audio *prtd = runtime->private_data;
	struct msm_plat_data *pdata;
	struct snd_pcm_hw_params *params;
	int ret;
	uint16_t bits_per_sample;

	if (!component) {
		pr_err("%s: component is NULL\n", __func__);
		return -EINVAL;
	}

	pdata = (struct msm_plat_data *)
		dev_get_drvdata(component->dev);
	if (!pdata) {
		pr_err("%s: platform data not populated\n", __func__);
		return -EINVAL;
	}
	if (!prtd || !prtd->audio_client) {
		pr_err("%s: private data null or audio client freed\n",
			__func__);
		return -EINVAL;
	}
	params = &soc_prtd->dpcm[substream->stream].hw_params;

	prtd->pcm_size = snd_pcm_lib_buffer_bytes(substream);
	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
	prtd->pcm_irq_pos = 0;
	/* rate and channels are sent to audio driver */
	prtd->samp_rate = runtime->rate;
	prtd->channel_mode = runtime->channels;
	if (prtd->enabled)
		return 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S32_LE:
		bits_per_sample = 32;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		bits_per_sample = 24;
		break;
	case SNDRV_PCM_FORMAT_S24_3LE:
		bits_per_sample = 24;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		bits_per_sample = 16;
		break;
	}

	ret = q6asm_open_write_v5(prtd->audio_client,FORMAT_LINEAR_PCM, 
	                         bits_per_sample,runtime->rate,runtime->channels,
	                         pdata->interleave);
	if (ret < 0) {
		pr_err("%s: q6asm_open_write failed (%d)\n",
		__func__, ret);
		q6asm_audio_client_free(prtd->audio_client);
		prtd->audio_client = NULL;
		return -ENOMEM;
	}
	prtd->session_id = prtd->audio_client->session;
	atomic_set(&prtd->out_count, runtime->periods);

	prtd->enabled = 1;
	prtd->cmd_pending = 0;
	prtd->cmd_interrupt = 0;

	ret = msm_pcm_routing_reg_phy_stream(soc_prtd->dai_link->id,
                                         prtd->audio_client->stream_id,
                                         prtd->session_id, substream->stream);
	if (ret < 0) {
		pr_err("%s: stream reg failed ret:%d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int msm_pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct snd_soc_component *component =
			snd_soc_rtdcom_lookup(soc_prtd, DRV_NAME);
	struct msm_plat_data *pdata;
	struct snd_pcm_hw_params *params;
	int ret = 0,i;
	uint16_t bits_per_sample = 16;

	if (!component) {
		pr_err("%s: component is NULL\n", __func__);
		return -EINVAL;
	}

	pdata = (struct msm_plat_data *)
		dev_get_drvdata(component->dev);
	if (!pdata) {
		pr_err("%s: platform data not populated\n", __func__);
		return -EINVAL;
	}
	if (!prtd || !prtd->audio_client) {
		pr_err("%s: private data null or audio client freed\n",
			__func__);
		return -EINVAL;
	}

	if (prtd->enabled == IDLE) {
		params = &soc_prtd->dpcm[substream->stream].hw_params;
		if ((params_format(params) == SNDRV_PCM_FORMAT_S24_LE) ||
			(params_format(params) == SNDRV_PCM_FORMAT_S24_3LE))
			bits_per_sample = 24;
		else if (params_format(params) == SNDRV_PCM_FORMAT_S32_LE)
			bits_per_sample = 32;

		ret = q6asm_open_read_v5(prtd->audio_client,FORMAT_LINEAR_PCM, 
								 bits_per_sample,runtime->rate,runtime->channels,
								 pdata->interleave);
		if (ret < 0) {
			pr_err("%s: q6asm_open_read failed\n", __func__);
			q6asm_audio_client_free(prtd->audio_client);
			prtd->audio_client = NULL;
			return -ENOMEM;
		}

		prtd->session_id = prtd->audio_client->session;
	}

	prtd->pcm_size = snd_pcm_lib_buffer_bytes(substream);
	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
	prtd->pcm_irq_pos = 0;
	prtd->samp_rate = runtime->rate;
	prtd->channel_mode = runtime->channels;

	ret = msm_pcm_routing_reg_phy_stream(soc_prtd->dai_link->id,
                                         prtd->audio_client->stream_id,
                                         prtd->session_id, substream->stream);
	if (ret < 0) {
		pr_err("%s: stream reg failed ret:%d\n", __func__, ret);
		return ret;
	}

	if (prtd->enabled == IDLE || prtd->enabled == STOPPED) {
		for (i = 0; i < runtime->periods; i++)
			q6asm_read(prtd->audio_client);
		prtd->periods = runtime->periods;
	}

    prtd->enabled = RUNNING;

	return ret;
}

static int msm_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		pr_debug("%s: Trigger start\n", __func__);

		ret = q6asm_run_nowait(prtd->audio_client, 0, 0, 0);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		pr_debug("SNDRV_PCM_TRIGGER_STOP\n");

		atomic_set(&prtd->start, 0);
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
			prtd->enabled = STOPPED;
			ret = q6asm_cmd_nowait(prtd->audio_client, CMD_PAUSE);
			break;
		}
		/* pending CMD_EOS isn't expected */
		WARN_ON_ONCE(test_bit(CMD_EOS, &prtd->cmd_pending));
		set_bit(CMD_EOS, &prtd->cmd_pending);
		ret = q6asm_cmd_nowait(prtd->audio_client, CMD_EOS);
		if (ret)
			clear_bit(CMD_EOS, &prtd->cmd_pending);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		pr_debug("SNDRV_PCM_TRIGGER_PAUSE\n");
		ret = q6asm_cmd_nowait(prtd->audio_client, CMD_PAUSE);
		atomic_set(&prtd->start, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int msm_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct snd_soc_component *component =
			snd_soc_rtdcom_lookup(soc_prtd, DRV_NAME);
	struct msm_audio *prtd;
	struct msm_plat_data *pdata;
	enum apr_subsys_state subsys_state;
	int session_type,stream_id;
	uint16_t fe_id = soc_prtd->dai_link->id;
	int ret = 0;

	if (!component) {
		pr_err("%s: component is NULL\n", __func__);
		return -EINVAL;
	}

	pdata = (struct msm_plat_data *)
		dev_get_drvdata(component->dev);
	if (!pdata) {
		pr_err("%s: platform data not populated\n", __func__);
		return -EINVAL;
	}

	subsys_state = apr_get_subsys_state();
	if (subsys_state == APR_SUBSYS_DOWN) {
		pr_debug("%s: adsp is down\n", __func__);
		return -ENETRESET;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		runtime->hw = msm_pcm_hardware_playback;

		if(fe_id == MSM_FRONTEND_DAI_MULTIMEDIA10) {
			session_type = ASM_SESSION_PLAYBACK_VOIP;
			audioVoipInfo.audioVoipRxSubstream = substream;
		} else {
			session_type = ASM_SESSION_PLAYBACK;
		}

		if(fe_id == MSM_FRONTEND_DAI_MULTIMEDIA9) {
			stream_id = ASM_STREAM_INCALL_MUSIC_PLAYBACK;
		} else {
			stream_id = pdata->perf_mode;
		}
	}else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE){
		runtime->hw = msm_pcm_hardware_capture;
		session_type = ASM_SESSION_RECORD;

		stream_id = ASM_STREAM_AUDIO_RECORD;
		if(msm_pcm_routing_route_is_set(MSM_BACKEND_DAI_INCALL_RECORD_RX,fe_id) &&
			msm_pcm_routing_route_is_set(MSM_BACKEND_DAI_INCALL_RECORD_TX,fe_id)){
			stream_id = ASM_STREAM_VOICE_RECORD_RX_AND_TX;
		}else if(msm_pcm_routing_route_is_set(MSM_BACKEND_DAI_INCALL_RECORD_RX,fe_id)){
			stream_id = ASM_STREAM_VOICE_RECORD_RX;
		}else if(msm_pcm_routing_route_is_set(MSM_BACKEND_DAI_INCALL_RECORD_TX,fe_id)){
			stream_id = ASM_STREAM_VOICE_RECORD_TX;
		}else if(fe_id == MSM_FRONTEND_DAI_MULTIMEDIA10){
			audioVoipInfo.audioVoipTxSubstream = substream;
			/* MULTIMEDIA10 use as audio voip */
			stream_id = ASM_STREAM_AUDIO_RECORD_EXT1;
			session_type = ASM_SESSION_RECORD_VOIP;
		}else if(fe_id == MSM_FRONTEND_DAI_MULTIMEDIA3){
			stream_id = ASM_STREAM_AUDIO_RECORD_EXT2;
		}else if(fe_id == MSM_FRONTEND_DAI_MULTIMEDIA2){
			stream_id = ASM_STREAM_FM_RECORD;
		}else if(fe_id == MSM_FRONTEND_DAI_MULTIMEDIA5){
			stream_id = ASM_STREAM_LOWLATENCY_RECORD;
		}
	} else {
		pr_err("Invalid Stream type %d\n", substream->stream);
		return -EINVAL;
	}
	pr_info("%s: fe_id %d substream->stream %d session_type %d stream_id %d\n", __func__,fe_id, substream->stream, session_type,stream_id);

	prtd = kzalloc(sizeof(struct msm_audio), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	prtd->substream = substream;
	prtd->audio_client = q6asm_audio_client_alloc(
				(app_cb)event_handler,prtd,session_type,stream_id);
	if (!prtd->audio_client) {
		pr_info("%s: Could not allocate memory\n", __func__);
		kfree(prtd);
		prtd = NULL;
		return -ENOMEM;
	}

	prtd->audio_client->dev = component->dev;
    prtd->audio_client->data_rw_logprint_count = 0;
    prtd->audio_client->data_rw_done_logprint_count = 0;

	ret = snd_pcm_hw_constraint_list(runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				&constraints_sample_rates);
	if (ret < 0)
		pr_info("snd_pcm_hw_constraint_list failed\n");
	/* Ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		pr_info("snd_pcm_hw_constraint_integer failed\n");

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_pcm_hw_constraint_minmax(runtime,
			SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
			PLAYBACK_MIN_NUM_PERIODS * PLAYBACK_MIN_PERIOD_SIZE,
			PLAYBACK_MAX_NUM_PERIODS * PLAYBACK_MAX_PERIOD_SIZE);
		if (ret < 0) {
			pr_err("constraint for buffer bytes min max ret = %d\n",
									ret);
		}
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ret = snd_pcm_hw_constraint_minmax(runtime,
			SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
			CAPTURE_MIN_NUM_PERIODS * CAPTURE_MIN_PERIOD_SIZE,
			CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE);
		if (ret < 0) {
			pr_err("constraint for buffer bytes min max ret = %d\n",
									ret);
		}
	}
	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret < 0) {
		pr_err("constraint for period bytes step ret = %d\n",
								ret);
	}
	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (ret < 0) {
		pr_err("constraint for buffer bytes step ret = %d\n",
								ret);
	}

	prtd->enabled = IDLE;
	prtd->dsp_cnt = 0;
	prtd->set_channel_map = false;
	prtd->reset_event = false;
	runtime->private_data = prtd;

	return 0;
}

static int msm_pcm_playback_copy(struct snd_pcm_substream *substream, int a,
	unsigned long hwoff, void __user *buf, unsigned long fbytes)
{
	int ret = 0;
	int xfer = 0;
	char *bufptr = NULL;
	void *data = NULL;
	uint32_t idx = 0;
	uint32_t size = 0;
	uint32_t retries = 0;

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = runtime->private_data;

#if 0
    if(prtd->audio_client->data_rw_logprint_count < DATA_RW_LOG_PRINT_THRESHOLD){
        pr_info("%s: prtd->out_count = %d\n",__func__, atomic_read(&prtd->out_count));
    } else {
	    pr_debug("%s: prtd->out_count = %d\n",__func__, atomic_read(&prtd->out_count));
    }
#endif

    pr_debug("%s: prtd->out_count = %d\n",__func__, atomic_read(&prtd->out_count));

	while ((fbytes > 0) && (retries < MAX_PB_COPY_RETRIES)) {
		if (prtd->reset_event) {
			pr_err("%s: In SSR return ENETRESET before wait\n",
				__func__);
			return -ENETRESET;
		}

		ret = wait_event_timeout(the_locks.write_wait,
				(atomic_read(&prtd->out_count)),
				msecs_to_jiffies(TIMEOUT_MS));
		if (!ret) {
			pr_info("%s: wait_event_timeout failed\n", __func__);
			ret = -ETIMEDOUT;
			goto fail;
		}
		ret = 0;

		if (prtd->reset_event) {
			pr_info("%s: In SSR return ENETRESET after wait\n",
				__func__);
			return -ENETRESET;
		}

		if (!atomic_read(&prtd->out_count)) {
			pr_info("%s: pcm stopped out_count 0\n", __func__);
			return 0;
		}

		data = q6asm_is_cpu_buf_avail(IN, prtd->audio_client, &size,
			&idx);
		if (data == NULL) {
			retries++;
			continue;
		} else {
			retries = 0;
		}

		if (fbytes > size)
			xfer = size;
		else
			xfer = fbytes;

		bufptr = data;
		if (bufptr) {
			pr_debug("%s:fbytes =%lu: xfer=%d size=%d\n",
				 __func__, fbytes, xfer, size);
			if (copy_from_user(bufptr, buf, xfer)) {
				ret = -EFAULT;
				pr_err("%s: copy_from_user failed\n",
					__func__);
				q6asm_cpu_buf_release(IN, prtd->audio_client);
				goto fail;
			}
			buf += xfer;
			fbytes -= xfer;
			pr_debug("%s:fbytes = %lu: xfer=%d\n", __func__,
				 fbytes, xfer);
			if (atomic_read(&prtd->start)) {
				pr_debug("%s:writing %d bytes of buffer to dsp\n",
						__func__, xfer);
				ret = q6asm_write(prtd->audio_client, xfer,
							0, 0, NO_TIMESTAMP);
				if (ret < 0) {
					ret = -EFAULT;
					q6asm_cpu_buf_release(IN,
						prtd->audio_client);
					goto fail;
				}
			} else
				atomic_inc(&prtd->out_needed);
			atomic_dec(&prtd->out_count);
		}
	}
fail:
	if (retries >= MAX_PB_COPY_RETRIES)
		ret = -ENOMEM;

	return  ret;
}

static int msm_pcm_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct msm_audio *prtd = runtime->private_data;
	struct snd_soc_component *component =
			snd_soc_rtdcom_lookup(soc_prtd, DRV_NAME);
	struct msm_plat_data *pdata;
	uint32_t timeout;
	int dir = 0;
	int ret = 0;
	uint16_t fe_id = soc_prtd->dai_link->id;

	pr_debug("%s: enter\n", __func__);

	if (!component) {
		pr_err("%s: component is NULL\n", __func__);
		return -EINVAL;
	}

	pdata = (struct msm_plat_data *) dev_get_drvdata(component->dev);
	if (!pdata) {
		pr_err("%s: platform data is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&pdata->lock);

	if (prtd->audio_client) {
#if 0
		/*
		 * Unvote to downgrade the Rx thread priority from
		 * RT Thread for Low-Latency use case.
		 */
		if (pdata) {
			if (pdata->perf_mode == LOW_LATENCY_PCM_MODE)
				apr_end_rx_rt(prtd->audio_client->apr);
		}

		/* determine timeout length */
		if (runtime->frame_bits == 0 || runtime->rate == 0) {
			timeout = CMD_EOS_MIN_TIMEOUT_LENGTH;
		} else {
			timeout = (runtime->period_size *
					CMD_EOS_TIMEOUT_MULTIPLIER) /
					((runtime->frame_bits / 8) *
					 runtime->rate);
			if (timeout < CMD_EOS_MIN_TIMEOUT_LENGTH)
				timeout = CMD_EOS_MIN_TIMEOUT_LENGTH;
		}
#endif
		if ((substream->stream == SNDRV_PCM_STREAM_PLAYBACK) && (fe_id == MSM_FRONTEND_DAI_MULTIMEDIA10)) {
			audioVoipInfo.audioVoipRxSubstream = NULL;
			pr_info("%s: close audio playback voip %d %d\n", __func__, substream->stream, fe_id);
		} else {
			pr_info("%s: session_type %d  stream_id %d\n", __func__, prtd->audio_client->session_type, prtd->audio_client->stream_id);
		}

		dir = IN;
		timeout = 300;
		ret = wait_event_timeout(the_locks.eos_wait,
					 !test_bit(CMD_EOS, &prtd->cmd_pending),
					 timeout);
		if (!ret)
			pr_err("%s: CMD_EOS failed, cmd_pending 0x%lx\n",
			       __func__, prtd->cmd_pending);

		msm_pcm_routing_dereg_phy_stream(soc_prtd->dai_link->id,
							SNDRV_PCM_STREAM_PLAYBACK);

		q6asm_cmd(prtd->audio_client, CMD_CLOSE);

		q6asm_audio_client_buf_free_contiguous(dir,
					prtd->audio_client);
		q6asm_audio_client_free(prtd->audio_client);
	}

	kfree(prtd);
	runtime->private_data = NULL;
	mutex_unlock(&pdata->lock);

	return 0;
}

static int msm_pcm_capture_copy(struct snd_pcm_substream *substream,
		 int channel, unsigned long hwoff, void __user *buf,
						 unsigned long fbytes)
{
	int ret = 0;
	int xfer;
	char *bufptr;
	void *data = NULL;
	uint32_t idx = 0;
	uint32_t size = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = substream->runtime->private_data;

#if 0
    if(prtd->audio_client->data_rw_logprint_count < DATA_RW_LOG_PRINT_THRESHOLD){
        pr_info("%s in_count %d\n", __func__,prtd->in_count);
    }
#endif

	pr_debug("%s\n", __func__);

	pr_debug("appl_ptr %d\n", (int)runtime->control->appl_ptr);
	pr_debug("hw_ptr %d\n", (int)runtime->status->hw_ptr);
	pr_debug("avail_min %d\n", (int)runtime->control->avail_min);

	if (prtd->reset_event) {
		pr_err("%s: In SSR return ENETRESET before wait\n", __func__);
		return -ENETRESET;
	}
	ret = wait_event_timeout(the_locks.read_wait,
			(atomic_read(&prtd->in_count)),
			msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_info("%s: wait_event_timeout failed\n", __func__);
		goto fail;
	}
	if (prtd->reset_event) {
		pr_info("%s: In SSR return ENETRESET after wait\n", __func__);
		return -ENETRESET;
	}
	if (!atomic_read(&prtd->in_count)) {
		pr_info("%s: pcm stopped in_count 0\n", __func__);
		return 0;
	}
	pr_debug("Checking if valid buffer is available...%pK\n",
						data);
	data = q6asm_is_cpu_buf_avail(OUT, prtd->audio_client, &size, &idx);
	bufptr = data;
	pr_debug("bufptr = 0x%p\n", bufptr);
	pr_debug("Size = %d\n", size);
	pr_debug("fbytes = %lu\n", fbytes);
	pr_debug("idx = %d\n", idx);
	if (bufptr) {
		xfer = fbytes;
		if (xfer > size)
			xfer = size;
		if (copy_to_user(buf, bufptr, xfer)) {
			pr_err("Failed to copy buf to user\n");
			ret = -EFAULT;
			q6asm_cpu_buf_release(OUT, prtd->audio_client);
			goto fail;
		}
		fbytes -= xfer;
		size -= xfer;
		pr_debug("%s:fbytes = %lu: size=%d: xfer=%d\n",
					__func__, fbytes, size, xfer);
		pr_debug(" Sending next buffer to dsp\n");
		memset(&prtd->in_frame_info[idx], 0,
		       sizeof(struct msm_audio_in_frame_info));
		atomic_dec(&prtd->in_count);
		ret = q6asm_read(prtd->audio_client);
		if (ret < 0) {
			pr_err("q6asm read failed\n");
			ret = -EFAULT;
			q6asm_cpu_buf_release(OUT, prtd->audio_client);
			goto fail;
		}
	} else
		pr_err("No valid buffer\n");

	pr_debug("Returning from capture_copy... %d\n", ret);
fail:
	return ret;
}

static int msm_pcm_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct msm_audio *prtd = runtime->private_data;
	struct snd_soc_component *component =
			snd_soc_rtdcom_lookup(soc_prtd, DRV_NAME);
	struct msm_plat_data *pdata;
	int dir = OUT;
	uint16_t fe_id = soc_prtd->dai_link->id;

	pr_debug("%s enter\n", __func__);

	if (!component) {
		pr_err("%s: component is NULL\n", __func__);
		return -EINVAL;
	}

	pdata = (struct msm_plat_data *) dev_get_drvdata(component->dev);
	if (!pdata) {
		pr_err("%s: platform data is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&pdata->lock);

	msm_pcm_routing_dereg_phy_stream(soc_prtd->dai_link->id,
		SNDRV_PCM_STREAM_CAPTURE);

	if (prtd->audio_client) {
		if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE) && (fe_id == MSM_FRONTEND_DAI_MULTIMEDIA10)) {
			audioVoipInfo.audioVoipTxSubstream = NULL;
			pr_info("%s: close audio capture voip %d %d\n", __func__, substream->stream, fe_id);
		} else {
			pr_info("%s: session_type %d stream_id %d\n", __func__, prtd->audio_client->session_type, prtd->audio_client->stream_id);
		}

		q6asm_cmd(prtd->audio_client, CMD_CLOSE);
		q6asm_audio_client_buf_free_contiguous(dir,
				prtd->audio_client);
		q6asm_audio_client_free(prtd->audio_client);
	}
	kfree(prtd);
	runtime->private_data = NULL;
	mutex_unlock(&pdata->lock);
	return 0;
}

static int msm_pcm_copy(struct snd_pcm_substream *substream, int a,
	 unsigned long hwoff, void __user *buf, unsigned long fbytes)
{
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = msm_pcm_playback_copy(substream, a, hwoff, buf, fbytes);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		ret = msm_pcm_capture_copy(substream, a, hwoff, buf, fbytes);
	return ret;
}

static int msm_pcm_close(struct snd_pcm_substream *substream)
{
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = msm_pcm_playback_close(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		ret = msm_pcm_capture_close(substream);
	return ret;
}

static int msm_pcm_prepare(struct snd_pcm_substream *substream)
{
	int ret = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = msm_pcm_playback_prepare(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		ret = msm_pcm_capture_prepare(substream);
	return ret;
}

static snd_pcm_uframes_t msm_pcm_pointer(struct snd_pcm_substream *substream)
{

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = runtime->private_data;

	if (prtd->pcm_irq_pos >= prtd->pcm_size)
		prtd->pcm_irq_pos = 0;

	pr_debug("%s pcm_irq_pos = %d  pcm_size:%d\n", __func__, prtd->pcm_irq_pos,prtd->pcm_size);
	return bytes_to_frames(runtime, (prtd->pcm_irq_pos));
}

static int msm_pcm_mmap(struct snd_pcm_substream *substream,
				struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = runtime->private_data;
	struct audio_client *ac = prtd->audio_client;
	struct audio_port_data *apd = ac->port;
	struct audio_buffer *ab;
	int dir = -1;
	pr_err("%s:debug periods %d\n", __func__,runtime->periods);

	prtd->mmap_flag = 1;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = IN;
	else
		dir = OUT;
	ab = &(apd[dir].buf[0]);

	return msm_audio_ion_mmap(ab, vma);
}

static int msm_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = runtime->private_data;
	struct snd_dma_buffer *dma_buf = &substream->dma_buffer;
	struct audio_buffer *buf;
	int dir, ret;

	pr_info("%s:buffer_bytes %d,periods %d,period_size %d,rate %d,channels %d\n", __func__,
	         params_buffer_bytes(params),params_periods(params),params_period_size(params),params_rate(params),params_channels(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dir = IN;
	else
		dir = OUT;
	ret = q6asm_audio_client_buf_alloc_contiguous(dir,
			prtd->audio_client,
			(params_buffer_bytes(params) / params_periods(params)),
			 params_periods(params));
	if (ret < 0) {
		pr_err("Audio Start: Buffer Allocation failed rc = %d\n",
							ret);
		return -ENOMEM;
	}
	buf = prtd->audio_client->port[dir].buf;
	if (buf == NULL || buf[0].data == NULL)
		return -ENOMEM;

	pr_debug("%s:buf = %pK\n", __func__, buf);
	dma_buf->dev.type = SNDRV_DMA_TYPE_DEV;
	dma_buf->dev.dev = substream->pcm->card->dev;
	dma_buf->private_data = NULL;
	dma_buf->area = buf[0].data;
	dma_buf->addr =  buf[0].phys;
	dma_buf->bytes = params_buffer_bytes(params);
	if (!dma_buf->area)
		return -ENOMEM;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	return 0;
}

static int msm_pcm_ioctl(struct snd_pcm_substream *substream,
			 unsigned int cmd, void __user *arg)
{
	struct msm_audio *prtd = NULL;
	struct snd_soc_pcm_runtime *rtd = NULL;
	uint64_t ses_time = 0, abs_time = 0;
	int64_t av_offset = 0;
	int32_t clock_id = -EINVAL;
	int rc = 0;
	struct snd_pcm_prsnt_position userarg;

	if (!substream || !substream->private_data) {
		pr_err("%s: Invalid %s\n", __func__,
			(!substream) ? "substream" : "private_data");
		return -EINVAL;
	}

	if (!substream->runtime) {
		pr_err("%s substream runtime not found\n", __func__);
		return -EINVAL;
	}

	prtd = substream->runtime->private_data;
	if (!prtd) {
		pr_err("%s prtd is null.\n", __func__);
		return -EINVAL;
	}

	rtd = substream->private_data;

	switch (cmd) {
	case SNDRV_PCM_IOCTL_DSP_POSITION:
		dev_dbg(rtd->dev, "%s: SNDRV_PCM_DSP_POSITION", __func__);
		if (!arg) {
			dev_err(rtd->dev, "%s: Invalid params DSP_POSITION\n",
				__func__);
			rc = -EINVAL;
			goto done;
		}
		memset(&userarg, 0, sizeof(userarg));
		if (copy_from_user(&userarg, arg, sizeof(userarg))) {
			dev_err(rtd->dev, "%s: err copyuser DSP_POSITION\n",
				__func__);
			rc = -EFAULT;
			goto done;
		}
		clock_id = userarg.clock_id;
		rc = q6asm_get_session_time_v2(prtd->audio_client, &ses_time,
					       &abs_time);
		if (rc) {
			pr_err("%s: q6asm_get_session_time_v2 failed, rc=%d\n",
				__func__, rc);
			goto done;
		}
		userarg.frames = div64_u64((ses_time * prtd->samp_rate),
					   1000000);

		rc = avcs_core_query_timer_offset(&av_offset, clock_id);
		if (rc) {
			pr_err("%s: avcs offset query failed, rc=%d\n",
				__func__, rc);
			goto done;
		}

		userarg.timestamp = abs_time + av_offset;
		if (copy_to_user(arg, &userarg, sizeof(userarg))) {
			dev_err(rtd->dev, "%s: err copy to user DSP_POSITION\n",
				__func__);
			rc = -EFAULT;
			goto done;
		}
		pr_debug("%s, vals f %lld, t %lld, avoff %lld, abst %lld, sess_time %llu sr %d\n",
			 __func__, userarg.frames, userarg.timestamp,
			 av_offset, abs_time, ses_time, prtd->samp_rate);
		break;
	default:
		rc = snd_pcm_lib_ioctl(substream, cmd, arg);
		break;
	}
done:
	return rc;
}

#if 0
#ifdef CONFIG_COMPAT
static int msm_pcm_compat_ioctl(struct snd_pcm_substream *substream,
			 unsigned int cmd, void __user *arg)
{
	return msm_pcm_ioctl(substream, cmd, arg);
}
#else
#define msm_pcm_compat_ioctl NULL
#endif
#endif 

static const struct snd_pcm_ops msm_pcm_ops = {
	.open           = msm_pcm_open,
	.copy_user	= msm_pcm_copy,
	.hw_params	= msm_pcm_hw_params,
	.close          = msm_pcm_close,
	.ioctl          = msm_pcm_ioctl,
	//.compat_ioctl   = msm_pcm_compat_ioctl,
	.prepare        = msm_pcm_prepare,
	.trigger        = msm_pcm_trigger,
	.pointer        = msm_pcm_pointer,
	.mmap		= msm_pcm_mmap,
};

static int msm_asoc_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	int ret = 0;

	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	return ret;
}


static struct snd_kcontrol_new msm_pcm_controls[] = {
	SOC_SINGLE_MULTI_EXT("Audio Voip Tx Mute", SND_SOC_NOPM, 0,
			     MAX_RAMP_DURATION,
			     0, 2, NULL, msm_audio_voip_mute_put),
	SOC_SINGLE_MULTI_EXT("Audio Voip Rx Gain", SND_SOC_NOPM, 0,
			     MAX_RAMP_DURATION,
			     0, 2, NULL, msm_audio_voip_gain_put),
};

static int msm_pcm_ctrl_probe(struct snd_soc_component *component)
{
	snd_soc_add_component_controls(component, msm_pcm_controls,
					ARRAY_SIZE(msm_pcm_controls));

	return 0;
}


#if 0
static snd_pcm_sframes_t msm_pcm_delay_blk(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_audio *prtd = runtime->private_data;
	struct audio_client *ac = prtd->audio_client;
	snd_pcm_sframes_t frames;
	int ret;

	ret = q6asm_get_path_delay(prtd->audio_client);
	if (ret) {
		pr_err("%s: get_path_delay failed, ret=%d\n", __func__, ret);
		return 0;
	}

	/* convert microseconds to frames */
	frames = ac->path_delay / 1000 * runtime->rate / 1000;

	/* also convert the remainder from the initial division */
	frames += ac->path_delay % 1000 * runtime->rate / 1000000;

	/* overcompensate for the loss of precision (empirical) */
	frames += 2;

	return frames;
}
#endif

static struct snd_soc_component_driver msm_soc_component = {
	.name		= DRV_NAME,
	.ops		= &msm_pcm_ops,
	.pcm_new	= msm_asoc_pcm_new,
	.probe		= msm_pcm_ctrl_probe,
	//.delay_blk      = msm_pcm_delay_blk,
};

static int msm_pcm_probe(struct platform_device *pdev)
{
	int rc;
	int id;
	struct msm_plat_data *pdata;
	const char *latency_level;

	audioVoipInfo.audioVoipTxSubstream = NULL;
	audioVoipInfo.audioVoipRxSubstream = NULL;

	rc = of_property_read_u32(pdev->dev.of_node,
				"jlq,msm-pcm-dsp-id", &id);
	if (rc) {
		dev_err(&pdev->dev, "%s: jlq,msm-pcm-dsp-id missing in DT node\n",
					__func__);
		return rc;
	}

	pdata = kzalloc(sizeof(struct msm_plat_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	if (of_property_read_bool(pdev->dev.of_node,
				"jlq,msm-pcm-low-latency")) {

		pdata->perf_mode = LOW_LATENCY_PCM_MODE;
		rc = of_property_read_string(pdev->dev.of_node,
			"jlq,latency-level", &latency_level);
		if (!rc) {
			if (!strcmp(latency_level, "ultra"))
				pdata->perf_mode = ULTRA_LOW_LATENCY_PCM_MODE;
			else if (!strcmp(latency_level, "ull-pp"))
				pdata->perf_mode =
					ULL_POST_PROCESSING_PCM_MODE;
		}
	} else {
		pdata->perf_mode = LEGACY_PCM_MODE;
	}
	
	pdata->interleave = INTERLEAVE_EN;
	
	mutex_init(&pdata->lock);
	dev_set_drvdata(&pdev->dev, pdata);

	dev_dbg(&pdev->dev, "%s: dev name %s\n",
				__func__, dev_name(&pdev->dev));
	return snd_soc_register_component(&pdev->dev,
					&msm_soc_component,
					NULL, 0);
}

static int msm_pcm_remove(struct platform_device *pdev)
{
	struct msm_plat_data *pdata;
	int i = 0;

	pdata = dev_get_drvdata(&pdev->dev);
	if (pdata) {
		for (i = 0; i < MSM_FRONTEND_DAI_MM_SIZE; i++) {
			kfree(pdata->chmixer_pspd[i][SESSION_TYPE_RX]);
			kfree(pdata->chmixer_pspd[i][SESSION_TYPE_TX]);
		}
	}
	mutex_destroy(&pdata->lock);
	kfree(pdata);
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}
static const struct of_device_id msm_pcm_dt_match[] = {
	{.compatible = "jlq,msm-pcm-dsp"},
	{}
};
MODULE_DEVICE_TABLE(of, msm_pcm_dt_match);

static struct platform_driver msm_pcm_driver = {
	.driver = {
		.name = "msm-pcm-dsp",
		.owner = THIS_MODULE,
		.of_match_table = msm_pcm_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = msm_pcm_probe,
	.remove = msm_pcm_remove,
};

int __init msm_pcm_dsp_init(void)
{
	pr_info("%s: enter!\n", __func__);

	init_waitqueue_head(&the_locks.enable_wait);
	init_waitqueue_head(&the_locks.eos_wait);
	init_waitqueue_head(&the_locks.write_wait);
	init_waitqueue_head(&the_locks.read_wait);


	return platform_driver_register(&msm_pcm_driver);
}

void msm_pcm_dsp_exit(void)
{
	platform_driver_unregister(&msm_pcm_driver);
}

MODULE_DESCRIPTION("PCM module platform driver");
MODULE_LICENSE("GPL v2");
