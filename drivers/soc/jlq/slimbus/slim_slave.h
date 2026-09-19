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

#ifndef __SLIM_SLAVE_H__
#define __SLIM_SLAVE_H__

#include "slim_inc.h"

#ifdef WCD9306
/*QCOM slave slimbus Enumeration Address*/
#define SLAVE_Manufacturer_ID   0x0217  /*QCOM MI*/
#define SLAVE_ProductId         0x00E0  /*QCOM PC*/

/*DI*/
#define SLAVE_InterfaceId        0x00  /*class: fd*/
#define SLAVE_GenericId          0x01  /*class: 00*/

/*IV*/
#define SLAVE_InstanceValue     0x00    /*IV*/
#endif

#ifdef WCD3950
/*QCOM slave slimbus Enumeration Address*/
#define SLAVE_Manufacturer_ID   0x0217  /*QCOM MI*/
#define SLAVE_ProductId         0x0220  /*QCOM PC*/

/*DI*/
#define SLAVE_InterfaceId        0x00  /*class: fd*/
#define SLAVE_GenericId          0x01  /*class: 00*/

/*IV*/
#define SLAVE_InstanceValue     0x00    /*IV*/
#endif



/*WCN3950 VE byte address*/
//0x000~0x3FF, JR510 0x000~0x1FF

void wcx_enable(void);


#endif
