/*
 * Copyright (c)2019-2021   JLQ Technology Co.,Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#ifndef __SLIM_API_H__
#define __SLIM_API_H__


//#define LOGTAG "[slimbus][JR510]"
#define LOGTAG "[slim]"


/*master slimbus state*/
#define MASTER_SLIM_STAT_UNINIT    0xff
#define MASTER_SLIM_STAT_INITED    0x01

int master_slimbus_state_get(void);
int jlq_slim_frm_clk_enable(void);
int jlq_slim_frm_clk_disable(void);


#endif
