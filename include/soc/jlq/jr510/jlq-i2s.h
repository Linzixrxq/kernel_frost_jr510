#ifndef __JLQ_I2S_H__
#define __JLQ_I2S_H__

#include <asm-generic/io.h>
#include <linux/clk.h>

/* Register Offset*/
#define PCM_I2S_SCLK_CFG        0x00
#define PCM_I2S_FSYNC_CFG       0x04
#define PCM_I2S_FIFO_STA        0x08
#define PCM_I2S_PCM_EN          0x0c
#define PCM_I2S_MODE            0x10
#define PCM_I2S_REC_FIFO        0x14
#define PCM_I2S_TRAN_FIFO       0x18
/* 0x20 ~ 0x28 res reg */
//#define PCM_I2S_INTR_STA_RAW    0x20
//#define PCM_I2S_INTR_STA_RAW    0x24
//#define PCM_I2S_INTR_R          0x28

/* PCM_I2S_SCLK_CFG 
 *     SCLK_DIV: [11:0]
 */
#define SCLK_DIV                0

/* PCM_I2S_FSYNC_CFG 
 *     CHANNEL_NUM: [18:16]
 *     FSYNC_BIT:   [13:12]
 *     FSYNC_DIV:   [11:0]
 */
#define CHANNEL_NUM             16
#define FSYNC_BIT               12
#define FSYNC_DIV               0

/* PCM_I2S_PCM_EN */
#define PCM_EN                  0

/* PCM_I2S_MODE 
 *    PCM_SLOT_NUM:    [17:16]
 *    DATA_FORMAT:     [12]
 *    PCM_TRAN_POL:    [11]
 *    PCM_REC_POL:     [10]
 *    PCM_SYNC_FORMAT: [9]
 *    I2S_MODE:        [8]
 *    SCLK_INVERT:     [7]
 *    DLY_MODE:        [6]
 *    FLUSH_TRAN_BUF:  [5]
 *    FLUSH_REC_BUF:   [4]
 *    I2S_EN:          [3]
 *    REC_EN:          [2]
 *    TRAN_EN:         [1]
 *    SLAVE_EN:        [0]
 */
#define PCM_SLOT_NUM            16
#define DATA_FORMAT             12
#define PCM_TRAN_POL            11
#define PCM_REC_POL             10
#define PCM_SYNC_FORMAT          9
#define I2S_MODE                 8
#define SCLK_INVERT              7
#define DLY_MODE                 6
#define FLUSH_TRAN_BUF           5
#define FLUSH_REC_BUF            4
#define I2S_EN                   3
#define REC_EN                   2
#define TRAN_EN                  1
#define SLAVE_EN                 0

/* clk div mask */
#define DIV_MASK                0xfff

/* channel num mask*/
#define CHANNEL_NUM_MASK        0x70000

/* channel num mask*/
#define FSYNC_BIT_MASK          0x3000

/* channel num mask*/
#define SLOT_NUM_MASK           0x30000

#define PCM_I2S_REG(info, offset)       (info->reg_base + offset)

enum {
	DAIFMT_I2S = 0x1,
	DAIFMT_PCM,
	DAIFMT_TDM,
};

enum {
	MASTER_MODE = 0x0,
	SLAVE_MODE,
};

enum {
	FORMAT_S16 = 16,
	FORMAT_S24 = 24,
	FORMAT_S32 = 32,
};

enum {
	TRIGGER_STOP = 0x0,
	TRIGGER_START,
};

enum {
	PLAYBACK_STREAM = 0x0,
	CAPTURE_STREAM,
};

/* jr510 res two i2s, support three modes: i2s/pcm/tdm. undefine function currently */
enum {
	PCM_I2S_0 = 0,
	PCM_I2S_1,
	PCM_I2S_MAX,
};

struct jlq_i2s_aif_config {
	void __iomem *reg_base;
	u32 format;
	u32 ms_mode;
	u32 dly_mode;
	u32 mclk_rate;
	u32 bits_per_sample;
	u32 channels;
	u32 rate;
	u32 playback_open_times;
	u32 record_open_times;
	struct clk *clk;
	struct mutex i2s_lock;
} __packed;


struct jlq_pcm_cfg {
	uint32_t sample_rate;
	uint16_t bit_width;
	uint16_t num_channels;
	uint16_t slot_number_mapping[4];
} __packed;

struct jlq_i2s_cfg {
	uint32_t num_channels;
	uint16_t bit_width;
	uint16_t ws_src;
	uint32_t sample_rate;
} __packed;

struct jlq_tdm_cfg {
	uint32_t num_channels;
	uint32_t sample_rate;
	uint32_t bit_width;
	uint16_t data_format;
} __packed;

union jlq_i2s_port_config {
	struct jlq_pcm_cfg pcm;
	struct jlq_i2s_cfg i2s;
	struct jlq_tdm_cfg tdm;
} __packed;


extern struct jlq_i2s_aif_config i2s_configs[PCM_I2S_MAX];
extern int jlq_i2s_config(u32 i2s_id);
extern int jlq_i2s_trigger(u32 i2s_id, u32 direction, u32 cmd);
extern int jlq_i2s_shutdown(u32 i2s_id);
extern int jlq_i2s_set_param(u16 port_id, u16 mode, 
			union jlq_i2s_port_config *i2s_port_config);


#endif
