// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#include "cam_sensor_dev.h"
#include "cam_req_mgr_dev.h"
#include "cam_sensor_soc.h"
#include "cam_sensor_core.h"
#include "camera_main.h"

#ifdef JLQ_CAMERA_EDIT
static struct dentry *debugfs_root;
#endif
static int cam_sensor_subdev_close_internal(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh)
{
	struct cam_sensor_ctrl_t *s_ctrl =
		v4l2_get_subdevdata(sd);

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "s_ctrl ptr is NULL");
		return -EINVAL;
	}

	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	cam_sensor_shutdown(s_ctrl);
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));

	return 0;
}

static int cam_sensor_subdev_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh)
{
	bool crm_active = cam_req_mgr_is_open(CAM_SENSOR);

	if (crm_active) {
		CAM_DBG(CAM_SENSOR, "CRM is ACTIVE, close should be from CRM");
		return 0;
	}

	return cam_sensor_subdev_close_internal(sd, fh);
}

static long cam_sensor_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	int rc = 0;
	struct cam_sensor_ctrl_t *s_ctrl =
		v4l2_get_subdevdata(sd);

	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		rc = cam_sensor_driver_cmd(s_ctrl, arg);
		if (rc)
			CAM_ERR(CAM_SENSOR,
				"Failed in Driver cmd: %d", rc);
		break;
	case CAM_SD_SHUTDOWN:
		if (!cam_req_mgr_is_shutdown()) {
			CAM_ERR(CAM_CORE, "SD shouldn't come from user space");
			return 0;
		}

		rc = cam_sensor_subdev_close_internal(sd, NULL);
		break;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid ioctl cmd: %d", cmd);
		rc = -ENOIOCTLCMD;
		break;
	}
	return rc;
}

#ifdef CONFIG_COMPAT
static long cam_sensor_init_subdev_do_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct cam_control cmd_data;
	int32_t rc = 0;

	if (copy_from_user(&cmd_data, (void __user *)arg,
		sizeof(cmd_data))) {
		CAM_ERR(CAM_SENSOR, "Failed to copy from user_ptr=%pK size=%zu",
			(void __user *)arg, sizeof(cmd_data));
		return -EFAULT;
	}

	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		rc = cam_sensor_subdev_ioctl(sd, cmd, &cmd_data);
		if (rc < 0)
			CAM_ERR(CAM_SENSOR, "cam_sensor_subdev_ioctl failed");
		break;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid compat ioctl cmd_type: %d", cmd);
		rc = -ENOIOCTLCMD;
		break;
	}

	if (!rc) {
		if (copy_to_user((void __user *)arg, &cmd_data,
			sizeof(cmd_data))) {
			CAM_ERR(CAM_SENSOR,
				"Failed to copy to user_ptr=%pK size=%zu",
				(void __user *)arg, sizeof(cmd_data));
			rc = -EFAULT;
		}
	}

	return rc;
}

#endif
static struct v4l2_subdev_core_ops cam_sensor_subdev_core_ops = {
	.ioctl = cam_sensor_subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = cam_sensor_init_subdev_do_ioctl,
#endif
	.s_power = cam_sensor_power,
};

static struct v4l2_subdev_ops cam_sensor_subdev_ops = {
	.core = &cam_sensor_subdev_core_ops,
};

static const struct v4l2_subdev_internal_ops cam_sensor_internal_ops = {
	.close = cam_sensor_subdev_close,
};

static int cam_sensor_init_subdev_params(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;

	s_ctrl->v4l2_dev_str.internal_ops =
		&cam_sensor_internal_ops;
	s_ctrl->v4l2_dev_str.ops =
		&cam_sensor_subdev_ops;
	strlcpy(s_ctrl->device_name, CAMX_SENSOR_DEV_NAME,
		sizeof(s_ctrl->device_name));
	s_ctrl->v4l2_dev_str.name =
		s_ctrl->device_name;
	s_ctrl->v4l2_dev_str.sd_flags =
		(V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS);
	s_ctrl->v4l2_dev_str.ent_function =
		CAM_SENSOR_DEVICE_TYPE;
	s_ctrl->v4l2_dev_str.token = s_ctrl;
	s_ctrl->v4l2_dev_str.close_seq_prior =
		CAM_SD_CLOSE_MEDIUM_PRIORITY;

	rc = cam_register_subdev(&(s_ctrl->v4l2_dev_str));
	if (rc)
		CAM_ERR(CAM_SENSOR, "Fail with cam_register_subdev rc: %d", rc);

	return rc;
}

static int32_t cam_sensor_driver_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int32_t rc = 0;
	int i = 0;
	struct cam_sensor_ctrl_t *s_ctrl = NULL;
	struct cam_hw_soc_info   *soc_info = NULL;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		CAM_ERR(CAM_SENSOR,
			"%s :i2c_check_functionality failed", client->name);
		return -EFAULT;
	}

	/* Create sensor control structure */
	s_ctrl = kzalloc(sizeof(*s_ctrl), GFP_KERNEL);
	if (!s_ctrl)
		return -ENOMEM;

	i2c_set_clientdata(client, s_ctrl);

	s_ctrl->io_master_info.client = client;
	soc_info = &s_ctrl->soc_info;
	soc_info->dev = &client->dev;
	soc_info->dev_name = client->name;

	/* Initialize sensor device type */
	s_ctrl->of_node = client->dev.of_node;
	s_ctrl->io_master_info.master_type = I2C_MASTER;
	s_ctrl->is_probe_succeed = 0;
	s_ctrl->last_flush_req = 0;

	rc = cam_sensor_parse_dt(s_ctrl);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "cam_sensor_parse_dt rc %d", rc);
		goto free_s_ctrl;
	}

	rc = cam_sensor_init_subdev_params(s_ctrl);
	if (rc)
		goto free_s_ctrl;

	s_ctrl->i2c_data.per_frame =
		kzalloc(sizeof(struct i2c_settings_array) *
		MAX_PER_FRAME_ARRAY, GFP_KERNEL);
	if (s_ctrl->i2c_data.per_frame == NULL) {
		rc = -ENOMEM;
		goto unreg_subdev;
	}

	s_ctrl->i2c_data.frame_skip =
		kzalloc(sizeof(struct i2c_settings_array) *
		MAX_PER_FRAME_ARRAY, GFP_KERNEL);
	if (s_ctrl->i2c_data.frame_skip == NULL) {
		rc = -ENOMEM;
		goto free_perframe;
	}

	INIT_LIST_HEAD(&(s_ctrl->i2c_data.init_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.config_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.streamon_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.streamoff_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.read_settings.list_head));

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		INIT_LIST_HEAD(&(s_ctrl->i2c_data.per_frame[i].list_head));
		INIT_LIST_HEAD(&(s_ctrl->i2c_data.frame_skip[i].list_head));
	}

	s_ctrl->bridge_intf.device_hdl = -1;
	s_ctrl->bridge_intf.link_hdl = -1;
	s_ctrl->bridge_intf.ops.get_dev_info = cam_sensor_publish_dev_info;
	s_ctrl->bridge_intf.ops.link_setup = cam_sensor_establish_link;
	s_ctrl->bridge_intf.ops.apply_req = cam_sensor_apply_request;
	s_ctrl->bridge_intf.ops.notify_frame_skip =
		cam_sensor_notify_frame_skip;
	s_ctrl->bridge_intf.ops.flush_req = cam_sensor_flush_request;

	s_ctrl->sensordata->power_info.dev = soc_info->dev;

	return rc;
free_perframe:
	kfree(s_ctrl->i2c_data.per_frame);
unreg_subdev:
	cam_unregister_subdev(&(s_ctrl->v4l2_dev_str));
free_s_ctrl:
	kfree(s_ctrl);
	return rc;
}

#ifdef JLQ_CAMERA_EDIT
/*****For example
***   To read sensor register which addr byte and data byte
***   echo "r 1 addr 1" > sensor0_debug && cat sensor0_deb***ug

***   To read sensor register which addr word and data byte
***   echo "r 2 addr 1" > sensor0_debug && cat sensor0_deb***ug

***   To read sensor register which addr byte and data word
***   echo "r 1 addr 2" > sensor0_debug && cat sensor0_deb***ug

***   To read sensor register which addr word and data word
***   echo "r 2 addr 2" > sensor0_debug && cat sensor0_deb***ug

***   To write sensor register which addr byte and data byte
***   echo "r 1 addr 1 data" > sensor0_debug
***   ...
*/
static struct g_reg_array_t {
	u32 reg_addr;
	u32 reg_value;
} g_reg_array;
static ssize_t cam_sensor_get_debug(struct file *file, char __user *buff,
				    size_t count, loff_t *ppos)
{
	struct cam_sensor_ctrl_t *s_ctrl = file->private_data;
	char debugfs_buf[256];
	u32 value;
	ssize_t len, ret = 0;

	if (s_ctrl->sensor_state == CAM_SENSOR_START) {
		value = g_reg_array.reg_value;
		CAM_DBG(CAM_SENSOR, "Current sensor addr: 0x%x value: 0x%x",
			g_reg_array.reg_addr, g_reg_array.reg_value);
		len = scnprintf(debugfs_buf, sizeof(debugfs_buf),
				"sensor register addr: 0x%x value: 0x%x\n",
				g_reg_array.reg_addr,
				value);
	} else {
		value = 0;
		len = scnprintf(debugfs_buf, sizeof(debugfs_buf),
				"sensor register invalid: 0x%x\n",
				value);
		CAM_ERR(CAM_SENSOR, "Invalid Operation");
	}

	ret = simple_read_from_buffer(buff, count, ppos,
				      debugfs_buf, strlen(debugfs_buf));

	return ret;
}

static ssize_t cam_sensor_set_debug(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	char                              debugfs_buf[256];
	u32                               buf_size;
	unsigned long                     missing;
	int32_t                           rc = 0;
	u8                                ret;
	u8                                addr_type;
	u8                                data_type;
	char                              op;
	u32                               reg_addr;
	u32                               reg_WriteValue;
	u32                               reg_ReadValue;
	struct cam_sensor_i2c_reg_array   i2c_reg;
	struct cam_sensor_i2c_reg_setting i2c_setting;
	struct cam_sensor_ctrl_t *        s_ctrl = file->private_data;

	if (s_ctrl->sensor_state == CAM_SENSOR_START) {
		buf_size = min(count, sizeof(debugfs_buf) - 1);
		missing = copy_from_user(debugfs_buf, buf, buf_size);
		if (missing)
			return -EFAULT;
		debugfs_buf[buf_size] = 0;

		ret = sscanf(debugfs_buf, "%c %d %x %d %x",
			     &op,
			     &addr_type, &reg_addr,
			     &data_type, &reg_WriteValue);
		if (ret < 0)
			CAM_ERR(CAM_SENSOR, "sscanf failed");

		i2c_setting.reg_setting = &i2c_reg;
		i2c_setting.size = 1;
		if (1 == addr_type)
			i2c_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		else if (2 == addr_type)
			i2c_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		if (1 == data_type)
			i2c_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		else if (2 == data_type)
			i2c_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		i2c_setting.delay = 1;
		/* i2c_setting.read_buf = &reg_ReadValue; */
		/* i2c_setting.read_buf_len = 1; */

		i2c_reg.delay = 0;
		i2c_reg.data_mask = 0;
		i2c_reg.reg_addr = reg_addr;
		i2c_reg.reg_data = reg_WriteValue;

		if (ret == 5 && op == 'w') {
			CAM_DBG(CAM_SENSOR, "Write reg_addr: 0x%x, data: 0x%x",
				reg_addr, reg_WriteValue);
			rc = camera_io_dev_write(&(s_ctrl->io_master_info),
						 &i2c_setting);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"Failed to write I2C settings: %d",
					rc);
				return rc;
			}
		} else if (ret == 4 && op == 'r') {
			rc = camera_io_dev_read(&(s_ctrl->io_master_info),
						reg_addr,
						&reg_ReadValue,
						i2c_setting.addr_type,
						i2c_setting.data_type);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR,
					"Failed to read I2C settings: %d",
					rc);
				return rc;
			}
			g_reg_array.reg_addr  = reg_addr;
			g_reg_array.reg_value = reg_ReadValue;
			CAM_DBG(CAM_SENSOR, "Read reg_addr: 0x%x, data: 0x%x",
				reg_addr, reg_ReadValue);
		}
	} else {
		CAM_ERR(CAM_SENSOR, "Invalid Operation");
	}
	return count;
}

static const struct file_operations cam_sensor_debug = {
	.open = simple_open,
	.write = cam_sensor_set_debug,
	.read = cam_sensor_get_debug,
};

static int cam_sensor_create_debugfs_entry(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	struct dentry *dbgfileptr = NULL;
	char debugfs_name[25];

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "null s_ctrl ptr");
		return -EINVAL;
	}

	if (!debugfs_root) {
		dbgfileptr = debugfs_create_dir("cam_sensor", NULL);
		if (!dbgfileptr) {
			CAM_ERR(CAM_SENSOR, "debugfs directory creation fail");
			rc = -ENOENT;
			goto end;
		}
		debugfs_root = dbgfileptr;
	}

	snprintf(debugfs_name, 25, "%s%d%s", "sensor",
		 s_ctrl->soc_info.index,
		 "_debug");
	dbgfileptr = debugfs_create_file(debugfs_name, 0644,
				   debugfs_root, s_ctrl, &cam_sensor_debug);
	if (IS_ERR(dbgfileptr)) {
		if (PTR_ERR(dbgfileptr) == -ENODEV)
			CAM_WARN(CAM_SENSOR, "DebugFS not enabled in kernel!");
		else
			rc = PTR_ERR(dbgfileptr);
	}
end:
	return rc;
}

static void cam_sensor_debug_unregister(void)
{
	debugfs_remove_recursive(debugfs_root);
	debugfs_root = NULL;
}
#endif

static int cam_sensor_component_bind(struct device *dev,
	struct device *master_dev, void *data)
{
	int32_t rc = 0, i = 0;
	struct cam_sensor_ctrl_t *s_ctrl = NULL;
	struct cam_hw_soc_info *soc_info = NULL;
	struct platform_device *pdev = to_platform_device(dev);

	/* Create sensor control structure */
	s_ctrl = devm_kzalloc(&pdev->dev,
		sizeof(struct cam_sensor_ctrl_t), GFP_KERNEL);
	if (!s_ctrl)
		return -ENOMEM;

	soc_info = &s_ctrl->soc_info;
	soc_info->pdev = pdev;
	soc_info->dev = &pdev->dev;
	soc_info->dev_name = pdev->name;

	/* Initialize sensor device type */
	s_ctrl->of_node = pdev->dev.of_node;
	s_ctrl->is_probe_succeed = 0;
	s_ctrl->last_flush_req = 0;

	/*fill in platform device*/
	s_ctrl->pdev = pdev;

	s_ctrl->io_master_info.master_type = CCI_MASTER;

	rc = cam_sensor_parse_dt(s_ctrl);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "failed: cam_sensor_parse_dt rc %d", rc);
		goto free_s_ctrl;
	}

	/* Fill platform device id*/
	pdev->id = soc_info->index;

	rc = cam_sensor_init_subdev_params(s_ctrl);
	if (rc)
		goto free_s_ctrl;

	s_ctrl->i2c_data.per_frame =
		kzalloc(sizeof(struct i2c_settings_array) *
		MAX_PER_FRAME_ARRAY, GFP_KERNEL);
	if (s_ctrl->i2c_data.per_frame == NULL) {
		rc = -ENOMEM;
		goto unreg_subdev;
	}

	s_ctrl->i2c_data.frame_skip =
		kzalloc(sizeof(struct i2c_settings_array) *
		MAX_PER_FRAME_ARRAY, GFP_KERNEL);
	if (s_ctrl->i2c_data.frame_skip == NULL) {
		rc = -ENOMEM;
		goto free_perframe;
	}

	INIT_LIST_HEAD(&(s_ctrl->i2c_data.init_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.config_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.streamon_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.streamoff_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.read_settings.list_head));

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		INIT_LIST_HEAD(&(s_ctrl->i2c_data.per_frame[i].list_head));
		INIT_LIST_HEAD(&(s_ctrl->i2c_data.frame_skip[i].list_head));
	}

	s_ctrl->bridge_intf.device_hdl = -1;
	s_ctrl->bridge_intf.link_hdl = -1;
	s_ctrl->bridge_intf.ops.get_dev_info = cam_sensor_publish_dev_info;
	s_ctrl->bridge_intf.ops.link_setup = cam_sensor_establish_link;
	s_ctrl->bridge_intf.ops.apply_req = cam_sensor_apply_request;
	s_ctrl->bridge_intf.ops.notify_frame_skip =
		cam_sensor_notify_frame_skip;
	s_ctrl->bridge_intf.ops.flush_req = cam_sensor_flush_request;

	s_ctrl->sensordata->power_info.dev = &pdev->dev;
	platform_set_drvdata(pdev, s_ctrl);
	s_ctrl->sensor_state = CAM_SENSOR_INIT;

#ifdef JLQ_CAMERA_EDIT
	rc = cam_sensor_create_debugfs_entry(s_ctrl);
	if (rc) {
		CAM_WARN(CAM_SENSOR, "debugfs creation failed");
		rc = 0;
	}
#endif
	CAM_DBG(CAM_SENSOR, "Component bound successfully");

	return rc;

free_perframe:
	kfree(s_ctrl->i2c_data.per_frame);
unreg_subdev:
	cam_unregister_subdev(&(s_ctrl->v4l2_dev_str));
free_s_ctrl:
	devm_kfree(&pdev->dev, s_ctrl);
	return rc;
}

static void cam_sensor_component_unbind(struct device *dev,
	struct device *master_dev, void *data)
{
	int                        i;
	struct cam_sensor_ctrl_t  *s_ctrl;
	struct cam_hw_soc_info    *soc_info;
	struct platform_device *pdev = to_platform_device(dev);

	s_ctrl = platform_get_drvdata(pdev);
	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "sensor device is NULL");
		return;
	}
#ifdef JLQ_CAMERA_EDIT
	cam_sensor_debug_unregister();
#endif
	CAM_DBG(CAM_SENSOR, "Component unbind called for: %s", pdev->name);
	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	cam_sensor_shutdown(s_ctrl);
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	cam_unregister_subdev(&(s_ctrl->v4l2_dev_str));
	soc_info = &s_ctrl->soc_info;
	for (i = 0; i < soc_info->num_clk; i++)
		devm_clk_put(soc_info->dev, soc_info->clk[i]);

	kfree(s_ctrl->i2c_data.per_frame);
	kfree(s_ctrl->i2c_data.frame_skip);
	platform_set_drvdata(pdev, NULL);
	v4l2_set_subdevdata(&(s_ctrl->v4l2_dev_str.sd), NULL);
	devm_kfree(&pdev->dev, s_ctrl);
}

const static struct component_ops cam_sensor_component_ops = {
	.bind = cam_sensor_component_bind,
	.unbind = cam_sensor_component_unbind,
};

static int cam_sensor_platform_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &cam_sensor_component_ops);
	return 0;
}

static int cam_sensor_driver_i2c_remove(struct i2c_client *client)
{
	int                        i;
	struct cam_sensor_ctrl_t  *s_ctrl = i2c_get_clientdata(client);
	struct cam_hw_soc_info    *soc_info;

	if (!s_ctrl) {
		CAM_ERR(CAM_SENSOR, "sensor device is NULL");
		return 0;
	}

	CAM_DBG(CAM_SENSOR, "i2c remove invoked");
	mutex_lock(&(s_ctrl->cam_sensor_mutex));
	cam_sensor_shutdown(s_ctrl);
	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	cam_unregister_subdev(&(s_ctrl->v4l2_dev_str));
	soc_info = &s_ctrl->soc_info;
	for (i = 0; i < soc_info->num_clk; i++)
		devm_clk_put(soc_info->dev, soc_info->clk[i]);

	kfree(s_ctrl->i2c_data.per_frame);
	kfree(s_ctrl->i2c_data.frame_skip);
	v4l2_set_subdevdata(&(s_ctrl->v4l2_dev_str.sd), NULL);
	kfree(s_ctrl);

	return 0;
}

static const struct of_device_id cam_sensor_driver_dt_match[] = {
	{.compatible = "qcom,cam-sensor"},
	{}
};

static int32_t cam_sensor_driver_platform_probe(
	struct platform_device *pdev)
{
	int rc = 0;

	CAM_DBG(CAM_SENSOR, "Adding Sensor component");
	rc = component_add(&pdev->dev, &cam_sensor_component_ops);
	if (rc)
		CAM_ERR(CAM_SENSOR, "failed to add component rc: %d", rc);

	return rc;
}

MODULE_DEVICE_TABLE(of, cam_sensor_driver_dt_match);

struct platform_driver cam_sensor_platform_driver = {
	.probe = cam_sensor_driver_platform_probe,
	.driver = {
		.name = "qcom,camera",
		.owner = THIS_MODULE,
		.of_match_table = cam_sensor_driver_dt_match,
		.suppress_bind_attrs = true,
	},
	.remove = cam_sensor_platform_remove,
};

static const struct i2c_device_id i2c_id[] = {
	{SENSOR_DRIVER_I2C, (kernel_ulong_t)NULL},
	{ }
};

static struct i2c_driver cam_sensor_driver_i2c = {
	.id_table = i2c_id,
	.probe = cam_sensor_driver_i2c_probe,
	.remove = cam_sensor_driver_i2c_remove,
	.driver = {
		.name = SENSOR_DRIVER_I2C,
	},
};

int cam_sensor_driver_init(void)
{
	int32_t rc = 0;

	rc = platform_driver_register(&cam_sensor_platform_driver);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "platform_driver_register Failed: rc = %d",
			rc);
		return rc;
	}

	rc = i2c_add_driver(&cam_sensor_driver_i2c);
	if (rc)
		CAM_ERR(CAM_SENSOR, "i2c_add_driver failed rc = %d", rc);

	return rc;
}

void cam_sensor_driver_exit(void)
{
	platform_driver_unregister(&cam_sensor_platform_driver);
	i2c_del_driver(&cam_sensor_driver_i2c);
}

MODULE_DESCRIPTION("cam_sensor_driver");
MODULE_LICENSE("GPL v2");
