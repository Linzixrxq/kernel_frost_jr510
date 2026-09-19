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
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <ipc/apr.h>
#include <ipc/apr_tal.h>

#include "tinylog_controller.h"
#include "tinylog_drv.h"

#define LONG_TIME_OUT 1000
#define LOG_POINT_CLOSE_ALL (0XFF)
extern int apr_send_pkt(void *handle, uint32_t *buf);


static int log_app_open(struct jlq_log_private *drvdata, unsigned int app_id,
	unsigned int in_buf_level, unsigned int ex_buf_level)
{
	int ret = -1;
	struct asm_log_app_open open;

	pr_debug("%s: open log app %d,in buf level %d,ext buf level %d\n",
		__func__, app_id, in_buf_level, ex_buf_level);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	open.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	open.hdr.pkt_size = sizeof(open);
	open.hdr.src_port = 0;
	open.hdr.dest_port = 0;
	open.hdr.src_svc = 0;
	open.hdr.src_domain = APR_DOMAIN_APPS;
	open.hdr.dest_svc = APR_SVC_TINYLOG;
	open.hdr.dest_domain = APR_DOMAIN_ADSP;
	open.hdr.token = 0;
	open.hdr.opcode = ASM_LOG_APP_OPEN;

	open.app_id = app_id;
	open.in_buf_level = in_buf_level;
	open.ex_buf_level = ex_buf_level;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&open);
	if (ret != sizeof(open)) {
		pr_err("%s: open log app %d failed %d\n", __func__, app_id, ret);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for open log app response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_app_close(struct jlq_log_private *drvdata, unsigned int app_id)
{
	int ret = -1;
	struct asm_log_app_close close;

	pr_debug("%s: close log app %d", __func__, app_id);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	close.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	close.hdr.pkt_size = sizeof(close);
	close.hdr.src_port = 0;
	close.hdr.dest_port = 0;
	close.hdr.src_svc = 0;
	close.hdr.src_domain = APR_DOMAIN_APPS;
	close.hdr.dest_svc = APR_SVC_TINYLOG;
	close.hdr.dest_domain = APR_DOMAIN_ADSP;
	close.hdr.token = 0;
	close.hdr.opcode = ASM_LOG_APP_CLOSE;

	close.app_id = app_id;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&close);
	if (ret != sizeof(close)) {
		pr_err("%s: close log app %d failed %d\n", __func__, app_id, ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for log app close response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_app_set_msg_level(struct jlq_log_private *drvdata,
	unsigned int app_id, unsigned int module_id, unsigned int log_level)
{
	int ret = -1;
	struct asm_log_app_module_message_level_set module_level;

	pr_debug("%s: set module log level, app id %d, module id %d, log level %d\n",
		 __func__, app_id, module_id, log_level);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	module_level.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	module_level.hdr.pkt_size = sizeof(module_level);
	module_level.hdr.src_port = 0;
	module_level.hdr.dest_port = 0;
	module_level.hdr.src_svc = 0;
	module_level.hdr.src_domain = APR_DOMAIN_APPS;
	module_level.hdr.dest_svc = APR_SVC_TINYLOG;
	module_level.hdr.dest_domain = APR_DOMAIN_ADSP;
	module_level.hdr.token = 0;
	module_level.hdr.opcode = ASM_LOG_APP_MODULE_MESSAGE_LEVEL_SET;

	module_level.app_id = app_id;
	module_level.module_id = module_id;
	module_level.log_level = log_level;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&module_level);
	if (ret != sizeof(module_level)) {
		pr_err("%s: set module log level failed %d, app id %d, log level %d\n",
			__func__, ret, app_id, log_level);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for setting module log level response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_app_set_all_msg_level(struct jlq_log_private *drvdata,
	unsigned int app_id, unsigned int log_level)
{
	int ret = -1;
	struct asm_log_app_all_message_level_set all_level;

	pr_debug("%s: set log level to all module, app id %d, log level %d\n",
		__func__, app_id, log_level);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	all_level.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	all_level.hdr.pkt_size = sizeof(all_level);
	all_level.hdr.src_port = 0;
	all_level.hdr.dest_port = 0;
	all_level.hdr.src_svc = 0;
	all_level.hdr.src_domain = APR_DOMAIN_APPS;
	all_level.hdr.dest_svc = APR_SVC_TINYLOG;
	all_level.hdr.dest_domain = APR_DOMAIN_ADSP;
	all_level.hdr.token = 0;
	all_level.hdr.opcode = ASM_LOG_APP_ALL_MESSAGE_LEVEL_SET;

	all_level.app_id = app_id;
	all_level.log_level = log_level;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&all_level);
	if (ret != sizeof(all_level)) {
		pr_err("%s: set all log level failed %d, app id %d, log level %d\n",
			__func__, ret, app_id, log_level);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for setting all module log level response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_app_set_buffer_level(struct jlq_log_private *drvdata,
	unsigned int in_buf_level, unsigned int ex_buf_level)
{
	int ret = -1;
	struct asm_log_app_buffer_level_set buf_level;

	pr_debug("%s: set log buffer level, in buf level %d, ex buf level %d\n",
		__func__, in_buf_level, ex_buf_level);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	buf_level.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	buf_level.hdr.pkt_size = sizeof(buf_level);
	buf_level.hdr.src_port = 0;
	buf_level.hdr.dest_port = 0;
	buf_level.hdr.src_svc = 0;
	buf_level.hdr.src_domain = APR_DOMAIN_APPS;
	buf_level.hdr.dest_svc = APR_SVC_TINYLOG;
	buf_level.hdr.dest_domain = APR_DOMAIN_ADSP;
	buf_level.hdr.token = 0;
	buf_level.hdr.opcode = ASM_LOG_APP_BUFFER_LEVEL_SET;

	buf_level.in_buf_level = in_buf_level;
	buf_level.ex_buf_level = ex_buf_level;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&buf_level);
	if (ret != sizeof(buf_level)) {
		pr_err("%s: set log buffer level failed %d, in buf level %d, ex buf level %d\n",
			__func__, ret, in_buf_level, ex_buf_level);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for setting log buffer level response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_point_open(struct jlq_log_private *drvdata,
	unsigned int dump_point, unsigned int in_buf_level,
	unsigned int ex_buf_level)
{
	int ret = -1;
	struct asm_log_point_open open;

	pr_debug("%s: open dump point %d, in buf level %d, ex buf level %d\n",
		__func__, dump_point, in_buf_level, ex_buf_level);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	open.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	open.hdr.pkt_size = sizeof(open);
	open.hdr.src_port = 0;
	open.hdr.dest_port = 0;
	open.hdr.src_svc = 0;
	open.hdr.src_domain = APR_DOMAIN_APPS;
	open.hdr.dest_svc = APR_SVC_TINYLOG;
	open.hdr.dest_domain = APR_DOMAIN_ADSP;
	open.hdr.token = 0;
	open.hdr.opcode = ASM_LOG_POINT_OPEN;

	open.dump_point = dump_point;
	open.in_buf_level = in_buf_level;
	open.ex_buf_level = ex_buf_level;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&open);
	if (ret != sizeof(open)) {
		pr_err("%s: open dump point %d failed %d, in buf level %d, ex buf level %d\n",
			__func__, dump_point, ret, in_buf_level, ex_buf_level);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for dump point open response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_point_close(struct jlq_log_private *drvdata,
	unsigned int dump_point)
{
	int ret = -1;
	struct asm_log_point_close close;

	pr_debug("%s: close dump point %d\n", __func__, dump_point);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	close.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	close.hdr.pkt_size = sizeof(close);
	close.hdr.src_port = 0;
	close.hdr.dest_port = 0;
	close.hdr.src_svc = 0;
	close.hdr.src_domain = APR_DOMAIN_APPS;
	close.hdr.dest_svc = APR_SVC_TINYLOG;
	close.hdr.dest_domain = APR_DOMAIN_ADSP;
	close.hdr.token = 0;
	close.hdr.opcode = ASM_LOG_POINT_CLOSE;

	close.dump_point = dump_point;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&close);
	if (ret != sizeof(close)) {
		pr_err("%s: close dump point %d failed %d\n",
			__func__, dump_point, ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for dump point close response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_point_close_all(struct jlq_log_private *drvdata)
{
	int ret = -1;
	struct asm_log_point_close close;

	pr_debug("%s: close all dump point\n", __func__);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	close.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	close.hdr.pkt_size = sizeof(close);
	close.hdr.src_port = 0;
	close.hdr.dest_port = 0;
	close.hdr.src_svc = 0;
	close.hdr.src_domain = APR_DOMAIN_APPS;
	close.hdr.dest_svc = APR_SVC_TINYLOG;
	close.hdr.dest_domain = APR_DOMAIN_ADSP;
	close.hdr.token = 0;
	close.hdr.opcode = ASM_LOG_POINT_CLOSE;

	close.dump_point = LOG_POINT_CLOSE_ALL;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&close);
	if (ret != sizeof(close)) {
		pr_err("%s: close all dump point %d failed %d\n",
			__func__, LOG_POINT_CLOSE_ALL, ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for dump point close response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int log_point_set_buffer_level(struct jlq_log_private *drvdata,
	unsigned int in_buf_level, unsigned int ex_buf_level)
{
	int ret = -1;
	struct asm_log_point_buffer_level_set buf_level;

	pr_debug("%s: set dump buffer level, in buffer level %d, ex buffer level %d\n",
			__func__, in_buf_level, ex_buf_level);

	mutex_lock(&drvdata->cmd_lock);
	atomic_set(&drvdata->log_cmd_state, -1);
	buf_level.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	buf_level.hdr.pkt_size = sizeof(buf_level);
	buf_level.hdr.src_port = 0;
	buf_level.hdr.dest_port = 0;
	buf_level.hdr.src_svc = 0;
	buf_level.hdr.src_domain = APR_DOMAIN_APPS;
	buf_level.hdr.dest_svc = APR_SVC_TINYLOG;
	buf_level.hdr.dest_domain = APR_DOMAIN_ADSP;
	buf_level.hdr.token = 0;
	buf_level.hdr.opcode = ASM_LOG_POINT_BUFFER_LEVEL_SET;

	buf_level.in_buf_level = in_buf_level;
	buf_level.ex_buf_level = ex_buf_level;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&buf_level);
	if (ret != sizeof(buf_level)) {
		pr_err("%s: set dump point buffer level failed %d, in buffer level %d, ex buffer level %d\n",
			__func__, ret, in_buf_level, ex_buf_level);
		ret = -EINVAL;
		goto fail_cmd;
	}

	ret = wait_event_timeout(drvdata->log_cmd_wait,
			(atomic_read(&drvdata->log_cmd_state) >= 0),
			msecs_to_jiffies(LONG_TIME_OUT));
	if (!ret) {
		pr_err("%s: timeout. waited for dump buffer level response\n", __func__);
		ret = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&drvdata->log_cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n", __func__);
		ret = -EINVAL;
		goto fail_cmd;
	}

	mutex_unlock(&drvdata->cmd_lock);
	return 0;
fail_cmd:
	mutex_unlock(&drvdata->cmd_lock);
	return ret;
}

static int sys_time_sync_put (struct jlq_log_private *drvdata, struct asm_sys_time_sync *sync_time)
{
	int ret = -1;
	struct asm_sys_time_sync sTime;


	pr_debug("%s: set sync time: %d %d, %d %d\n",
			__func__, sync_time->rtc_cnt_h, sync_time->rtc_cnt_l, sync_time->sys_cnt_h, sync_time->sys_cnt_l);

	sTime.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
						APR_HDR_LEN(APR_HDR_SIZE),
						APR_PKT_VER);
	sTime.hdr.pkt_size = sizeof(sTime);
	sTime.hdr.src_port = 0;
	sTime.hdr.dest_port = 0;
	sTime.hdr.src_svc = 0;
	sTime.hdr.src_domain = APR_DOMAIN_APPS;
	sTime.hdr.dest_svc = APR_SVC_TINYLOG;
	sTime.hdr.dest_domain = APR_DOMAIN_ADSP;
	sTime.hdr.token = 0;
	sTime.hdr.opcode = ASM_SYS_TIME_SYNC_SET;

	sTime.rtc_cnt_h = sync_time->rtc_cnt_h;
	sTime.rtc_cnt_l = sync_time->rtc_cnt_l;
	sTime.sys_cnt_h = sync_time->sys_cnt_h;
	sTime.sys_cnt_l = sync_time->sys_cnt_l;

	ret = apr_send_pkt(drvdata->apr, (uint32_t *)&sTime);
	if (ret != sizeof(sTime)) {
		pr_err("%s: set sync time failed %d.\n", __func__, ret);
		ret = -EINVAL;
		goto fail_cmd;
	}

	return 0;
fail_cmd:
	return ret;
}


static const struct log_controller_operations log_ctrl_ops = {
	.log_app_open = log_app_open,
	.log_app_close = log_app_close,
	.log_app_set_msg_level = log_app_set_msg_level,
	.log_app_set_all_msg_level = log_app_set_all_msg_level,
	.log_app_set_buffer_level = log_app_set_buffer_level,
	.log_point_open = log_point_open,
	.log_point_close = log_point_close,
	.log_point_close_all = log_point_close_all,
	.log_point_set_buffer_level = log_point_set_buffer_level,
	.sys_time_sync_put = sys_time_sync_put,
};

const struct log_controller_operations *get_log_controller_ops(void)
{
	return &log_ctrl_ops;
}


