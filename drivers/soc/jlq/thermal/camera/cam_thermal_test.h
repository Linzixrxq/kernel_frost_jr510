// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
#  Copyright (c) 2021-2022 JLQ Technology Co., Ltd. or its affiliates.
#  All Rights Reserved.
#  Confidential and Proprietary - JLQ Technology Co., Ltd. or its affiliates.
-------------------------------------------------------------------------------
#  @file cam_thermal.h
#  @brief jlq camera thermal and bwlm test driver header
#******************************************************************************/

#ifndef __CAM_THERMAL_TEST_H__
#define __CAM_THERMAL_TEST_H__
#include <linux/device.h>

struct cam_thermal_trip {
    struct list_head trip;
    int low_temp;
    int high_temp;
};

struct cam_thermal_dev {
    struct device *dev;
    int temp;
    struct list_head trips;
};
#endif
