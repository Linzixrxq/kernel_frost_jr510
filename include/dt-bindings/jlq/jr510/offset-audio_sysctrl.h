/*
 * JLQ Devices clock dtsi include
 *
 * Copyright 2018~2020 JLQ Technology Co.,
 * Ltd. or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __JR510_REGS_AUDIO_SYSCTRL_H
#define __JR510_REGS_AUDIO_SYSCTRL_H

#define ADSP_CLK_CTRL	                  (0x0000)
#define ADSP_STATE	                      (0x0004)
#define ADSP_CTRL	                      (0x000C)
#define ADSP_RST_CTRL	                  (0x0014)
#define ADSP_QOS_USER                     (0x0018)
#define SLIMBUS_CTRL                      (0x0100)
#define SLIMBUS_CLK_CTRL	              (0x0104)
#define SLIMBUS_RST_CTRL	              (0x0108)
#define I2S0_CLK_CTRL	                  (0x0200)
#define I2S0_RST_CTRL	                  (0x0204)
#define I2S1_CLK_CTRL	                  (0x0300)
#define I2S1_RST_CTRL	                  (0x0304)
#define DMAS0_CLK_CTRL	                  (0x0400)
#define DMAS0_RST_CTRL	                  (0x0404)
#define DMAS0_QOS_USER	                  (0x0408)
#define DMAS1_CLK_CTRL	                  (0x0500)
#define DMAS1_RST_CTRL	                  (0x0504)
#define DMAS1_QOS_USER	                  (0x0508)
#define BOLERO_CLK_CTRL	                  (0x0600)
#define BOLERO_CLK_SEL	                  (0x0604)
#define WDT_CLK_CTRL	                  (0x0700)
#define WDT_CTRL	                      (0x0704)
#define WDT_RST_CTRL	                  (0x0708)
#define CRG_CTRL                          (0x0800)
#define CRG_LP_ST                         (0x0804)
#define CRG_LP_ST1                        (0x0808)

#endif /* __JR510_REGS_AUDIO_SYSCTRL_H. */

