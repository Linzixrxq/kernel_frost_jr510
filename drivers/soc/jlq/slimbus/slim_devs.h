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

#ifndef __SLIM_DEVS_H__
#define __SLIM_DEVS_H__


/*master frame eaddr*/
extern unsigned char master_frd_ea[];

/*master interface eaddr*/
extern unsigned char master_ifd_ea[];

/*master generic eaddr*/
extern unsigned char master_gd_ea[];

/*slave interface eaddr*/
extern unsigned char slave_ifd_ea[];

/*slave generic eaddr*/
extern unsigned char slave_gd_ea[];

#define SLIM_DEVS_CHECK_OK  (1)

void slim_devs_init(void);
void slim_devs_save(unsigned int class, unsigned char *ea, unsigned char la);
void slim_devs_list(void);
int slim_devs_check(void);
void slim_ctrl_devices_list(void);
void slim_devs_reset(void);



#endif
