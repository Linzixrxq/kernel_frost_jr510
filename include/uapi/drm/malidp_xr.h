/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 */

#ifndef _MALIDP_XR_H_
#define _MALIDP_XR_H_

#ifndef __KERNEL__
#include <stdint.h>
#else
#include <linux/types.h>
#endif

/* sensor buffer info:
 * dma_fd: file descriptor for sensor buffer
 * offset: buffer content offset from start
 * element_size: head pose size including timestamp, quarternion and position
 * element_num: the number of element in the ring
 * left_offset/right_offset: the ring address from buffer start address for
			     left/right eys.
 */

struct malidp_sensor_buffer_info {
	int dma_fd;
	uint32_t offset;
	uint32_t element_size;
	uint32_t element_num;
	uint32_t left_offset, right_offset;
};

/* quaternion describe object rotation
 * position describe ojbect transfrom
 * for usermode, x,y,z,w are float
 * for kernel driver, we use float32(uint32_t)
 */
#ifndef __KERNEL__
struct malidp_quaternion
{
	float x, y, z, w;
};

struct malidp_position
{
	float x, y, z;
};
#else
struct malidp_quaternion
{
	uint32_t x, y, z, w;
};

struct malidp_position
{
	uint32_t x, y, z;
};
#endif

struct malidp_sensor_data
{
	uint32_t timestamp;
	struct malidp_position pos;
	struct malidp_quaternion quat;
};

#endif
