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
#include "fpc1020.h"
#include "jlq_fp_sensor.h"

#ifdef CONFIG_MICROTRUST_TEE_SUPPORT
const struct TEEC_UUID uuid_fpc = { 0x7778c03f, 0xc30c, 0x4dd0, {0xa3, 0x19, 0xea, 0x29, 0x64, 0x3d, 0x4d, 0x4b}};
#endif

int fp_sensor_vendor = VENDOR_NONE; // 1: fpc; 2: goodix; 3: silead
EXPORT_SYMBOL(fp_sensor_vendor);

static struct fpc_dev g_fpc_dev;
static irqreturn_t fpc_irq_handler(int irq, void *handle);

static int fpc_regulator_power(void)
{
	#define vdd 3000000UL
	int status = regulator_set_voltage(g_fpc_dev.vdd_reg, vdd, vdd);
	if (status != 0) {
		pr_err("%s, err set voltag = %d\n", __func__, status);
		return GENERIC_ERR;
	}
	status = regulator_enable(g_fpc_dev.vdd_reg);
	if (status != 0) {
		pr_err("%s, err enable = %d\n", __func__, status);
		return GENERIC_ERR;
	}
	pr_info("%s\n", __func__);
	return GENERIC_OK;
}

static int fpc_read_id(void)
{
	int status = 0;
	struct spi_message m;
	u8 tx[6] = {0xfc, 0, 0, 0, 0, 0};
	struct spi_transfer id = {
		.speed_hz = 1000000,
		.tx_buf = tx,
		.rx_buf = tx,
		.len = sizeof(tx),
	};
	status = spi_setup(g_fpc_dev.spidev);
	pr_info("%s spi_setup = %d\n", __func__, status);
	spi_message_init(&m);
	spi_message_add_tail(&id, &m);
	status = spi_sync(g_fpc_dev.spidev, &m);
	pr_info("%s, spi_sync = %d id = %2x %2x %2x %2x %2x %2x\n", __func__,
			status, tx[0], tx[1], tx[2], tx[3], tx[4], tx[5]);
	//if (tx[1] < 0x10) {
	//	return GENERIC_ERR;
	//}

	if (tx[1] == 0x10 && (tx[2] == 0x13 || tx[2] == 0x23 || tx[2] == 0x31)) {
		fp_sensor_vendor = VENDOR_FPC;
		pr_info("%s, fp sensor vendor is fpc\n", __func__);
	} else if ((tx[4] == 0x35 && tx[5] == 0x61) || (tx[4] == 0x50 && tx[5] == 0x62)) {
		fp_sensor_vendor = VENDOR_SILEAD;
		pr_info("%s, fp sensor vendor is silead\n", __func__);
	} else if (tx[1] >= 0x10) {
		fp_sensor_vendor = VENDOR_FPC;
		pr_info("%s, fp sensor vendor is fpc, other model number\n", __func__);
	} else {
		u8 tx1[5] = {0xf1, 0, 0, 0, 0};
		struct spi_transfer id1 = {
			.speed_hz = 1000000,
			.tx_buf = tx1,
			.rx_buf = tx1,
			.len = sizeof(tx1),
		};
		spi_message_init(&m);
		spi_message_add_tail(&id1, &m);
		status = spi_sync(g_fpc_dev.spidev, &m);
		pr_info("%s, spi_sync = %d id = %2x %2x %2x %2x %2x\n", __func__, status, tx1[0], tx1[1], tx1[2], tx1[3], tx1[4]);
		if ((tx1[1] == 0x03 || tx1[1] == 0x04) && tx1[4] == 0x25) {
			fp_sensor_vendor = VENDOR_GOODIX;
			pr_info("%s, fp sensor vendor is goodix\n", __func__);
		}
	}

	if (fp_sensor_vendor != VENDOR_FPC) {
		return GENERIC_ERR;
	}
	return GENERIC_OK;
}

static void fpc_pinctrl(void)
{
	int i = 0;
	const int len = 4;
	const int ms[len] = {2, 2, 2, 3};
	const char ptl_name[len][16] = {"fpc_vdd_on", "fpc_rst_hi", "fpc_rst_lo", "fpc_rst_hi"};
	struct pinctrl_state *ptl_state = NULL;
	for (i = 0; i < len; i++) {
		ptl_state = pinctrl_lookup_state(g_fpc_dev.ptl, ptl_name[i]);
		if (!IS_ERR(ptl_state)) {
			pr_info("%s, found:%s\n", __func__, ptl_name[i]);
			pinctrl_select_state(g_fpc_dev.ptl, ptl_state);
			msleep(ms[i]);
		} else {
			pr_info("%s, no found:%s\n", __func__, ptl_name[i]);
		}
	}
}

static void fpc_gpio_exit(struct device *dev)
{
	int i = 0;
	const char ptl_name[2][16] = {"fpc_vdd_lo", "fpc_rst_lo"};
	struct pinctrl_state *ptl_state = NULL;
	if (!IS_ERR(g_fpc_dev.vdd_reg)) {
		//regulator_disable(g_fpc_dev.vdd_reg);
	}
	if (g_fpc_dev.irq_num != 0){
		disable_irq_nosync(g_fpc_dev.irq_num);
		devm_free_irq(dev, g_fpc_dev.irq_num, dev);
		g_fpc_dev.irq_num = 0;
	}
	if(!IS_ERR(g_fpc_dev.ptl)){
		for (i = 0; i < 2; i++) {
			ptl_state = pinctrl_lookup_state(g_fpc_dev.ptl, ptl_name[i]);
			if (!IS_ERR(ptl_state)) {
				if (0 != i) {
					pinctrl_select_state(g_fpc_dev.ptl, ptl_state);
				}
			}
		}
	} else {
		for (i = 0; i < FPC_GPIO_NUM;i++) {
			if (gpio_is_valid(g_fpc_dev.gpio[i])) {
				if (VDD != i) {
					gpio_set_value(g_fpc_dev.gpio[i], 0);
				}
				devm_gpio_free(dev, g_fpc_dev.gpio[i]);
			}
		}
	}
	fpc_ree_exit();
}

static int fpc_wakeup_init(struct device *dev)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	g_fpc_dev.ttw_wl = wakeup_source_register(dev, "fpc_ttw_wl");
#else
	wakeup_source_init(g_fpc_dev.ttw_wl, "fpc_ttw_wl");
#endif

#ifdef CONFIG_MICROTRUST_TEE_SUPPORT
	memcpy(&uuid_fp, &uuid_fpc, sizeof(struct TEEC_UUID));
#endif

	fpc_ree_init(g_fpc_dev.spidev);
	enable_irq_wake(g_fpc_dev.irq_num);
	return GENERIC_OK;
}

static int fpc_irq_init(struct device *dev)
{
	int status = 0;
	int irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	//g_fpc_dev.irq_num = irq_of_parse_and_map(g_fpc_dev.node, 0);
	g_fpc_dev.irq_num = gpio_to_irq(g_fpc_dev.gpio[IRQ]);
	if (g_fpc_dev.irq_num == 0) {
		pr_err("%s, err irq map\n", __func__);
		return GENERIC_ERR;
	}
	status = devm_request_threaded_irq(dev, g_fpc_dev.irq_num,
			NULL, fpc_irq_handler, irqf, dev_name(dev), dev);
	if (status != 0) {
		pr_err("%s, err request irq num = %d, status = %d\n", __func__, g_fpc_dev.irq_num, status);
		return GENERIC_ERR;
	}
	pr_info("%s, irq_num = %d\n", __func__, g_fpc_dev.irq_num);
	return GENERIC_OK;
}

static int fpc_gpio_init(struct device *dev)
{
	if (!IS_ERR(g_fpc_dev.ptl)) {
		fpc_pinctrl();
	} else {
		if (gpio_is_valid(g_fpc_dev.gpio[VDD])) {
			pr_info("%s, gpio power\n", __func__);
			gpio_direction_output(g_fpc_dev.gpio[VDD], 1);
		} else if (!IS_ERR(g_fpc_dev.vdd_reg)) {
			fpc_regulator_power();
		}
		if (gpio_is_valid(g_fpc_dev.gpio[RST])) {
			pr_info("%s gpio reset\n", __func__);
			msleep(2);
			gpio_direction_output(g_fpc_dev.gpio[RST], 1);
			msleep(2);
			gpio_set_value(g_fpc_dev.gpio[RST], 0);
			msleep(2);
			gpio_set_value(g_fpc_dev.gpio[RST], 1);
			msleep(3);
		}
	}
	pr_info("%s, after reset irq = %d\n", __func__, gpio_get_value(g_fpc_dev.gpio[IRQ]));
	if (g_fpc_dev.spidev != NULL && fpc_read_id() != 0) {
		fpc_gpio_exit(dev);
		return GENERIC_ERR;
	}
	return GENERIC_OK;
}

static int fpc_dts_init(struct device *dev)
{
	int i = 0;
	int status = 0;
	const char *node_name = "fpc,fpc1020";
	const char gpio_name[FPC_GPIO_NUM][8] = {"fpc_vdd", "fpc_rst", "fpc_irq"};
	const char *vdd_name = "vdd_fpc";
	g_fpc_dev.ptl = devm_pinctrl_get(dev);
	g_fpc_dev.vdd_reg = devm_regulator_get_optional(dev, vdd_name);
	g_fpc_dev.node = of_find_compatible_node(NULL, NULL, node_name);
	if (g_fpc_dev.node == NULL) {
		pr_err("%s, err node %s\n", __func__, node_name);
		return GENERIC_ERR;
	}
	for (i = 0; i < FPC_GPIO_NUM; i++) {
		g_fpc_dev.gpio[i] = of_get_named_gpio(g_fpc_dev.node, gpio_name[i], 0);
		if (!gpio_is_valid(g_fpc_dev.gpio[i])) {
			pr_info("%s, no %s\n", __func__, gpio_name[i]);
			continue;
		}
		status = devm_gpio_request(dev, g_fpc_dev.gpio[i], gpio_name[i]);
		if (status) {
			pr_err("%s, err request %s, status = %d\n", __func__, gpio_name[i], status);
			return GENERIC_ERR;
		}
	}
	return GENERIC_OK;
}

static int fpc_driver_init(struct device *dev)
{
	#define len 4
	int i = 0;
	int (*fpc_p[len])(struct device *dev) = {fpc_dts_init, fpc_gpio_init, fpc_irq_init, fpc_wakeup_init};
	for (i = 0; i < len; i++) {
		if(fpc_p[i](dev) != GENERIC_OK) return GENERIC_ERR;
	}
	return GENERIC_OK;
}

static ssize_t clk_enable_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	(void)dev;(void)attr;(void)buf;
#ifdef CONFIG_JLQ_SOLUTION
	/*if (*buf == '1') {
		clk_prepare_enable(g_fpc_dev.spidev);
	} else {
		clk_disable_unprepare(g_fpc_dev.spidev);
	}
	pr_info("%s, buf = %s\n", __func__, buf);*/
#endif
	return count;
}
static DEVICE_ATTR(clk_enable, S_IWUSR, NULL, clk_enable_set);

static ssize_t wakeup_enable_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	__pm_wakeup_event(g_fpc_dev.ttw_wl, 2000);
	(void)dev;(void)attr;(void)buf;
	return count;
}
static DEVICE_ATTR(wakeup_enable, S_IWUSR, NULL, wakeup_enable_set);

static ssize_t compatible_all_set(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	if (0 == strncmp(buf, "enable", strlen("enable")) && g_fpc_dev.irq_num == 0) {
		if (fpc_driver_init(dev) != GENERIC_OK) {
			return GENERIC_ERR;
		}
	} else if (0 == strncmp(buf, "disable", strlen("disable")) && g_fpc_dev.irq_num != 0) {
		fpc_gpio_exit(dev);
	}
	(void)attr;
	return count;
}

static DEVICE_ATTR(compatible_all, S_IWUSR, NULL, compatible_all_set);

static ssize_t irq_get(struct device *dev, struct device_attribute *attr, char *buf)
{
	(void)dev;(void)attr;
	return scnprintf(buf, PAGE_SIZE, "%i\n", gpio_get_value(g_fpc_dev.gpio[IRQ]));
}

static ssize_t irq_ack(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	(void)dev;(void)attr;(void)buf;
	return count;
}
static DEVICE_ATTR(irq, S_IRUSR | S_IWUSR, irq_get, irq_ack);

static struct attribute *fpc_attributes[] = {
	&dev_attr_wakeup_enable.attr,
	&dev_attr_irq.attr,
	&dev_attr_clk_enable.attr,
	&dev_attr_compatible_all.attr,
	NULL
};

static struct attribute_group fpc_attribute_group = {
	.attrs = fpc_attributes,
};

static irqreturn_t fpc_irq_handler(int irq, void *handle)
{
	struct device *dev = (struct device *)handle;
	sysfs_notify(&dev->kobj, NULL, dev_attr_irq.attr.name);
	return IRQ_HANDLED;
}

int fpc_spi_probe(struct spi_device *dev)
{
	int status = sysfs_create_group(&dev->dev.kobj, &fpc_attribute_group);
	if (status != 0) {
		pr_err("%s err sysfs_create_group = %d\n", __func__, status);
		return GENERIC_ERR;
	}
	g_fpc_dev.spidev = dev;
	return fpc_driver_init(&dev->dev);
}

int fpc_spi_remove(struct spi_device *dev)
{
	fpc_gpio_exit(&dev->dev);
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	wakeup_source_unregister(g_fpc_dev.ttw_wl);
#else
	wakeup_source_trash(g_fpc_dev.ttw_wl);
#endif
	sysfs_remove_group(&dev->dev.kobj, &fpc_attribute_group);
	return GENERIC_OK;
}

int fpc_plat_probe(struct platform_device *dev)
{
	pr_info("%s,  fpc\n", __func__);
	int status = sysfs_create_group(&dev->dev.kobj, &fpc_attribute_group);
	if (status != 0) {
		pr_err("%s err sysfs_create_group = %d\n", __func__, status);
		return GENERIC_ERR;
	}
	return fpc_driver_init(&dev->dev);
}

int fpc_plat_remove(struct platform_device *dev)
{
	fpc_gpio_exit(&dev->dev);
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	wakeup_source_unregister(g_fpc_dev.ttw_wl);
#else
	wakeup_source_trash(g_fpc_dev.ttw_wl);
#endif
	sysfs_remove_group(&dev->dev.kobj, &fpc_attribute_group);
	return GENERIC_OK;
}

static struct of_device_id fpc_of_match[] = {
	{ .compatible = "fpc,fpc1020", },
	{}
};

MODULE_DEVICE_TABLE(of, fpc_of_match);

static struct spi_driver fpc_spi_driver = {
	.driver = {
		.name = FPC_MODULE_NAME,
		.owner = THIS_MODULE,
		.bus = &spi_bus_type,
		.of_match_table = fpc_of_match,
	},
	.probe = fpc_spi_probe,
	.remove = fpc_spi_remove,
};

static struct platform_driver fpc_plat_driver = {
	.driver = {
		.name = FPC_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = fpc_of_match,
	},
	.probe = fpc_plat_probe,
	.remove = fpc_plat_remove,
};

static int __init fpc_init(void)
{	pr_err("%s, 222 fpc\n", __func__);
int status = 0;
#ifdef FPC_SPI
 status = spi_register_driver(&fpc_spi_driver);
#else
 status = platform_driver_register(&fpc_plat_driver);
 #endif
 return status;
}

static void __exit fpc_exit(void)
{
#ifdef FPC_SPI
 spi_unregister_driver(&fpc_spi_driver);
#else
 platform_driver_unregister(&fpc_plat_driver);
  #endif
}

module_init(fpc_init);
module_exit(fpc_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("sheldon <sheldon.xie@fingerprints.com>");
MODULE_SOFTDEP("pre: spi-jlq");

