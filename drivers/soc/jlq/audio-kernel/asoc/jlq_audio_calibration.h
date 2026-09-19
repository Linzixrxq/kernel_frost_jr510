// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 JLQ Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org.
 */
#ifndef _JLQ_AUDIO_CALIBRATION_H
#define _JLQ_AUDIO_CALIBRATION_H

/******************************************************************************
 * enum define
 *****************************************************************************/
enum {
	AUDIO_CAL_EFFECT_IMMEDIATE  = 0,
	AUDIO_CAL_EFFECT_DELAY      = 1,
	AUDIO_CAL_EFFECT_DONOT_SEND = 2,
};

/******************************************************************************
 * external function protocol type
 *****************************************************************************/
extern int audio_cal_load(int flag);


#endif /* end of _JLQ_AUDIO_CALIBRATION_H */

