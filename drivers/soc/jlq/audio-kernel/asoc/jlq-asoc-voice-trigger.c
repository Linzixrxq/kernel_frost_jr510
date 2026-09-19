/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
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

#include <linux/of_device.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>
#include <dsp/msm_audio_ion.h>

#include "msm-pcm-q6-v2.h"
#include "msm-pcm-routing-v2.h"
#include <dsp/q6core.h>
#include "../include/uapi/sound/lsm_params.h"
#include <sound/hwdep.h>

#define TIMEOUT_MS	10000 //10 only for haps,correct to 1s on jr510 board
#define DRV_NAME "jlq-asoc-voice-trigger"
#define VT_KWS_BUF_SIZE 48*1024 /*16000*2*1*1.5s*/


enum stream_state {
	IDLE = 0,
	STOPPED,
	RUNNING,
};

enum {
	VT_MODE_ASR_START = 0,
	VT_MODE_ASR_FINISH = 1,
};

struct voice_trigger_pdata {
	int wakeup_in_dsp;
	int flag;
	int voice_recognition_i2s;
	int voice_wakeup_i2s;
	int voice_recognition_active;
	int voice_wakeup_active;
	struct audio_uevent_data *uevent_data;
	struct jlq_audio *prtd;
	struct audio_buffer *kws_buf;
	int kws_buf_size;
	struct snd_lsm_sound_model_v2 snd_model;
	void *sm_buf;
	int sm_buf_size;
	size_t dma_bytes;
	int vt_mode;
	struct mutex vt_lock;
	struct mutex vt_api_lock;
};

struct voice_trigger_pdata voice_trigger;
static struct audio_locks the_locks;

static struct snd_pcm_hardware jlq_pcm_hardware_capture = {
	.info =                 (SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats =              (SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE |
				SNDRV_PCM_FMTBIT_S32_LE),
	.rates =                SNDRV_PCM_RATE_8000_48000,
	.rate_min =             8000,
	.rate_max =             48000,
	.channels_min =         1,
	.channels_max =         8,
	.buffer_bytes_max =     CAPTURE_MAX_NUM_PERIODS *
				CAPTURE_MAX_PERIOD_SIZE,
	.period_bytes_min =	CAPTURE_MIN_PERIOD_SIZE,
	.period_bytes_max =     CAPTURE_MAX_PERIOD_SIZE,
	.periods_min =          CAPTURE_MIN_NUM_PERIODS,
	.periods_max =          CAPTURE_MAX_NUM_PERIODS,
	.fifo_size =            0,
};

/* Conventional and unconventional sample rate supported */
static unsigned int supported_sample_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};

static struct snd_pcm_hw_constraint_list constraints_sample_rates = {
	.count = ARRAY_SIZE(supported_sample_rates),
	.list = supported_sample_rates,
	.mask = 0,
};

static int jlq_vt_mode_control_get(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = voice_trigger.vt_mode;
	pr_debug("%s: vt mode value: %ld\n", __func__,
				ucontrol->value.integer.value[0]);
	return 0;
}

static int jlq_vt_mode_control_put(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
    int ret = 0,i;

    pr_debug("%s vt mode value:%ld\n", __func__,
    ucontrol->value.integer.value[0]);

    mutex_lock(&voice_trigger.vt_lock);
    voice_trigger.vt_mode = ucontrol->value.integer.value[0];
    if(voice_trigger.vt_mode == VT_MODE_ASR_START){
        q6asm_vt_set_mode(voice_trigger.prtd->audio_client,VT_MODE_ASR_START);

        voice_trigger.prtd->pcm_irq_pos = 0;
        for (i = 0; i < voice_trigger.prtd->periods; i++)
            q6asm_vt_read_nolock(voice_trigger.prtd->audio_client);
    } else if(voice_trigger.vt_mode == VT_MODE_ASR_FINISH){
        q6asm_vt_set_mode(voice_trigger.prtd->audio_client,VT_MODE_ASR_FINISH);
    } else {
        pr_err("%s error vt mode value:%d \n", __func__,voice_trigger.vt_mode);
    }
    mutex_unlock(&voice_trigger.vt_lock);

    return ret;
}


static const struct snd_kcontrol_new vt_mode_control[] = {
	SOC_SINGLE_EXT("vt mode", SND_SOC_NOPM, 0,
	2, 0, jlq_vt_mode_control_get,
	jlq_vt_mode_control_put),
};

void do_vt_event_work(struct work_struct *work)
{
	char event[25] = "";

	/*devpath:/kernel/q6audio/voice_trigger_uevent*/
	//pr_err("devpath:%s \n",kobject_get_path(&voice_trigger.uevent_data->kobj, GFP_KERNEL));
	snprintf(event, sizeof(event), "VOICE_WAKEUP_EVENT");
	q6core_send_uevent(voice_trigger.uevent_data, event);
}

static void event_handler(uint32_t opcode,
		uint32_t token, uint32_t *payload, void *priv)
{
	struct jlq_audio *prtd = priv;
	struct snd_pcm_substream *substream = prtd->substream;
	int buf_index;

	switch (opcode) {
	case ASM_VT_READ_DONE: {
		pr_debug("ASM_VT_READ_DONE: token = 0x%08x, size = %d.\n", token, payload[2]);
		buf_index = q6asm_get_buf_index_from_token(token);
		if (buf_index >= CAPTURE_MAX_NUM_PERIODS) {
			pr_err("%s: buffer index %u is out of range.\n",
				__func__, buf_index);
			return;
		}

		prtd->in_frame_info[buf_index].size = payload[2];
		/* assume data size = 0 during flushing */
		if (prtd->in_frame_info[buf_index].size) {
			prtd->pcm_irq_pos += prtd->in_frame_info[buf_index].size;
			pr_debug("pcm_irq_pos=%d\n", prtd->pcm_irq_pos);
			if (atomic_read(&prtd->start))
				snd_pcm_period_elapsed(substream);
			if (atomic_read(&prtd->in_count) <= prtd->periods)
				atomic_inc(&prtd->in_count);
			wake_up(&the_locks.read_wait);
		} else {
			pr_debug("%s: reclaim flushed buf in_count %x\n",
				__func__, atomic_read(&prtd->in_count));
			prtd->pcm_irq_pos += prtd->pcm_count;
			atomic_inc(&prtd->in_count);
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
		case ASM_VT_OPEN:
			pr_info("ASM_VT_OPEN ACK\n");
			atomic_set(&prtd->start, 1);
			break;
		default:
			pr_debug("%s:Payload = [0x%x]stat[0x%x]\n",
				__func__, payload[0], payload[1]);
			break;
		}
		break;
	}
	case ASM_VT_EVENT: {
		pr_info("%s: ASM_VT_EVENT\n",__func__);
		//flush_work(&prtd->vt_event_work);
		//schedule_work(&prtd->vt_event_work);
		pr_info("%s: send vt uevent\n",__func__);
		q6core_send_uevent(voice_trigger.uevent_data, "VOICE_WAKEUP_EVENT");
		break;
	}
	default:
		pr_err("Not Supported Event opcode[0x%x]\n", opcode);
		break;
	}
}

static int jlq_vt_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jlq_audio *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct snd_pcm_hw_params *params;
	int ret = 0;
	uint16_t bits_per_sample = 16;

	if (!prtd || !prtd->audio_client) {
		pr_err("%s: private data null or audio client freed\n",
			__func__);
		return -EINVAL;
	}

	if (prtd->enabled == IDLE) {
		pr_debug("%s:periods=%d\n", __func__, runtime->periods);
		params = &soc_prtd->dpcm[substream->stream].hw_params;
		if ((params_format(params) == SNDRV_PCM_FORMAT_S24_LE) ||
			(params_format(params) == SNDRV_PCM_FORMAT_S24_3LE))
			bits_per_sample = 24;
		else if (params_format(params) == SNDRV_PCM_FORMAT_S32_LE)
			bits_per_sample = 32;

		pr_info("%s Opening %d-ch PCM read stream, stream id %d\n",
				__func__, params_channels(params),
				prtd->audio_client->stream_id);

		ret = q6asm_vt_open(prtd->audio_client,
			                bits_per_sample,runtime->rate,runtime->channels,
			                lower_32_bits((uint64_t)voice_trigger.kws_buf[0].phys),
			                voice_trigger.kws_buf_size);
		if (ret < 0) {
			pr_err("%s: q6asm_vt_open failed\n", __func__);
			q6asm_audio_client_buf_free_contiguous(OUT,
				prtd->audio_client);
			msm_audio_ion_free(voice_trigger.kws_buf[0].dma_buf);
			kfree(voice_trigger.kws_buf);
			voice_trigger.kws_buf = NULL;
			q6asm_audio_client_free(prtd->audio_client);
			prtd->audio_client = NULL;
			return -ENOMEM;
		}

		pr_debug("%s: session ID %d\n",
				__func__, prtd->audio_client->session);
		prtd->session_id = prtd->audio_client->session;
	}

	prtd->pcm_size = snd_pcm_lib_buffer_bytes(substream);
	prtd->pcm_count = snd_pcm_lib_period_bytes(substream);
	prtd->pcm_irq_pos = 0;
	prtd->samp_rate = runtime->rate;
	prtd->channel_mode = runtime->channels;

	ret = msm_pcm_routing_reg_phy_stream(soc_prtd->dai_link->id,
                                         prtd->audio_client->stream_id,
                                         prtd->session_id, SNDRV_PCM_STREAM_CAPTURE);

#if 0
	if (prtd->enabled == IDLE || prtd->enabled == STOPPED) {
		for (i = 0; i < runtime->periods; i++)
			q6asm_vt_read(prtd->audio_client);
		prtd->periods = runtime->periods;
	}

	if (prtd->enabled != IDLE)
		return 0;

	prtd->enabled = RUNNING;
#else
	prtd->periods = runtime->periods;
	prtd->enabled = RUNNING;
#endif
	return ret;
}

static int jlq_vt_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct snd_soc_component *component =
			snd_soc_rtdcom_lookup(soc_prtd, DRV_NAME);
	struct jlq_audio *prtd;
	int ret = 0;

	pr_info("%s:\n", __func__);

	prtd = kzalloc(sizeof(struct jlq_audio), GFP_KERNEL);
	if (prtd == NULL) {
		return -ENOMEM;
	}
	prtd->substream = substream;
	prtd->audio_client = q6asm_audio_client_alloc(
				(app_cb)event_handler,prtd,ASM_SESSION_VT,0);
	if (!prtd->audio_client) {
		pr_err("%s: Could not allocate memory\n", __func__);
		kfree(prtd);
		return -ENOMEM;
	}

	prtd->audio_client->dev = component->dev;

	runtime->hw = jlq_pcm_hardware_capture;

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

	ret = snd_pcm_hw_constraint_minmax(runtime,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
		CAPTURE_MIN_NUM_PERIODS * CAPTURE_MIN_PERIOD_SIZE,
		CAPTURE_MAX_NUM_PERIODS * CAPTURE_MAX_PERIOD_SIZE);
	if (ret < 0) {
		pr_err("constraint for buffer bytes min max ret = %d\n",
								ret);
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
	voice_trigger.prtd = prtd;
	mutex_init(&voice_trigger.vt_lock);
	mutex_init(&voice_trigger.vt_api_lock);

	INIT_WORK(&prtd->vt_event_work,do_vt_event_work);

	return 0;
}

static int jlq_vt_copy(struct snd_pcm_substream *substream,
		 int channel, snd_pcm_uframes_t hwoff, void __user *buf,
						 snd_pcm_uframes_t frames)
{
	int ret = 0;
	int fbytes = 0;
	int xfer;
	char *bufptr;
	void *data = NULL;
	static uint32_t idx;
	static uint32_t size;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jlq_audio *prtd = substream->runtime->private_data;

	pr_debug("%s\n", __func__);
	//fbytes = frames_to_bytes(runtime, frames);
	fbytes = (int)frames;

	pr_debug("appl_ptr %d\n", (int)runtime->control->appl_ptr);
	pr_debug("hw_ptr %d\n", (int)runtime->status->hw_ptr);
	pr_debug("avail_min %d\n", (int)runtime->control->avail_min);

	if (prtd->reset_event) {
		pr_err("%s: In SSR return ENETRESET before wait\n", __func__);
		return -ENETRESET;
	}

	ret = wait_event_timeout(the_locks.read_wait,
			(atomic_read(&prtd->in_count)), msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		pr_debug("%s: wait_event_timeout failed\n", __func__);
		goto fail;
	}
	if (prtd->reset_event) {
		pr_err("%s: In SSR return ENETRESET after wait\n", __func__);
		return -ENETRESET;
	}
	if (!atomic_read(&prtd->in_count)) {
		pr_debug("%s: pcm stopped in_count 0\n", __func__);
		return 0;
	}
	pr_debug("Checking if valid buffer is available...%pK\n",
						data);
	data = q6asm_is_cpu_buf_avail(OUT, prtd->audio_client, &size, &idx);
	bufptr = data;
	pr_debug("Size = %d\n", size);
	pr_debug("fbytes = %d\n", fbytes);
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
		pr_debug("%s:fbytes = %d: size=%d: xfer=%d\n",
					__func__, fbytes, size, xfer);
		pr_debug(" Sending next buffer to dsp\n");
		memset(&prtd->in_frame_info[idx], 0,
		       sizeof(struct jlq_audio_in_frame_info));
		atomic_dec(&prtd->in_count);
		ret = q6asm_vt_read(prtd->audio_client);
		if (ret < 0) {
			pr_err("asm read failed\n");
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

static int jlq_vt_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jlq_audio *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	int dir = OUT;
	int ret = 0;

	pr_info("%s\n", __func__);

	msm_pcm_routing_dereg_phy_stream(soc_prtd->dai_link->id,
                                     SNDRV_PCM_STREAM_CAPTURE);

	if (prtd->audio_client) {

		q6asm_vt_close(prtd->audio_client);

		q6asm_audio_client_buf_free_contiguous(dir,
				prtd->audio_client);
		q6asm_audio_client_free(prtd->audio_client);
		if(voice_trigger.kws_buf && voice_trigger.kws_buf[0].dma_buf)
			msm_audio_ion_free(voice_trigger.kws_buf[0].dma_buf);
	}

	mutex_destroy(&voice_trigger.vt_lock);
	mutex_destroy(&voice_trigger.vt_api_lock);

	if(voice_trigger.kws_buf){
		kfree(voice_trigger.kws_buf);
		voice_trigger.kws_buf = NULL;
	}
	kfree(prtd);
	runtime->private_data = NULL;

	return ret;
}

static snd_pcm_uframes_t jlq_vt_pointer(struct snd_pcm_substream *substream)
{

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jlq_audio *prtd = runtime->private_data;

	if (prtd->pcm_irq_pos >= prtd->pcm_size)
		prtd->pcm_irq_pos = 0;

	pr_debug("%s pcm_irq_pos = %d  pcm_size:%d\n", __func__, prtd->pcm_irq_pos,prtd->pcm_size);
	return bytes_to_frames(runtime, (prtd->pcm_irq_pos));
}

static int jlq_vt_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jlq_audio *prtd = runtime->private_data;
	struct snd_dma_buffer *dma_buf = &substream->dma_buffer;
	struct audio_buffer *buf;
	int dir = OUT, ret;
	struct audio_buffer *kws_buf = NULL;
	int bytes_to_alloc = VT_KWS_BUF_SIZE;
	size_t len;
	int i;
	uint8_t *tbuf;

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


	voice_trigger.kws_buf = kzalloc(sizeof(struct audio_buffer),GFP_KERNEL);
	kws_buf = voice_trigger.kws_buf;
	if (!kws_buf) {
		pr_err("%s: kws buffer allocation failed\n", __func__);
		return -ENOMEM;
	}

	bytes_to_alloc = PAGE_ALIGN(bytes_to_alloc);
#ifdef AUDIO_TEST
	len = 0;
	kws_buf[0].data = dma_alloc_coherent(
		prtd->audio_client->dev, bytes_to_alloc, &kws_buf[0].phys, GFP_KERNEL);
#else
	ret = msm_audio_ion_alloc(&kws_buf[0].dma_buf,
							 bytes_to_alloc,
							 &kws_buf[0].phys, &len,
							 &kws_buf[0].data);
#endif
	if (ret) {
		pr_err("%s: Audio ION alloc kws buffer is failed, ret = %d\n",
			__func__, ret);
		return -ENOMEM;
	}

#if 1
	tbuf = (uint8_t *)voice_trigger.kws_buf[0].data;
	for(i=0; i<bytes_to_alloc; i++){
		tbuf[i] = i%255;
	}
#endif

	pr_info("%s: data[%p]phys[0x%x]  bytes_to_alloc %d\n",__func__,
			kws_buf[0].data,(uint32_t)kws_buf[0].phys,bytes_to_alloc);

	voice_trigger.kws_buf_size = bytes_to_alloc;
	voice_trigger.dma_bytes = params_buffer_bytes(params);
	voice_trigger.vt_mode = VT_MODE_ASR_FINISH;

	return 0;
}

static int jlq_vt_pcm_mmap(struct snd_pcm_substream *substream,
				struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;
	int i;
	uint8_t *buf;

	ret = msm_audio_ion_mmap(voice_trigger.kws_buf, vma);
	runtime->dma_bytes = voice_trigger.dma_bytes;
	runtime->access = SNDRV_PCM_ACCESS_RW_INTERLEAVED;
	pr_info("%s: vt kws buffer data[%p]phys[0x%x]size[%d]  dma_bytes %d\n",__func__,
			voice_trigger.kws_buf[0].data,
			(uint32_t)voice_trigger.kws_buf[0].phys,
			voice_trigger.kws_buf[0].size,
			(uint32_t)runtime->dma_bytes);

#if 1
	buf = (uint8_t *)voice_trigger.kws_buf[0].data;
	for(i=0; i<48000; i++){
		buf[i] = i%255;
    }
#endif

	return ret;
}

static int kws_data_read(struct snd_pcm_substream *substream,
				      unsigned long arg)
{
	int ret,copy_size;
	struct snd_kws_data kws_data;
	struct snd_kws_data __user *p_user_kws_data = (struct snd_kws_data __user *)arg;

	if (copy_from_user(&kws_data, p_user_kws_data, sizeof(struct snd_kws_data))) {
		pr_err("%s: copy sound model data from user failed\n",__func__);
	}

	pr_info("%s: buf %p size %d \n",__func__,kws_data.buf,kws_data.size);

	if(kws_data.size < voice_trigger.kws_buf_size){
		pr_err("%s: user kws buffer is too small,user buffer size is %d, dsp buffer size is %d\n",
			   __func__,kws_data.size,voice_trigger.kws_buf_size);
		return -1;
	}

	if(kws_data.size >= voice_trigger.kws_buf_size)
		copy_size = voice_trigger.kws_buf_size;
	else
		copy_size = kws_data.size;

	if (copy_to_user(kws_data.buf, voice_trigger.kws_buf[0].data,copy_size)) {
		pr_err("%s: Failed to copy buf to user\n",__func__);
		ret = -1;
	}else{
		pr_info("%s: copy kws data[%d] to user\n",__func__,voice_trigger.kws_buf_size);
		ret = voice_trigger.kws_buf_size;
	}
	pr_info("%s: exit ret %d\n",__func__,ret);
	return ret;

}

static int jlq_vt_ioctl(struct snd_pcm_substream *substream,
			 unsigned int cmd, unsigned long arg)
{
	int err = 0;
	int i;
	struct snd_pcm_runtime *runtime;
	struct snd_soc_pcm_runtime *rtd;
	struct jlq_audio *prtd;
	struct snd_lsm_sound_model_v2 __user *p_model;
	char str_data[50];

	if (!substream || !substream->private_data) {
		pr_err("%s: Invalid %s\n", __func__,
			(!substream) ? "substream" : "private_data");
		return -EINVAL;
	}
	runtime = substream->runtime;
	prtd = runtime->private_data;
	rtd = substream->private_data;

	mutex_lock(&voice_trigger.vt_api_lock);
	switch (cmd) {
	case SNDRV_LSM_REG_SND_MODEL: {
		dev_dbg(rtd->dev, "%s: register sound model\n",__func__);

		p_model = (struct snd_lsm_sound_model_v2 __user *)arg;
		if (copy_from_user(&voice_trigger.snd_model, p_model, sizeof(struct snd_lsm_sound_model_v2))) {
			err = -EFAULT;
			dev_err(rtd->dev,
				"%s: copy sound model data from user failed\n",
				__func__);
		}
		dev_dbg(rtd->dev, "%s: sound model data size %d\n",__func__,voice_trigger.snd_model.data_size);

		voice_trigger.sm_buf_size = voice_trigger.snd_model.data_size;
		if (voice_trigger.sm_buf_size <= 0) {
			pr_err("%s: sound model buffer size \n", __func__);
			return -ENOMEM;
		}
		voice_trigger.sm_buf = kzalloc(voice_trigger.sm_buf_size,GFP_KERNEL);
		if (!voice_trigger.sm_buf) {
			pr_err("%s: sound model buffer allocation failed\n", __func__);
			return -ENOMEM;
		}

		if (copy_from_user(voice_trigger.sm_buf, voice_trigger.snd_model.data, voice_trigger.sm_buf_size)) {
			dev_err(rtd->dev,
				"%s: copy sound model data from user failed, size %d\n",
				__func__,
				voice_trigger.sm_buf_size);
			return -ENOMEM;
		}

#if 1
		for(i=0; i<16; i++){
			sprintf(str_data+i*3,"%02X ",*((char *)voice_trigger.sm_buf+i));
		}
		str_data[3*16] = 0;
		printk("%s: sound model data %s \n",__func__,str_data);
#endif
		break;
	}
	case SNDRV_LSM_SET_MODULE_PARAMS:{
		dev_dbg(rtd->dev, "%s: set module parameters\n",__func__);
		break;
	}
	case SNDRV_LSM_DEREG_SND_MODEL:{
		dev_dbg(rtd->dev, "%s: deregister sound model\n",__func__);

		if(voice_trigger.sm_buf){
			kfree(voice_trigger.sm_buf);
			voice_trigger.sm_buf = NULL;
			voice_trigger.sm_buf_size = 0;
		}
		break;
	}
	case SNDRV_LSM_VT_START:{
		dev_dbg(rtd->dev, "%s: Starting LSM client session\n",__func__);
		q6asm_vt_run(prtd->audio_client);
		break;
	}
	case SNDRV_LSM_VT_STOP: {
		dev_dbg(rtd->dev,"%s: Stopping LSM client session\n",__func__);
		q6asm_vt_pause(prtd->audio_client);
		break;
	}
	case SNDRV_LSM_ASR_START:{
		dev_dbg(rtd->dev, "%s: Starting ASR\n",__func__);
		q6asm_vt_set_mode(voice_trigger.prtd->audio_client,VT_MODE_ASR_START);

		voice_trigger.prtd->pcm_irq_pos = 0;
		for (i = 0; i < voice_trigger.prtd->periods; i++)
			q6asm_vt_read_nolock(voice_trigger.prtd->audio_client);

		break;
	}
	case SNDRV_LSM_ASR_STOP: {
		dev_dbg(rtd->dev,"%s: Stopping ASR\n",__func__);
        q6asm_vt_set_mode(voice_trigger.prtd->audio_client,VT_MODE_ASR_FINISH);
		break;
	}
	case SNDRV_LSM_READ_KWS_DATA: {
		dev_dbg(rtd->dev,"%s: read kws data\n",__func__);
		kws_data_read(substream,arg);
		break;
	}
	default:
		dev_err(rtd->dev,"%s: not supported cmd 0x%X\n",__func__,cmd);
		break;
	}

	mutex_unlock(&voice_trigger.vt_api_lock);
	return err;
}

static int jlq_pcm_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct snd_pcm *pcm = hw->private_data;
	struct snd_pcm_substream *substream = pcm->streams[OUT].substream;

	pr_err("%s cmd 0x%X\n", __func__,cmd);

	if (!substream) {
		pr_err("%s substream not found\n", __func__);
		ret = -ENODEV;
	}

	jlq_vt_ioctl(substream,cmd,arg);

	return ret;
}

static int jlq_pcm_add_hwdep_dev(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_hwdep *hwdep;
	int rc;
	char id[20] = "VT_HWDEP";

	snprintf(id, sizeof(id), "VT_HWDEP_%d", runtime->pcm->device);
	pr_debug("%s: pcm dev %d\n", __func__, runtime->pcm->device);
	rc = snd_hwdep_new(runtime->card->snd_card,
			   &id[0],
			   HWDEP_VT_FE_BASE + runtime->pcm->device,
			   &hwdep);
	if (!hwdep || rc < 0) {
		pr_err("%s: hwdep intf failed to create %s - hwdep\n", __func__,
		       id);
		return rc;
	}

	pr_debug("%s: create VT hwdep intf success\n", __func__);

	//hwdep->iface = SNDRV_HWDEP_IFACE_AUDIO_BE; /* for lack of a FE iface */
	hwdep->private_data = runtime->pcm; /* of type struct snd_pcm */
	hwdep->ops.ioctl = jlq_pcm_hwdep_ioctl;
	return 0;
}

static int jlq_vt_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	int ret;

	ret = jlq_pcm_add_hwdep_dev(rtd);
	if (ret)
		pr_err("%s: Could not add hw dep node\n", __func__);
	return ret;
}


static void jlq_vt_release_uevent_data(struct kobject *kobj)
{
	struct audio_uevent_data *data = container_of(kobj,
						      struct audio_uevent_data,
						      kobj);
	kfree(data);
}

static int jlq_vt_component_probe(struct snd_soc_component *component)
{
	snd_soc_add_component_controls(component,
				vt_mode_control,
				ARRAY_SIZE(vt_mode_control));

	return 0;
}


static struct snd_pcm_ops jlq_vt_ops = {
	.open           = jlq_vt_open,
	.copy_user		= jlq_vt_copy,
	.hw_params	    = jlq_vt_hw_params,
	.close          = jlq_vt_close,
	.prepare        = jlq_vt_prepare,
	.pointer        = jlq_vt_pointer,
	.mmap		    = jlq_vt_pcm_mmap,
};

static struct snd_soc_component_driver jlq_vt_component = {
	.name		= DRV_NAME,
	.ops		= &jlq_vt_ops,
	.probe		= jlq_vt_component_probe,
	.pcm_new	= jlq_vt_pcm_new,
};

static int jlq_vt_probe(struct platform_device *pdev)
{
	int rc;

	voice_trigger.uevent_data = kzalloc(sizeof(*(voice_trigger.uevent_data)), GFP_KERNEL);
	if (!voice_trigger.uevent_data)
		return -ENOMEM;

	voice_trigger.uevent_data->ktype.release = jlq_vt_release_uevent_data;
	q6core_init_uevent_data(voice_trigger.uevent_data, "voice_trigger_uevent");

	rc = of_property_read_u32(pdev->dev.of_node,
				"wakeup_in_dsp", &voice_trigger.wakeup_in_dsp);
	if (rc) {
		dev_err(&pdev->dev, "%s: wakeup_in_dsp missing in DT node\n",
					__func__);
		return rc;
	}

	rc = of_property_read_u32(pdev->dev.of_node,
				"wakeup_i2s", &voice_trigger.voice_wakeup_i2s);
	if (rc) {
		dev_err(&pdev->dev, "%s: wakeup_i2s missing in DT node\n",
					__func__);
		return rc;
	}

	rc = of_property_read_u32(pdev->dev.of_node,
				"recognition_i2s", &voice_trigger.voice_recognition_i2s);
	if (rc) {
		dev_err(&pdev->dev, "%s: recognition_i2s missing in DT node\n",
					__func__);
		return rc;
	}
	voice_trigger.voice_wakeup_active = 0;

	dev_dbg(&pdev->dev, "%s: dev name %s\n",
				__func__, dev_name(&pdev->dev));
	return snd_soc_register_component(&pdev->dev,
					&jlq_vt_component,
					NULL, 0);
}

static int jlq_vt_remove(struct platform_device *pdev)
{
	q6core_destroy_uevent_data(voice_trigger.uevent_data);
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}
static const struct of_device_id jlq_vt_dt_match[] = {
	{.compatible = "jlq,jlq-voice-trigger"},
	{}
};
MODULE_DEVICE_TABLE(of, jlq_vt_dt_match);

static struct platform_driver jlq_vt_driver = {
	.driver = {
		.name = "jlq-voice-trigger",
		.owner = THIS_MODULE,
		.of_match_table = jlq_vt_dt_match,
	},
	.probe = jlq_vt_probe,
	.remove = jlq_vt_remove,
};

static int __init jlq_soc_platform_init(void)
{
	init_waitqueue_head(&the_locks.enable_wait);
	init_waitqueue_head(&the_locks.read_wait);

	return platform_driver_register(&jlq_vt_driver);
}
module_init(jlq_soc_platform_init);

static void __exit jlq_soc_platform_exit(void)
{
	platform_driver_unregister(&jlq_vt_driver);
}
module_exit(jlq_soc_platform_exit);

MODULE_DESCRIPTION("VT module platform driver");
MODULE_LICENSE("GPL v2");
