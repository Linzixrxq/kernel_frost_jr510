/*
 * JLQ Devices Pin controller driver include
 *
 * Copyright 2018~2019 JLQ Technology Co.,
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


#ifndef _DT_BINDINGS_JLQ_PINCTRL_H
#define _DT_BINDINGS_JLQ_PINCTRL_H

/* Pull type */
#define MUXPIN_PULL_DISABLE	0
#define MUXPIN_PULL_UP	1
#define MUXPIN_PULL_DOWN	2
#define MUXPIN_PULL_DEFAULT	0xffff

/* Drive strength */
#define TYPE0_DRIVE_LEVEL0	0
#define TYPE0_DRIVE_LEVEL1	1
#define TYPE0_DRIVE_LEVEL2	2
#define TYPE0_DRIVE_LEVEL3	3

#define TYPE1_DRIVE_LEVEL0	0
#define TYPE1_DRIVE_LEVEL1	1
#define TYPE1_DRIVE_LEVEL2	2
#define TYPE1_DRIVE_LEVEL3	3
#define TYPE1_DRIVE_LEVEL4	4
#define TYPE1_DRIVE_LEVEL5	5

#define TYPE2_DRIVE_LEVEL0	0
#define TYPE2_DRIVE_LEVEL1	1
#define TYPE2_DRIVE_LEVEL2	2
#define TYPE2_DRIVE_LEVEL3	3

#define MUXPIN_DRIVE_MAX	0xffff
#define MUXPIN_DRIVE_DEFAULT	0xffff

#define  SCHMITT_TRIGGER_ENABLE	(1)
#define  SCHMITT_TRIGGER_DISABLE	(0)
#define  SLEW_RATE_ENABLE	(1)
#define  SLEW_RATE_DISABLE	(0)

#endif /* _DT_BINDINGS_JLQ_PINCTRL_H */
