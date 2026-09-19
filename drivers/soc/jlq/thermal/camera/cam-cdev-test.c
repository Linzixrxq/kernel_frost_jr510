// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
#  Copyright (c) 2021-2022 JLQ Technology Co., Ltd. or its affiliates.
#  All Rights Reserved.
#  Confidential and Proprietary - JLQ Technology Co., Ltd. or its affiliates.
-------------------------------------------------------------------------------
#  @file cam-cdev.c
#  @brief jlq camera thermal test cooling device
#******************************************************************************/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/version.h>
#include "cam-cdev-test.h"

static const struct of_device_id cam_cdev_match_table[] = {
    {
        .compatible = "jlq,qtang2-camera-test-cdev",
        .data = NULL,
    },
    {}
};
MODULE_DEVICE_TABLE(of, cam_cdev_match_table);

static int cam_get_max_state(struct thermal_cooling_device *cdev,
        unsigned long *state)
{
    struct cam_cdev_dev *cdev_dev = cdev->devdata;
    if (!cdev_dev)
        return -EINVAL;
    *state = cdev_dev->max_state;
    return 0;
}

static int cam_get_cur_state(struct thermal_cooling_device *cdev,
        unsigned long *state)
{
    struct cam_cdev_dev *cdev_dev = cdev->devdata;
    if (!cdev_dev)
        return -EINVAL;
    *state = cdev_dev->cur_state;
    return 0;
}

static int cam_set_cur_state(struct thermal_cooling_device *cdev,
        unsigned long state)
{
    struct cam_cdev_dev *cdev_dev = cdev->devdata;
    char *envp[3];
    if (!cdev_dev)
        return -EINVAL;
    cdev_dev->cur_state = state > cdev_dev->max_state ? cdev_dev->max_state : state;
    envp[0] = kasprintf(GFP_KERNEL, "NAME=%s", cdev->type);
    envp[1] = kasprintf(GFP_KERNEL, "STATE=%lu", cdev_dev->cur_state);
    envp[2] = NULL;
    kobject_uevent_env(&cdev->device.kobj, KOBJ_CHANGE, envp);
    pr_err("%s: %lu\n", cdev->type, cdev_dev->cur_state);
    return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
static int cam_get_min_state(struct thermal_cooling_device *cdev,
        unsigned long *state)
{
    struct cam_cdev_dev *cdev_dev = cdev->devdata;
    if (!cdev_dev)
        return -EINVAL;
    *state = cdev_dev->min_state;
    return 0;
}

static int cam_set_min_state(struct thermal_cooling_device *cdev,
        unsigned long state)
{
    struct cam_cdev_dev *cdev_dev = cdev->devdata;
    if (!cdev_dev)
        return -EINVAL;
    cdev_dev->min_state = state > cdev_dev->cur_state ? cdev_dev->cur_state : state;
    return 0;
}
#endif
static struct thermal_cooling_device_ops cooling_device_ops = {
    .get_max_state = cam_get_max_state,
    .get_cur_state = cam_get_cur_state,
    .set_cur_state = cam_set_cur_state,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
    .set_min_state = cam_set_min_state,
    .get_min_state = cam_get_min_state,
#endif
};

static void cam_cdev_release(struct device *dev, void *res) {
    thermal_cooling_device_unregister(*(struct thermal_cooling_device **)res);
}

static int cam_cdev_probe(struct platform_device *pdev)
{
    struct thermal_cooling_device *cdev = NULL, **ptr;
    int ret = 0;
    struct cam_cdev_dev *cdev_dev = devm_kzalloc(&pdev->dev, sizeof(struct cam_cdev_dev), GFP_KERNEL);
    if (cdev_dev == NULL) {
        pr_err("%s: devm_kzalloc failed\n");
        ret = -ENOMEM;
        goto cam_cdev_probe_exit;
    }
    cdev_dev->max_state = 2;
    platform_set_drvdata(pdev, cdev_dev);
    cdev_dev->dev = &pdev->dev;
    cdev = thermal_of_cooling_device_register(
            pdev->dev.of_node,
            pdev->dev.of_node->name,
            cdev_dev,
            &cooling_device_ops);
    if (cdev == NULL) {
        pr_err("%s: thermal_of_cooling_device_register failed\n");
        ret = -EINVAL;
        goto cam_cdev_probe_exit;
    }
    ptr = devres_alloc(cam_cdev_release, sizeof(*ptr),
                   GFP_KERNEL);
    if (ptr == NULL) {
        pr_err("%s: devres_alloc failed\n");
        ret = -ENOMEM;
        goto cam_cdev_probe_exit;
    }
    *ptr = cdev;
    devres_add(&pdev->dev, ptr);
cam_cdev_probe_exit:
    return ret;
}

static int cam_cdev_remove(struct platform_device *pdev)
{
    return 0;
}

static struct platform_driver cam_cdev_driver = {
    .probe = cam_cdev_probe,
    .remove = cam_cdev_remove,
    .driver = {
        .name = "jlq,qtang2-camera-test-cdev",
        .of_match_table = cam_cdev_match_table,
    },
};

int jlq_cam_cdev_test_init(void)
{
    pr_err("%s\n", __func__);
    return platform_driver_register(&cam_cdev_driver);
}

void jlq_cam_cdev_test_exit(void)
{
    pr_err("%s\n", __func__);
    platform_driver_unregister(&cam_cdev_driver);
}
