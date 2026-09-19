/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/input.h>

#define MAX_BUF_SIZE	256
#define VKEY_VER_CODE	"0x01"
char *vkey_buf;

struct vkey_data {
	int virtual_menu_startx;
	int virtual_home_startx;
	int virtual_back_startx;
	int virtual_key_starty;
	int virtual_key_width;
	int virtual_key_height;
	struct kobject *vkey_obj;
};

static ssize_t virtual_key_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	strlcpy(buf, vkey_buf, MAX_BUF_SIZE);
	return strnlen(buf, MAX_BUF_SIZE);
}

static struct kobj_attribute ts_virtual_keys_attr = {
	.attr = {
		.name = "virtualkeys.gslX680",
		.mode = 0444,
	},
	.show = virtual_key_show,
};

static struct attribute *virtual_key_attributes[] = {
	&ts_virtual_keys_attr.attr,
	NULL
};

static struct attribute_group virtual_key_group = {
	.attrs = virtual_key_attributes
};


static int vkey_parse_dt(struct device *dev, struct vkey_data *vdata)
{
	int rc, temp_val;
	struct device_node *np = dev->of_node;

	rc = of_property_read_u32(np, "vir_menu_startx", &temp_val);
	if (!rc)
		vdata->virtual_menu_startx = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "vir_home_startx", &temp_val);
	if (!rc)
		vdata->virtual_home_startx = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "vir_back_startx", &temp_val);
	if (!rc)
		vdata->virtual_back_startx = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "vir_key_width", &temp_val);
	if (!rc)
		vdata->virtual_key_width = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "vir_key_height", &temp_val);
	if (!rc)
		vdata->virtual_key_height = temp_val;
	else
		return rc;

	rc = of_property_read_u32(np, "vir_key_starty", &temp_val);
	if (!rc)
		vdata->virtual_key_starty = temp_val;
	else
		return rc;
	return 0;
}

static int vkeys_probe(struct platform_device *pdev)
{
	struct vkey_data *vdata;
	int ret;

	vkey_buf = devm_kzalloc(&pdev->dev, MAX_BUF_SIZE, GFP_KERNEL);

	vdata = devm_kzalloc(&pdev->dev, sizeof(*vdata), GFP_KERNEL);

	if (pdev->dev.of_node) {
		ret = vkey_parse_dt(&pdev->dev, vdata);
		if (ret) {
			dev_err(&pdev->dev, "get virtualkey cordinate failed\n");
			return -ENODEV;
		}
	} else {
		dev_err(&pdev->dev, "only dts support, ret\n");
		return -ENODEV;
	}

	vdata->vkey_obj = kobject_create_and_add("board_properties", NULL);
	if (!vdata->vkey_obj) {
		dev_err(&pdev->dev, "unable to create kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(vdata->vkey_obj, &virtual_key_group);
	if (ret < 0)
		goto err_create_sysfs;

	sprintf(vkey_buf,
		__stringify(EV_KEY) ":" __stringify(KEY_MENU) ":%d:%d:%d:%d:"
		__stringify(EV_KEY) ":" __stringify(KEY_HOME) ":%d:%d:%d:%d:"
		__stringify(EV_KEY) ":" __stringify(KEY_BACK) ":%d:%d:%d:%d\n",
		vdata->virtual_menu_startx, vdata->virtual_key_starty,
		vdata->virtual_key_width, vdata->virtual_key_height,
		vdata->virtual_home_startx, vdata->virtual_key_starty,
		vdata->virtual_key_width, vdata->virtual_key_height,
		vdata->virtual_back_startx, vdata->virtual_key_starty,
		vdata->virtual_key_width, vdata->virtual_key_height);

	platform_set_drvdata(pdev, vdata);
	dev_info(&pdev->dev, "gen-virtualkey version: %s\n", VKEY_VER_CODE);
	return 0;

err_create_sysfs:
	kobject_put(vdata->vkey_obj);
	dev_err(&pdev->dev, "Create virtual key properties failed!\n");
	return -ENODEV;
}

static int vkeys_remove(struct platform_device *pdev)
{
	struct vkey_data *vdata = platform_get_drvdata(pdev);

	sysfs_remove_group(vdata->vkey_obj, &virtual_key_group);
	kobject_put(vdata->vkey_obj);

	return 0;
}

static const struct of_device_id vkey_match_table[] = {
	{ .compatible = "jlq,gen-vkeys",},
	{ },
};

static struct platform_driver vkeys_driver = {
	.probe = vkeys_probe,
	.remove = vkeys_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "gen_vkeys",
		.of_match_table = vkey_match_table,
	},
};

module_platform_driver(vkeys_driver);
MODULE_LICENSE("GPL v2");
