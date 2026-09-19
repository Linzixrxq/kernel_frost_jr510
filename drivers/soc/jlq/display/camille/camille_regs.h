/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */
#ifndef _CAMILLE_REGS_H_
#define _CAMILLE_REGS_H_

#include <linux/io.h>
#include "camille.h"

#define  ALL_TM_CTRL1        (0x000)
#define  ALL_TM_CTRL2        (0x004)
#define  ALL_TM_CTRL3        (0x008)
#define  ALL_TM_IRQ_RAW      (0x00C)
#define  ALL_TM_IRQ_MASK     (0x010)
#define  ALL_TM_IRQ_STATUS   (0x014)
#define  ALL_TM_STATUS1      (0x018)
#define  ALL_TM_STATUS2      (0x01C)
#define  LTM_STATUS1         (0x020)
#define  LTM_STATUS2         (0x024)
#define  LTM_STATUS3         (0x028)
#define  LTM_ERR             (0x02C)
#define  LTM_CTRL1           (0x030)
#define  LTM_CTRL2           (0x034)
#define  LTM_CTRL3           (0x038)
#define  LTM_IMAGE_SIZE      (0x03C)
#define  LTM_BLOCK_SIZE      (0x040)
#define  LTM_BLOCK_NUM       (0x044)
#define  LTM_LINEAR_ADJ      (0x048)
#define  LTM_STRENGTH        (0x04C)
#define  PWM_CTRL0           (0x050)
#define  PWM_CTRL1           (0x054)
#define  PWM_CTRL2           (0x058)
#define  PWM_CTRL3           (0x05C)
#define  AGTM_KRAM_WPORT     (0x060)
#define  AGTM_KRAM1_RADDR    (0x064)
#define  AGTM_KRAM1_RDATA    (0x068)
#define  AGTM_KRAM2_RADDR    (0x06C)
#define  AGTM_KRAM2_RDATA    (0x070)
#define  PWM_CTRL1_SH        (0x154)
#define  PWM_CTRL2_SH        (0x158)

#define LTM_ADJ_CURVE0_ADDR   (0x200)

#define  PWM_EN	BIT(0)
#define  TM_ENABLE	BIT(0)
#define  TM_ENABLE_SHIFT          (6)
#define  CLTM_ENABLE_SHIFT        (5)
#define  AGTM_ENABLE_SHIFT        (4)
#define  AGTM_INPUT_MUX_SHIFT     (3)
#define  CLTM_INPUT_MUX_SHIFT     (2)
#define  TM_OUTPUT_MUX_SHIFT      (0)

#define PIXEL_TOO_EARLY_ERR       BIT(7)

#define STRENGTH_STEP_ERR         BIT(4)
#define IMAGE_SIZE_ERR            BIT(3)
#define BLOCK_NUM_ERR             BIT(2)
#define BLOCK_SIZE_ERR            BIT(1)
#define ADJ_RATIO_NEG_1024_ERR    BIT(0)
/* TM */
#define TM_MAPPING_OK             BIT(6)

/* CLTM */
#define CLTM_SMOOTH_OK            BIT(5)
#define CLTM_HIST_OK              BIT(4)
#define CLTM_CDF_OK               BIT(3)

#define TM_UNDERRUN               BIT(2)
#define SHADOW_UPDATE_OK          BIT(0)

#define CAMILLE_ERR_EVENTS    \
	(TM_UNDERRUN | ADJ_RATIO_NEG_1024_ERR | BLOCK_SIZE_ERR |\
	BLOCK_NUM_ERR | IMAGE_SIZE_ERR | STRENGTH_STEP_ERR |\
	PIXEL_TOO_EARLY_ERR)

#define SHADOW_UPDATE_EN         BIT(1)
#define SHADOW_UPDATE_FORCE      BIT(0)

#define LUT_W_EN                BIT(27)
#define LUT_W_ADDR_SHIFT        (19)
#define LUT_W_DATA_SHIFT        (0)
#define LUT_ALL_WRITE_OK        BIT(28)

static inline u32 camille_read(struct camille_dev *camille, u32 reg)
{
	return readl(camille->regs_base + reg);
}

static inline void camille_write(struct camille_dev *camille, u32 reg, u32 val)
{
	writel(val, camille->regs_base + reg);
}

static inline void camille_write_mask(struct camille_dev *camille,
			u32 reg, u32 val, u32 mask)
{
	u32 tmp = camille_read(camille, reg);

	tmp &= (~mask);
	camille_write(camille, reg, val | tmp);
}
#endif /*_CAMILLE_REGS_H_*/
