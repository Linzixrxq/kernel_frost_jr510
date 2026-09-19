// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
#  Copyright (c) 2021-2022 JLQ Technology Co., Ltd. or its affiliates.
#  All Rights Reserved.
#  Confidential and Proprietary - JLQ Technology Co., Ltd. or its affiliates.
-------------------------------------------------------------------------------
#  @file cam_thermal.c
#  @brief jlq camera thermal and bwlm driver
#******************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include "cam_thermal.h"
#include "cam-cdev.h"

static const struct of_device_id cam_thermal_match_table[] = {
    {
        .compatible = "jlq,qtang2-camera-bwlm-sensor",
        .data = NULL,
    },
    {}
};
MODULE_DEVICE_TABLE(of, cam_thermal_match_table);

static int cam_thermal_bwlm_get_temp(void *data, int *temp)
{
    struct cam_thermal_dev *therm_dev = data;
    *temp = therm_dev->temp;
    return 0;
}

static int cam_thermal_bwlm_set_trip_temp(void *data, int low_temp, int high_temp)
{
    struct cam_thermal_dev *therm_dev = data;
    struct cam_thermal_trip *trip;
    list_for_each_entry(trip, &therm_dev->trips, trip) {
        if (trip->low_temp == low_temp && trip->high_temp == high_temp)
            break;
    }
    if (&(trip->trip) == &therm_dev->trips) {
        trip = devm_kzalloc(therm_dev->dev, sizeof(struct cam_thermal_trip), GFP_KERNEL);
        trip->low_temp = low_temp;
        trip->high_temp = high_temp;
        list_add_tail(&trip->trip, &therm_dev->trips);
    }
    return 0;
}

static struct thermal_zone_of_device_ops cam_thermal_bwlm_ops = {
    .get_temp = cam_thermal_bwlm_get_temp,
    .set_trips = cam_thermal_bwlm_set_trip_temp,
};

static int cam_thermal_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct thermal_zone_device *tzd = NULL;
    struct cam_thermal_dev *therm_dev = devm_kzalloc(&pdev->dev, sizeof(struct cam_thermal_dev), GFP_KERNEL);
    if (therm_dev == NULL) {
        pr_err("%s: devm_kzalloc failed\n");
        ret = -ENOMEM;
        goto cam_thermal_probe_exit;
    }
    platform_set_drvdata(pdev, therm_dev);
    therm_dev->dev = &pdev->dev;
    INIT_LIST_HEAD(&therm_dev->trips);
    tzd = devm_thermal_zone_of_sensor_register(
            &(pdev->dev), 0,
            therm_dev, &cam_thermal_bwlm_ops);
    if (tzd == NULL) {
        pr_err("%s: devm_thermal_zone_of_sensor_register failed\n");
        ret = -EINVAL;
        goto cam_thermal_probe_exit;
    }
cam_thermal_probe_exit:
    return ret;
}

static int cam_thermal_remove(struct platform_device *pdev)
{
    return 0;
}

struct cam_bwlm_show_buf{
    char *buf;
    ssize_t cnt;
};

static int cam_bwlm_show_iter(struct device *dev, void *buf) {
    struct cam_bwlm_show_buf *tmp = buf;
    struct platform_device *pdev = to_platform_device(dev);
    struct cam_thermal_dev *therm_dev = platform_get_drvdata(pdev);
    if (therm_dev)
        tmp->cnt += snprintf(tmp->buf + tmp->cnt, PAGE_SIZE - tmp->cnt, "%s: %d\n", pdev->name, therm_dev->temp);
    return 0;
}

static ssize_t cam_bwlm_show(struct device_driver *driver, char *buf) {
    struct cam_bwlm_show_buf tmp = {.buf = buf, .cnt = 0};
    if(0 != driver_for_each_device(driver, NULL, &tmp, cam_bwlm_show_iter))
        pr_info("%s failed\n", __func__);
    return tmp.cnt;
}

static int cam_bwlm_store_iter(struct device *dev, void *buf) {
    struct platform_device *pdev = to_platform_device(dev);
    struct cam_thermal_dev *therm_dev = platform_get_drvdata(pdev);
    if (therm_dev) {
        int temp = simple_strtol(buf, NULL, 10);
        if (temp < 0)
            temp = 0;
        else if (temp > 2)
            temp = 2;
        if (temp != therm_dev->temp) {
            struct thermal_zone_device *tzd = NULL;
            tzd = thermal_zone_get_zone_by_name("qtang2-camera-bwlm");
            if (tzd) {
                int i;
                int diff = temp - therm_dev->temp;
                int cnt = abs(diff);
                int step = diff/cnt;
                for (i = 0; i < cnt; i++) {
                    therm_dev->temp += step;
                    thermal_zone_device_update(tzd, THERMAL_EVENT_UNSPECIFIED);
                }
            }
        }
    }
    return 0;
}

static ssize_t cam_bwlm_store(struct device_driver *driver, const char *buf,
        size_t count) {
    if(0 != driver_for_each_device(driver, NULL, (void*)buf, cam_bwlm_store_iter))
        pr_info("%s failed\n", __func__);
    return count;
}

static DRIVER_ATTR_RW(cam_bwlm);
static struct attribute *cam_thermal_attrs[] = {
    &driver_attr_cam_bwlm.attr,
    NULL,
};
ATTRIBUTE_GROUPS(cam_thermal);

static struct platform_driver cam_thermal_driver = {
    .probe = cam_thermal_probe,
    .remove = cam_thermal_remove,
    .driver = {
        .name = "jlq,qtang2-camera-bwlm-sensor",
        .of_match_table = cam_thermal_match_table,
        .groups = cam_thermal_groups
    },
};

static int cam_thermal_init(void)
{
    int ret;
    pr_err("%s\n", __func__);
    ret = jlq_cam_cdev_init();
    if (ret)
        return ret;
    return platform_driver_register(&cam_thermal_driver);
}

static void cam_thermal_exit(void)
{
    pr_err("%s\n", __func__);
    jlq_cam_cdev_exit();
    platform_driver_unregister(&cam_thermal_driver);
}

module_init(cam_thermal_init);
module_exit(cam_thermal_exit);
MODULE_DESCRIPTION("camera thermal/bwlm driver");
MODULE_LICENSE("GPL v2");
