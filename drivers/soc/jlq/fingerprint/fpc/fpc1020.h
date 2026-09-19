/*
 * FPC Capacitive Fingerprint sensor device driver
 *
 * This driver will control the platform resources that the FPC fingerprint
 * sensor needs to operate. The major things are probing the sensor to check
 * that it is actually connected and let the Kernel know this and with that also
 * enabling and disabling of regulators, enabling and disabling of platform
 * clocks.
 * *
 * The driver will expose most of its available functionality in sysfs which
 * enables dynamic control of these features from eg. a user space process.
 *
 * The sensor's IRQ events will be pushed to Kernel's event handling system and
 * are exposed in the drivers event node. This makes it possible for a user
 * space process to poll the input node and receive IRQ events easily. Usually
 * this node is available under /dev/input/eventX where 'X' is a number given by
 * the event system. A user space process will need to traverse all the event
 * nodes and ask for its parent's name (through EVIOCGNAME) which should match
 * the value in device tree named input-device-name.
 *
 *
 * Copyright (c) 2020-2021 Fingerprint Cards AB <tech@fingerprints.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version 2
 * as published by the Free Software Foundation.
 */

#ifndef __FPC1020_H
#define __FPC1020_H

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
#include <linux/regulator/consumer.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

#define FPC_MODULE_NAME "fpc1020"
#define GENERIC_OK 0
#define GENERIC_ERR -1
#define FPC_GPIO_NUM 3
#define FPC_REE 0
#define FPC_REE_SPIDEV_MAJOR 233 //234
#define FPC_REE_BUF_SIZE (1024 * 32)

#ifdef CONFIG_JLQ_SOLUTION
#define FPC_SPI 1
#else
#define FPC_SPI 0
#endif

#ifdef CONFIG_MICROTRUST_TEE_SUPPORT
#include "teei_fp.h"
#include "tee_client_api.h"
#endif

struct fpc_dev {
	s32 irq_num;
	s32 gpio[FPC_GPIO_NUM];
	struct regulator *vdd_reg;
	struct spi_device *spidev;
	struct wakeup_source *ttw_wl;
	struct device_node *node;
	struct pinctrl *ptl;
};

enum gpio_name
{
	VDD,
	RST,
	IRQ,
};

struct fpc_ree_dev
{
	u8     *tx_buf;
	u8     *rx_buf;
	dev_t  devt;
	struct class *ree_class;
	struct spi_device *spidev;
};

int fpc_ree_init(struct spi_device *);
void fpc_ree_exit(void);
int fpc_spi_probe(struct spi_device *dev);
int fpc_spi_remove(struct spi_device *dev);
int fpc_plat_probe(struct platform_device *dev);
int fpc_plat_remove(struct platform_device *dev);

#endif // __FPC1020_H
