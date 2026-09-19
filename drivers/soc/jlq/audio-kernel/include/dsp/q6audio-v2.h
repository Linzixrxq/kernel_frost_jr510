/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2013, 2015, The Linux Foundation. All rights reserved.
 */

#ifndef _Q6_AUDIO_H_
#define _Q6_AUDIO_H_

#include <ipc/apr.h>

#define DATA_RW_LOG_PRINT_THRESHOLD 3

struct msm_fm_close_info *get_fm_info(void);

enum {
	LEGACY_PCM_MODE = 0,
	LOW_LATENCY_PCM_MODE,
	ULTRA_LOW_LATENCY_PCM_MODE,
	ULL_POST_PROCESSING_PCM_MODE,
};

enum {
	ASM_SESSION_PLAYBACK = 1,
	ASM_SESSION_RECORD,
	ASM_SESSION_VT,
	ASM_SESSION_PLAYBACK_VOIP,
	ASM_SESSION_RECORD_VOIP,
	ASM_SESSION_MAX,
};

enum {
	ASM_STREAM_DEEPBUFFER_PLAYBACK = 0,
	ASM_STREAM_LOWLATENCY_PLAYBACK = 1,
	ASM_STREAM_INCALL_MUSIC_PLAYBACK = 2,
};

enum {
	ASM_STREAM_AUDIO_RECORD = 1,
	ASM_STREAM_VOICE_RECORD_TX = 2,
	ASM_STREAM_VOICE_RECORD_RX = 3,
	ASM_STREAM_VOICE_RECORD_RX_AND_TX = 4,
	ASM_STREAM_AUDIO_RECORD_EXT1 = 5,
	ASM_STREAM_AUDIO_RECORD_EXT2 = 6,
	ASM_STREAM_FM_RECORD = 7,
	ASM_STREAM_LOWLATENCY_RECORD = ASM_STREAM_AUDIO_RECORD_EXT2,
};

enum {
	INTERLEAVE_DIS                                   = 0x0,
	INTERLEAVE_EN                                    = 0x1,
};

int q6audio_get_port_index(u16 port_id);

int q6audio_convert_virtual_to_portid(u16 port_id);

int q6audio_validate_port(u16 port_id);

int q6audio_is_digital_pcm_interface(u16 port_id);

int q6audio_get_port_id(u16 port_id);

uint8_t jlqaudio_get_dsp_fe_id(u16 port_id,int session_type);

uint8_t jlqaudio_get_dsp_be_id(u16 port_id);

int jlqaudio_set_headphone_type(int type);

#endif
