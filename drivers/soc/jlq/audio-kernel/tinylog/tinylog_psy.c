// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 JLQ Corporation
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
#include "tinylog_psy.h"
#include "tinylog_controller.h"
#include "tinylog_drv.h"

static struct jlq_log_private *log_priv = NULL;

static ssize_t jlq_log_point_set_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	uint32_t point, enable, in_buf_level, ex_buf_level = 0;
	int ret = 0;
	struct point_info *point_info = &log_priv->jlq_point_info;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	if (log_priv->log_state != RUNNING) {
		pr_err("%s: log module is not enable, log_state = %d.\n",
			__func__, log_priv->log_state);
		return count;
	}

	sscanf(buf, "%10d %10d %10d %10d",
		&point, &enable, &in_buf_level, &ex_buf_level);

	pr_info("%s: point = %d, enable = %d, in_buf_level = %d, ex_buf_level = %d, count = %d.\n",
		__func__, point, enable, in_buf_level, ex_buf_level, (int)count);

	if (point >= POINT_MAX) {
		pr_err("%s: point value error, point = %d.\n",
				__func__, point);
		return count;
	}

	if (enable && point_info->point_state[point] == STOPPED) {
		if (in_buf_level < WATER_LEVEL_ONE_MESSAGE ||
			in_buf_level > WATER_LEVEL_80_PRECENT_POOL) {
			pr_err("%s: in_buf_level value error, level = %d.\n",
				__func__, in_buf_level);
			return count;
		}

		if (ex_buf_level < WATER_LEVEL_ONE_MESSAGE ||
			ex_buf_level > WATER_LEVEL_80_PRECENT_POOL) {
			pr_err("%s: ex_buf_level value error, level = %d.\n",
				__func__, ex_buf_level);
			return count;
		}

		ret = log_priv->log_ctrl_ops->log_point_open(log_priv, point,
			in_buf_level, ex_buf_level);
		if (ret < 0) {
			pr_err("%s: jlq log point open error, ret = %d.\n",
				__func__, ret);
			return count;
		}

		if (point_info->ex_virt_buf == NULL) {
			pr_info("%s: start to remap physical address 0x%x, size %d.\n",
				__func__, point_info->ex_buf_addr, point_info->ex_buf_len);
			point_info->ex_virt_buf = ioremap_nocache(point_info->ex_buf_addr,
				point_info->ex_buf_len);
			if (point_info->ex_virt_buf == NULL) {
				pr_err("%s: Failed to remap physical address 0x%x.\n",
					__func__, point_info->ex_buf_addr);
			}
			pr_info("%s: Success to remap physical address 0x%x to virt address 0x%llx, size %d.\n",
				__func__, point_info->ex_buf_addr, point_info->ex_virt_buf, point_info->ex_buf_len);
		} else {
			pr_info("%s: physical address 0x%x was already remapped to virt address 0x%llx, size %d.\n",
				__func__, point_info->ex_buf_addr, point_info->ex_virt_buf, point_info->ex_buf_len);
		}

		point_info->point_state[point] = RUNNING;
	} else if (!enable && point_info->point_state[point] == RUNNING) {
		ret = log_priv->log_ctrl_ops->log_point_close(log_priv, point);
		if (ret < 0) {
			pr_err("%s: jlq log point close error, ret = %d.\n",
				__func__, ret);
			return count;
		}
		if (point_info->ex_virt_buf != NULL) {
			iounmap(point_info->ex_virt_buf);
			point_info->ex_virt_buf = NULL;
		}
		point_info->point_state[point] = STOPPED;
	} else
		return count;

	point_info->in_buf_water_level = in_buf_level;
	point_info->ex_buf_water_level = ex_buf_level;

	return count;
}

static ssize_t jlq_log_app_set_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	uint32_t app_id, enable, in_buf_level, ex_buf_level;
	int ret = 0;

	struct app_info *app = &log_priv->jlq_app_info;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	if (log_priv->log_state != RUNNING) {
		pr_err("%s: log module is not enable, log_state = %d.\n",
			__func__, log_priv->log_state);
		return count;
	}

	sscanf(buf, "%10d %10d %10d %10d %10d",
		&app_id, &enable, &in_buf_level, &ex_buf_level);

	pr_info("%s: app_id = %d, enable = %d, in_buf_level = %d, ex_buf_level = %d, count = %d.\n",
		__func__, app_id, enable, in_buf_level, ex_buf_level, (int)count);

	if (app_id >= APP_ID_MAX) {
		pr_err("%s: app id value error, app_id = %d.\n",
			__func__, app_id);
		return count;
	}

	if (enable && app->app_state[app_id] == STOPPED) {
		if (in_buf_level < WATER_LEVEL_ONE_MESSAGE ||
				in_buf_level > WATER_LEVEL_80_PRECENT_POOL) {
			pr_err("%s: in_buf_level value error, level = %d.\n",
				__func__, in_buf_level);
			return count;
		}
		if (ex_buf_level < WATER_LEVEL_ONE_MESSAGE ||
				ex_buf_level > WATER_LEVEL_80_PRECENT_POOL) {
			pr_err("%s: ex_buf_level value error, level = %d.\n",
				__func__, ex_buf_level);
			return count;
		}
		ret = log_priv->log_ctrl_ops->log_app_open(log_priv, app_id,
				in_buf_level, ex_buf_level);
		if (ret < 0) {
			pr_err("%s: jlq log app open error, ret = %d.\n", __func__, ret);
			return count;
		}

		pr_info("%s: shared ring buf addr: 0x%x, length:%d.\n", __func__,
				app->ex_buf_addr, app->ex_buf_len);
		pr_info("%s: fmt addr: 0x%x, length:%d.\n", __func__,
				app->string_img_addr[app_id], app->string_img_len[app_id]);

		app->ex_virt_buf = ioremap_nocache(app->ex_buf_addr, app->ex_buf_len);
		if (app->ex_virt_buf == NULL) {
			pr_err("%s: Failed to remap physical rb addr 0x%x\n",
					__func__,
					app->ex_buf_addr);
			return count;
		}
		pr_info("%s: Success to remap physical rb addr 0x%x to virt addr: 0x%llx\n", __func__,
			app->ex_buf_addr, app->ex_virt_buf);

		app->string_img_virt_buf[app_id] =
			ioremap_nocache(app->string_img_addr[app_id], app->string_img_len[app_id]);
		if (app->string_img_virt_buf[app_id] == NULL) {
			pr_err("%s: Failed to remap physical fmt str addr 0x%x\n",
					__func__,
					app->string_img_addr[app_id]);
			return count;
		}
		pr_info("%s: Success to remap physical fmt str addr 0x%x to virt addr: 0x%llx\n",
				__func__,
				app->string_img_addr[app_id],
				app->string_img_virt_buf[app_id]);

		pr_info("%s: app %d string img build info: %s.\n",
			__func__,
			app_id,
			(char *)(app->string_img_virt_buf[app_id] + app->build_info_offset[app_id]));

		app->app_state[app_id] = RUNNING;
	} else if (!enable && app->app_state[app_id] == RUNNING) {
		ret = log_priv->log_ctrl_ops->log_app_close(log_priv, app_id);
		if (ret < 0) {
			pr_err("%s: jlq log app close error, ret = %d.\n", __func__, ret);
			return count;
		}
		if (app->ex_virt_buf != NULL) {
			iounmap(app->ex_virt_buf);
			app->ex_virt_buf = NULL;
		}
		if (app->string_img_virt_buf[app_id] != NULL) {
			iounmap(app->string_img_virt_buf[app_id]);
			app->string_img_virt_buf[app_id] = NULL;
		}
		app->app_state[app_id] = STOPPED;
	} else
		return count;

	app->in_buf_water_level = in_buf_level;
	app->ex_buf_water_level = ex_buf_level;

	return count;
}

static ssize_t jlq_log_app_message_level_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;
	int i = 0;
	int j = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < APP_ID_MAX; i++) {
		for (j = 0; j < MID_MAX; j++) {
			count += sprintf(&buf[count],
				"app_id = %d, module_id = %d log_level = %d\n",
				i,
				j,
				log_priv->jlq_app_info.log_level[i][j]);

		}
	}
	pr_info("%s: count = %d.\n", __func__, (int)count);

	return count;
}

static ssize_t jlq_log_app_message_level_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	uint32_t app_id, module_id, log_level = 0;
	int ret = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	if (log_priv->log_state != RUNNING) {
		pr_err("%s: log module is not enable, log_state = %d.\n",
			__func__, log_priv->log_state);
		return count;
	}

	sscanf(buf, "%10d %10d %10d", &app_id, &module_id, &log_level);

	pr_info("%s: app_id = %d, module_id = %d, log_level = %d, count = %d.\n",
		__func__, app_id, module_id, log_level, (int)count);

	if (log_level > LEVEL_DEBUG) {
		pr_err("%s: level value error, level = %d.\n",
			__func__, log_level);
		return count;
	}

	if (app_id >= APP_ID_MAX) {
		pr_err("%s: app id value error, app_id = %d.\n",
			__func__, app_id);
		return count;
	}

	if (module_id >= MID_MAX) {
		pr_err("%s: module id value error, module_id = %d.\n",
			__func__, app_id);
		return count;
	}

	ret = log_priv->log_ctrl_ops->log_app_set_msg_level(log_priv,
		app_id, module_id, log_level);
	if (ret < 0) {
		pr_err("%s: set log app message level error, ret = %d.\n",
			__func__, ret);
		return count;
	}

	log_priv->jlq_app_info.log_level[app_id][module_id] = log_level;

	return count;
}

static ssize_t jlq_log_app_message_all_level_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	uint32_t app_id, log_level = 0;
	int ret = 0;
	uint32_t i = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	if (log_priv->log_state != RUNNING) {
		pr_err("%s: log module is not enable, log_state = %d.\n",
			__func__, log_priv->log_state);
		return count;
	}

	sscanf(buf, "%10d %10d", &app_id, &log_level);

	pr_info("%s: app_id = %d, log_level = %d, count = %d.\n",
		__func__, app_id, log_level, (int)count);

	if (log_level > LEVEL_DEBUG) {
		pr_err("%s: level value error, level = %d.\n",
			__func__, log_level);
		return count;
	}

	if (app_id >= APP_ID_MAX) {
		pr_err("%s: app id value error, app_id = %d.\n",
			__func__, app_id);
		return count;
	}

	ret = log_priv->log_ctrl_ops->log_app_set_all_msg_level(log_priv,
		app_id, log_level);
	if (ret < 0) {
		pr_err("%s: set log app message all level error, ret = %d.\n",
			__func__, ret);
		return count;
	}

	for (i = 0; i < MID_MAX; i++)
		log_priv->jlq_app_info.log_level[app_id][i] = log_level;

	return count;
}

static ssize_t jlq_log_app_buffer_level_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	count += sprintf(&buf[count],
			"in_buf_water_level = %d, ex_buf_water_level = %d\n",
			log_priv->jlq_app_info.in_buf_water_level,
			log_priv->jlq_app_info.ex_buf_water_level);

	pr_info("%s: count = %d.\n", __func__, (int)count);

	return count;
}

static ssize_t jlq_log_app_buffer_level_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	uint32_t in_buf_level, ex_buf_level = 0;
	int ret = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	if (log_priv->log_state != RUNNING) {
		pr_err("%s: log module is not enable, log_state = %d.\n",
			__func__, log_priv->log_state);
		return count;
	}

	sscanf(buf, "%10d %10d", &in_buf_level, &ex_buf_level);

	pr_info("%s: in_buf_level = %d, ex_buf_level = %d, count = %d.\n",
		__func__, in_buf_level, ex_buf_level, (int)count);

	if (in_buf_level < WATER_LEVEL_ONE_MESSAGE ||
		in_buf_level > WATER_LEVEL_80_PRECENT_POOL) {
		pr_err("%s: in_buf_level value error, level = %d.\n",
			__func__, in_buf_level);
		return count;
	}

	if (ex_buf_level < WATER_LEVEL_ONE_MESSAGE ||
		ex_buf_level > WATER_LEVEL_80_PRECENT_POOL) {
		pr_err("%s: ex_buf_level value error, level = %d.\n",
			__func__, ex_buf_level);
		return count;
	}

	ret = log_priv->log_ctrl_ops->log_app_set_buffer_level(log_priv,
		in_buf_level, ex_buf_level);
	if (ret < 0) {
		pr_err("%s: set log app buffer level error, ret = %d.\n",
			__func__, ret);
		return count;
	}

	log_priv->jlq_app_info.in_buf_water_level = in_buf_level;
	log_priv->jlq_app_info.ex_buf_water_level = ex_buf_level;

	return count;
}

static ssize_t jlq_log_point_buffer_level_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	count += sprintf(&buf[count],
				"in_buf_level = %d, ex_buf_level = %d\n",
				log_priv->jlq_point_info.in_buf_water_level,
				log_priv->jlq_point_info.ex_buf_water_level);

	pr_info("%s: count = %d.\n", __func__, (int)count);

	return count;
}

static ssize_t jlq_log_point_buffer_level_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	uint32_t in_buf_water_level = 0;
	uint32_t ex_buf_water_level = 0;
	int ret = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	if (log_priv->log_state != RUNNING) {
		pr_err("%s: log module is not enable, log_state = %d.\n",
			__func__, log_priv->log_state);
		return count;
	}

	sscanf(buf, "%10d %10d", &in_buf_water_level, &ex_buf_water_level);

	pr_info("%s: in_buf_water_level = %d, ex_buf_water_level = %d, count = %d.\n",
		__func__, in_buf_water_level, ex_buf_water_level, (int)count);

	if (in_buf_water_level < WATER_LEVEL_ONE_MESSAGE ||
		in_buf_water_level > WATER_LEVEL_80_PRECENT_POOL) {
		pr_err("%s: in_buf_water_level value error, level = %d.\n",
			__func__, in_buf_water_level);
		return count;
	}

	if (ex_buf_water_level < WATER_LEVEL_ONE_MESSAGE ||
		ex_buf_water_level > WATER_LEVEL_80_PRECENT_POOL) {
		pr_err("%s: ex_buf_water_level value error, level = %d.\n",
			__func__, ex_buf_water_level);
		return count;
	}

	ret  = log_priv->log_ctrl_ops->log_point_set_buffer_level(log_priv,
		in_buf_water_level, ex_buf_water_level);
	if (ret < 0) {
		pr_err("%s: set log point buffer level error, ret = %d.\n",
			__func__, ret);
		return count;
	}

	log_priv->jlq_point_info.in_buf_water_level = in_buf_water_level;
	log_priv->jlq_point_info.ex_buf_water_level = ex_buf_water_level;

	return count;
}

static ssize_t jlq_log_enable_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	count += sprintf(&buf[count], "jlq log state: %d\n", log_priv->log_state);

	pr_info("%s: count = %d.\n", __func__, (int)count);

	return count;
}

static ssize_t jlq_sys_time_sync_store(struct kobject *kobj,
	struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	struct asm_sys_time_sync timeSync;
	int ret = 0;

	if (log_priv == NULL) {
		pr_err("%s: adsp psy was not specified\n", __func__);
		return -EINVAL;
	}

	sscanf(buf, "%10d %10d %10d %10d", &timeSync.rtc_cnt_h, &timeSync.rtc_cnt_l, &timeSync.sys_cnt_h, &timeSync.sys_cnt_l);

	pr_info("%s: timeSync: rtc = %d, %d. system count = %d, %d.\n",
		__func__, timeSync.rtc_cnt_h, timeSync.rtc_cnt_l, timeSync.sys_cnt_h, timeSync.sys_cnt_l);

	if (log_priv->log_ctrl_ops->sys_time_sync_put) {
		ret  = log_priv->log_ctrl_ops->sys_time_sync_put(log_priv,
			(struct asm_sys_time_sync *)&timeSync);
		if (ret < 0) {
			pr_err("%s: set sys time sync error, ret = %d.\n",
				__func__, ret);
		}
	} else {
		pr_err("%s: sys_time_sync_put is null.\n", __func__);
	}

	return count;
}


static struct kobj_attribute jlq_log_enable_attribute =
	__ATTR(log_enable_show, 0660, jlq_log_enable_show, NULL);

static struct kobj_attribute jlq_log_app_set_attribute =
	__ATTR(log_app_set, 0660, NULL, jlq_log_app_set_store);

static struct kobj_attribute jlq_log_app_message_level_set_attribute =
	__ATTR(log_app_message_level, 0660, jlq_log_app_message_level_show, jlq_log_app_message_level_store);

static struct kobj_attribute jlq_log_app_message_all_level_set_attribute =
	__ATTR(log_app_message_all_level_set, 0660, NULL, jlq_log_app_message_all_level_store);

static struct kobj_attribute jlq_log_app_buffer_level_set_attribute =
	__ATTR(log_app_buffer_level, 0660, jlq_log_app_buffer_level_show, jlq_log_app_buffer_level_store);

static struct kobj_attribute jlq_log_point_set_attribute =
	__ATTR(log_point_set, 0660, NULL, jlq_log_point_set_store);

static struct kobj_attribute jlq_log_point_level_set_attribute =
	__ATTR(log_point_buffer_level, 0660, jlq_log_point_buffer_level_show, jlq_log_point_buffer_level_store);

static struct kobj_attribute jlq_sys_time_sync_attribute =
	__ATTR(sys_time_sync, 0660, NULL, jlq_sys_time_sync_store);



int create_adsp_log_psy(void *drv)
{
	int ret = 0;
	struct jlq_log_private *drvdata = (struct jlq_log_private *)drv;

	drvdata->jlq_log_obj = kobject_create_and_add("adsp_log", kernel_kobj);
	if (!drvdata->jlq_log_obj) {
		pr_err("%s: sysfs create and add failed\n",
						__func__);
		ret = -ENOMEM;
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_log_point_set_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file log point set failed %d\n",
							__func__, ret);
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_log_point_level_set_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file log level set failed %d\n",
							__func__, ret);
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_log_app_message_level_set_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file log message level set failed %d\n",
							__func__, ret);
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_log_app_message_all_level_set_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file log message all level set failed %d\n",
							__func__, ret);
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_log_app_buffer_level_set_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file log buffer level set failed %d\n",
							__func__, ret);
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_log_enable_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file log enable failed %d\n",
							__func__, ret);
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_log_app_set_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file log app set failed %d\n",
							__func__, ret);
		goto error_return;
	}

	ret = sysfs_create_file(drvdata->jlq_log_obj,
			&jlq_sys_time_sync_attribute.attr);
	if (ret) {
		pr_err("%s: sysfs create file sys time sync failed %d\n",
							__func__, ret);
		goto error_return;
	}

	log_priv = (struct jlq_log_private *)drv;
	return 0;
error_return:
	return ret;
}

void destroy_adsp_log_psy(void *drv)
{
	struct jlq_log_private *drvdata = (struct jlq_log_private *)drv;

	if (drvdata->jlq_log_obj) {
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_log_point_set_attribute.attr);
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_log_app_set_attribute.attr);
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_log_point_level_set_attribute.attr);
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_log_app_message_level_set_attribute.attr);
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_log_app_message_all_level_set_attribute.attr);
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_log_app_buffer_level_set_attribute.attr);
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_log_enable_attribute.attr);
		sysfs_remove_file(drvdata->jlq_log_obj,
						&jlq_sys_time_sync_attribute.attr);

		kobject_del(drvdata->jlq_log_obj);
		drvdata->jlq_log_obj = NULL;
	}
	log_priv = NULL;
}


