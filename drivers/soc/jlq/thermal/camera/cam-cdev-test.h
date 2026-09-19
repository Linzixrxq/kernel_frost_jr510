// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
#  Copyright (c) 2021-2022 JLQ Technology Co., Ltd. or its affiliates.
#  All Rights Reserved.
#  Confidential and Proprietary - JLQ Technology Co., Ltd. or its affiliates.
-------------------------------------------------------------------------------
#  @file cam-cdev-test.h
#  @brief jlq camera thermal test cooling device header
#******************************************************************************/

#ifndef __CAM_CDEV_TEST_H__
#define __CAM_CDEV_TEST_H__
#include <linux/device.h>

struct cam_cdev_dev {
    struct device *dev;
    unsigned long max_state;
    unsigned long cur_state;
    unsigned long min_state;
};
int jlq_cam_cdev_test_init(void);
void jlq_cam_cdev_test_exit(void);
#endif
