// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <dsp/apr_audio-v2.h>
#include <dsp/q6afe-v2.h>
#include <dsp/sp_params.h>
#include <dsp/q6core.h>
#include "msm-dai-q6-v2.h"
#include <asoc/core.h>
#include "jlq_dai_slim.h"

#define MSM_DAI_PRI_AUXPCM_DT_DEV_ID 1
#define MSM_DAI_SEC_AUXPCM_DT_DEV_ID 2
#define MSM_DAI_TERT_AUXPCM_DT_DEV_ID 3
#define MSM_DAI_QUAT_AUXPCM_DT_DEV_ID 4
#define MSM_DAI_QUIN_AUXPCM_DT_DEV_ID 5
#define MSM_DAI_SEN_AUXPCM_DT_DEV_ID 6

#define MSM_DAI_TWS_CHANNEL_MODE_ONE 1
#define MSM_DAI_TWS_CHANNEL_MODE_TWO 2

#define spdif_clock_value(rate) (2*rate*32*2)
#define CHANNEL_STATUS_SIZE 24
#define CHANNEL_STATUS_MASK_INIT 0x0
#define CHANNEL_STATUS_MASK 0x4
#define PREEMPH_MASK 0x38
#define PREEMPH_SHIFT 3
#define GET_PREEMPH(b) ((b & PREEMPH_MASK) >> PREEMPH_SHIFT)
#define AFE_API_VERSION_CLOCK_SET 1
#define MSM_DAI_SYSFS_ENTRY_MAX_LEN 64

#define DAI_FORMATS_S16_S24_S32_LE (SNDRV_PCM_FMTBIT_S16_LE | \
				    SNDRV_PCM_FMTBIT_S24_LE | \
				    SNDRV_PCM_FMTBIT_S32_LE)

static int msm_mi2s_get_port_id(u32 mi2s_id, int stream, u16 *port_id);

enum {
	ENC_FMT_NONE,
	DEC_FMT_NONE = ENC_FMT_NONE,
	ENC_FMT_SBC = ASM_MEDIA_FMT_SBC,
	DEC_FMT_SBC = ASM_MEDIA_FMT_SBC,
	ENC_FMT_AAC_V2 = ASM_MEDIA_FMT_AAC_V2,
	DEC_FMT_AAC_V2 = ASM_MEDIA_FMT_AAC_V2,
	ENC_FMT_APTX = ASM_MEDIA_FMT_APTX,
	ENC_FMT_APTX_HD = ASM_MEDIA_FMT_APTX_HD,
	ENC_FMT_CELT = ASM_MEDIA_FMT_CELT,
	ENC_FMT_LDAC = ASM_MEDIA_FMT_LDAC,
	ENC_FMT_APTX_ADAPTIVE = ASM_MEDIA_FMT_APTX_ADAPTIVE,
	DEC_FMT_APTX_ADAPTIVE = ASM_MEDIA_FMT_APTX_ADAPTIVE,
	DEC_FMT_MP3 = ASM_MEDIA_FMT_MP3,
	ENC_FMT_APTX_AD_SPEECH = ASM_MEDIA_FMT_APTX_AD_SPEECH,
	DEC_FMT_APTX_AD_SPEECH = ASM_MEDIA_FMT_APTX_AD_SPEECH,
};

enum {
	SPKR_1,
	SPKR_2,
};

static const struct afe_clk_set lpass_clk_set_default = {
	AFE_API_VERSION_CLOCK_SET,
	Q6AFE_LPASS_CLK_ID_PRI_PCM_IBIT,
	Q6AFE_LPASS_OSR_CLK_2_P048_MHZ,
	Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	0,
};

static const struct afe_clk_cfg lpass_clk_cfg_default = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_OSR_CLK_2_P048_MHZ,
	0,
	Q6AFE_LPASS_CLK_SRC_INTERNAL,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	Q6AFE_LPASS_MODE_CLK1_VALID,
	0,
};
enum {
	STATUS_PORT_STARTED, /* track if AFE port has started */
	/* track AFE Tx port status for bi-directional transfers */
	STATUS_TX_PORT,
	/* track AFE Rx port status for bi-directional transfers */
	STATUS_RX_PORT,
	STATUS_MAX
};

enum {
	RATE_8KHZ,
	RATE_16KHZ,
	RATE_MAX_NUM_OF_AUX_PCM_RATES,
};

enum {
	IDX_PRIMARY_TDM_RX_0,
	IDX_PRIMARY_TDM_RX_1,
	IDX_PRIMARY_TDM_RX_2,
	IDX_PRIMARY_TDM_RX_3,
	IDX_PRIMARY_TDM_RX_4,
	IDX_PRIMARY_TDM_RX_5,
	IDX_PRIMARY_TDM_RX_6,
	IDX_PRIMARY_TDM_RX_7,
	IDX_PRIMARY_TDM_TX_0,
	IDX_PRIMARY_TDM_TX_1,
	IDX_PRIMARY_TDM_TX_2,
	IDX_PRIMARY_TDM_TX_3,
	IDX_PRIMARY_TDM_TX_4,
	IDX_PRIMARY_TDM_TX_5,
	IDX_PRIMARY_TDM_TX_6,
	IDX_PRIMARY_TDM_TX_7,
	IDX_SECONDARY_TDM_RX_0,
	IDX_SECONDARY_TDM_RX_1,
	IDX_SECONDARY_TDM_RX_2,
	IDX_SECONDARY_TDM_RX_3,
	IDX_SECONDARY_TDM_RX_4,
	IDX_SECONDARY_TDM_RX_5,
	IDX_SECONDARY_TDM_RX_6,
	IDX_SECONDARY_TDM_RX_7,
	IDX_SECONDARY_TDM_TX_0,
	IDX_SECONDARY_TDM_TX_1,
	IDX_SECONDARY_TDM_TX_2,
	IDX_SECONDARY_TDM_TX_3,
	IDX_SECONDARY_TDM_TX_4,
	IDX_SECONDARY_TDM_TX_5,
	IDX_SECONDARY_TDM_TX_6,
	IDX_SECONDARY_TDM_TX_7,
	IDX_TERTIARY_TDM_RX_0,
	IDX_TERTIARY_TDM_RX_1,
	IDX_TERTIARY_TDM_RX_2,
	IDX_TERTIARY_TDM_RX_3,
	IDX_TERTIARY_TDM_RX_4,
	IDX_TERTIARY_TDM_RX_5,
	IDX_TERTIARY_TDM_RX_6,
	IDX_TERTIARY_TDM_RX_7,
	IDX_TERTIARY_TDM_TX_0,
	IDX_TERTIARY_TDM_TX_1,
	IDX_TERTIARY_TDM_TX_2,
	IDX_TERTIARY_TDM_TX_3,
	IDX_TERTIARY_TDM_TX_4,
	IDX_TERTIARY_TDM_TX_5,
	IDX_TERTIARY_TDM_TX_6,
	IDX_TERTIARY_TDM_TX_7,
	IDX_QUATERNARY_TDM_RX_0,
	IDX_QUATERNARY_TDM_RX_1,
	IDX_QUATERNARY_TDM_RX_2,
	IDX_QUATERNARY_TDM_RX_3,
	IDX_QUATERNARY_TDM_RX_4,
	IDX_QUATERNARY_TDM_RX_5,
	IDX_QUATERNARY_TDM_RX_6,
	IDX_QUATERNARY_TDM_RX_7,
	IDX_QUATERNARY_TDM_TX_0,
	IDX_QUATERNARY_TDM_TX_1,
	IDX_QUATERNARY_TDM_TX_2,
	IDX_QUATERNARY_TDM_TX_3,
	IDX_QUATERNARY_TDM_TX_4,
	IDX_QUATERNARY_TDM_TX_5,
	IDX_QUATERNARY_TDM_TX_6,
	IDX_QUATERNARY_TDM_TX_7,
	IDX_QUINARY_TDM_RX_0,
	IDX_QUINARY_TDM_RX_1,
	IDX_QUINARY_TDM_RX_2,
	IDX_QUINARY_TDM_RX_3,
	IDX_QUINARY_TDM_RX_4,
	IDX_QUINARY_TDM_RX_5,
	IDX_QUINARY_TDM_RX_6,
	IDX_QUINARY_TDM_RX_7,
	IDX_QUINARY_TDM_TX_0,
	IDX_QUINARY_TDM_TX_1,
	IDX_QUINARY_TDM_TX_2,
	IDX_QUINARY_TDM_TX_3,
	IDX_QUINARY_TDM_TX_4,
	IDX_QUINARY_TDM_TX_5,
	IDX_QUINARY_TDM_TX_6,
	IDX_QUINARY_TDM_TX_7,
	IDX_SENARY_TDM_RX_0,
	IDX_SENARY_TDM_RX_1,
	IDX_SENARY_TDM_RX_2,
	IDX_SENARY_TDM_RX_3,
	IDX_SENARY_TDM_RX_4,
	IDX_SENARY_TDM_RX_5,
	IDX_SENARY_TDM_RX_6,
	IDX_SENARY_TDM_RX_7,
	IDX_SENARY_TDM_TX_0,
	IDX_SENARY_TDM_TX_1,
	IDX_SENARY_TDM_TX_2,
	IDX_SENARY_TDM_TX_3,
	IDX_SENARY_TDM_TX_4,
	IDX_SENARY_TDM_TX_5,
	IDX_SENARY_TDM_TX_6,
	IDX_SENARY_TDM_TX_7,
	IDX_TDM_MAX,
};

enum {
	IDX_GROUP_PRIMARY_TDM_RX,
	IDX_GROUP_PRIMARY_TDM_TX,
	IDX_GROUP_SECONDARY_TDM_RX,
	IDX_GROUP_SECONDARY_TDM_TX,
	IDX_GROUP_TERTIARY_TDM_RX,
	IDX_GROUP_TERTIARY_TDM_TX,
	IDX_GROUP_QUATERNARY_TDM_RX,
	IDX_GROUP_QUATERNARY_TDM_TX,
	IDX_GROUP_QUINARY_TDM_RX,
	IDX_GROUP_QUINARY_TDM_TX,
	IDX_GROUP_SENARY_TDM_RX,
	IDX_GROUP_SENARY_TDM_TX,
	IDX_GROUP_TDM_MAX,
};

struct msm_dai_q6_dai_data {
	DECLARE_BITMAP(status_mask, STATUS_MAX);
	DECLARE_BITMAP(hwfree_status, STATUS_MAX);
	u32 rate;
	u32 channels;
	u32 bitwidth;
	u32 cal_mode;
	u32 afe_rx_in_channels;
	u16 afe_rx_in_bitformat;
	u32 afe_tx_out_channels;
	u16 afe_tx_out_bitformat;
	struct afe_enc_config enc_config;
	struct afe_dec_config dec_config;
	union afe_port_config port_config;
	u16 vi_feed_mono;
	u32 xt_logging_disable;
	u32 interleave;
};

struct msm_dai_q6_mi2s_dai_config {
	u16 pdata_mi2s_lines;
	struct msm_dai_q6_dai_data mi2s_dai_data;
};

struct msm_dai_q6_mi2s_dai_data {
	u32 is_island_dai;
	u32 interleave;
	struct msm_dai_q6_mi2s_dai_config tx_dai;
	struct msm_dai_q6_mi2s_dai_config rx_dai;
};

struct msm_dai_q6_cdc_dma_dai_data {
	DECLARE_BITMAP(status_mask, STATUS_MAX);
	DECLARE_BITMAP(hwfree_status, STATUS_MAX);
	u32 rate;
	u32 channels;
	u32 bitwidth;
	u32 is_island_dai;
	u32 xt_logging_disable;
	u32 interleave;
	union afe_port_config port_config;
};

/* MI2S format field for AFE_PORT_CMD_I2S_CONFIG command
 *  0: linear PCM
 *  1: non-linear PCM
 *  2: PCM data in IEC 60968 container
 *  3: compressed data in IEC 60958 container
 *  9: DSD over PCM (DoP) with marker byte
 */
static const char *const mi2s_format[] = {
	"LPCM",
	"Compr",
	"LPCM-60958",
	"Compr-60958",
	"NA4",
	"NA5",
	"NA6",
	"NA7",
	"NA8",
	"DSD_DOP_W_MARKER"
};

static const char *const mi2s_vi_feed_mono[] = {
	"Left",
	"Right",
};

static const struct soc_enum mi2s_config_enum[] = {
	SOC_ENUM_SINGLE_EXT(10, mi2s_format),
	SOC_ENUM_SINGLE_EXT(2, mi2s_vi_feed_mono),
};

static const char *const cdc_dma_format[] = {
	"UNPACKED",
	"PACKED_16B",
};

static const struct soc_enum cdc_dma_config_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, cdc_dma_format),
};

static const char *const sb_format[] = {
	"UNPACKED",
	"PACKED_16B",
	"DSD_DOP",
};

static const struct soc_enum sb_config_enum[] = {
	SOC_ENUM_SINGLE_EXT(3, sb_format),
};

static const char * const xt_logging_disable_text[] = {
	"FALSE",
	"TRUE",
};

static const struct soc_enum xt_logging_disable_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, xt_logging_disable_text),
};

static const char *const tdm_data_format[] = {
	"LPCM",
	"Compr",
	"Gen Compr"
};

static const char *const tdm_header_type[] = {
	"Invalid",
	"Default",
	"Entertainment",
};

static const struct soc_enum tdm_config_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tdm_data_format), tdm_data_format),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tdm_header_type), tdm_header_type),
};

static DEFINE_MUTEX(tdm_mutex);

int msm_dai_q6_get_group_idx(u16 id)
{
	switch (id) {
	case AFE_GROUP_DEVICE_ID_PRIMARY_TDM_RX:
	case AFE_PORT_ID_PRIMARY_TDM_RX:
	case AFE_PORT_ID_PRIMARY_TDM_RX_1:
	case AFE_PORT_ID_PRIMARY_TDM_RX_2:
	case AFE_PORT_ID_PRIMARY_TDM_RX_3:
	case AFE_PORT_ID_PRIMARY_TDM_RX_4:
	case AFE_PORT_ID_PRIMARY_TDM_RX_5:
	case AFE_PORT_ID_PRIMARY_TDM_RX_6:
	case AFE_PORT_ID_PRIMARY_TDM_RX_7:
		return IDX_GROUP_PRIMARY_TDM_RX;
	case AFE_GROUP_DEVICE_ID_PRIMARY_TDM_TX:
	case AFE_PORT_ID_PRIMARY_TDM_TX:
	case AFE_PORT_ID_PRIMARY_TDM_TX_1:
	case AFE_PORT_ID_PRIMARY_TDM_TX_2:
	case AFE_PORT_ID_PRIMARY_TDM_TX_3:
	case AFE_PORT_ID_PRIMARY_TDM_TX_4:
	case AFE_PORT_ID_PRIMARY_TDM_TX_5:
	case AFE_PORT_ID_PRIMARY_TDM_TX_6:
	case AFE_PORT_ID_PRIMARY_TDM_TX_7:
		return IDX_GROUP_PRIMARY_TDM_TX;
	case AFE_GROUP_DEVICE_ID_SECONDARY_TDM_RX:
	case AFE_PORT_ID_SECONDARY_TDM_RX:
	case AFE_PORT_ID_SECONDARY_TDM_RX_1:
	case AFE_PORT_ID_SECONDARY_TDM_RX_2:
	case AFE_PORT_ID_SECONDARY_TDM_RX_3:
	case AFE_PORT_ID_SECONDARY_TDM_RX_4:
	case AFE_PORT_ID_SECONDARY_TDM_RX_5:
	case AFE_PORT_ID_SECONDARY_TDM_RX_6:
	case AFE_PORT_ID_SECONDARY_TDM_RX_7:
		return IDX_GROUP_SECONDARY_TDM_RX;
	case AFE_GROUP_DEVICE_ID_SECONDARY_TDM_TX:
	case AFE_PORT_ID_SECONDARY_TDM_TX:
	case AFE_PORT_ID_SECONDARY_TDM_TX_1:
	case AFE_PORT_ID_SECONDARY_TDM_TX_2:
	case AFE_PORT_ID_SECONDARY_TDM_TX_3:
	case AFE_PORT_ID_SECONDARY_TDM_TX_4:
	case AFE_PORT_ID_SECONDARY_TDM_TX_5:
	case AFE_PORT_ID_SECONDARY_TDM_TX_6:
	case AFE_PORT_ID_SECONDARY_TDM_TX_7:
		return IDX_GROUP_SECONDARY_TDM_TX;
	case AFE_GROUP_DEVICE_ID_TERTIARY_TDM_RX:
	case AFE_PORT_ID_TERTIARY_TDM_RX:
	case AFE_PORT_ID_TERTIARY_TDM_RX_1:
	case AFE_PORT_ID_TERTIARY_TDM_RX_2:
	case AFE_PORT_ID_TERTIARY_TDM_RX_3:
	case AFE_PORT_ID_TERTIARY_TDM_RX_4:
	case AFE_PORT_ID_TERTIARY_TDM_RX_5:
	case AFE_PORT_ID_TERTIARY_TDM_RX_6:
	case AFE_PORT_ID_TERTIARY_TDM_RX_7:
		return IDX_GROUP_TERTIARY_TDM_RX;
	case AFE_GROUP_DEVICE_ID_TERTIARY_TDM_TX:
	case AFE_PORT_ID_TERTIARY_TDM_TX:
	case AFE_PORT_ID_TERTIARY_TDM_TX_1:
	case AFE_PORT_ID_TERTIARY_TDM_TX_2:
	case AFE_PORT_ID_TERTIARY_TDM_TX_3:
	case AFE_PORT_ID_TERTIARY_TDM_TX_4:
	case AFE_PORT_ID_TERTIARY_TDM_TX_5:
	case AFE_PORT_ID_TERTIARY_TDM_TX_6:
	case AFE_PORT_ID_TERTIARY_TDM_TX_7:
		return IDX_GROUP_TERTIARY_TDM_TX;
	case AFE_GROUP_DEVICE_ID_QUATERNARY_TDM_RX:
	case AFE_PORT_ID_QUATERNARY_TDM_RX:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_1:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_2:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_3:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_4:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_5:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_6:
	case AFE_PORT_ID_QUATERNARY_TDM_RX_7:
		return IDX_GROUP_QUATERNARY_TDM_RX;
	case AFE_GROUP_DEVICE_ID_QUATERNARY_TDM_TX:
	case AFE_PORT_ID_QUATERNARY_TDM_TX:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_1:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_2:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_3:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_4:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_5:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_6:
	case AFE_PORT_ID_QUATERNARY_TDM_TX_7:
		return IDX_GROUP_QUATERNARY_TDM_TX;
	case AFE_GROUP_DEVICE_ID_QUINARY_TDM_RX:
	case AFE_PORT_ID_QUINARY_TDM_RX:
	case AFE_PORT_ID_QUINARY_TDM_RX_1:
	case AFE_PORT_ID_QUINARY_TDM_RX_2:
	case AFE_PORT_ID_QUINARY_TDM_RX_3:
	case AFE_PORT_ID_QUINARY_TDM_RX_4:
	case AFE_PORT_ID_QUINARY_TDM_RX_5:
	case AFE_PORT_ID_QUINARY_TDM_RX_6:
	case AFE_PORT_ID_QUINARY_TDM_RX_7:
		return IDX_GROUP_QUINARY_TDM_RX;
	case AFE_GROUP_DEVICE_ID_QUINARY_TDM_TX:
	case AFE_PORT_ID_QUINARY_TDM_TX:
	case AFE_PORT_ID_QUINARY_TDM_TX_1:
	case AFE_PORT_ID_QUINARY_TDM_TX_2:
	case AFE_PORT_ID_QUINARY_TDM_TX_3:
	case AFE_PORT_ID_QUINARY_TDM_TX_4:
	case AFE_PORT_ID_QUINARY_TDM_TX_5:
	case AFE_PORT_ID_QUINARY_TDM_TX_6:
	case AFE_PORT_ID_QUINARY_TDM_TX_7:
		return IDX_GROUP_QUINARY_TDM_TX;
	case AFE_GROUP_DEVICE_ID_SENARY_TDM_RX:
	case AFE_PORT_ID_SENARY_TDM_RX:
	case AFE_PORT_ID_SENARY_TDM_RX_1:
	case AFE_PORT_ID_SENARY_TDM_RX_2:
	case AFE_PORT_ID_SENARY_TDM_RX_3:
	case AFE_PORT_ID_SENARY_TDM_RX_4:
	case AFE_PORT_ID_SENARY_TDM_RX_5:
	case AFE_PORT_ID_SENARY_TDM_RX_6:
	case AFE_PORT_ID_SENARY_TDM_RX_7:
		return IDX_GROUP_SENARY_TDM_RX;
	case AFE_GROUP_DEVICE_ID_SENARY_TDM_TX:
	case AFE_PORT_ID_SENARY_TDM_TX:
	case AFE_PORT_ID_SENARY_TDM_TX_1:
	case AFE_PORT_ID_SENARY_TDM_TX_2:
	case AFE_PORT_ID_SENARY_TDM_TX_3:
	case AFE_PORT_ID_SENARY_TDM_TX_4:
	case AFE_PORT_ID_SENARY_TDM_TX_5:
	case AFE_PORT_ID_SENARY_TDM_TX_6:
	case AFE_PORT_ID_SENARY_TDM_TX_7:
		return IDX_GROUP_SENARY_TDM_TX;
	default: return -EINVAL;
	}
}

int msm_dai_q6_get_port_idx(u16 id)
{
	switch (id) {
	case AFE_PORT_ID_PRIMARY_TDM_RX:
		return IDX_PRIMARY_TDM_RX_0;
	case AFE_PORT_ID_PRIMARY_TDM_TX:
		return IDX_PRIMARY_TDM_TX_0;
	case AFE_PORT_ID_PRIMARY_TDM_RX_1:
		return IDX_PRIMARY_TDM_RX_1;
	case AFE_PORT_ID_PRIMARY_TDM_TX_1:
		return IDX_PRIMARY_TDM_TX_1;
	case AFE_PORT_ID_PRIMARY_TDM_RX_2:
		return IDX_PRIMARY_TDM_RX_2;
	case AFE_PORT_ID_PRIMARY_TDM_TX_2:
		return IDX_PRIMARY_TDM_TX_2;
	case AFE_PORT_ID_PRIMARY_TDM_RX_3:
		return IDX_PRIMARY_TDM_RX_3;
	case AFE_PORT_ID_PRIMARY_TDM_TX_3:
		return IDX_PRIMARY_TDM_TX_3;
	case AFE_PORT_ID_PRIMARY_TDM_RX_4:
		return IDX_PRIMARY_TDM_RX_4;
	case AFE_PORT_ID_PRIMARY_TDM_TX_4:
		return IDX_PRIMARY_TDM_TX_4;
	case AFE_PORT_ID_PRIMARY_TDM_RX_5:
		return IDX_PRIMARY_TDM_RX_5;
	case AFE_PORT_ID_PRIMARY_TDM_TX_5:
		return IDX_PRIMARY_TDM_TX_5;
	case AFE_PORT_ID_PRIMARY_TDM_RX_6:
		return IDX_PRIMARY_TDM_RX_6;
	case AFE_PORT_ID_PRIMARY_TDM_TX_6:
		return IDX_PRIMARY_TDM_TX_6;
	case AFE_PORT_ID_PRIMARY_TDM_RX_7:
		return IDX_PRIMARY_TDM_RX_7;
	case AFE_PORT_ID_PRIMARY_TDM_TX_7:
		return IDX_PRIMARY_TDM_TX_7;
	case AFE_PORT_ID_SECONDARY_TDM_RX:
		return IDX_SECONDARY_TDM_RX_0;
	case AFE_PORT_ID_SECONDARY_TDM_TX:
		return IDX_SECONDARY_TDM_TX_0;
	case AFE_PORT_ID_SECONDARY_TDM_RX_1:
		return IDX_SECONDARY_TDM_RX_1;
	case AFE_PORT_ID_SECONDARY_TDM_TX_1:
		return IDX_SECONDARY_TDM_TX_1;
	case AFE_PORT_ID_SECONDARY_TDM_RX_2:
		return IDX_SECONDARY_TDM_RX_2;
	case AFE_PORT_ID_SECONDARY_TDM_TX_2:
		return IDX_SECONDARY_TDM_TX_2;
	case AFE_PORT_ID_SECONDARY_TDM_RX_3:
		return IDX_SECONDARY_TDM_RX_3;
	case AFE_PORT_ID_SECONDARY_TDM_TX_3:
		return IDX_SECONDARY_TDM_TX_3;
	case AFE_PORT_ID_SECONDARY_TDM_RX_4:
		return IDX_SECONDARY_TDM_RX_4;
	case AFE_PORT_ID_SECONDARY_TDM_TX_4:
		return IDX_SECONDARY_TDM_TX_4;
	case AFE_PORT_ID_SECONDARY_TDM_RX_5:
		return IDX_SECONDARY_TDM_RX_5;
	case AFE_PORT_ID_SECONDARY_TDM_TX_5:
		return IDX_SECONDARY_TDM_TX_5;
	case AFE_PORT_ID_SECONDARY_TDM_RX_6:
		return IDX_SECONDARY_TDM_RX_6;
	case AFE_PORT_ID_SECONDARY_TDM_TX_6:
		return IDX_SECONDARY_TDM_TX_6;
	case AFE_PORT_ID_SECONDARY_TDM_RX_7:
		return IDX_SECONDARY_TDM_RX_7;
	case AFE_PORT_ID_SECONDARY_TDM_TX_7:
		return IDX_SECONDARY_TDM_TX_7;
	case AFE_PORT_ID_TERTIARY_TDM_RX:
		return IDX_TERTIARY_TDM_RX_0;
	case AFE_PORT_ID_TERTIARY_TDM_TX:
		return IDX_TERTIARY_TDM_TX_0;
	case AFE_PORT_ID_TERTIARY_TDM_RX_1:
		return IDX_TERTIARY_TDM_RX_1;
	case AFE_PORT_ID_TERTIARY_TDM_TX_1:
		return IDX_TERTIARY_TDM_TX_1;
	case AFE_PORT_ID_TERTIARY_TDM_RX_2:
		return IDX_TERTIARY_TDM_RX_2;
	case AFE_PORT_ID_TERTIARY_TDM_TX_2:
		return IDX_TERTIARY_TDM_TX_2;
	case AFE_PORT_ID_TERTIARY_TDM_RX_3:
		return IDX_TERTIARY_TDM_RX_3;
	case AFE_PORT_ID_TERTIARY_TDM_TX_3:
		return IDX_TERTIARY_TDM_TX_3;
	case AFE_PORT_ID_TERTIARY_TDM_RX_4:
		return IDX_TERTIARY_TDM_RX_4;
	case AFE_PORT_ID_TERTIARY_TDM_TX_4:
		return IDX_TERTIARY_TDM_TX_4;
	case AFE_PORT_ID_TERTIARY_TDM_RX_5:
		return IDX_TERTIARY_TDM_RX_5;
	case AFE_PORT_ID_TERTIARY_TDM_TX_5:
		return IDX_TERTIARY_TDM_TX_5;
	case AFE_PORT_ID_TERTIARY_TDM_RX_6:
		return IDX_TERTIARY_TDM_RX_6;
	case AFE_PORT_ID_TERTIARY_TDM_TX_6:
		return IDX_TERTIARY_TDM_TX_6;
	case AFE_PORT_ID_TERTIARY_TDM_RX_7:
		return IDX_TERTIARY_TDM_RX_7;
	case AFE_PORT_ID_TERTIARY_TDM_TX_7:
		return IDX_TERTIARY_TDM_TX_7;
	case AFE_PORT_ID_QUATERNARY_TDM_RX:
		return IDX_QUATERNARY_TDM_RX_0;
	case AFE_PORT_ID_QUATERNARY_TDM_TX:
		return IDX_QUATERNARY_TDM_TX_0;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_1:
		return IDX_QUATERNARY_TDM_RX_1;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_1:
		return IDX_QUATERNARY_TDM_TX_1;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_2:
		return IDX_QUATERNARY_TDM_RX_2;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_2:
		return IDX_QUATERNARY_TDM_TX_2;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_3:
		return IDX_QUATERNARY_TDM_RX_3;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_3:
		return IDX_QUATERNARY_TDM_TX_3;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_4:
		return IDX_QUATERNARY_TDM_RX_4;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_4:
		return IDX_QUATERNARY_TDM_TX_4;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_5:
		return IDX_QUATERNARY_TDM_RX_5;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_5:
		return IDX_QUATERNARY_TDM_TX_5;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_6:
		return IDX_QUATERNARY_TDM_RX_6;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_6:
		return IDX_QUATERNARY_TDM_TX_6;
	case AFE_PORT_ID_QUATERNARY_TDM_RX_7:
		return IDX_QUATERNARY_TDM_RX_7;
	case AFE_PORT_ID_QUATERNARY_TDM_TX_7:
		return IDX_QUATERNARY_TDM_TX_7;
	case AFE_PORT_ID_QUINARY_TDM_RX:
		return IDX_QUINARY_TDM_RX_0;
	case AFE_PORT_ID_QUINARY_TDM_TX:
		return IDX_QUINARY_TDM_TX_0;
	case AFE_PORT_ID_QUINARY_TDM_RX_1:
		return IDX_QUINARY_TDM_RX_1;
	case AFE_PORT_ID_QUINARY_TDM_TX_1:
		return IDX_QUINARY_TDM_TX_1;
	case AFE_PORT_ID_QUINARY_TDM_RX_2:
		return IDX_QUINARY_TDM_RX_2;
	case AFE_PORT_ID_QUINARY_TDM_TX_2:
		return IDX_QUINARY_TDM_TX_2;
	case AFE_PORT_ID_QUINARY_TDM_RX_3:
		return IDX_QUINARY_TDM_RX_3;
	case AFE_PORT_ID_QUINARY_TDM_TX_3:
		return IDX_QUINARY_TDM_TX_3;
	case AFE_PORT_ID_QUINARY_TDM_RX_4:
		return IDX_QUINARY_TDM_RX_4;
	case AFE_PORT_ID_QUINARY_TDM_TX_4:
		return IDX_QUINARY_TDM_TX_4;
	case AFE_PORT_ID_QUINARY_TDM_RX_5:
		return IDX_QUINARY_TDM_RX_5;
	case AFE_PORT_ID_QUINARY_TDM_TX_5:
		return IDX_QUINARY_TDM_TX_5;
	case AFE_PORT_ID_QUINARY_TDM_RX_6:
		return IDX_QUINARY_TDM_RX_6;
	case AFE_PORT_ID_QUINARY_TDM_TX_6:
		return IDX_QUINARY_TDM_TX_6;
	case AFE_PORT_ID_QUINARY_TDM_RX_7:
		return IDX_QUINARY_TDM_RX_7;
	case AFE_PORT_ID_QUINARY_TDM_TX_7:
		return IDX_QUINARY_TDM_TX_7;
	case AFE_PORT_ID_SENARY_TDM_RX:
		return IDX_SENARY_TDM_RX_0;
	case AFE_PORT_ID_SENARY_TDM_TX:
		return IDX_SENARY_TDM_TX_0;
	case AFE_PORT_ID_SENARY_TDM_RX_1:
		return IDX_SENARY_TDM_RX_1;
	case AFE_PORT_ID_SENARY_TDM_TX_1:
		return IDX_SENARY_TDM_TX_1;
	case AFE_PORT_ID_SENARY_TDM_RX_2:
		return IDX_SENARY_TDM_RX_2;
	case AFE_PORT_ID_SENARY_TDM_TX_2:
		return IDX_SENARY_TDM_TX_2;
	case AFE_PORT_ID_SENARY_TDM_RX_3:
		return IDX_SENARY_TDM_RX_3;
	case AFE_PORT_ID_SENARY_TDM_TX_3:
		return IDX_SENARY_TDM_TX_3;
	case AFE_PORT_ID_SENARY_TDM_RX_4:
		return IDX_SENARY_TDM_RX_4;
	case AFE_PORT_ID_SENARY_TDM_TX_4:
		return IDX_SENARY_TDM_TX_4;
	case AFE_PORT_ID_SENARY_TDM_RX_5:
		return IDX_SENARY_TDM_RX_5;
	case AFE_PORT_ID_SENARY_TDM_TX_5:
		return IDX_SENARY_TDM_TX_5;
	case AFE_PORT_ID_SENARY_TDM_RX_6:
		return IDX_SENARY_TDM_RX_6;
	case AFE_PORT_ID_SENARY_TDM_TX_6:
		return IDX_SENARY_TDM_TX_6;
	case AFE_PORT_ID_SENARY_TDM_RX_7:
		return IDX_SENARY_TDM_RX_7;
	case AFE_PORT_ID_SENARY_TDM_TX_7:
		return IDX_SENARY_TDM_TX_7;
	default: return -EINVAL;
	}
}

static int msm_dai_q6_dai_add_route(struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_route intercon;
	struct snd_soc_dapm_context *dapm;

	if (!dai) {
		pr_err("%s: Invalid params dai\n", __func__);
		return -EINVAL;
	}
	if (!dai->driver) {
		pr_err("%s: Invalid params dai driver\n", __func__);
		return -EINVAL;
	}
	dapm = snd_soc_component_get_dapm(dai->component);
	memset(&intercon, 0, sizeof(intercon));
	if (dai->driver->playback.stream_name &&
		dai->driver->playback.aif_name) {
		dev_dbg(dai->dev, "%s: add route for widget %s",
				__func__, dai->driver->playback.stream_name);
		intercon.source = dai->driver->playback.aif_name;
		intercon.sink = dai->driver->playback.stream_name;
		dev_dbg(dai->dev, "%s: src %s sink %s\n",
				__func__, intercon.source, intercon.sink);
		snd_soc_dapm_add_routes(dapm, &intercon, 1);
		snd_soc_dapm_ignore_suspend(dapm, intercon.sink);
	}
	if (dai->driver->capture.stream_name &&
		dai->driver->capture.aif_name) {
		dev_dbg(dai->dev, "%s: add route for widget %s",
				__func__, dai->driver->capture.stream_name);
		intercon.sink = dai->driver->capture.aif_name;
		intercon.source = dai->driver->capture.stream_name;
		dev_dbg(dai->dev, "%s: src %s sink %s\n",
				__func__, intercon.source, intercon.sink);
		snd_soc_dapm_add_routes(dapm, &intercon, 1);
		snd_soc_dapm_ignore_suspend(dapm, intercon.source);
	}
	return 0;
}

static int msm_dai_q6_island_mode_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];
	u16 port_id = (u16)kcontrol->private_value;

	pr_debug("%s: island mode = %d\n", __func__, value);
	trace_printk("%s: island mode = %d\n", __func__, value);

	afe_set_island_mode_cfg(port_id, value);
	return 0;
}

static int msm_dai_q6_island_mode_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int value;
	u16 port_id = (u16)kcontrol->private_value;

	afe_get_island_mode_cfg(port_id, &value);
	ucontrol->value.integer.value[0] = value;
	return 0;
}

static void island_mx_ctl_private_free(struct snd_kcontrol *kcontrol)
{
	struct snd_kcontrol_new *knew = snd_kcontrol_chip(kcontrol);

	kfree(knew);
}

static int msm_dai_q6_add_island_mx_ctls(struct snd_card *card,
				      const char *dai_name,
				      int dai_id, void *dai_data)
{
	const char *mx_ctl_name = "TX island";
	char *mixer_str = NULL;
	int dai_str_len = 0, ctl_len = 0;
	int rc = 0;
	struct snd_kcontrol_new *knew = NULL;
	struct snd_kcontrol *kctl = NULL;

	dai_str_len = strlen(dai_name) + 1;

	/* Add island related mixer controls */
	ctl_len = dai_str_len + strlen(mx_ctl_name) + 1;
	mixer_str = kzalloc(ctl_len, GFP_KERNEL);
	if (!mixer_str)
		return -ENOMEM;

	snprintf(mixer_str, ctl_len, "%s %s", dai_name, mx_ctl_name);

	knew = kzalloc(sizeof(struct snd_kcontrol_new), GFP_KERNEL);
	if (!knew) {
		kfree(mixer_str);
		return -ENOMEM;
	}
	knew->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	knew->info = snd_ctl_boolean_mono_info;
	knew->get = msm_dai_q6_island_mode_get;
	knew->put = msm_dai_q6_island_mode_put;
	knew->name = mixer_str;
	knew->private_value = dai_id;
	kctl = snd_ctl_new1(knew, knew);
	if (!kctl) {
		kfree(knew);
		kfree(mixer_str);
		return -ENOMEM;
	}
	kctl->private_free = island_mx_ctl_private_free;
	rc = snd_ctl_add(card, kctl);
	if (rc < 0)
		pr_err("%s: err add config ctl, DAI = %s\n",
			__func__, dai_name);
	kfree(mixer_str);

	return rc;
}

/*
 * For single CPU DAI registration, the dai id needs to be
 * set explicitly in the dai probe as ASoC does not read
 * the cpu->driver->id field rather it assigns the dai id
 * from the device name that is in the form %s.%d. This dai
 * id should be assigned to back-end AFE port id and used
 * during dai prepare. For multiple dai registration, it
 * is not required to call this function, however the dai->
 * driver->id field must be defined and set to corresponding
 * AFE Port id.
 */
static inline void msm_dai_q6_set_dai_id(struct snd_soc_dai *dai)
{
	if (!dai->driver) {
		dev_err(dai->dev, "DAI driver is not set\n");
		return;
	}
	if (!dai->driver->id) {
		dev_dbg(dai->dev, "DAI driver id is not set\n");
		return;
	}
	dai->id = dai->driver->id;
}

static int msm_dai_q6_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
    if(dai->id == SLIMBUS_7_RX ||
        dai->id == SLIMBUS_7_TX ||
        dai->id == SLIMBUS_8_TX){
        return jlq_dai_slim_startup(substream, dai);
    }
    return 0;
}

static int msm_dai_q6_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int rc = 0;

	if (!test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {

		if(dai->id == SLIMBUS_7_RX ||
		   dai->id == SLIMBUS_7_TX ||
		   dai->id == SLIMBUS_8_TX){
			rc = jlq_dai_slim_prepare(substream, dai);
			if(rc < 0)
			{
				pr_err("%s call jlq_dai_slim_prepare error\n", __func__);
				return rc;
			}
		}

		if (dai_data->enc_config.format != ENC_FMT_NONE) {
			int bitwidth = 0;

			switch (dai_data->afe_rx_in_bitformat) {
			case SNDRV_PCM_FORMAT_S32_LE:
				bitwidth = 32;
				break;
			case SNDRV_PCM_FORMAT_S24_LE:
				bitwidth = 24;
				break;
			case SNDRV_PCM_FORMAT_S16_LE:
			default:
				bitwidth = 16;
				break;
			}
			pr_debug("%s: calling AFE_PORT_START_V2 with enc_format: %d\n",
				 __func__, dai_data->enc_config.format);
			rc = afe_port_start_v2(dai->id, &dai_data->port_config,
					       dai_data->rate,
					       dai_data->afe_rx_in_channels,
					       bitwidth,
					       &dai_data->enc_config, NULL);
			if (rc < 0)
				pr_err("%s: afe_port_start_v2 failed error: %d\n",
					__func__, rc);
		} else if (dai_data->dec_config.format != DEC_FMT_NONE) {
			int bitwidth = 0;

			/*
			 * If bitwidth is not configured set default value to
			 * zero, so that decoder port config uses slim device
			 * bit width value in afe decoder config.
			 */
			switch (dai_data->afe_tx_out_bitformat) {
			case SNDRV_PCM_FORMAT_S32_LE:
				bitwidth = 32;
				break;
			case SNDRV_PCM_FORMAT_S24_LE:
				bitwidth = 24;
				break;
			case SNDRV_PCM_FORMAT_S16_LE:
				bitwidth = 16;
				break;
			default:
				bitwidth = 0;
				break;
			}
			pr_debug("%s: calling AFE_PORT_START_V2 with dec format: %d\n",
				 __func__, dai_data->dec_config.format);
			rc = afe_port_start_v2(dai->id, &dai_data->port_config,
					       dai_data->rate,
					       dai_data->afe_tx_out_channels,
					       bitwidth,
					       NULL, &dai_data->dec_config);
			if (rc < 0) {
				pr_err("%s: fail to open AFE port 0x%x\n",
					__func__, dai->id);
			}
		} else {
			rc = afe_port_start(dai->id, &dai_data->port_config,
						dai_data->rate);
		}
		if (rc < 0)
			dev_err(dai->dev, "fail to open AFE port 0x%x\n",
				dai->id);
		else
			set_bit(STATUS_PORT_STARTED,
				dai_data->status_mask);
	}
	return rc;
}

static int msm_dai_q6_cdc_hw_params(struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai, int stream)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	dai_data->channels = params_channels(params);
	switch (dai_data->channels) {
	case 2:
		dai_data->port_config.i2s.mono_stereo = MSM_AFE_STEREO;
		break;
	case 1:
		dai_data->port_config.i2s.mono_stereo = MSM_AFE_MONO;
		break;
	default:
		return -EINVAL;
		pr_err("%s: err channels %d\n",
			__func__, dai_data->channels);
		break;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_SPECIAL:
		dai_data->port_config.i2s.bit_width = 16;
	    dai_data->bitwidth = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		dai_data->port_config.i2s.bit_width = 24;
	    dai_data->bitwidth = 24;
		break;
	default:
		pr_err("%s: format %d\n",
			__func__, params_format(params));
		return -EINVAL;
	}

	dai_data->rate = params_rate(params);
	dai_data->port_config.i2s.sample_rate = dai_data->rate;
	dai_data->port_config.i2s.i2s_cfg_minor_version =
						AFE_API_VERSION_I2S_CONFIG;
	dai_data->port_config.i2s.data_format =  AFE_LINEAR_PCM_DATA;
	dev_dbg(dai->dev, " channel %d sample rate %d entered\n",
	dai_data->channels, dai_data->rate);

	dai_data->port_config.i2s.channel_mode = 1;
	return 0;
}

static u16 num_of_bits_set(u16 sd_line_mask)
{
	u8 num_bits_set = 0;

	while (sd_line_mask) {
		num_bits_set++;
		sd_line_mask = sd_line_mask & (sd_line_mask - 1);
	}
	return num_bits_set;
}

static int msm_dai_q6_i2s_hw_params(struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai, int stream)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct msm_i2s_data *i2s_pdata =
			(struct msm_i2s_data *) dai->dev->platform_data;

	dai_data->channels = params_channels(params);
	if (num_of_bits_set(i2s_pdata->sd_lines) == 1) {
		switch (dai_data->channels) {
		case 2:
			dai_data->port_config.i2s.mono_stereo = MSM_AFE_STEREO;
			break;
		case 1:
			dai_data->port_config.i2s.mono_stereo = MSM_AFE_MONO;
			break;
		default:
			pr_warn("%s: greater than stereo has not been validated %d",
				__func__, dai_data->channels);
			break;
		}
	}
	dai_data->rate = params_rate(params);
	dai_data->port_config.i2s.sample_rate = dai_data->rate;
	dai_data->port_config.i2s.i2s_cfg_minor_version =
						AFE_API_VERSION_I2S_CONFIG;
	dai_data->port_config.i2s.data_format =  AFE_LINEAR_PCM_DATA;
	/* Q6 only supports 16 as now */
	dai_data->port_config.i2s.bit_width = 16;
	dai_data->port_config.i2s.channel_mode = 1;
	dai_data->bitwidth = 16;

	return 0;
}

static int msm_dai_q6_slim_bus_hw_params(struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai, int stream)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	dai_data->channels = params_channels(params);
	dai_data->rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_SPECIAL:
		dai_data->port_config.slim_sch.bit_width = 16;
	    dai_data->bitwidth = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		dai_data->port_config.slim_sch.bit_width = 24;
	    dai_data->bitwidth = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dai_data->port_config.slim_sch.bit_width = 32;
		dai_data->bitwidth = 32;
		break;
	default:
		pr_err("%s: format %d\n",
			__func__, params_format(params));
		return -EINVAL;
	}

	dai_data->port_config.slim_sch.sb_cfg_minor_version =
				AFE_API_VERSION_SLIMBUS_CONFIG;
	dai_data->port_config.slim_sch.sample_rate = dai_data->rate;
	dai_data->port_config.slim_sch.num_channels = dai_data->channels;

	dev_dbg(dai->dev, "%s:slimbus_dev_id[%hu] bit_wd[%hu] format[%hu]\n"
		"num_channel %hu  shared_ch_mapping[0]  %hu\n"
		"slave_port_mapping[1]  %hu slave_port_mapping[2]  %hu\n"
		"sample_rate %d\n", __func__,
		dai_data->port_config.slim_sch.slimbus_dev_id,
		dai_data->port_config.slim_sch.bit_width,
		dai_data->port_config.slim_sch.data_format,
		dai_data->port_config.slim_sch.num_channels,
		dai_data->port_config.slim_sch.shared_ch_mapping[0],
		dai_data->port_config.slim_sch.shared_ch_mapping[1],
		dai_data->port_config.slim_sch.shared_ch_mapping[2],
		dai_data->rate);

	return 0;
}

static int msm_dai_q6_usb_audio_hw_params(struct snd_pcm_hw_params *params,
					  struct snd_soc_dai *dai, int stream)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	dai_data->channels = params_channels(params);
	dai_data->rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_SPECIAL:
		dai_data->port_config.usb_audio.bit_width = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		dai_data->port_config.usb_audio.bit_width = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dai_data->port_config.usb_audio.bit_width = 32;
		break;

	default:
		dev_err(dai->dev, "%s: invalid format %d\n",
			__func__, params_format(params));
		return -EINVAL;
	}
	dai_data->port_config.usb_audio.cfg_minor_version =
					AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG;
	dai_data->port_config.usb_audio.num_channels = dai_data->channels;
	dai_data->port_config.usb_audio.sample_rate = dai_data->rate;

	dev_dbg(dai->dev, "%s: dev_id[0x%x] bit_wd[%hu] format[%hu]\n"
		"num_channel %hu  sample_rate %d\n", __func__,
		dai_data->port_config.usb_audio.dev_token,
		dai_data->port_config.usb_audio.bit_width,
		dai_data->port_config.usb_audio.data_format,
		dai_data->port_config.usb_audio.num_channels,
		dai_data->port_config.usb_audio.sample_rate);

	return 0;
}

static int msm_dai_q6_bt_fm_hw_params(struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai, int stream)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	dai_data->channels = params_channels(params);
	dai_data->rate = params_rate(params);

	dev_dbg(dai->dev, "channels %d sample rate %d entered\n",
		dai_data->channels, dai_data->rate);

	memset(&dai_data->port_config, 0, sizeof(dai_data->port_config));

	pr_debug("%s: setting bt_fm parameters\n", __func__);

	dai_data->port_config.int_bt_fm.bt_fm_cfg_minor_version =
					AFE_API_VERSION_INTERNAL_BT_FM_CONFIG;
	dai_data->port_config.int_bt_fm.num_channels = dai_data->channels;
	dai_data->port_config.int_bt_fm.sample_rate = dai_data->rate;
	dai_data->port_config.int_bt_fm.bit_width = 16;

	return 0;
}

static int msm_dai_q6_afe_rtproxy_hw_params(struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	dai_data->rate = params_rate(params);
	dai_data->port_config.rtproxy.num_channels = params_channels(params);
	dai_data->port_config.rtproxy.sample_rate = params_rate(params);

	pr_debug("channel %d entered,dai_id: %d,rate: %d\n",
	dai_data->port_config.rtproxy.num_channels, dai->id, dai_data->rate);

	dai_data->port_config.rtproxy.rt_proxy_cfg_minor_version =
				AFE_API_VERSION_RT_PROXY_CONFIG;
	dai_data->port_config.rtproxy.bit_width = 16; /* Q6 only supports 16 */
	dai_data->port_config.rtproxy.interleaved = 1;
	dai_data->port_config.rtproxy.frame_size = params_period_bytes(params);
	dai_data->port_config.rtproxy.jitter_allowance =
				dai_data->port_config.rtproxy.frame_size/2;
	dai_data->port_config.rtproxy.low_water_mark = 0;
	dai_data->port_config.rtproxy.high_water_mark = 0;

	return 0;
}

static int msm_dai_q6_pseudo_port_hw_params(struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai, int stream)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	dai_data->channels = params_channels(params);
	dai_data->rate = params_rate(params);
	dai_data->bitwidth = 16;

	/* Q6 only supports 16 as now */
	dai_data->port_config.pseudo_port.pseud_port_cfg_minor_version =
				AFE_API_VERSION_PSEUDO_PORT_CONFIG;
	dai_data->port_config.pseudo_port.num_channels =
				params_channels(params);
	dai_data->port_config.pseudo_port.bit_width = 16;
	dai_data->port_config.pseudo_port.data_format = 0;
	dai_data->port_config.pseudo_port.timing_mode =
				AFE_PSEUDOPORT_TIMING_MODE_TIMER;
	dai_data->port_config.pseudo_port.sample_rate = params_rate(params);

	dev_dbg(dai->dev, "%s: bit_wd[%hu] num_channels [%hu] format[%hu]\n"
		"timing Mode %hu sample_rate %d\n", __func__,
		dai_data->port_config.pseudo_port.bit_width,
		dai_data->port_config.pseudo_port.num_channels,
		dai_data->port_config.pseudo_port.data_format,
		dai_data->port_config.pseudo_port.timing_mode,
		dai_data->port_config.pseudo_port.sample_rate);

	return 0;
}

/* Current implementation assumes hw_param is called once
 * This may not be the case but what to do when ADM and AFE
 * port are already opened and parameter changes
 */
static int msm_dai_q6_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	int rc = 0;
    struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);
	struct afe_device_config config;
	switch (dai->id) {
	case PRIMARY_I2S_TX:
	case PRIMARY_I2S_RX:
	case SECONDARY_I2S_RX:
		rc = msm_dai_q6_cdc_hw_params(params, dai, substream->stream);
		break;
	case MI2S_RX:
		rc = msm_dai_q6_i2s_hw_params(params, dai, substream->stream);
		break;
	case SLIMBUS_0_RX:
	case SLIMBUS_1_RX:
	case SLIMBUS_2_RX:
	case SLIMBUS_3_RX:
	case SLIMBUS_4_RX:
	case SLIMBUS_5_RX:
	case SLIMBUS_6_RX:
	case SLIMBUS_7_RX:
	case SLIMBUS_8_RX:
	case SLIMBUS_9_RX:
	case SLIMBUS_0_TX:
	case SLIMBUS_1_TX:
	case SLIMBUS_2_TX:
	case SLIMBUS_3_TX:
	case SLIMBUS_4_TX:
	case SLIMBUS_5_TX:
	case SLIMBUS_6_TX:
	case SLIMBUS_7_TX:
	case SLIMBUS_8_TX:
	case SLIMBUS_9_TX:
		rc = msm_dai_q6_slim_bus_hw_params(params, dai,
				substream->stream);
		break;
	case INT_BT_SCO_RX:
	case INT_BT_SCO_TX:
	case INT_BT_A2DP_RX:
	case INT_FM_RX:
	case INT_FM_TX:
		rc = msm_dai_q6_bt_fm_hw_params(params, dai, substream->stream);
		break;
	case AFE_PORT_ID_USB_RX:
	case AFE_PORT_ID_USB_TX:
		rc = msm_dai_q6_usb_audio_hw_params(params, dai,
						    substream->stream);
		break;
	case RT_PROXY_DAI_001_TX:
	case RT_PROXY_DAI_001_RX:
	case RT_PROXY_DAI_002_TX:
	case RT_PROXY_DAI_002_RX:
		rc = msm_dai_q6_afe_rtproxy_hw_params(params, dai);
		break;
	case VOICE_PLAYBACK_TX:
	case VOICE2_PLAYBACK_TX:
	case VOICE_RECORD_RX:
	case VOICE_RECORD_TX:
		rc = msm_dai_q6_pseudo_port_hw_params(params,
						dai, substream->stream);
		break;
	default:
		dev_err(dai->dev, "invalid AFE port ID 0x%x\n", dai->id);
		rc = -EINVAL;
		break;
	}

	if(dai->id == SLIMBUS_7_RX ||
	   dai->id == SLIMBUS_7_TX ||
	   dai->id == SLIMBUS_8_TX){
	    rc = jlq_dai_slim_hw_params(substream, params, dai);
	    if(rc < 0)
	    {
	        pr_err("%s call jlq_dai_slim_hw_params error\n", __func__);
	        return rc;
	    }
	}
/*
	if(dai->id == SLIMBUS_8_TX){
		jlq_dai_slim_prepare(substream,dai);
	}
*/
    config.bit_width = dai_data->bitwidth;
    config.channel_num = dai_data->channels;
    config.sample_rate = dai_data->rate;
    config.interleave = dai_data->interleave;
	rc = afe_port_open(dai->id, &config);
	if (rc < 0)
		dev_err(dai->dev, "fail to open AFE port 0x%x\n",
			dai->id);

	return rc;
}

static void msm_dai_q6_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int rc = 0;


	if (test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		pr_debug("%s: stop pseudo port:%d\n", __func__,  dai->id);

		if(dai->id == SLIMBUS_7_RX ||
		   dai->id == SLIMBUS_7_TX ||
		   dai->id == SLIMBUS_8_TX){
			jlq_dai_slim_shutdown(substream,dai);
		}

		rc = afe_close(dai->id); /* can block */
		if (rc < 0)
			dev_err(dai->dev, "fail to close AFE port\n");
		pr_debug("%s: dai_data->status_mask = %ld\n", __func__,
			*dai_data->status_mask);
		clear_bit(STATUS_PORT_STARTED, dai_data->status_mask);
	}
}

static int msm_dai_q6_cdc_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		dai_data->port_config.i2s.ws_src = 1; /* CPU is master */
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		dai_data->port_config.i2s.ws_src = 0; /* CPU is slave */
		break;
	default:
		pr_err("%s: fmt 0x%x\n",
			__func__, fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	return 0;
}

static int msm_dai_q6_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	int rc = 0;

	dev_dbg(dai->dev, "%s: id = %d fmt[%d]\n", __func__,
							dai->id, fmt);

	if(dai->id == SLIMBUS_7_RX ||
	   dai->id == SLIMBUS_7_TX ||
	   dai->id == SLIMBUS_8_TX){
        return 0;
	}

	switch (dai->id) {
	case PRIMARY_I2S_TX:
	case PRIMARY_I2S_RX:
	case MI2S_RX:
	case SECONDARY_I2S_RX:
		rc = msm_dai_q6_cdc_set_fmt(dai, fmt);
		break;
	default:
		dev_err(dai->dev, "invalid cpu_dai id 0x%x\n", dai->id);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int msm_dai_q6_set_channel_map(struct snd_soc_dai *dai,
				unsigned int tx_num, unsigned int *tx_slot,
				unsigned int rx_num, unsigned int *rx_slot)

{
	int rc = 0;
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);
	unsigned int i = 0;

	dev_dbg(dai->dev, "%s: id = %d\n", __func__, dai->id);
	switch (dai->id) {
	case SLIMBUS_0_RX:
	case SLIMBUS_1_RX:
	case SLIMBUS_2_RX:
	case SLIMBUS_3_RX:
	case SLIMBUS_4_RX:
	case SLIMBUS_5_RX:
	case SLIMBUS_6_RX:
	case SLIMBUS_7_RX:
	case SLIMBUS_8_RX:
	case SLIMBUS_9_RX:
		/*
		 * channel number to be between 128 and 255.
		 * For RX port use channel numbers
		 * from 138 to 144 for pre-Taiko
		 * from 144 to 159 for Taiko
		 */
		if (!rx_slot) {
			pr_err("%s: rx slot not found\n", __func__);
			return -EINVAL;
		}
		if (rx_num > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
			pr_err("%s: invalid rx num %d\n", __func__, rx_num);
			return -EINVAL;
		}

		for (i = 0; i < rx_num; i++) {
			dai_data->port_config.slim_sch.shared_ch_mapping[i] =
			    rx_slot[i];
			pr_debug("%s: find number of channels[%d] ch[%d]\n",
			       __func__, i, rx_slot[i]);
		}
		dai_data->port_config.slim_sch.num_channels = rx_num;
		pr_debug("%s: SLIMBUS_%d_RX cnt[%d] ch[%d %d]\n", __func__,
			(dai->id - SLIMBUS_0_RX) / 2, rx_num,
			dai_data->port_config.slim_sch.shared_ch_mapping[0],
			dai_data->port_config.slim_sch.shared_ch_mapping[1]);

		break;
	case SLIMBUS_0_TX:
	case SLIMBUS_1_TX:
	case SLIMBUS_2_TX:
	case SLIMBUS_3_TX:
	case SLIMBUS_4_TX:
	case SLIMBUS_5_TX:
	case SLIMBUS_6_TX:
	case SLIMBUS_7_TX:
	case SLIMBUS_8_TX:
	case SLIMBUS_9_TX:
		/*
		 * channel number to be between 128 and 255.
		 * For TX port use channel numbers
		 * from 128 to 137 for pre-Taiko
		 * from 128 to 143 for Taiko
		 */
		if (!tx_slot) {
			pr_err("%s: tx slot not found\n", __func__);
			return -EINVAL;
		}
		if (tx_num > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
			pr_err("%s: invalid tx num %d\n", __func__, tx_num);
			return -EINVAL;
		}

		for (i = 0; i < tx_num; i++) {
			dai_data->port_config.slim_sch.shared_ch_mapping[i] =
			    tx_slot[i];
			pr_debug("%s: find number of channels[%d] ch[%d]\n",
				 __func__, i, tx_slot[i]);
		}
		dai_data->port_config.slim_sch.num_channels = tx_num;
		pr_debug("%s:SLIMBUS_%d_TX cnt[%d] ch[%d %d]\n", __func__,
			(dai->id - SLIMBUS_0_TX) / 2, tx_num,
			dai_data->port_config.slim_sch.shared_ch_mapping[0],
			dai_data->port_config.slim_sch.shared_ch_mapping[1]);
		break;
	default:
		dev_err(dai->dev, "invalid cpu_dai id 0x%x\n", dai->id);
		rc = -EINVAL;
		break;
	}

	if(dai->id == SLIMBUS_7_RX ||
	   dai->id == SLIMBUS_7_TX ||
	   dai->id == SLIMBUS_8_TX){
	    rc = jlq_dai_slim_set_channel_map(dai, tx_num, tx_slot, rx_num, rx_slot);
		if(rc < 0)
	    {
	        pr_err("%s call jlq_dai_slim_set_channel_map error\n", __func__);
	        return rc;
	    }
	}

	return rc;
}

/* all ports with excursion logging requirement can use this digital_mute api */
static int msm_dai_q6_spk_digital_mute(struct snd_soc_dai *dai,
				       int mute)
{
	int port_id = dai->id;
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	if (mute && !dai_data->xt_logging_disable)
		afe_get_sp_xt_logging_data(port_id);

	return 0;
}

static struct snd_soc_dai_ops msm_dai_q6_ops = {
	.startup    = msm_dai_q6_startup,
	.prepare	= msm_dai_q6_prepare,
	.hw_params	= msm_dai_q6_hw_params,
	.shutdown	= msm_dai_q6_shutdown,
	.set_fmt	= msm_dai_q6_set_fmt,
	.set_channel_map = msm_dai_q6_set_channel_map,
};

static struct snd_soc_dai_ops msm_dai_slimbus_0_rx_ops = {
	.prepare	= msm_dai_q6_prepare,
	.hw_params	= msm_dai_q6_hw_params,
	.shutdown	= msm_dai_q6_shutdown,
	.set_fmt	= msm_dai_q6_set_fmt,
	.set_channel_map = msm_dai_q6_set_channel_map,
	.digital_mute = msm_dai_q6_spk_digital_mute,
};

static int msm_dai_q6_cal_info_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u16 port_id = ((struct soc_enum *)
					kcontrol->private_value)->reg;

	dai_data->cal_mode = ucontrol->value.integer.value[0];
	pr_debug("%s: setting cal_mode to %d\n",
		__func__, dai_data->cal_mode);
	afe_set_cal_mode(port_id, dai_data->cal_mode);

	return 0;
}

static int msm_dai_q6_cal_info_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	ucontrol->value.integer.value[0] = dai_data->cal_mode;
	return 0;
}

static int msm_dai_q6_cdc_dma_xt_logging_disable_put(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_cdc_dma_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		dai_data->xt_logging_disable = ucontrol->value.integer.value[0];
		pr_debug("%s: setting xt logging disable to %d\n",
			__func__, dai_data->xt_logging_disable);
	}

	return 0;
}

static int msm_dai_q6_cdc_dma_xt_logging_disable_get(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_cdc_dma_dai_data *dai_data = kcontrol->private_data;

	if (dai_data)
		ucontrol->value.integer.value[0] = dai_data->xt_logging_disable;
	return 0;
}

static int msm_dai_q6_sb_xt_logging_disable_put(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		dai_data->xt_logging_disable = ucontrol->value.integer.value[0];
		pr_debug("%s: setting xt logging disable to %d\n",
			__func__, dai_data->xt_logging_disable);
	}

	return 0;
}

static int msm_dai_q6_sb_xt_logging_disable_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data)
		ucontrol->value.integer.value[0] = dai_data->xt_logging_disable;
	return 0;
}

static int msm_dai_q6_sb_format_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	int value = ucontrol->value.integer.value[0];

	if (dai_data) {
		dai_data->port_config.slim_sch.data_format = value;
		pr_debug("%s: format = %d\n",  __func__, value);
	}

	return 0;
}

static int msm_dai_q6_sb_format_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data)
		ucontrol->value.integer.value[0] =
			dai_data->port_config.slim_sch.data_format;

	return 0;
}

static int msm_dai_q6_usb_audio_cfg_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u32 val = ucontrol->value.integer.value[0];

	if (dai_data) {
		dai_data->port_config.usb_audio.dev_token = val;
		pr_debug("%s: dev_token = 0x%x\n",  __func__,
				 dai_data->port_config.usb_audio.dev_token);
	} else {
		pr_err("%s: dai_data is NULL\n", __func__);
	}

	return 0;
}

static int msm_dai_q6_usb_audio_cfg_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		ucontrol->value.integer.value[0] =
			 dai_data->port_config.usb_audio.dev_token;
		pr_debug("%s: dev_token = 0x%x\n",  __func__,
				 dai_data->port_config.usb_audio.dev_token);
	} else {
		pr_err("%s: dai_data is NULL\n", __func__);
	}

	return 0;
}

static int msm_dai_q6_usb_audio_endian_cfg_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u32 val = ucontrol->value.integer.value[0];

	if (dai_data) {
		dai_data->port_config.usb_audio.endian = val;
		pr_debug("%s: endian = 0x%x\n",  __func__,
				 dai_data->port_config.usb_audio.endian);
	} else {
		pr_err("%s: dai_data is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int msm_dai_q6_usb_audio_endian_cfg_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		ucontrol->value.integer.value[0] =
			 dai_data->port_config.usb_audio.endian;
		pr_debug("%s: endian = 0x%x\n",  __func__,
				 dai_data->port_config.usb_audio.endian);
	} else {
		pr_err("%s: dai_data is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int msm_dai_q6_usb_audio_svc_interval_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u32 val = ucontrol->value.integer.value[0];

	if (!dai_data) {
		pr_err("%s: dai_data is NULL\n", __func__);
		return -EINVAL;
	}
	dai_data->port_config.usb_audio.service_interval = val;
	pr_debug("%s: new service interval = %u\n",  __func__,
		dai_data->port_config.usb_audio.service_interval);
	return 0;
}

static int msm_dai_q6_usb_audio_svc_interval_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (!dai_data) {
		pr_err("%s: dai_data is NULL\n", __func__);
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] =
		 dai_data->port_config.usb_audio.service_interval;
	pr_debug("%s: service interval = %d\n",  __func__,
		     dai_data->port_config.usb_audio.service_interval);
	return 0;
}

static int  msm_dai_q6_afe_enc_cfg_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(struct afe_enc_config);

	return 0;
}

static int msm_dai_q6_afe_enc_cfg_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		int format_size = sizeof(dai_data->enc_config.format);

		pr_debug("%s: encoder config for %d format\n",
			 __func__, dai_data->enc_config.format);
		memcpy(ucontrol->value.bytes.data,
			&dai_data->enc_config.format,
			format_size);
		switch (dai_data->enc_config.format) {
		case ENC_FMT_SBC:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_sbc_enc_cfg_t));
			break;
		case ENC_FMT_AAC_V2:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_aac_enc_cfg_t));
			break;
		case ENC_FMT_APTX:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_aptx_enc_cfg_t));
			break;
		case ENC_FMT_APTX_HD:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_custom_enc_cfg_t));
			break;
		case ENC_FMT_CELT:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_celt_enc_cfg_t));
			break;
		case ENC_FMT_LDAC:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_ldac_enc_cfg_t));
			break;
		case ENC_FMT_APTX_ADAPTIVE:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_aptx_ad_enc_cfg_t));
			break;
		case ENC_FMT_APTX_AD_SPEECH:
			memcpy(ucontrol->value.bytes.data + format_size,
				&dai_data->enc_config.data,
				sizeof(struct asm_aptx_ad_speech_enc_cfg_t));
			break;

		default:
			pr_debug("%s: unknown format = %d\n",
				 __func__, dai_data->enc_config.format);
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static int msm_dai_q6_afe_enc_cfg_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		int format_size = sizeof(dai_data->enc_config.format);

		memset(&dai_data->enc_config, 0x0,
			sizeof(struct afe_enc_config));
		memcpy(&dai_data->enc_config.format,
			ucontrol->value.bytes.data,
			format_size);
		pr_debug("%s: Received encoder config for %d format\n",
			 __func__, dai_data->enc_config.format);
		switch (dai_data->enc_config.format) {
		case ENC_FMT_SBC:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_sbc_enc_cfg_t));
			break;
		case ENC_FMT_AAC_V2:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_aac_enc_cfg_t));
			break;
		case ENC_FMT_APTX:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_aptx_enc_cfg_t));
			break;
		case ENC_FMT_APTX_HD:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_custom_enc_cfg_t));
			break;
		case ENC_FMT_CELT:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_celt_enc_cfg_t));
			break;
		case ENC_FMT_LDAC:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_ldac_enc_cfg_t));
			break;
		case ENC_FMT_APTX_ADAPTIVE:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_aptx_ad_enc_cfg_t));
			break;
		case ENC_FMT_APTX_AD_SPEECH:
			memcpy(&dai_data->enc_config.data,
				ucontrol->value.bytes.data + format_size,
				sizeof(struct asm_aptx_ad_speech_enc_cfg_t));
			break;

		default:
			pr_debug("%s: Ignore enc config for unknown format = %d\n",
				 __func__, dai_data->enc_config.format);
			ret = -EINVAL;
			break;
		}
	} else
		ret = -EINVAL;

	return ret;
}

static const char *const afe_chs_text[] = {"Zero", "One", "Two"};

static const struct soc_enum afe_chs_enum[] = {
	SOC_ENUM_SINGLE_EXT(3, afe_chs_text),
};

static const char *const afe_bit_format_text[] = {"S16_LE", "S24_LE",
							"S32_LE"};

static const struct soc_enum afe_bit_format_enum[] = {
	SOC_ENUM_SINGLE_EXT(3, afe_bit_format_text),
};

static const char *const tws_chs_mode_text[] = {"Zero", "One", "Two"};

static const struct soc_enum tws_chs_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(tws_chs_mode_text), tws_chs_mode_text),
};

static int msm_dai_q6_afe_input_channel_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		ucontrol->value.integer.value[0] = dai_data->afe_rx_in_channels;
		pr_debug("%s:afe input channel = %d\n",
			  __func__, dai_data->afe_rx_in_channels);
	}

	return 0;
}

static int msm_dai_q6_afe_input_channel_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		dai_data->afe_rx_in_channels = ucontrol->value.integer.value[0];
		pr_debug("%s: updating afe input channel : %d\n",
			__func__, dai_data->afe_rx_in_channels);
	}

	return 0;
}

static int msm_dai_q6_tws_channel_mode_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = kcontrol->private_data;
	struct msm_dai_q6_dai_data *dai_data = NULL;

	if (dai)
		dai_data = dev_get_drvdata(dai->dev);

	if (dai_data) {
		ucontrol->value.integer.value[0] =
				dai_data->enc_config.mono_mode;
		pr_debug("%s:tws channel mode = %d\n",
			 __func__, dai_data->enc_config.mono_mode);
	}

	return 0;
}

static int msm_dai_q6_tws_channel_mode_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = kcontrol->private_data;
	struct msm_dai_q6_dai_data *dai_data = NULL;
	int ret = 0;
	u32 format = 0;

	if (dai)
		dai_data = dev_get_drvdata(dai->dev);

	if (dai_data)
		format = dai_data->enc_config.format;
	else
		goto exit;

	if (format == ENC_FMT_APTX || format == ENC_FMT_APTX_ADAPTIVE) {
		if (test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
			ret = afe_set_tws_channel_mode(format,
				dai->id, ucontrol->value.integer.value[0]);
			if (ret < 0) {
				pr_err("%s: channel mode setting failed for TWS\n",
				__func__);
				goto exit;
			} else {
				pr_debug("%s: updating tws channel mode : %d\n",
				__func__, dai_data->enc_config.mono_mode);
			}
		}
		if (ucontrol->value.integer.value[0] ==
			MSM_DAI_TWS_CHANNEL_MODE_ONE ||
			ucontrol->value.integer.value[0] ==
			MSM_DAI_TWS_CHANNEL_MODE_TWO)
			dai_data->enc_config.mono_mode =
				ucontrol->value.integer.value[0];
		else
			return -EINVAL;
	}
exit:
	return ret;
}

static int msm_dai_q6_afe_input_bit_format_get(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}

	switch (dai_data->afe_rx_in_bitformat) {
	case SNDRV_PCM_FORMAT_S32_LE:
		ucontrol->value.integer.value[0] = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}
	pr_debug("%s: afe input bit format : %ld\n",
		  __func__, ucontrol->value.integer.value[0]);

	return 0;
}

static int msm_dai_q6_afe_input_bit_format_put(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		dai_data->afe_rx_in_bitformat = SNDRV_PCM_FORMAT_S32_LE;
		break;
	case 1:
		dai_data->afe_rx_in_bitformat = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		dai_data->afe_rx_in_bitformat = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	pr_debug("%s: updating afe input bit format : %d\n",
		__func__, dai_data->afe_rx_in_bitformat);

	return 0;
}

static int msm_dai_q6_afe_output_bit_format_get(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}

	switch (dai_data->afe_tx_out_bitformat) {
	case SNDRV_PCM_FORMAT_S32_LE:
		ucontrol->value.integer.value[0] = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}
	pr_debug("%s: afe output bit format : %ld\n",
		  __func__, ucontrol->value.integer.value[0]);

	return 0;
}

static int msm_dai_q6_afe_output_bit_format_put(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		dai_data->afe_tx_out_bitformat = SNDRV_PCM_FORMAT_S32_LE;
		break;
	case 1:
		dai_data->afe_tx_out_bitformat = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		dai_data->afe_tx_out_bitformat = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	pr_debug("%s: updating afe output bit format : %d\n",
		__func__, dai_data->afe_tx_out_bitformat);

	return 0;
}

static int msm_dai_q6_afe_output_channel_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		ucontrol->value.integer.value[0] =
			dai_data->afe_tx_out_channels;
		pr_debug("%s:afe output channel = %d\n",
			  __func__, dai_data->afe_tx_out_channels);
	}
	return 0;
}

static int msm_dai_q6_afe_output_channel_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (dai_data) {
		dai_data->afe_tx_out_channels =
			ucontrol->value.integer.value[0];
		pr_debug("%s: updating afe output channel : %d\n",
			__func__, dai_data->afe_tx_out_channels);
	}
	return 0;
}

static int msm_dai_q6_afe_scrambler_mode_get(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}
	ucontrol->value.integer.value[0] = dai_data->enc_config.scrambler_mode;

	return 0;
}

static int msm_dai_q6_afe_scrambler_mode_put(
			struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}
	dai_data->enc_config.scrambler_mode = ucontrol->value.integer.value[0];
	pr_debug("%s: afe scrambler mode : %d\n",
		  __func__, dai_data->enc_config.scrambler_mode);
	return 0;
}

static const struct snd_kcontrol_new afe_enc_config_controls[] = {
	{
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_INACTIVE),
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SLIM_7_RX Encoder Config",
		.info = msm_dai_q6_afe_enc_cfg_info,
		.get = msm_dai_q6_afe_enc_cfg_get,
		.put = msm_dai_q6_afe_enc_cfg_put,
	},
	SOC_ENUM_EXT("AFE Input Channels", afe_chs_enum[0],
		     msm_dai_q6_afe_input_channel_get,
		     msm_dai_q6_afe_input_channel_put),
	SOC_ENUM_EXT("AFE Input Bit Format", afe_bit_format_enum[0],
		     msm_dai_q6_afe_input_bit_format_get,
		     msm_dai_q6_afe_input_bit_format_put),
	SOC_SINGLE_EXT("AFE Scrambler Mode",
		       0, 0, 1, 0,
		       msm_dai_q6_afe_scrambler_mode_get,
		       msm_dai_q6_afe_scrambler_mode_put),
	SOC_ENUM_EXT("TWS Channel Mode", tws_chs_mode_enum[0],
		       msm_dai_q6_tws_channel_mode_get,
		       msm_dai_q6_tws_channel_mode_put),
	{
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_INACTIVE),
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SLIM_7_RX APTX_AD Enc Cfg",
		.info = msm_dai_q6_afe_enc_cfg_info,
		.get = msm_dai_q6_afe_enc_cfg_get,
		.put = msm_dai_q6_afe_enc_cfg_put,
	}
};

static int  msm_dai_q6_afe_dec_cfg_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(struct afe_dec_config);

	return 0;
}

static int msm_dai_q6_afe_feedback_dec_cfg_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u32 format_size = 0;
	u32 abr_size = 0;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}

	format_size = sizeof(dai_data->dec_config.format);
	memcpy(ucontrol->value.bytes.data,
		&dai_data->dec_config.format,
		format_size);

	pr_debug("%s: abr_dec_cfg for %d format\n",
			__func__, dai_data->dec_config.format);
	abr_size = sizeof(dai_data->dec_config.abr_dec_cfg.imc_info);
	memcpy(ucontrol->value.bytes.data + format_size,
		&dai_data->dec_config.abr_dec_cfg,
		sizeof(struct afe_imc_dec_enc_info));

	switch (dai_data->dec_config.format) {
	case DEC_FMT_APTX_AD_SPEECH:
		pr_debug("%s: afe_dec_cfg for %d format\n",
				__func__, dai_data->dec_config.format);
		memcpy(ucontrol->value.bytes.data + format_size + abr_size,
			&dai_data->dec_config.data,
			sizeof(struct asm_aptx_ad_speech_dec_cfg_t));
		break;
	default:
		pr_debug("%s: no afe_dec_cfg for format %d\n",
				__func__, dai_data->dec_config.format);
		break;
	}

	return 0;
}

static int msm_dai_q6_afe_feedback_dec_cfg_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u32 format_size = 0;
	u32 abr_size = 0;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}

	memset(&dai_data->dec_config, 0x0,
		sizeof(struct afe_dec_config));
	format_size = sizeof(dai_data->dec_config.format);
	memcpy(&dai_data->dec_config.format,
		ucontrol->value.bytes.data,
		format_size);

	pr_debug("%s: abr_dec_cfg for %d format\n",
			__func__, dai_data->dec_config.format);
	abr_size = sizeof(dai_data->dec_config.abr_dec_cfg.imc_info);
	memcpy(&dai_data->dec_config.abr_dec_cfg,
		ucontrol->value.bytes.data + format_size,
		sizeof(struct afe_imc_dec_enc_info));
	dai_data->dec_config.abr_dec_cfg.is_abr_enabled = true;

	switch (dai_data->dec_config.format) {
	case DEC_FMT_APTX_AD_SPEECH:
		pr_debug("%s: afe_dec_cfg for %d format\n",
				__func__, dai_data->dec_config.format);
		memcpy(&dai_data->dec_config.data,
			ucontrol->value.bytes.data + format_size + abr_size,
			sizeof(struct asm_aptx_ad_speech_dec_cfg_t));
		break;
	default:
		pr_debug("%s: no afe_dec_cfg for format %d\n",
				__func__, dai_data->dec_config.format);
		break;
	}
	return 0;
}

static int msm_dai_q6_afe_dec_cfg_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u32 format_size = 0;
	int ret = 0;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}

	format_size = sizeof(dai_data->dec_config.format);
	memcpy(ucontrol->value.bytes.data,
		&dai_data->dec_config.format,
		format_size);
	switch (dai_data->dec_config.format) {
	case DEC_FMT_AAC_V2:
		memcpy(ucontrol->value.bytes.data + format_size,
			&dai_data->dec_config.data,
			sizeof(struct asm_aac_dec_cfg_v2_t));
		break;
	case DEC_FMT_APTX_ADAPTIVE:
		memcpy(ucontrol->value.bytes.data + format_size,
			&dai_data->dec_config.data,
			sizeof(struct asm_aptx_ad_dec_cfg_t));
		break;
	case DEC_FMT_SBC:
	case DEC_FMT_MP3:
		/* No decoder specific data available */
		break;
	default:
		pr_err("%s: Invalid format %d\n",
				__func__, dai_data->dec_config.format);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int msm_dai_q6_afe_dec_cfg_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	u32 format_size = 0;
	int ret = 0;

	if (!dai_data) {
		pr_err("%s: Invalid dai data\n", __func__);
		return -EINVAL;
	}

	memset(&dai_data->dec_config, 0x0,
		sizeof(struct afe_dec_config));
	format_size = sizeof(dai_data->dec_config.format);
	memcpy(&dai_data->dec_config.format,
		ucontrol->value.bytes.data,
		format_size);
	pr_debug("%s: Received decoder config for %d format\n",
			__func__, dai_data->dec_config.format);
	switch (dai_data->dec_config.format) {
	case DEC_FMT_AAC_V2:
		memcpy(&dai_data->dec_config.data,
			ucontrol->value.bytes.data + format_size,
			sizeof(struct asm_aac_dec_cfg_v2_t));
		break;
	case DEC_FMT_SBC:
		memcpy(&dai_data->dec_config.data,
			ucontrol->value.bytes.data + format_size,
			sizeof(struct asm_sbc_dec_cfg_t));
		break;
	case DEC_FMT_APTX_ADAPTIVE:
		memcpy(&dai_data->dec_config.data,
			ucontrol->value.bytes.data + format_size,
			sizeof(struct asm_aptx_ad_dec_cfg_t));
		break;
	default:
		pr_err("%s: Invalid format %d\n",
				__func__, dai_data->dec_config.format);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct snd_kcontrol_new afe_dec_config_controls[] = {
	{
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_INACTIVE),
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SLIM_7_TX Decoder Config",
		.info = msm_dai_q6_afe_dec_cfg_info,
		.get = msm_dai_q6_afe_feedback_dec_cfg_get,
		.put = msm_dai_q6_afe_feedback_dec_cfg_put,
	},
	{
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_INACTIVE),
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "SLIM_9_TX Decoder Config",
		.info = msm_dai_q6_afe_dec_cfg_info,
		.get = msm_dai_q6_afe_dec_cfg_get,
		.put = msm_dai_q6_afe_dec_cfg_put,
	},
	SOC_ENUM_EXT("AFE Output Channels", afe_chs_enum[0],
		     msm_dai_q6_afe_output_channel_get,
		     msm_dai_q6_afe_output_channel_put),
	SOC_ENUM_EXT("AFE Output Bit Format", afe_bit_format_enum[0],
		     msm_dai_q6_afe_output_bit_format_get,
		     msm_dai_q6_afe_output_bit_format_put),
};

static int msm_dai_q6_slim_rx_drift_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(struct afe_param_id_dev_timing_stats);

	return 0;
}

static int msm_dai_q6_slim_rx_drift_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ret = -EINVAL;
	struct afe_param_id_dev_timing_stats timing_stats;
	struct snd_soc_dai *dai = kcontrol->private_data;
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	if (!test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		pr_debug("%s: afe port not started. dai_data->status_mask = %ld\n",
			__func__, *dai_data->status_mask);
		goto done;
	}

	memset(&timing_stats, 0, sizeof(struct afe_param_id_dev_timing_stats));
	ret = afe_get_av_dev_drift(&timing_stats, dai->id);
	if (ret) {
		pr_err("%s: Error getting AFE Drift for port %d, err=%d\n",
			__func__, dai->id, ret);

		goto done;
	}

	memcpy(ucontrol->value.bytes.data, (void *)&timing_stats,
			sizeof(struct afe_param_id_dev_timing_stats));
done:
	return ret;
}

static const char * const afe_cal_mode_text[] = {
	"CAL_MODE_DEFAULT", "CAL_MODE_NONE"
};

static const struct soc_enum slim_2_rx_enum =
	SOC_ENUM_SINGLE(SLIMBUS_2_RX, 0, ARRAY_SIZE(afe_cal_mode_text),
			afe_cal_mode_text);

static const struct soc_enum rt_proxy_1_rx_enum =
	SOC_ENUM_SINGLE(RT_PROXY_PORT_001_RX, 0, ARRAY_SIZE(afe_cal_mode_text),
			afe_cal_mode_text);

static const struct soc_enum rt_proxy_1_tx_enum =
	SOC_ENUM_SINGLE(RT_PROXY_PORT_001_TX, 0, ARRAY_SIZE(afe_cal_mode_text),
			afe_cal_mode_text);

static const struct snd_kcontrol_new sb_config_controls[] = {
	SOC_ENUM_EXT("SLIM_4_TX Format", sb_config_enum[0],
		     msm_dai_q6_sb_format_get,
		     msm_dai_q6_sb_format_put),
	SOC_ENUM_EXT("SLIM_2_RX SetCalMode", slim_2_rx_enum,
		     msm_dai_q6_cal_info_get,
		     msm_dai_q6_cal_info_put),
	SOC_ENUM_EXT("SLIM_2_RX Format", sb_config_enum[0],
		     msm_dai_q6_sb_format_get,
		     msm_dai_q6_sb_format_put),
	SOC_ENUM_EXT("SLIM_0_RX XTLoggingDisable", xt_logging_disable_enum[0],
		     msm_dai_q6_sb_xt_logging_disable_get,
		     msm_dai_q6_sb_xt_logging_disable_put),
};

static const struct snd_kcontrol_new rt_proxy_config_controls[] = {
	SOC_ENUM_EXT("RT_PROXY_1_RX SetCalMode", rt_proxy_1_rx_enum,
		     msm_dai_q6_cal_info_get,
		     msm_dai_q6_cal_info_put),
	SOC_ENUM_EXT("RT_PROXY_1_TX SetCalMode", rt_proxy_1_tx_enum,
		     msm_dai_q6_cal_info_get,
		     msm_dai_q6_cal_info_put),
};

static const struct snd_kcontrol_new usb_audio_cfg_controls[] = {
	SOC_SINGLE_EXT("USB_AUDIO_RX dev_token", 0, 0, UINT_MAX, 0,
			msm_dai_q6_usb_audio_cfg_get,
			msm_dai_q6_usb_audio_cfg_put),
	SOC_SINGLE_EXT("USB_AUDIO_RX endian", 0, 0, 1, 0,
			msm_dai_q6_usb_audio_endian_cfg_get,
			msm_dai_q6_usb_audio_endian_cfg_put),
	SOC_SINGLE_EXT("USB_AUDIO_TX dev_token", 0, 0, UINT_MAX, 0,
			msm_dai_q6_usb_audio_cfg_get,
			msm_dai_q6_usb_audio_cfg_put),
	SOC_SINGLE_EXT("USB_AUDIO_TX endian", 0, 0, 1, 0,
			msm_dai_q6_usb_audio_endian_cfg_get,
			msm_dai_q6_usb_audio_endian_cfg_put),
	SOC_SINGLE_EXT("USB_AUDIO_RX service_interval", SND_SOC_NOPM, 0,
			UINT_MAX, 0,
			msm_dai_q6_usb_audio_svc_interval_get,
			msm_dai_q6_usb_audio_svc_interval_put),
};

static const struct snd_kcontrol_new avd_drift_config_controls[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= "SLIMBUS_0_RX DRIFT",
		.info	= msm_dai_q6_slim_rx_drift_info,
		.get	= msm_dai_q6_slim_rx_drift_get,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= "SLIMBUS_6_RX DRIFT",
		.info	= msm_dai_q6_slim_rx_drift_info,
		.get	= msm_dai_q6_slim_rx_drift_get,
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface	= SNDRV_CTL_ELEM_IFACE_PCM,
		.name	= "SLIMBUS_7_RX DRIFT",
		.info	= msm_dai_q6_slim_rx_drift_info,
		.get	= msm_dai_q6_slim_rx_drift_get,
	},
};

static inline void msm_dai_q6_set_slim_dev_id(struct snd_soc_dai *dai)
{
	int rc = 0;
	int slim_dev_id = 0;
	const char *q6_slim_dev_id = "jlq,msm-dai-q6-slim-dev-id";
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);

	dai_data->port_config.slim_sch.slimbus_dev_id = AFE_SLIMBUS_DEVICE_1;

	rc = of_property_read_u32(dai->dev->of_node, q6_slim_dev_id,
				  &slim_dev_id);
	if (rc) {
		dev_dbg(dai->dev,
			"%s: missing %s in dt node\n", __func__, q6_slim_dev_id);
		return;
	}

	dev_dbg(dai->dev, "%s: slim_dev_id = %d\n", __func__, slim_dev_id);

	if (slim_dev_id >= AFE_SLIMBUS_DEVICE_1 &&
	    slim_dev_id <= AFE_SLIMBUS_DEVICE_2)
		dai_data->port_config.slim_sch.slimbus_dev_id = slim_dev_id;
}

static int msm_dai_q6_dai_probe(struct snd_soc_dai *dai)
{
	struct msm_dai_q6_dai_data *dai_data;
	int rc = 0;

	if (!dai) {
		pr_err("%s: Invalid params dai\n", __func__);
		return -EINVAL;
	}
	if (!dai->dev) {
		pr_err("%s: Invalid params dai dev\n", __func__);
		return -EINVAL;
	}

	dai_data = kzalloc(sizeof(struct msm_dai_q6_dai_data), GFP_KERNEL);
	if (!dai_data)
		return -ENOMEM;
	else
		dev_set_drvdata(dai->dev, dai_data);

	dai_data->interleave = INTERLEAVE_EN;

	msm_dai_q6_set_dai_id(dai);

	if ((dai->id >= SLIMBUS_0_RX) && (dai->id <= SLIMBUS_9_TX))
		msm_dai_q6_set_slim_dev_id(dai);

	switch (dai->id) {
	case SLIMBUS_4_TX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&sb_config_controls[0],
				 dai_data));
		break;
	case SLIMBUS_2_RX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&sb_config_controls[1],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&sb_config_controls[2],
				 dai_data));
		break;
	case SLIMBUS_7_RX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_enc_config_controls[0],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_enc_config_controls[1],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_enc_config_controls[2],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_enc_config_controls[3],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_enc_config_controls[4],
				 dai));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_enc_config_controls[5],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				snd_ctl_new1(&avd_drift_config_controls[2],
					dai));
		break;
	case SLIMBUS_7_TX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_dec_config_controls[0],
				 dai_data));
		break;
	case SLIMBUS_9_TX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_dec_config_controls[1],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_dec_config_controls[2],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&afe_dec_config_controls[3],
				 dai_data));
		break;
	case RT_PROXY_DAI_001_RX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&rt_proxy_config_controls[0],
				 dai_data));
		break;
	case RT_PROXY_DAI_001_TX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&rt_proxy_config_controls[1],
				 dai_data));
		break;
	case AFE_PORT_ID_USB_RX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&usb_audio_cfg_controls[0],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&usb_audio_cfg_controls[1],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&usb_audio_cfg_controls[4],
				 dai_data));
		break;
	case AFE_PORT_ID_USB_TX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&usb_audio_cfg_controls[2],
				 dai_data));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&usb_audio_cfg_controls[3],
				 dai_data));
		break;
	case SLIMBUS_0_RX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				snd_ctl_new1(&avd_drift_config_controls[0],
					dai));
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&sb_config_controls[3],
				 dai_data));
		break;
	case SLIMBUS_6_RX:
		rc = snd_ctl_add(dai->component->card->snd_card,
				snd_ctl_new1(&avd_drift_config_controls[1],
					dai));
		break;
	}
	if (rc < 0)
		dev_err(dai->dev, "%s: err add config ctl, DAI = %s\n",
			__func__, dai->name);

	rc = msm_dai_q6_dai_add_route(dai);
	return rc;
}

static int msm_dai_q6_dai_remove(struct snd_soc_dai *dai)
{
	struct msm_dai_q6_dai_data *dai_data;
	int rc;

	dai_data = dev_get_drvdata(dai->dev);

	/* If AFE port is still up, close it */
	if (test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		pr_debug("%s: stop pseudo port:%d\n", __func__,  dai->id);
		rc = afe_close(dai->id); /* can block */
		if (rc < 0)
			dev_err(dai->dev, "fail to close AFE port\n");
		clear_bit(STATUS_PORT_STARTED, dai_data->status_mask);
	}
	kfree(dai_data);

	return 0;
}

static struct snd_soc_dai_driver msm_dai_q6_afe_rx_dai[] = {
	{
		.playback = {
			.stream_name = "AFE Playback",
			.aif_name = "PCM_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =	48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = RT_PROXY_DAI_001_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			 .stream_name = "AFE-PROXY RX",
			 .rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			 SNDRV_PCM_RATE_16000,
			 .formats = SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE,
			 .channels_min = 1,
			 .channels_max = 2,
			 .rate_min =     8000,
			 .rate_max =	48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = RT_PROXY_DAI_002_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
};

static struct snd_soc_dai_driver msm_dai_q6_afe_lb_tx_dai[] = {
	{
		.capture = {
			.stream_name = "AFE Loopback Capture",
			.aif_name = "AFE_LOOPBACK_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
			 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
			 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
			 SNDRV_PCM_RATE_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_3LE |
			SNDRV_PCM_FMTBIT_S32_LE ),
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.id = AFE_LOOPBACK_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
};

static struct snd_soc_dai_driver msm_dai_q6_afe_tx_dai[] = {
	{
		.capture = {
			.stream_name = "AFE Capture",
			.aif_name = "PCM_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = RT_PROXY_DAI_002_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "AFE-PROXY TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = RT_PROXY_DAI_001_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
};

static struct snd_soc_dai_driver msm_dai_q6_bt_sco_rx_dai = {
	.playback = {
		.stream_name = "Internal BT-SCO Playback",
		.aif_name = "INT_BT_SCO_RX",
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min = 1,
		.channels_max = 1,
		.rate_max = 16000,
		.rate_min = 8000,
	},
	.ops = &msm_dai_q6_ops,
	.id = INT_BT_SCO_RX,
	.probe = msm_dai_q6_dai_probe,
	.remove = msm_dai_q6_dai_remove,
};

static struct snd_soc_dai_driver msm_dai_q6_bt_a2dp_rx_dai = {
	.playback = {
		.stream_name = "Internal BT-A2DP Playback",
		.aif_name = "INT_BT_A2DP_RX",
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min = 1,
		.channels_max = 2,
		.rate_max = 48000,
		.rate_min = 48000,
	},
	.ops = &msm_dai_q6_ops,
	.id = INT_BT_A2DP_RX,
	.probe = msm_dai_q6_dai_probe,
	.remove = msm_dai_q6_dai_remove,
};

static struct snd_soc_dai_driver msm_dai_q6_bt_sco_tx_dai = {
	.capture = {
		.stream_name = "Internal BT-SCO Capture",
		.aif_name = "INT_BT_SCO_TX",
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min = 1,
		.channels_max = 1,
		.rate_max = 16000,
		.rate_min = 8000,
	},
	.ops = &msm_dai_q6_ops,
	.id = INT_BT_SCO_TX,
	.probe = msm_dai_q6_dai_probe,
	.remove = msm_dai_q6_dai_remove,
};

static struct snd_soc_dai_driver msm_dai_q6_fm_rx_dai = {
	.playback = {
		.stream_name = "Internal FM Playback",
		.aif_name = "INT_FM_RX",
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
		SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_max = 48000,
		.rate_min = 8000,
	},
	.ops = &msm_dai_q6_ops,
	.id = INT_FM_RX,
	.probe = msm_dai_q6_dai_probe,
	.remove = msm_dai_q6_dai_remove,
};

static struct snd_soc_dai_driver msm_dai_q6_fm_tx_dai = {
	.capture = {
		.stream_name = "Internal FM Capture",
		.aif_name = "INT_FM_TX",
		.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
		SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_max = 48000,
		.rate_min = 8000,
	},
	.ops = &msm_dai_q6_ops,
	.id = INT_FM_TX,
	.probe = msm_dai_q6_dai_probe,
	.remove = msm_dai_q6_dai_remove,
};

static struct snd_soc_dai_driver msm_dai_q6_voc_playback_dai[] = {
	{
		.playback = {
			.stream_name = "Voice Farend Playback",
			.aif_name = "VOICE_PLAYBACK_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = VOICE_PLAYBACK_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Voice2 Farend Playback",
			.aif_name = "VOICE2_PLAYBACK_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
				 SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = VOICE2_PLAYBACK_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
};

static struct snd_soc_dai_driver msm_dai_q6_incall_record_dai[] = {
	{
		.capture = {
			.stream_name = "Voice Uplink Capture",
			.aif_name = "INCALL_RECORD_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = VOICE_RECORD_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Voice Downlink Capture",
			.aif_name = "INCALL_RECORD_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_ops,
		.id = VOICE_RECORD_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
};

static struct snd_soc_dai_driver msm_dai_q6_usb_rx_dai = {
	.playback = {
		.stream_name = "USB Audio Playback",
		.aif_name = "USB_AUDIO_RX",
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
			 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
			 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			 SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
			 SNDRV_PCM_RATE_384000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 1,
		.channels_max = 8,
		.rate_max = 384000,
		.rate_min = 8000,
	},
	.ops = &msm_dai_q6_ops,
	.id = AFE_PORT_ID_USB_RX,
	.probe = msm_dai_q6_dai_probe,
	.remove = msm_dai_q6_dai_remove,
};

static struct snd_soc_dai_driver msm_dai_q6_usb_tx_dai = {
	.capture = {
		.stream_name = "USB Audio Capture",
		.aif_name = "USB_AUDIO_TX",
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
			 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
			 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			 SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
			 SNDRV_PCM_RATE_384000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 1,
		.channels_max = 8,
		.rate_max = 384000,
		.rate_min = 8000,
	},
	.ops = &msm_dai_q6_ops,
	.id = AFE_PORT_ID_USB_TX,
	.probe = msm_dai_q6_dai_probe,
	.remove = msm_dai_q6_dai_remove,
};

static struct snd_soc_dai_driver msm_dai_q6_slimbus_rx_dai[] = {
	{
		.playback = {
			.stream_name = "Slimbus Playback",
			.aif_name = "SLIMBUS_0_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_slimbus_0_rx_ops,
		.id = SLIMBUS_0_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus1 Playback",
			.aif_name = "SLIMBUS_1_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_1_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus2 Playback",
			.aif_name = "SLIMBUS_2_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_2_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus3 Playback",
			.aif_name = "SLIMBUS_3_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_3_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus4 Playback",
			.aif_name = "SLIMBUS_4_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_4_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus6 Playback",
			.aif_name = "SLIMBUS_6_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_6_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus5 Playback",
			.aif_name = "SLIMBUS_5_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_5_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus7 Playback",
			.aif_name = "SLIMBUS_7_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_7_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus8 Playback",
			.aif_name = "SLIMBUS_8_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_8_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.playback = {
			.stream_name = "Slimbus9 Playback",
			.aif_name = "SLIMBUS_9_RX",
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = DAI_FORMATS_S16_S24_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_9_RX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
};

static struct snd_soc_dai_driver msm_dai_q6_slimbus_tx_dai[] = {
	{
		.capture = {
			.stream_name = "Slimbus Capture",
			.aif_name = "SLIMBUS_0_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_0_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus1 Capture",
			.aif_name = "SLIMBUS_1_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_1_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus2 Capture",
			.aif_name = "SLIMBUS_2_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_2_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus3 Capture",
			.aif_name = "SLIMBUS_3_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 2,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_3_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus4 Capture",
			.aif_name = "SLIMBUS_4_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 2,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_4_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus5 Capture",
			.aif_name = "SLIMBUS_5_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_5_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus6 Capture",
			.aif_name = "SLIMBUS_6_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_6_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus7 Capture",
			.aif_name = "SLIMBUS_7_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_7_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus8 Capture",
			.aif_name = "SLIMBUS_8_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_8_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
	{
		.capture = {
			.stream_name = "Slimbus9 Capture",
			.aif_name = "SLIMBUS_9_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 192000,
		},
		.ops = &msm_dai_q6_ops,
		.id = SLIMBUS_9_TX,
		.probe = msm_dai_q6_dai_probe,
		.remove = msm_dai_q6_dai_remove,
	},
};

static int msm_dai_q6_mi2s_format_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	int value = ucontrol->value.integer.value[0];

	dai_data->port_config.i2s.data_format = value;
	pr_debug("%s: value = %d, channel = %d, line = %d\n",
		 __func__, value, dai_data->port_config.i2s.mono_stereo,
		 dai_data->port_config.i2s.channel_mode);
	return 0;
}

static int msm_dai_q6_mi2s_format_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	ucontrol->value.integer.value[0] =
		dai_data->port_config.i2s.data_format;
	return 0;
}

static int msm_dai_q6_mi2s_vi_feed_mono_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;
	int value = ucontrol->value.integer.value[0];

	dai_data->vi_feed_mono = value;
	pr_debug("%s: value = %d\n", __func__, value);
	return 0;
}

static int msm_dai_q6_mi2s_vi_feed_mono_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_dai_data *dai_data = kcontrol->private_data;

	ucontrol->value.integer.value[0] = dai_data->vi_feed_mono;
	return 0;
}

static const struct snd_kcontrol_new mi2s_config_controls[] = {
	SOC_ENUM_EXT("PRI MI2S RX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("SEC MI2S RX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("TERT MI2S RX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("QUAT MI2S RX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("QUIN MI2S RX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("SENARY MI2S RX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("PRI MI2S TX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("SEC MI2S TX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("TERT MI2S TX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("QUAT MI2S TX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("QUIN MI2S TX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("SENARY MI2S TX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
	SOC_ENUM_EXT("INT5 MI2S TX Format", mi2s_config_enum[0],
		     msm_dai_q6_mi2s_format_get,
		     msm_dai_q6_mi2s_format_put),
};

static const struct snd_kcontrol_new mi2s_vi_feed_controls[] = {
	SOC_ENUM_EXT("INT5 MI2S VI MONO", mi2s_config_enum[1],
		     msm_dai_q6_mi2s_vi_feed_mono_get,
		     msm_dai_q6_mi2s_vi_feed_mono_put),
};

static int msm_dai_q6_dai_mi2s_probe(struct snd_soc_dai *dai)
{
	struct msm_dai_q6_mi2s_dai_data *mi2s_dai_data =
			dev_get_drvdata(dai->dev);
	struct msm_mi2s_pdata *mi2s_pdata =
			(struct msm_mi2s_pdata *) dai->dev->platform_data;
	struct snd_kcontrol *kcontrol = NULL;
	int rc = 0;
	const struct snd_kcontrol_new *ctrl = NULL;
	const struct snd_kcontrol_new *vi_feed_ctrl = NULL;
	u16 dai_id = 0;

	dai->id = mi2s_pdata->intf_id;

	if (mi2s_dai_data->rx_dai.mi2s_dai_data.port_config.i2s.channel_mode) {
		if (dai->id == MSM_PRIM_MI2S)
			ctrl = &mi2s_config_controls[0];
		if (dai->id == MSM_SEC_MI2S)
			ctrl = &mi2s_config_controls[1];
		if (dai->id == MSM_TERT_MI2S)
			ctrl = &mi2s_config_controls[2];
		if (dai->id == MSM_QUAT_MI2S)
			ctrl = &mi2s_config_controls[3];
		if (dai->id == MSM_QUIN_MI2S)
			ctrl = &mi2s_config_controls[4];
		if (dai->id == MSM_SENARY_MI2S)
			ctrl = &mi2s_config_controls[5];
	}

	if (ctrl) {
		kcontrol = snd_ctl_new1(ctrl,
					&mi2s_dai_data->rx_dai.mi2s_dai_data);
		rc = snd_ctl_add(dai->component->card->snd_card, kcontrol);
		if (rc < 0) {
			dev_err(dai->dev, "%s: err add RX fmt ctl DAI = %s\n",
				__func__, dai->name);
			goto rtn;
		}
	}

	ctrl = NULL;
	if (mi2s_dai_data->tx_dai.mi2s_dai_data.port_config.i2s.channel_mode) {
		if (dai->id == MSM_PRIM_MI2S)
			ctrl = &mi2s_config_controls[6];
		if (dai->id == MSM_SEC_MI2S)
			ctrl = &mi2s_config_controls[7];
		if (dai->id == MSM_TERT_MI2S)
			ctrl = &mi2s_config_controls[8];
		if (dai->id == MSM_QUAT_MI2S)
			ctrl = &mi2s_config_controls[9];
		if (dai->id == MSM_QUIN_MI2S)
			ctrl = &mi2s_config_controls[10];
		if (dai->id == MSM_SENARY_MI2S)
			ctrl = &mi2s_config_controls[11];
		if (dai->id == MSM_INT5_MI2S)
			ctrl = &mi2s_config_controls[12];
	}

	if (ctrl) {
		rc = snd_ctl_add(dai->component->card->snd_card,
				snd_ctl_new1(ctrl,
				&mi2s_dai_data->tx_dai.mi2s_dai_data));
		if (rc < 0) {
			if (kcontrol)
				snd_ctl_remove(dai->component->card->snd_card,
						kcontrol);
			dev_err(dai->dev, "%s: err add TX fmt ctl DAI = %s\n",
				__func__, dai->name);
		}
	}

	if (dai->id == MSM_INT5_MI2S)
		vi_feed_ctrl = &mi2s_vi_feed_controls[0];

	if (vi_feed_ctrl) {
		rc = snd_ctl_add(dai->component->card->snd_card,
				snd_ctl_new1(vi_feed_ctrl,
				&mi2s_dai_data->tx_dai.mi2s_dai_data));

		if (rc < 0) {
			dev_err(dai->dev, "%s: err add TX vi feed channel ctl DAI = %s\n",
				__func__, dai->name);
		}
	}

	if (mi2s_dai_data->is_island_dai) {
		msm_mi2s_get_port_id(dai->id, SNDRV_PCM_STREAM_CAPTURE,
				     &dai_id);
		rc = msm_dai_q6_add_island_mx_ctls(
						dai->component->card->snd_card,
						dai->name, dai_id,
						(void *)mi2s_dai_data);
	}

	rc = msm_dai_q6_dai_add_route(dai);
rtn:
	return rc;
}


static int msm_dai_q6_dai_mi2s_remove(struct snd_soc_dai *dai)
{
	struct msm_dai_q6_mi2s_dai_data *mi2s_dai_data =
		dev_get_drvdata(dai->dev);
	int rc;

	/* If AFE port is still up, close it */
	if (test_bit(STATUS_PORT_STARTED,
		     mi2s_dai_data->rx_dai.mi2s_dai_data.status_mask)) {
		rc = afe_close(MI2S_RX); /* can block */
		if (rc < 0)
			dev_err(dai->dev, "fail to close MI2S_RX port\n");
		clear_bit(STATUS_PORT_STARTED,
			  mi2s_dai_data->rx_dai.mi2s_dai_data.status_mask);
	}
	if (test_bit(STATUS_PORT_STARTED,
		     mi2s_dai_data->tx_dai.mi2s_dai_data.status_mask)) {
		rc = afe_close(MI2S_TX); /* can block */
		if (rc < 0)
			dev_err(dai->dev, "fail to close MI2S_TX port\n");
		clear_bit(STATUS_PORT_STARTED,
			  mi2s_dai_data->tx_dai.mi2s_dai_data.status_mask);
	}
	return 0;
}

static int msm_dai_q6_mi2s_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{

	return 0;
}

static int msm_mi2s_get_port_id(u32 mi2s_id, int stream, u16 *port_id)
{
	int ret = 0;

	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		switch (mi2s_id) {
		case MSM_PRIM_MI2S:
			*port_id = AFE_PORT_ID_PRIMARY_MI2S_RX;
			break;
		case MSM_SEC_MI2S:
			*port_id = AFE_PORT_ID_SECONDARY_MI2S_RX;
			break;
		case MSM_TERT_MI2S:
			*port_id = AFE_PORT_ID_TERTIARY_MI2S_RX;
			break;
		case MSM_QUAT_MI2S:
			*port_id = AFE_PORT_ID_QUATERNARY_MI2S_RX;
			break;
		case MSM_SEC_MI2S_SD1:
			*port_id = AFE_PORT_ID_SECONDARY_MI2S_RX_SD1;
			break;
		case MSM_QUIN_MI2S:
			*port_id = AFE_PORT_ID_QUINARY_MI2S_RX;
			break;
		case MSM_SENARY_MI2S:
			*port_id = AFE_PORT_ID_SENARY_MI2S_RX;
			break;
		case MSM_INT0_MI2S:
			*port_id = AFE_PORT_ID_INT0_MI2S_RX;
			break;
		case MSM_INT1_MI2S:
			*port_id = AFE_PORT_ID_INT1_MI2S_RX;
			break;
		case MSM_INT2_MI2S:
			*port_id = AFE_PORT_ID_INT2_MI2S_RX;
			break;
		case MSM_INT3_MI2S:
			*port_id = AFE_PORT_ID_INT3_MI2S_RX;
			break;
		case MSM_INT4_MI2S:
			*port_id = AFE_PORT_ID_INT4_MI2S_RX;
			break;
		case MSM_INT5_MI2S:
			*port_id = AFE_PORT_ID_INT5_MI2S_RX;
			break;
		case MSM_INT6_MI2S:
			*port_id = AFE_PORT_ID_INT6_MI2S_RX;
			break;
		default:
			pr_err("%s: playback err id 0x%x\n",
				__func__, mi2s_id);
			ret = -1;
			break;
		}
	break;
	case SNDRV_PCM_STREAM_CAPTURE:
		switch (mi2s_id) {
		case MSM_PRIM_MI2S:
			*port_id = AFE_PORT_ID_PRIMARY_MI2S_TX;
			break;
		case MSM_SEC_MI2S:
			*port_id = AFE_PORT_ID_SECONDARY_MI2S_TX;
			break;
		case MSM_TERT_MI2S:
			*port_id = AFE_PORT_ID_TERTIARY_MI2S_TX;
			break;
		case MSM_QUAT_MI2S:
			*port_id = AFE_PORT_ID_QUATERNARY_MI2S_TX;
			break;
		case MSM_QUIN_MI2S:
			*port_id = AFE_PORT_ID_QUINARY_MI2S_TX;
			break;
		case MSM_SENARY_MI2S:
			*port_id = AFE_PORT_ID_SENARY_MI2S_TX;
			break;
		case MSM_INT0_MI2S:
			*port_id = AFE_PORT_ID_INT0_MI2S_TX;
			break;
		case MSM_INT1_MI2S:
			*port_id = AFE_PORT_ID_INT1_MI2S_TX;
			break;
		case MSM_INT2_MI2S:
			*port_id = AFE_PORT_ID_INT2_MI2S_TX;
			break;
		case MSM_INT3_MI2S:
			*port_id = AFE_PORT_ID_INT3_MI2S_TX;
			break;
		case MSM_INT4_MI2S:
			*port_id = AFE_PORT_ID_INT4_MI2S_TX;
			break;
		case MSM_INT5_MI2S:
			*port_id = AFE_PORT_ID_INT5_MI2S_TX;
			break;
		case MSM_INT6_MI2S:
			*port_id = AFE_PORT_ID_INT6_MI2S_TX;
			break;
		default:
			pr_err("%s: capture err id 0x%x\n", __func__, mi2s_id);
			ret = -1;
			break;
		}
	break;
	default:
		pr_err("%s: default err %d\n", __func__, stream);
		ret = -1;
	break;
	}
	pr_debug("%s: port_id = 0x%x\n", __func__, *port_id);
	return ret;
}

static int msm_dai_q6_mi2s_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct msm_dai_q6_mi2s_dai_data *mi2s_dai_data =
		dev_get_drvdata(dai->dev);
	struct msm_dai_q6_dai_data *dai_data =
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		 &mi2s_dai_data->rx_dai.mi2s_dai_data :
		 &mi2s_dai_data->tx_dai.mi2s_dai_data);
	u16 port_id = 0;
	int rc = 0;

	if (msm_mi2s_get_port_id(dai->id, substream->stream,
				 &port_id) != 0) {
		dev_err(dai->dev, "%s: Invalid Port ID 0x%x\n",
				__func__, port_id);
		return -EINVAL;
	}

	dev_dbg(dai->dev, "%s: dai id %d, afe port id = 0x%x\n"
		"dai_data->channels = %u sample_rate = %u\n", __func__,
		dai->id, port_id, dai_data->channels, dai_data->rate);

	if (!test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		/* PORT START should be set if prepare called
		 * in active state.
		 */
		rc = afe_port_start(port_id, &dai_data->port_config,
				    dai_data->rate);
		if (rc < 0)
			dev_err(dai->dev, "fail to open AFE port 0x%x\n",
				dai->id);
		else
			set_bit(STATUS_PORT_STARTED,
				dai_data->status_mask);
	}
	if (!test_bit(STATUS_PORT_STARTED, dai_data->hwfree_status)) {
		set_bit(STATUS_PORT_STARTED, dai_data->hwfree_status);
		dev_dbg(dai->dev, "%s: set hwfree_status to started\n",
				__func__);
	}
	return rc;
}

static int msm_dai_q6_mi2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
    int rc = 0;
	struct msm_dai_q6_mi2s_dai_data *mi2s_dai_data =
		dev_get_drvdata(dai->dev);
	struct msm_dai_q6_mi2s_dai_config *mi2s_dai_config =
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		&mi2s_dai_data->rx_dai : &mi2s_dai_data->tx_dai);
	struct msm_dai_q6_dai_data *dai_data = &mi2s_dai_config->mi2s_dai_data;
	struct afe_param_id_i2s_cfg *i2s = &dai_data->port_config.i2s;
	struct afe_device_config config;

	dai_data->channels = params_channels(params);
	switch (dai_data->channels) {
	case 15:
	case 16:
		switch (mi2s_dai_config->pdata_mi2s_lines) {
		case AFE_PORT_I2S_16CHS:
			dai_data->port_config.i2s.channel_mode
				= AFE_PORT_I2S_16CHS;
			break;
		default:
			goto error_invalid_data;
		};
		break;
	case 13:
	case 14:
		switch (mi2s_dai_config->pdata_mi2s_lines) {
		case AFE_PORT_I2S_14CHS:
		case AFE_PORT_I2S_16CHS:
			dai_data->port_config.i2s.channel_mode
				= AFE_PORT_I2S_14CHS;
			break;
		default:
			goto error_invalid_data;
		};
		break;
	case 11:
	case 12:
		switch (mi2s_dai_config->pdata_mi2s_lines) {
		case AFE_PORT_I2S_12CHS:
		case AFE_PORT_I2S_14CHS:
		case AFE_PORT_I2S_16CHS:
			dai_data->port_config.i2s.channel_mode
				= AFE_PORT_I2S_12CHS;
			break;
		default:
			goto error_invalid_data;
		};
		break;
	case 9:
	case 10:
		switch (mi2s_dai_config->pdata_mi2s_lines) {
		case AFE_PORT_I2S_10CHS:
		case AFE_PORT_I2S_12CHS:
		case AFE_PORT_I2S_14CHS:
		case AFE_PORT_I2S_16CHS:
			dai_data->port_config.i2s.channel_mode
				= AFE_PORT_I2S_10CHS;
			break;
		default:
			goto error_invalid_data;
		};
		break;
	case 8:
	case 7:
		if (mi2s_dai_config->pdata_mi2s_lines < AFE_PORT_I2S_8CHS)
			goto error_invalid_data;
		else
			if (mi2s_dai_config->pdata_mi2s_lines
					== AFE_PORT_I2S_8CHS_2)
				dai_data->port_config.i2s.channel_mode =
						AFE_PORT_I2S_8CHS_2;
			else
				dai_data->port_config.i2s.channel_mode =
						AFE_PORT_I2S_8CHS;
		break;
	case 6:
	case 5:
		if (mi2s_dai_config->pdata_mi2s_lines < AFE_PORT_I2S_6CHS)
			goto error_invalid_data;
		dai_data->port_config.i2s.channel_mode = AFE_PORT_I2S_6CHS;
		break;
	case 4:
	case 3:
		switch (mi2s_dai_config->pdata_mi2s_lines) {
		case AFE_PORT_I2S_SD0:
		case AFE_PORT_I2S_SD1:
		case AFE_PORT_I2S_SD2:
		case AFE_PORT_I2S_SD3:
		case AFE_PORT_I2S_SD4:
		case AFE_PORT_I2S_SD5:
		case AFE_PORT_I2S_SD6:
		case AFE_PORT_I2S_SD7:
			goto error_invalid_data;
			break;
		case AFE_PORT_I2S_QUAD01:
		case AFE_PORT_I2S_QUAD23:
		case AFE_PORT_I2S_QUAD45:
		case AFE_PORT_I2S_QUAD67:
			dai_data->port_config.i2s.channel_mode =
				mi2s_dai_config->pdata_mi2s_lines;
			break;
		case AFE_PORT_I2S_8CHS_2:
			dai_data->port_config.i2s.channel_mode =
					AFE_PORT_I2S_QUAD45;
			break;
		default:
			dai_data->port_config.i2s.channel_mode =
					AFE_PORT_I2S_QUAD01;
			break;
		};
		break;
	case 2:
	case 1:
		if (mi2s_dai_config->pdata_mi2s_lines < AFE_PORT_I2S_SD0)
			goto error_invalid_data;
		switch (mi2s_dai_config->pdata_mi2s_lines) {
		case AFE_PORT_I2S_SD0:
		case AFE_PORT_I2S_SD1:
		case AFE_PORT_I2S_SD2:
		case AFE_PORT_I2S_SD3:
		case AFE_PORT_I2S_SD4:
		case AFE_PORT_I2S_SD5:
		case AFE_PORT_I2S_SD6:
		case AFE_PORT_I2S_SD7:
			dai_data->port_config.i2s.channel_mode =
				mi2s_dai_config->pdata_mi2s_lines;
			break;
		case AFE_PORT_I2S_QUAD01:
		case AFE_PORT_I2S_6CHS:
		case AFE_PORT_I2S_8CHS:
		case AFE_PORT_I2S_10CHS:
		case AFE_PORT_I2S_12CHS:
		case AFE_PORT_I2S_14CHS:
		case AFE_PORT_I2S_16CHS:
			if (dai_data->vi_feed_mono == SPKR_1)
				dai_data->port_config.i2s.channel_mode =
							AFE_PORT_I2S_SD0;
			else
				dai_data->port_config.i2s.channel_mode =
							AFE_PORT_I2S_SD1;
			break;
		case AFE_PORT_I2S_QUAD23:
			dai_data->port_config.i2s.channel_mode =
						AFE_PORT_I2S_SD2;
			break;
		case AFE_PORT_I2S_QUAD45:
			dai_data->port_config.i2s.channel_mode =
						AFE_PORT_I2S_SD4;
			break;
		case AFE_PORT_I2S_QUAD67:
			dai_data->port_config.i2s.channel_mode =
						AFE_PORT_I2S_SD6;
			break;
		}
		if (dai_data->channels == 2)
			dai_data->port_config.i2s.mono_stereo =
						MSM_AFE_CH_STEREO;
		else
			dai_data->port_config.i2s.mono_stereo = MSM_AFE_MONO;
		break;
	default:
		pr_err("%s: default err channels %d\n",
			__func__, dai_data->channels);
		goto error_invalid_data;
	}
	dai_data->rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_SPECIAL:
		dai_data->port_config.i2s.bit_width = 16;
		dai_data->bitwidth = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		dai_data->port_config.i2s.bit_width = 24;
		dai_data->bitwidth = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dai_data->port_config.i2s.bit_width = 32;
		dai_data->bitwidth = 32;
		break;
	default:
		pr_err("%s: format %d\n",
			__func__, params_format(params));
		return -EINVAL;
	}

	dai_data->port_config.i2s.i2s_cfg_minor_version =
			AFE_API_VERSION_I2S_CONFIG;
	dai_data->port_config.i2s.sample_rate = dai_data->rate;
	if ((test_bit(STATUS_PORT_STARTED,
	    mi2s_dai_data->rx_dai.mi2s_dai_data.status_mask) &&
	    test_bit(STATUS_PORT_STARTED,
	    mi2s_dai_data->rx_dai.mi2s_dai_data.hwfree_status)) ||
	    (test_bit(STATUS_PORT_STARTED,
	    mi2s_dai_data->tx_dai.mi2s_dai_data.status_mask) &&
	    test_bit(STATUS_PORT_STARTED,
	    mi2s_dai_data->tx_dai.mi2s_dai_data.hwfree_status))) {
		if ((mi2s_dai_data->tx_dai.mi2s_dai_data.rate !=
		    mi2s_dai_data->rx_dai.mi2s_dai_data.rate) ||
		   (mi2s_dai_data->rx_dai.mi2s_dai_data.bitwidth !=
		    mi2s_dai_data->tx_dai.mi2s_dai_data.bitwidth)) {
			dev_err(dai->dev, "%s: Error mismatch in HW params\n"
				"Tx sample_rate = %u bit_width = %hu\n"
				"Rx sample_rate = %u bit_width = %hu\n"
				, __func__,
				mi2s_dai_data->tx_dai.mi2s_dai_data.rate,
				mi2s_dai_data->tx_dai.mi2s_dai_data.bitwidth,
				mi2s_dai_data->rx_dai.mi2s_dai_data.rate,
				mi2s_dai_data->rx_dai.mi2s_dai_data.bitwidth);
			return -EINVAL;
		}
	}
	dev_dbg(dai->dev, "%s: dai id %d dai_data->channels = %d\n"
		"sample_rate = %u i2s_cfg_minor_version = 0x%x\n"
		"bit_width = %hu  channel_mode = 0x%x mono_stereo = %#x\n"
		"ws_src = 0x%x sample_rate = %u data_format = 0x%x\n"
		"reserved = %u\n", __func__, dai->id, dai_data->channels,
		dai_data->rate, i2s->i2s_cfg_minor_version, i2s->bit_width,
		i2s->channel_mode, i2s->mono_stereo, i2s->ws_src,
		i2s->sample_rate, i2s->data_format, i2s->reserved);

    config.bit_width = dai_data->bitwidth;
    config.channel_num = dai_data->channels;
    config.sample_rate = dai_data->rate;
    config.interleave = dai_data->interleave;
	rc = afe_port_open(dai->id, &config);

	if (rc < 0)
		dev_err(dai->dev, "fail to open AFE port 0x%x\n",
			dai->id);

	return 0;

error_invalid_data:
	pr_err("%s: dai_data->channels = %d channel_mode = %d\n", __func__,
		 dai_data->channels, dai_data->port_config.i2s.channel_mode);
	return -EINVAL;
}


static int msm_dai_q6_mi2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct msm_dai_q6_mi2s_dai_data *mi2s_dai_data =
	dev_get_drvdata(dai->dev);

	if (test_bit(STATUS_PORT_STARTED,
	    mi2s_dai_data->rx_dai.mi2s_dai_data.status_mask) ||
	    test_bit(STATUS_PORT_STARTED,
	    mi2s_dai_data->tx_dai.mi2s_dai_data.status_mask)) {
		dev_err(dai->dev, "%s: err chg i2s mode while dai running",
			__func__);
		return -EPERM;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		mi2s_dai_data->rx_dai.mi2s_dai_data.port_config.i2s.ws_src = 1;
		mi2s_dai_data->tx_dai.mi2s_dai_data.port_config.i2s.ws_src = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		mi2s_dai_data->rx_dai.mi2s_dai_data.port_config.i2s.ws_src = 0;
		mi2s_dai_data->tx_dai.mi2s_dai_data.port_config.i2s.ws_src = 0;
		break;
	default:
		pr_err("%s: fmt %d\n",
			__func__, fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	return 0;
}

static int msm_dai_q6_mi2s_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct msm_dai_q6_mi2s_dai_data *mi2s_dai_data =
			dev_get_drvdata(dai->dev);
	struct msm_dai_q6_dai_data *dai_data =
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		 &mi2s_dai_data->rx_dai.mi2s_dai_data :
		 &mi2s_dai_data->tx_dai.mi2s_dai_data);

	if (test_bit(STATUS_PORT_STARTED, dai_data->hwfree_status)) {
		clear_bit(STATUS_PORT_STARTED, dai_data->hwfree_status);
		dev_dbg(dai->dev, "%s: clear hwfree_status\n", __func__);
	}
	return 0;
}

static void msm_dai_q6_mi2s_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct msm_dai_q6_mi2s_dai_data *mi2s_dai_data =
			dev_get_drvdata(dai->dev);
	struct msm_dai_q6_dai_data *dai_data =
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
		 &mi2s_dai_data->rx_dai.mi2s_dai_data :
		 &mi2s_dai_data->tx_dai.mi2s_dai_data);
	 u16 port_id = 0;
	int rc = 0;

	if (msm_mi2s_get_port_id(dai->id, substream->stream,
				 &port_id) != 0) {
		dev_err(dai->dev, "%s: Invalid Port ID 0x%x\n",
				__func__, port_id);
	}

	dev_dbg(dai->dev, "%s: closing afe port id = 0x%x\n",
			__func__, port_id);

	if (test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		rc = afe_close(port_id);
		if (rc < 0)
			dev_err(dai->dev, "fail to close AFE port\n");
		clear_bit(STATUS_PORT_STARTED, dai_data->status_mask);
	}
	if (test_bit(STATUS_PORT_STARTED, dai_data->hwfree_status))
		clear_bit(STATUS_PORT_STARTED, dai_data->hwfree_status);
}

static struct snd_soc_dai_ops msm_dai_q6_mi2s_ops = {
	.startup	= msm_dai_q6_mi2s_startup,
	.prepare	= msm_dai_q6_mi2s_prepare,
	.hw_params	= msm_dai_q6_mi2s_hw_params,
	.hw_free	= msm_dai_q6_mi2s_hw_free,
	.set_fmt	= msm_dai_q6_mi2s_set_fmt,
	.shutdown	= msm_dai_q6_mi2s_shutdown,
};

/* Channel min and max are initialized base on platform data */
static struct snd_soc_dai_driver msm_dai_q6_mi2s_dai[] = {
	{
		.playback = {
			.stream_name = "Primary MI2S Playback",
			.aif_name = "PRI_MI2S_RX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     384000,
		},
		.capture = {
			.stream_name = "Primary MI2S Capture",
			.aif_name = "PRI_MI2S_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "Primary MI2S",
		.id = MSM_PRIM_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "Secondary MI2S Playback",
			.aif_name = "SEC_MI2S_RX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.capture = {
			.stream_name = "Secondary MI2S Capture",
			.aif_name = "SEC_MI2S_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "Secondary MI2S",
		.id = MSM_SEC_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "Tertiary MI2S Playback",
			.aif_name = "TERT_MI2S_RX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.capture = {
			.stream_name = "Tertiary MI2S Capture",
			.aif_name = "TERT_MI2S_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "Tertiary MI2S",
		.id = MSM_TERT_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "Quaternary MI2S Playback",
			.aif_name = "QUAT_MI2S_RX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.capture = {
			.stream_name = "Quaternary MI2S Capture",
			.aif_name = "QUAT_MI2S_TX",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				 SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "Quaternary MI2S",
		.id = MSM_QUAT_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "Quinary MI2S Playback",
			.aif_name = "QUIN_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.capture = {
			.stream_name = "Quinary MI2S Capture",
			.aif_name = "QUIN_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "Quinary MI2S",
		.id = MSM_QUIN_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "Senary MI2S Playback",
			.aif_name = "SEN_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "Senary MI2S Capture",
			.aif_name = "SENARY_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "Senary MI2S",
		.id = MSM_SENARY_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "Secondary MI2S Playback SD1",
			.aif_name = "SEC_MI2S_RX_SD1",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.id = MSM_SEC_MI2S_SD1,
	},
	{
		.playback = {
			.stream_name = "INT0 MI2S Playback",
			.aif_name = "INT0_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.capture = {
			.stream_name = "INT0 MI2S Capture",
			.aif_name = "INT0_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "INT0 MI2S",
		.id = MSM_INT0_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "INT1 MI2S Playback",
			.aif_name = "INT1_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "INT1 MI2S Capture",
			.aif_name = "INT1_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "INT1 MI2S",
		.id = MSM_INT1_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "INT2 MI2S Playback",
			.aif_name = "INT2_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "INT2 MI2S Capture",
			.aif_name = "INT2_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "INT2 MI2S",
		.id = MSM_INT2_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "INT3 MI2S Playback",
			.aif_name = "INT3_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "INT3 MI2S Capture",
			.aif_name = "INT3_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "INT3 MI2S",
		.id = MSM_INT3_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "INT4 MI2S Playback",
			.aif_name = "INT4_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     192000,
		},
		.capture = {
			.stream_name = "INT4 MI2S Capture",
			.aif_name = "INT4_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "INT4 MI2S",
		.id = MSM_INT4_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "INT5 MI2S Playback",
			.aif_name = "INT5_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "INT5 MI2S Capture",
			.aif_name = "INT5_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "INT5 MI2S",
		.id = MSM_INT5_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
	{
		.playback = {
			.stream_name = "INT6 MI2S Playback",
			.aif_name = "INT6_MI2S_RX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S24_3LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.capture = {
			.stream_name = "INT6 MI2S Capture",
			.aif_name = "INT6_MI2S_TX",
			.rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_8000 |
			SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_min =     8000,
			.rate_max =     48000,
		},
		.ops = &msm_dai_q6_mi2s_ops,
		.name = "INT6 MI2S",
		.id = MSM_INT6_MI2S,
		.probe = msm_dai_q6_dai_mi2s_probe,
		.remove = msm_dai_q6_dai_mi2s_remove,
	},
};


static int msm_dai_q6_mi2s_get_lineconfig(u16 sd_lines, u16 *config_ptr,
					  unsigned int *ch_cnt)
{
	u8 num_of_sd_lines;

	num_of_sd_lines = num_of_bits_set(sd_lines);
	switch (num_of_sd_lines) {
	case 0:
		pr_debug("%s: no line is assigned\n", __func__);
		break;
	case 1:
		switch (sd_lines) {
		case MSM_MI2S_SD0:
			*config_ptr = AFE_PORT_I2S_SD0;
			break;
		case MSM_MI2S_SD1:
			*config_ptr = AFE_PORT_I2S_SD1;
			break;
		case MSM_MI2S_SD2:
			*config_ptr = AFE_PORT_I2S_SD2;
			break;
		case MSM_MI2S_SD3:
			*config_ptr = AFE_PORT_I2S_SD3;
			break;
		case MSM_MI2S_SD4:
			*config_ptr = AFE_PORT_I2S_SD4;
			break;
		case MSM_MI2S_SD5:
			*config_ptr = AFE_PORT_I2S_SD5;
			break;
		case MSM_MI2S_SD6:
			*config_ptr = AFE_PORT_I2S_SD6;
			break;
		case MSM_MI2S_SD7:
			*config_ptr = AFE_PORT_I2S_SD7;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	case 2:
		switch (sd_lines) {
		case MSM_MI2S_SD0 | MSM_MI2S_SD1:
			*config_ptr = AFE_PORT_I2S_QUAD01;
			break;
		case MSM_MI2S_SD2 | MSM_MI2S_SD3:
			*config_ptr = AFE_PORT_I2S_QUAD23;
			break;
		case MSM_MI2S_SD4 | MSM_MI2S_SD5:
			*config_ptr = AFE_PORT_I2S_QUAD45;
			break;
		case MSM_MI2S_SD6 | MSM_MI2S_SD7:
			*config_ptr = AFE_PORT_I2S_QUAD67;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	case 3:
		switch (sd_lines) {
		case MSM_MI2S_SD0 | MSM_MI2S_SD1 | MSM_MI2S_SD2:
			*config_ptr = AFE_PORT_I2S_6CHS;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	case 4:
		switch (sd_lines) {
		case MSM_MI2S_SD0 | MSM_MI2S_SD1 | MSM_MI2S_SD2 | MSM_MI2S_SD3:
			*config_ptr = AFE_PORT_I2S_8CHS;
			break;
		case MSM_MI2S_SD4 | MSM_MI2S_SD5 | MSM_MI2S_SD6 | MSM_MI2S_SD7:
			*config_ptr = AFE_PORT_I2S_8CHS_2;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	case 5:
		switch (sd_lines) {
		case MSM_MI2S_SD0 | MSM_MI2S_SD1 | MSM_MI2S_SD2
		   | MSM_MI2S_SD3 | MSM_MI2S_SD4:
			*config_ptr = AFE_PORT_I2S_10CHS;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	case 6:
		switch (sd_lines) {
		case MSM_MI2S_SD0 | MSM_MI2S_SD1 | MSM_MI2S_SD2
		   | MSM_MI2S_SD3 | MSM_MI2S_SD4 | MSM_MI2S_SD5:
			*config_ptr = AFE_PORT_I2S_12CHS;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	case 7:
		switch (sd_lines) {
		case MSM_MI2S_SD0 | MSM_MI2S_SD1 | MSM_MI2S_SD2 | MSM_MI2S_SD3
		   | MSM_MI2S_SD4 | MSM_MI2S_SD5 | MSM_MI2S_SD6:
			*config_ptr = AFE_PORT_I2S_14CHS;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	case 8:
		switch (sd_lines) {
		case MSM_MI2S_SD0 | MSM_MI2S_SD1 | MSM_MI2S_SD2 | MSM_MI2S_SD3
		   | MSM_MI2S_SD4 | MSM_MI2S_SD5 | MSM_MI2S_SD6 | MSM_MI2S_SD7:
			*config_ptr = AFE_PORT_I2S_16CHS;
			break;
		default:
			pr_err("%s: invalid SD lines %d\n",
				   __func__, sd_lines);
			goto error_invalid_data;
		}
		break;
	default:
		pr_err("%s: invalid SD lines %d\n", __func__, num_of_sd_lines);
		goto error_invalid_data;
	}
	*ch_cnt = num_of_sd_lines;
	return 0;

error_invalid_data:
	pr_err("%s: invalid data\n", __func__);
	return -EINVAL;
}

static int msm_dai_q6_mi2s_platform_data_validation(
	struct platform_device *pdev, struct snd_soc_dai_driver *dai_driver)
{
	struct msm_dai_q6_mi2s_dai_data *dai_data = dev_get_drvdata(&pdev->dev);
	struct msm_mi2s_pdata *mi2s_pdata =
			(struct msm_mi2s_pdata *) pdev->dev.platform_data;
	unsigned int ch_cnt;
	int rc = 0;
	u16 sd_line;

	if (mi2s_pdata == NULL) {
		pr_err("%s: mi2s_pdata NULL", __func__);
		return -EINVAL;
	}

	rc = msm_dai_q6_mi2s_get_lineconfig(mi2s_pdata->rx_sd_lines,
					    &sd_line, &ch_cnt);
	if (rc < 0) {
		dev_err(&pdev->dev, "invalid MI2S RX sd line config\n");
		goto rtn;
	}

	if (ch_cnt) {
		dai_data->rx_dai.mi2s_dai_data.port_config.i2s.channel_mode =
		sd_line;
		dai_data->rx_dai.pdata_mi2s_lines = sd_line;
		dai_driver->playback.channels_min = 1;
		dai_driver->playback.channels_max = ch_cnt << 1;
	} else {
		dai_driver->playback.channels_min = 0;
		dai_driver->playback.channels_max = 0;
	}
	rc = msm_dai_q6_mi2s_get_lineconfig(mi2s_pdata->tx_sd_lines,
					    &sd_line, &ch_cnt);
	if (rc < 0) {
		dev_err(&pdev->dev, "invalid MI2S TX sd line config\n");
		goto rtn;
	}

	if (ch_cnt) {
		dai_data->tx_dai.mi2s_dai_data.port_config.i2s.channel_mode =
		sd_line;
		dai_data->tx_dai.pdata_mi2s_lines = sd_line;
		dai_driver->capture.channels_min = 1;
		dai_driver->capture.channels_max = ch_cnt << 1;
	} else {
		dai_driver->capture.channels_min = 0;
		dai_driver->capture.channels_max = 0;
	}

	dev_dbg(&pdev->dev, "%s: playback sdline 0x%x capture sdline 0x%x\n",
		__func__, dai_data->rx_dai.pdata_mi2s_lines,
		dai_data->tx_dai.pdata_mi2s_lines);
	dev_dbg(&pdev->dev, "%s: playback ch_max %d capture ch_mx %d\n",
		__func__, dai_driver->playback.channels_max,
		dai_driver->capture.channels_max);
rtn:
	return rc;
}

static const struct snd_soc_component_driver msm_q6_mi2s_dai_component = {
	.name		= "msm-dai-q6-mi2s",
};
static int msm_dai_q6_mi2s_dev_probe(struct platform_device *pdev)
{
	struct msm_dai_q6_mi2s_dai_data *dai_data;
	const char *q6_mi2s_dev_id = "jlq,msm-dai-q6-mi2s-dev-id";
	u32 tx_line = 0;
	u32  rx_line = 0;
	u32 mi2s_intf = 0;
	struct msm_mi2s_pdata *mi2s_pdata;
	int rc;

	rc = of_property_read_u32(pdev->dev.of_node, q6_mi2s_dev_id,
				  &mi2s_intf);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: missing 0x%x in dt node\n", __func__, mi2s_intf);
		goto rtn;
	}

	dev_dbg(&pdev->dev, "dev name %s dev id 0x%x\n", dev_name(&pdev->dev),
		mi2s_intf);

	if ((mi2s_intf < MSM_MI2S_MIN || mi2s_intf > MSM_MI2S_MAX)
		|| (mi2s_intf >= ARRAY_SIZE(msm_dai_q6_mi2s_dai))) {
		dev_err(&pdev->dev,
			"%s: Invalid MI2S ID %u from Device Tree\n",
			__func__, mi2s_intf);
		rc = -ENXIO;
		goto rtn;
	}

	pdev->id = mi2s_intf;

	mi2s_pdata = kzalloc(sizeof(struct msm_mi2s_pdata), GFP_KERNEL);
	if (!mi2s_pdata) {
		rc = -ENOMEM;
		goto rtn;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "jlq,msm-mi2s-rx-lines",
				  &rx_line);
	if (rc) {
		dev_err(&pdev->dev, "%s: Rx line from DT file %s\n", __func__,
			"jlq,msm-mi2s-rx-lines");
		goto free_pdata;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "jlq,msm-mi2s-tx-lines",
				  &tx_line);
	if (rc) {
		dev_err(&pdev->dev, "%s: Tx line from DT file %s\n", __func__,
			"jlq,msm-mi2s-tx-lines");
		goto free_pdata;
	}
	dev_dbg(&pdev->dev, "dev name %s Rx line 0x%x , Tx ine 0x%x\n",
		dev_name(&pdev->dev), rx_line, tx_line);
	mi2s_pdata->rx_sd_lines = rx_line;
	mi2s_pdata->tx_sd_lines = tx_line;
	mi2s_pdata->intf_id = mi2s_intf;

	dai_data = kzalloc(sizeof(struct msm_dai_q6_mi2s_dai_data),
			   GFP_KERNEL);
	if (!dai_data) {
		rc = -ENOMEM;
		goto free_pdata;
	} else
		dev_set_drvdata(&pdev->dev, dai_data);

	dai_data->interleave = INTERLEAVE_EN;

	rc = of_property_read_u32(pdev->dev.of_node,
				    "jlq,msm-dai-is-island-supported",
				    &dai_data->is_island_dai);
	if (rc)
		dev_dbg(&pdev->dev, "island supported entry not found\n");

	pdev->dev.platform_data = mi2s_pdata;

	rc = msm_dai_q6_mi2s_platform_data_validation(pdev,
			&msm_dai_q6_mi2s_dai[mi2s_intf]);
	if (rc < 0)
		goto free_dai_data;

	rc = snd_soc_register_component(&pdev->dev, &msm_q6_mi2s_dai_component,
	&msm_dai_q6_mi2s_dai[mi2s_intf], 1);
	if (rc < 0)
		goto err_register;
	return 0;

err_register:
	dev_err(&pdev->dev, "fail to msm_dai_q6_mi2s_dev_probe\n");
free_dai_data:
	kfree(dai_data);
free_pdata:
	kfree(mi2s_pdata);
rtn:
	return rc;
}

static int msm_dai_q6_mi2s_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct snd_soc_component_driver msm_dai_q6_component = {
	.name		= "msm-dai-q6-dev",
};

static int msm_dai_q6_dev_probe(struct platform_device *pdev)
{
	int rc, id, i, len;
	const char *q6_dev_id = "jlq,msm-dai-q6-dev-id";
	char stream_name[80];

	rc = of_property_read_u32(pdev->dev.of_node, q6_dev_id, &id);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: missing %s in dt node\n", __func__, q6_dev_id);
		return rc;
	}

	pdev->id = id;

	pr_debug("%s: dev name %s, id:%d\n", __func__,
		 dev_name(&pdev->dev), pdev->id);

	switch (id) {
	case SLIMBUS_0_RX:
		strlcpy(stream_name, "Slimbus Playback", 80);
		goto register_slim_playback;
	case SLIMBUS_2_RX:
		strlcpy(stream_name, "Slimbus2 Playback", 80);
		goto register_slim_playback;
	case SLIMBUS_1_RX:
		strlcpy(stream_name, "Slimbus1 Playback", 80);
		goto register_slim_playback;
	case SLIMBUS_3_RX:
		strlcpy(stream_name, "Slimbus3 Playback", 80);
		goto register_slim_playback;
	case SLIMBUS_4_RX:
		strlcpy(stream_name, "Slimbus4 Playback", 80);
		goto register_slim_playback;
	case SLIMBUS_5_RX:
		strlcpy(stream_name, "Slimbus5 Playback", 80);
		goto register_slim_playback;
	case SLIMBUS_6_RX:
		strlcpy(stream_name, "Slimbus6 Playback", 80);
		goto register_slim_playback;
	case SLIMBUS_7_RX:
		strlcpy(stream_name, "Slimbus7 Playback", sizeof(stream_name));
		goto register_slim_playback;
	case SLIMBUS_8_RX:
		strlcpy(stream_name, "Slimbus8 Playback", sizeof(stream_name));
		goto register_slim_playback;
	case SLIMBUS_9_RX:
		strlcpy(stream_name, "Slimbus9 Playback", sizeof(stream_name));
		goto register_slim_playback;
register_slim_playback:
		rc = -ENODEV;
		len = strnlen(stream_name, 80);
		for (i = 0; i < ARRAY_SIZE(msm_dai_q6_slimbus_rx_dai); i++) {
			if (msm_dai_q6_slimbus_rx_dai[i].playback.stream_name &&
				!strcmp(stream_name,
				msm_dai_q6_slimbus_rx_dai[i]
				.playback.stream_name)) {
				rc = snd_soc_register_component(&pdev->dev,
				&msm_dai_q6_component,
				&msm_dai_q6_slimbus_rx_dai[i], 1);
				break;
			}
		}
		if (rc)
			pr_err("%s: Device not found stream name %s\n",
				__func__, stream_name);
		break;
	case SLIMBUS_0_TX:
		strlcpy(stream_name, "Slimbus Capture", 80);
		goto register_slim_capture;
	case SLIMBUS_1_TX:
		strlcpy(stream_name, "Slimbus1 Capture", 80);
		goto register_slim_capture;
	case SLIMBUS_2_TX:
		strlcpy(stream_name, "Slimbus2 Capture", 80);
		goto register_slim_capture;
	case SLIMBUS_3_TX:
		strlcpy(stream_name, "Slimbus3 Capture", 80);
		goto register_slim_capture;
	case SLIMBUS_4_TX:
		strlcpy(stream_name, "Slimbus4 Capture", 80);
		goto register_slim_capture;
	case SLIMBUS_5_TX:
		strlcpy(stream_name, "Slimbus5 Capture", 80);
		goto register_slim_capture;
	case SLIMBUS_6_TX:
		strlcpy(stream_name, "Slimbus6 Capture", 80);
		goto register_slim_capture;
	case SLIMBUS_7_TX:
		strlcpy(stream_name, "Slimbus7 Capture", sizeof(stream_name));
		goto register_slim_capture;
	case SLIMBUS_8_TX:
		strlcpy(stream_name, "Slimbus8 Capture", sizeof(stream_name));
		goto register_slim_capture;
	case SLIMBUS_9_TX:
		strlcpy(stream_name, "Slimbus9 Capture", sizeof(stream_name));
		goto register_slim_capture;
register_slim_capture:
		rc = -ENODEV;
		len = strnlen(stream_name, 80);
		for (i = 0; i < ARRAY_SIZE(msm_dai_q6_slimbus_tx_dai); i++) {
			if (msm_dai_q6_slimbus_tx_dai[i].capture.stream_name &&
				!strcmp(stream_name,
				msm_dai_q6_slimbus_tx_dai[i]
				.capture.stream_name)) {
				rc = snd_soc_register_component(&pdev->dev,
				&msm_dai_q6_component,
				&msm_dai_q6_slimbus_tx_dai[i], 1);
				break;
			}
		}
		if (rc)
			pr_err("%s: Device not found stream name %s\n",
				__func__, stream_name);
		break;
	case AFE_LOOPBACK_TX:
		rc = snd_soc_register_component(&pdev->dev,
						&msm_dai_q6_component,
						&msm_dai_q6_afe_lb_tx_dai[0],
						1);
		break;
	case INT_BT_SCO_RX:
		rc = snd_soc_register_component(&pdev->dev,
			&msm_dai_q6_component, &msm_dai_q6_bt_sco_rx_dai, 1);
		break;
	case INT_BT_SCO_TX:
		rc = snd_soc_register_component(&pdev->dev,
			&msm_dai_q6_component, &msm_dai_q6_bt_sco_tx_dai, 1);
		break;
	case INT_BT_A2DP_RX:
		rc = snd_soc_register_component(&pdev->dev,
			&msm_dai_q6_component, &msm_dai_q6_bt_a2dp_rx_dai, 1);
		break;
	case INT_FM_RX:
		rc = snd_soc_register_component(&pdev->dev,
			&msm_dai_q6_component, &msm_dai_q6_fm_rx_dai, 1);
		break;
	case INT_FM_TX:
		rc = snd_soc_register_component(&pdev->dev,
			&msm_dai_q6_component, &msm_dai_q6_fm_tx_dai, 1);
		break;
	case AFE_PORT_ID_USB_RX:
		rc = snd_soc_register_component(&pdev->dev,
			&msm_dai_q6_component, &msm_dai_q6_usb_rx_dai, 1);
		break;
	case AFE_PORT_ID_USB_TX:
		rc = snd_soc_register_component(&pdev->dev,
			&msm_dai_q6_component, &msm_dai_q6_usb_tx_dai, 1);
		break;
	case RT_PROXY_DAI_001_RX:
		strlcpy(stream_name, "AFE Playback", 80);
		goto register_afe_playback;
	case RT_PROXY_DAI_002_RX:
		strlcpy(stream_name, "AFE-PROXY RX", 80);
register_afe_playback:
		rc = -ENODEV;
		len = strnlen(stream_name, 80);
		for (i = 0; i < ARRAY_SIZE(msm_dai_q6_afe_rx_dai); i++) {
			if (msm_dai_q6_afe_rx_dai[i].playback.stream_name &&
			    !strcmp(stream_name,
			    msm_dai_q6_afe_rx_dai[i].playback.stream_name)) {
				rc = snd_soc_register_component(&pdev->dev,
					&msm_dai_q6_component,
					&msm_dai_q6_afe_rx_dai[i], 1);
				break;
			}
		}
		if (rc)
			pr_err("%s: Device not found stream name %s\n",
			__func__, stream_name);
		break;
	case RT_PROXY_DAI_001_TX:
		strlcpy(stream_name, "AFE-PROXY TX", 80);
		goto register_afe_capture;
	case RT_PROXY_DAI_002_TX:
		strlcpy(stream_name, "AFE Capture", 80);
register_afe_capture:
		rc = -ENODEV;
		len = strnlen(stream_name, 80);
		for (i = 0; i < ARRAY_SIZE(msm_dai_q6_afe_tx_dai); i++) {
			if (msm_dai_q6_afe_tx_dai[i].capture.stream_name &&
				!strcmp(stream_name,
				msm_dai_q6_afe_tx_dai[i].capture.stream_name)) {
				rc = snd_soc_register_component(&pdev->dev,
					&msm_dai_q6_component,
					&msm_dai_q6_afe_tx_dai[i], 1);
				break;
			}
		}
		if (rc)
			pr_err("%s: Device not found stream name %s\n",
			__func__, stream_name);
		break;
	case VOICE_PLAYBACK_TX:
		strlcpy(stream_name, "Voice Farend Playback", 80);
		goto register_voice_playback;
	case VOICE2_PLAYBACK_TX:
		strlcpy(stream_name, "Voice2 Farend Playback", 80);
register_voice_playback:
		rc = -ENODEV;
		len = strnlen(stream_name, 80);
		for (i = 0; i < ARRAY_SIZE(msm_dai_q6_voc_playback_dai); i++) {
			if (msm_dai_q6_voc_playback_dai[i].playback.stream_name
			    && !strcmp(stream_name,
			 msm_dai_q6_voc_playback_dai[i].playback.stream_name)) {
				rc = snd_soc_register_component(&pdev->dev,
					&msm_dai_q6_component,
					&msm_dai_q6_voc_playback_dai[i], 1);
				break;
			}
		}
		if (rc)
			pr_err("%s Device not found stream name %s\n",
			       __func__, stream_name);
		break;
	case VOICE_RECORD_RX:
		strlcpy(stream_name, "Voice Downlink Capture", 80);
		goto register_uplink_capture;
	case VOICE_RECORD_TX:
		strlcpy(stream_name, "Voice Uplink Capture", 80);
register_uplink_capture:
		rc = -ENODEV;
		len = strnlen(stream_name, 80);
		for (i = 0; i < ARRAY_SIZE(msm_dai_q6_incall_record_dai); i++) {
			if (msm_dai_q6_incall_record_dai[i].capture.stream_name
			    && !strcmp(stream_name,
			    msm_dai_q6_incall_record_dai[i].
			    capture.stream_name)) {
				rc = snd_soc_register_component(&pdev->dev,
					&msm_dai_q6_component,
					&msm_dai_q6_incall_record_dai[i], 1);
				break;
			}
		}
		if (rc)
			pr_err("%s: Device not found stream name %s\n",
			__func__, stream_name);
		break;

	default:
		rc = -ENODEV;
		break;
	}

	return rc;
}

static int msm_dai_q6_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct of_device_id msm_dai_q6_dev_dt_match[] = {
	{ .compatible = "jlq,msm-dai-q6-dev", },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_dai_q6_dev_dt_match);

static struct platform_driver msm_dai_q6_dev = {
	.probe  = msm_dai_q6_dev_probe,
	.remove = msm_dai_q6_dev_remove,
	.driver = {
		.name = "msm-dai-q6-dev",
		.owner = THIS_MODULE,
		.of_match_table = msm_dai_q6_dev_dt_match,
		.suppress_bind_attrs = true,
	},
};

static int msm_dai_q6_probe(struct platform_device *pdev)
{
	int rc;

	pr_debug("%s: dev name %s, id:%d\n", __func__,
		 dev_name(&pdev->dev), pdev->id);
	rc = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev, "%s: failed to add child nodes, rc=%d\n",
			__func__, rc);
	} else
		dev_dbg(&pdev->dev, "%s: added child node\n", __func__);

	return rc;
}

static int msm_dai_q6_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	return 0;
}

static const struct of_device_id msm_dai_q6_dt_match[] = {
	{ .compatible = "jlq,msm-dai-q6", },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_dai_q6_dt_match);
static struct platform_driver msm_dai_q6 = {
	.probe  = msm_dai_q6_probe,
	.remove = msm_dai_q6_remove,
	.driver = {
		.name = "msm-dai-q6",
		.owner = THIS_MODULE,
		.of_match_table = msm_dai_q6_dt_match,
		.suppress_bind_attrs = true,
	},
};

static int msm_dai_mi2s_q6_probe(struct platform_device *pdev)
{
	int rc;

	rc = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev, "%s: failed to add child nodes, rc=%d\n",
			__func__, rc);
	} else
		dev_dbg(&pdev->dev, "%s: added child node\n", __func__);
	return rc;
}

static int msm_dai_mi2s_q6_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id msm_dai_mi2s_dt_match[] = {
	{ .compatible = "jlq,msm-dai-mi2s", },
	{ }
};

MODULE_DEVICE_TABLE(of, msm_dai_mi2s_dt_match);

static struct platform_driver msm_dai_mi2s_q6 = {
	.probe  = msm_dai_mi2s_q6_probe,
	.remove = msm_dai_mi2s_q6_remove,
	.driver = {
		.name = "msm-dai-mi2s",
		.owner = THIS_MODULE,
		.of_match_table = msm_dai_mi2s_dt_match,
		.suppress_bind_attrs = true,
	},
};

static const struct of_device_id msm_dai_q6_mi2s_dev_dt_match[] = {
	{ .compatible = "jlq,msm-dai-q6-mi2s", },
	{ }
};

MODULE_DEVICE_TABLE(of, msm_dai_q6_mi2s_dev_dt_match);

static struct platform_driver msm_dai_q6_mi2s_driver = {
	.probe  = msm_dai_q6_mi2s_dev_probe,
	.remove  = msm_dai_q6_mi2s_dev_remove,
	.driver = {
		.name = "msm-dai-q6-mi2s",
		.owner = THIS_MODULE,
		.of_match_table = msm_dai_q6_mi2s_dev_dt_match,
		.suppress_bind_attrs = true,
	},
};

static int msm_dai_q6_cdc_dma_format_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_cdc_dma_dai_data *dai_data = kcontrol->private_data;
	int value = ucontrol->value.integer.value[0];

	dai_data->port_config.cdc_dma.data_format = value;
	pr_debug("%s: format = %d\n", __func__, value);
	return 0;
}

static int msm_dai_q6_cdc_dma_format_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct msm_dai_q6_cdc_dma_dai_data *dai_data = kcontrol->private_data;

	ucontrol->value.integer.value[0] =
		dai_data->port_config.cdc_dma.data_format;
	return 0;
}

static const struct snd_kcontrol_new cdc_dma_config_controls[] = {
	SOC_ENUM_EXT("WSA_CDC_DMA_0 TX Format", cdc_dma_config_enum[0],
		     msm_dai_q6_cdc_dma_format_get,
		     msm_dai_q6_cdc_dma_format_put),
	SOC_ENUM_EXT("WSA_CDC_DMA_0 RX XTLoggingDisable",
		     xt_logging_disable_enum[0],
		     msm_dai_q6_cdc_dma_xt_logging_disable_get,
		     msm_dai_q6_cdc_dma_xt_logging_disable_put),
};

/* SOC probe for codec DMA interface */
static int msm_dai_q6_dai_cdc_dma_probe(struct snd_soc_dai *dai)
{
	struct msm_dai_q6_cdc_dma_dai_data *dai_data = NULL;
	int rc = 0;

	if (!dai) {
		pr_err("%s: Invalid params dai\n", __func__);
		return -EINVAL;
	}
	if (!dai->dev) {
		pr_err("%s: Invalid params dai dev\n", __func__);
		return -EINVAL;
	}

	msm_dai_q6_set_dai_id(dai);
	dai_data = dev_get_drvdata(dai->dev);

	switch (dai->id) {
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_0:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&cdc_dma_config_controls[0],
				 dai_data));
		break;
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_0:
		rc = snd_ctl_add(dai->component->card->snd_card,
				 snd_ctl_new1(&cdc_dma_config_controls[1],
				 dai_data));
		break;
	default:
		break;
	}

	if (rc < 0)
		dev_err(dai->dev, "%s: err add config ctl, DAI = %s\n",
			__func__, dai->name);

	if (dai_data->is_island_dai)
		rc = msm_dai_q6_add_island_mx_ctls(
						dai->component->card->snd_card,
						dai->name, dai->id,
						(void *)dai_data);

	rc = msm_dai_q6_dai_add_route(dai);
	return rc;
}

static int msm_dai_q6_dai_cdc_dma_remove(struct snd_soc_dai *dai)
{
	struct msm_dai_q6_cdc_dma_dai_data *dai_data =
		dev_get_drvdata(dai->dev);
	int rc = 0;

	/* If AFE port is still up, close it */
	if (test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		dev_dbg(dai->dev, "%s: stop codec dma port:%d\n", __func__,
			dai->id);
		rc = afe_close(dai->id); /* can block */
		if (rc < 0)
			dev_err(dai->dev, "fail to close AFE port\n");
		clear_bit(STATUS_PORT_STARTED, dai_data->status_mask);
	}
	return rc;
}

static int msm_dai_q6_cdc_dma_set_channel_map(struct snd_soc_dai *dai,
			unsigned int tx_num_ch, unsigned int *tx_ch_mask,
			unsigned int rx_num_ch, unsigned int *rx_ch_mask)

{
	int rc = 0;
	struct msm_dai_q6_cdc_dma_dai_data *dai_data =
						dev_get_drvdata(dai->dev);
	unsigned int ch_mask = 0, ch_num = 0;

	dev_dbg(dai->dev, "%s: id = %d\n", __func__, dai->id);
	switch (dai->id) {
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_1:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_0:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_1:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_2:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_3:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_4:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_5:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_6:
	case AFE_PORT_ID_RX_CODEC_DMA_RX_7:
		if (!rx_ch_mask) {
			dev_err(dai->dev, "%s: invalid rx ch mask\n", __func__);
			return -EINVAL;
		}
		if (rx_num_ch > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
			dev_err(dai->dev, "%s: invalid rx_num_ch %d\n",
				__func__, rx_num_ch);
			return -EINVAL;
		}
		ch_mask = *rx_ch_mask;
		ch_num = rx_num_ch;
		break;
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_WSA_CODEC_DMA_TX_2:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_0:
	case AFE_PORT_ID_VA_CODEC_DMA_TX_1:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_0:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_1:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_2:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_3:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_4:
	case AFE_PORT_ID_TX_CODEC_DMA_TX_5:
		if (!tx_ch_mask) {
			dev_err(dai->dev, "%s: invalid tx ch mask\n", __func__);
			return -EINVAL;
		}
		if (tx_num_ch > AFE_PORT_MAX_AUDIO_CHAN_CNT) {
			dev_err(dai->dev, "%s: invalid tx_num_ch %d\n",
				__func__, tx_num_ch);
			return -EINVAL;
		}
		ch_mask = *tx_ch_mask;
		ch_num = tx_num_ch;
		break;
	default:
		dev_err(dai->dev, "%s: invalid dai id %d\n", __func__, dai->id);
		return -EINVAL;
	}

	dai_data->port_config.cdc_dma.active_channels_mask = ch_mask;
	dev_dbg(dai->dev, "%s: CDC_DMA_%d_ch cnt[%d] ch mask[0x%x]\n", __func__,
			dai->id, ch_num, ch_mask);
	return rc;
}

static int msm_dai_q6_cdc_dma_hw_params(
		struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
    int rc = 0;
	struct msm_dai_q6_cdc_dma_dai_data *dai_data =
						dev_get_drvdata(dai->dev);
	struct afe_device_config config;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_SPECIAL:
		dai_data->port_config.cdc_dma.bit_width = 16;
	    dai_data->bitwidth = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		dai_data->port_config.cdc_dma.bit_width = 24;
	    dai_data->bitwidth = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dai_data->port_config.cdc_dma.bit_width = 32;
		dai_data->bitwidth = 32;
		break;
	default:
		dev_err(dai->dev, "%s: format %d\n",
			__func__, params_format(params));
		return -EINVAL;
	}

	dai_data->rate = params_rate(params);
	dai_data->channels = params_channels(params);

	dai_data->port_config.cdc_dma.cdc_dma_cfg_minor_version =
				AFE_API_VERSION_CODEC_DMA_CONFIG;
	dai_data->port_config.cdc_dma.sample_rate = dai_data->rate;
	dai_data->port_config.cdc_dma.num_channels = dai_data->channels;
	dev_dbg(dai->dev, "%s: bit_wd[%hu] format[%hu]\n"
		"num_channel %hu sample_rate %d\n", __func__,
		dai_data->port_config.cdc_dma.bit_width,
		dai_data->port_config.cdc_dma.data_format,
		dai_data->port_config.cdc_dma.num_channels,
		dai_data->rate);

    config.bit_width = dai_data->bitwidth;
    config.channel_num = dai_data->channels;
    config.sample_rate = dai_data->rate;
    config.interleave = dai_data->interleave;
	rc = afe_port_open(dai->id, &config);

	if (rc < 0)
		dev_err(dai->dev, "fail to open AFE port 0x%x\n",
			dai->id);


	return 0;
}

static int msm_dai_q6_cdc_dma_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct msm_dai_q6_cdc_dma_dai_data *dai_data =
						dev_get_drvdata(dai->dev);
	int rc = 0;

	if (!test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		if ((dai->id == AFE_PORT_ID_WSA_CODEC_DMA_TX_0) &&
			(dai_data->port_config.cdc_dma.data_format == 1))
			dai_data->port_config.cdc_dma.data_format =
				AFE_LINEAR_PCM_DATA_PACKED_16BIT;

		rc = afe_port_start(dai->id, &dai_data->port_config,
						dai_data->rate);
		if (rc < 0)
			dev_err(dai->dev, "fail to open AFE port 0x%x\n",
				dai->id);
		else
			set_bit(STATUS_PORT_STARTED,
				dai_data->status_mask);
	}
	return rc;
}


static void msm_dai_q6_cdc_dma_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct msm_dai_q6_dai_data *dai_data = dev_get_drvdata(dai->dev);
	int rc = 0;

	if (test_bit(STATUS_PORT_STARTED, dai_data->status_mask)) {
		dev_dbg(dai->dev, "%s: stop AFE port:%d\n", __func__,
			dai->id);
		rc = afe_close(dai->id); /* can block */
		if (rc < 0)
			dev_err(dai->dev, "fail to close AFE port\n");

		dev_dbg(dai->dev, "%s: dai_data->status_mask = %ld\n", __func__,
			*dai_data->status_mask);
		clear_bit(STATUS_PORT_STARTED, dai_data->status_mask);
	}

	if (test_bit(STATUS_PORT_STARTED, dai_data->hwfree_status))
		clear_bit(STATUS_PORT_STARTED, dai_data->hwfree_status);
}

static struct snd_soc_dai_ops msm_dai_q6_cdc_dma_ops = {
	.prepare          = msm_dai_q6_cdc_dma_prepare,
	.hw_params        = msm_dai_q6_cdc_dma_hw_params,
	.shutdown         = msm_dai_q6_cdc_dma_shutdown,
	.set_channel_map = msm_dai_q6_cdc_dma_set_channel_map,
};

static struct snd_soc_dai_ops msm_dai_q6_cdc_wsa_dma_ops = {
	.prepare          = msm_dai_q6_cdc_dma_prepare,
	.hw_params        = msm_dai_q6_cdc_dma_hw_params,
	.shutdown         = msm_dai_q6_cdc_dma_shutdown,
	.set_channel_map = msm_dai_q6_cdc_dma_set_channel_map,
	.digital_mute = msm_dai_q6_spk_digital_mute,
};

static struct snd_soc_dai_driver msm_dai_q6_cdc_dma_dai[] = {
	{
		.playback = {
			.stream_name = "WSA CDC DMA0 Playback",
			.aif_name = "WSA_CDC_DMA_RX_0",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "WSA_CDC_DMA_RX_0",
		.ops = &msm_dai_q6_cdc_wsa_dma_ops,
		.id = AFE_PORT_ID_WSA_CODEC_DMA_RX_0,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "WSA CDC DMA0 Capture",
			.aif_name = "WSA_CDC_DMA_TX_0",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "WSA_CDC_DMA_TX_0",
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_WSA_CODEC_DMA_TX_0,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
		},
	{
		.playback = {
			.stream_name = "WSA CDC DMA1 Playback",
			.aif_name = "WSA_CDC_DMA_RX_1",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "WSA_CDC_DMA_RX_1",
		.ops = &msm_dai_q6_cdc_wsa_dma_ops,
		.id = AFE_PORT_ID_WSA_CODEC_DMA_RX_1,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "WSA CDC DMA1 Capture",
			.aif_name = "WSA_CDC_DMA_TX_1",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "WSA_CDC_DMA_TX_1",
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_WSA_CODEC_DMA_TX_1,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "WSA CDC DMA2 Capture",
			.aif_name = "WSA_CDC_DMA_TX_2",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "WSA_CDC_DMA_TX_2",
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_WSA_CODEC_DMA_TX_2,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "VA CDC DMA0 Capture",
			.aif_name = "VA_CDC_DMA_TX_0",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "VA_CDC_DMA_TX_0",
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_VA_CODEC_DMA_TX_0,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "VA CDC DMA1 Capture",
			.aif_name = "VA_CDC_DMA_TX_1",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "VA_CDC_DMA_TX_1",
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_VA_CODEC_DMA_TX_1,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "VA CDC DMA2 Capture",
			.aif_name = "VA_CDC_DMA_TX_2",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.name = "VA_CDC_DMA_TX_2",
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_VA_CODEC_DMA_TX_2,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.playback = {
			.stream_name = "RX CDC DMA0 Playback",
			.aif_name = "RX_CDC_DMA_RX_0",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_0,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "TX CDC DMA0 Capture",
			.aif_name = "TX_CDC_DMA_TX_0",
			.rates = SNDRV_PCM_RATE_8000 |
				SNDRV_PCM_RATE_16000 |
				SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_192000 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 3,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_TX_CODEC_DMA_TX_0,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.playback = {
			.stream_name = "RX CDC DMA1 Playback",
			.aif_name = "RX_CDC_DMA_RX_1",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_1,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "TX CDC DMA1 Capture",
			.aif_name = "TX_CDC_DMA_TX_1",
			.rates = SNDRV_PCM_RATE_8000 |
				SNDRV_PCM_RATE_16000 |
				SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_192000 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 3,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_TX_CODEC_DMA_TX_1,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.playback = {
			.stream_name = "RX CDC DMA2 Playback",
			.aif_name = "RX_CDC_DMA_RX_2",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_2,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "TX CDC DMA2 Capture",
			.aif_name = "TX_CDC_DMA_TX_2",
			.rates = SNDRV_PCM_RATE_8000 |
				SNDRV_PCM_RATE_16000 |
				SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_192000 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_TX_CODEC_DMA_TX_2,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},	{
		.playback = {
			.stream_name = "RX CDC DMA3 Playback",
			.aif_name = "RX_CDC_DMA_RX_3",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_3,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "TX CDC DMA3 Capture",
			.aif_name = "TX_CDC_DMA_TX_3",
			.rates = SNDRV_PCM_RATE_8000 |
				SNDRV_PCM_RATE_16000 |
				SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_192000 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_TX_CODEC_DMA_TX_3,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.playback = {
			.stream_name = "RX CDC DMA4 Playback",
			.aif_name = "RX_CDC_DMA_RX_4",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 6,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_4,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "TX CDC DMA4 Capture",
			.aif_name = "TX_CDC_DMA_TX_4",
			.rates = SNDRV_PCM_RATE_8000 |
				SNDRV_PCM_RATE_16000 |
				SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_192000 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_TX_CODEC_DMA_TX_4,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.playback = {
			.stream_name = "RX CDC DMA5 Playback",
			.aif_name = "RX_CDC_DMA_RX_5",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_5,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.capture = {
			.stream_name = "TX CDC DMA5 Capture",
			.aif_name = "TX_CDC_DMA_TX_5",
			.rates = SNDRV_PCM_RATE_8000 |
				SNDRV_PCM_RATE_16000 |
				SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_192000 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_TX_CODEC_DMA_TX_5,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.playback = {
			.stream_name = "RX CDC DMA6 Playback",
			.aif_name = "RX_CDC_DMA_RX_6",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_6,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
	{
		.playback = {
			.stream_name = "RX CDC DMA7 Playback",
			.aif_name = "RX_CDC_DMA_RX_7",
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 |
				SNDRV_PCM_RATE_384000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S24_3LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 384000,
		},
		.ops = &msm_dai_q6_cdc_dma_ops,
		.id = AFE_PORT_ID_RX_CODEC_DMA_RX_7,
		.probe = msm_dai_q6_dai_cdc_dma_probe,
		.remove = msm_dai_q6_dai_cdc_dma_remove,
	},
};

static const struct snd_soc_component_driver msm_q6_cdc_dma_dai_component = {
	.name = "msm-dai-cdc-dma-dev",
};

/* DT related probe for each codec DMA interface device */
static int msm_dai_q6_cdc_dma_dev_probe(struct platform_device *pdev)
{
	const char *q6_cdc_dma_dev_id = "jlq,msm-dai-cdc-dma-dev-id";
	u32 cdc_dma_id = 0;
	int i;
	int rc = 0;
	struct msm_dai_q6_cdc_dma_dai_data *dai_data = NULL;

	rc = of_property_read_u32(pdev->dev.of_node, q6_cdc_dma_dev_id,
				  &cdc_dma_id);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: missing 0x%x in dt node\n", __func__, cdc_dma_id);
		return rc;
	}

	dev_dbg(&pdev->dev, "%s: dev name %s dev id 0x%x\n", __func__,
		dev_name(&pdev->dev), cdc_dma_id);

	pdev->id = cdc_dma_id;

	dai_data = devm_kzalloc(&pdev->dev,
				sizeof(struct msm_dai_q6_cdc_dma_dai_data),
				GFP_KERNEL);

	if (!dai_data)
		return -ENOMEM;

	rc = of_property_read_u32(pdev->dev.of_node,
				    "jlq,msm-dai-is-island-supported",
				    &dai_data->is_island_dai);
	if (rc)
		dev_dbg(&pdev->dev, "island supported entry not found\n");

    dai_data->interleave = INTERLEAVE_EN;
	dev_set_drvdata(&pdev->dev, dai_data);

	for (i = 0; i < ARRAY_SIZE(msm_dai_q6_cdc_dma_dai); i++) {
		if (msm_dai_q6_cdc_dma_dai[i].id == cdc_dma_id) {
			return snd_soc_register_component(&pdev->dev,
				&msm_q6_cdc_dma_dai_component,
				&msm_dai_q6_cdc_dma_dai[i], 1);
		}
	}
	return -ENODEV;
}

static int msm_dai_q6_cdc_dma_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct of_device_id msm_dai_q6_cdc_dma_dev_dt_match[] = {
	{ .compatible = "jlq,msm-dai-cdc-dma-dev", },
	{ }
};

MODULE_DEVICE_TABLE(of, msm_dai_q6_cdc_dma_dev_dt_match);

static struct platform_driver msm_dai_q6_cdc_dma_driver = {
	.probe  = msm_dai_q6_cdc_dma_dev_probe,
	.remove  = msm_dai_q6_cdc_dma_dev_remove,
	.driver = {
		.name = "msm-dai-cdc-dma-dev",
		.owner = THIS_MODULE,
		.of_match_table = msm_dai_q6_cdc_dma_dev_dt_match,
		.suppress_bind_attrs = true,
	},
};

/* DT related probe for codec DMA interface device group */
static int msm_dai_cdc_dma_q6_probe(struct platform_device *pdev)
{
	int rc;

	rc = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev, "%s: failed to add child nodes, rc=%d\n",
			__func__, rc);
	} else
		dev_dbg(&pdev->dev, "%s: added child node\n", __func__);
	return rc;
}

static int msm_dai_cdc_dma_q6_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	return 0;
}

static const struct of_device_id msm_dai_cdc_dma_dt_match[] = {
	{ .compatible = "jlq,msm-dai-cdc-dma", },
	{ }
};

MODULE_DEVICE_TABLE(of, msm_dai_cdc_dma_dt_match);

static struct platform_driver msm_dai_cdc_dma_q6 = {
	.probe  = msm_dai_cdc_dma_q6_probe,
	.remove = msm_dai_cdc_dma_q6_remove,
	.driver = {
		.name = "msm-dai-cdc-dma",
		.owner = THIS_MODULE,
		.of_match_table = msm_dai_cdc_dma_dt_match,
		.suppress_bind_attrs = true,
	},
};

int __init msm_dai_q6_init(void)
{
	int rc;

	rc = platform_driver_register(&msm_dai_q6);
	if (rc) {
		pr_err("%s: fail to register dai q6 driver", __func__);
		goto fail;
	}

	rc = platform_driver_register(&msm_dai_q6_dev); //slimbus
	if (rc) {
		pr_err("%s: fail to register dai q6 dev driver", __func__);
		goto dai_q6_dev_fail;
	}

	rc = platform_driver_register(&msm_dai_q6_mi2s_driver); //i2s
	if (rc) {
		pr_err("%s: fail to register dai MI2S dev drv\n", __func__);
		goto dai_q6_mi2s_drv_fail;
	}

	rc = platform_driver_register(&msm_dai_mi2s_q6);
	if (rc) {
		pr_err("%s: fail to register dai MI2S\n", __func__);
		goto dai_mi2s_q6_fail;
	}

	rc = platform_driver_register(&msm_dai_q6_cdc_dma_driver); //bolero
	if (rc) {
		pr_err("%s: fail to register dai CDC DMA dev\n", __func__);
		goto dai_cdc_dma_q6_dev_fail;
	}

	rc = platform_driver_register(&msm_dai_cdc_dma_q6);
	if (rc) {
		pr_err("%s: fail to register dai CDC DMA\n", __func__);
		goto dai_cdc_dma_q6_fail;
	}
	return rc;

dai_cdc_dma_q6_fail:
	platform_driver_unregister(&msm_dai_q6_cdc_dma_driver);
dai_cdc_dma_q6_dev_fail:
	platform_driver_unregister(&msm_dai_mi2s_q6);
dai_mi2s_q6_fail:
	platform_driver_unregister(&msm_dai_q6_mi2s_driver);
dai_q6_mi2s_drv_fail:
	platform_driver_unregister(&msm_dai_q6_dev);
dai_q6_dev_fail:
	platform_driver_unregister(&msm_dai_q6);
fail:

	return rc;
}

void msm_dai_q6_exit(void)
{
	platform_driver_unregister(&msm_dai_cdc_dma_q6);
	platform_driver_unregister(&msm_dai_q6_cdc_dma_driver);
	platform_driver_unregister(&msm_dai_mi2s_q6);
	platform_driver_unregister(&msm_dai_q6_mi2s_driver);
	platform_driver_unregister(&msm_dai_q6_dev);
	platform_driver_unregister(&msm_dai_q6);
}

/* Module information */
MODULE_DESCRIPTION("MSM DSP DAI driver");
MODULE_LICENSE("GPL v2");
