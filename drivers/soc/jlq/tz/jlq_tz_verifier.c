// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018~2021 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */
#define DEBUG
#include <linux/printk.h>

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/vmalloc.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#include "soc/jlq/jlq_verifier.h"

#define TZ_DBG_NO_TEE

#define TZ_VERIFIER_SESS_MAX		10
#define TZ_VERIFIER_SHM_MAX			10

static struct tee_context *tz_context;
static int context_initialized;
static struct mutex		tz_lock;

static struct tee_ioctl_open_session_arg teec_session_array[TZ_VERIFIER_SESS_MAX];
static struct tee_shm *teec_shm_array[TZ_VERIFIER_SHM_MAX];


struct tz_verifier {
	struct tee_ioctl_open_session_arg *session;
	struct tee_shm *sharedmem;
};

const static uuid_t jlq_tz_verifier_uuid = UUID_INIT(0x4c4a23f2, 0x3300, 0x11ec,
					0x8a, 0x5a, 0x17, 0x34, 0x8e, 0x13, 0x7f, 0xdd);


static inline void tz_session_init(void)
{
	int start = 0;

	for (; start < TZ_VERIFIER_SESS_MAX; start++)
		teec_session_array[start].session = -1;
}

void tz_verifier_destroy(void)
{
	if (context_initialized)
		tee_client_close_context(tz_context);
	context_initialized = 0;
}


static int tz_find_free_session(int start)
{
	TZV_LOG_VERBOSE("%s start:%d\n", __func__, start);
	for (; start < TZ_VERIFIER_SESS_MAX; start++) {
		if (teec_session_array[start].session < 0)
			break;
	}

	TZV_LOG_VERBOSE("%s found:%d end\n", __func__, start);
	if (start < TZ_VERIFIER_SESS_MAX)
		return start;

	return -1;
}

static int tz_find_free_shm(int start)
{
	TZV_LOG_VERBOSE("%s start:%d\n", __func__, start);

	for (; start < TZ_VERIFIER_SHM_MAX; start++) {
		if (!teec_shm_array[start])
			break;
		TZV_LOG_VERBOSE("%s [%d]:%x\n", __func__, start, teec_shm_array[start]->kaddr);
	}

	TZV_LOG_VERBOSE("%s found:%d end\n", __func__, start);
	if (start < TZ_VERIFIER_SHM_MAX)
		return start;

	return -1;
}

static inline struct tee_ioctl_open_session_arg *tz_get_session(int id)
{
	if (id < 0)
		return NULL;

	if (id < TZ_VERIFIER_SESS_MAX)
		return &teec_session_array[id];

	return NULL;
}

static inline struct tee_shm **tz_get_shm(int id)
{
	if (id < 0)
		return NULL;

	if (id < TZ_VERIFIER_SHM_MAX)
		return &teec_shm_array[id];

	return NULL;
}

static int tee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}


int tz_verifier_init(void)
{
	struct tee_ioctl_open_session_arg *sess_arg;
	int session_id;
	int ret = 0;

	TZV_LOG_VERBOSE("%s\n", __func__);

	if (!context_initialized) {
		TZV_LOG_DBG("%s context init\n", __func__);
		/* Open context with TEE driver */
		tz_context = tee_client_open_context(NULL, tee_ctx_match, NULL, NULL);
		if (IS_ERR(tz_context)) {
			TZV_LOG_ERR("Failed to initialize tz context, err: %x", ret);
			return -EIO;
		}
		tz_session_init();
		mutex_init(&tz_lock);
		context_initialized = 1;
	}

	session_id = tz_find_free_session(0);
	if (session_id < 0) {
		TZV_LOG_ERR("%s: get session fail\n", __func__);
		return -EBUSY;
	}

	sess_arg = tz_get_session(session_id);

	memset(sess_arg, 0, sizeof(*sess_arg));
	memcpy(sess_arg->uuid, jlq_tz_verifier_uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg->clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg->num_params = 0;

	ret = tee_client_open_session(tz_context, sess_arg, NULL);
	if ((ret < 0) || (sess_arg->ret != 0)) {
		TZV_LOG_ERR("%s: Failed to open session, err=%x\n",
			__func__, sess_arg->ret);
		sess_arg->session = -1;
		return -EIO;
	}

	TZV_LOG_VERBOSE("%s end\n", __func__);
	return sess_arg->session;
}
EXPORT_SYMBOL(tz_verifier_init);

void tz_verifier_deinit(int session_id, int destroy)
{
	struct tee_ioctl_open_session_arg *sess_arg = tz_get_session(session_id);

	TZV_LOG_DBG("%s session_id:%d destroy:%d\n", __func__, session_id, destroy);

	if (sess_arg) {
		tee_client_close_session(tz_context, sess_arg->session);
		sess_arg->session = -1;
	}

	if (destroy)
		tz_verifier_destroy();

	TZV_LOG_DBG("%s session_id:%d destroy:%d end\n", __func__, session_id, destroy);
}
EXPORT_SYMBOL(tz_verifier_deinit);

int tz_verifier_alloc_shm(unsigned long size)
{
	struct tee_shm **ppsharedmem;
	struct tee_shm *psharedmem;
	int shm_id;

	TZV_LOG_VERBOSE("%s\n", __func__);

	if (size < 1)
		return -EIO;

	shm_id = tz_find_free_shm(0);
	if (shm_id < 0) {
		TZV_LOG_ERR("%s: get shm fail\n", __func__);
		return -EBUSY;
	}

	ppsharedmem = tz_get_shm(shm_id);

	psharedmem = tee_shm_alloc(tz_context, size,
				      TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(psharedmem)) {
		TZV_LOG_ERR("Failed to register %d shared memory,err: %x",
		(unsigned int)size, PTR_ERR(psharedmem));
		return -EIO;
	}

	*ppsharedmem = psharedmem;

	TZV_LOG_VERBOSE("%s end\n", __func__);
	return shm_id;
}
EXPORT_SYMBOL(tz_verifier_alloc_shm);

int tz_verifier_get_shm_info(int shm_id, char **buffer, size_t *size)
{
	struct tee_shm **shm = tz_get_shm(shm_id);
	char *ptr_v;

	TZV_LOG_VERBOSE("%s\n", __func__);

	if (!*shm)
		return -EINVAL;

	ptr_v = tee_shm_get_va(*shm, 0);

	if (buffer)
		*buffer = ptr_v;

	if (size)
		*size = (*shm)->size;

	TZV_LOG_VERBOSE("%s shm_id:%d 0x%x@%d\n", __func__, shm_id, ptr_v, (*shm)->size);

	if (!ptr_v)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(tz_verifier_get_shm_info);

void tz_verifier_free_shm(int shm_id)
{
	struct tee_shm **shm = tz_get_shm(shm_id);

	TZV_LOG_VERBOSE("%s id:%d %x end\n", __func__, shm_id, *shm);

	tee_shm_free(*shm);
	*shm = NULL;
}
EXPORT_SYMBOL(tz_verifier_free_shm);

int tz_verifier_invode_command(int session_id, int shm_id, unsigned int command, int cmdsize)
{
	struct tee_ioctl_open_session_arg *session_arg = tz_get_session(session_id);
	struct tee_shm **shm = tz_get_shm(shm_id);
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	int ret;

	TZV_LOG_VERBOSE("%s sess:%d shmid:%d cmd:%d@%d\n", __func__, session_id, shm_id, command, cmdsize);

	if (!session_arg || !*shm)
		return -EINVAL;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	inv_arg.func = command;
	inv_arg.session = session_arg->session;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = *shm;
	if (cmdsize < 0)
		param[0].u.memref.size = (*shm)->size;
	else
		param[0].u.memref.size = cmdsize;
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(tz_context, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		pr_err("Failed to invoke command %d, err: %x", command, inv_arg.ret);
		return -EIO;
	}

	TZV_LOG_VERBOSE("%s sess:%d shmid:%d cmd:%d@%d end\n", __func__, session_id, shm_id, command, cmdsize);
	return 0;
}
EXPORT_SYMBOL(tz_verifier_invode_command);

int tz_verifier_reg_command(int session_id, int shm_id, struct verifier_reg_op *verifier_reg)
{
	int ret;
	char *buffer;
	size_t size;
	int offset;
	int argsize = sizeof(struct verifier_reg_op);

	TZV_LOG_VERBOSE("%s sess:%d shmid:%d verifier_reg:%x\n", __func__, session_id, shm_id, verifier_reg);
	TZV_LOG_VERBOSE("verifier_reg id:%d, op:%d, offset:%d, val:%d\n", verifier_reg->ssid, verifier_reg->op_type, verifier_reg->offset, verifier_reg->value);

	if (tz_verifier_get_shm_info(shm_id, &buffer, &size)) {
		pr_err("shm is NULL\n");
		return -ENOMEM;
	}

	if (size < argsize) {
		pr_err("shm is too small! %d < %d\n", size, argsize);
		return -ENOMEM;
	}

	offset = offsetof(struct verifier_reg_op, value);
	memcpy(buffer, verifier_reg, argsize);

	ret = tz_verifier_invode_command(session_id, shm_id, TZCMD_OP_REG, argsize);
	if (ret) {
		pr_err("%s read reg fail! ret:%d\n", ret);
		return ret;
	}

	if (verifier_reg->op_type == TZCMD_REG_READ)
		memcpy(&verifier_reg->value, &buffer[offset], sizeof(unsigned int));

	TZV_LOG_VERBOSE("verifier_reg id:%d, op:%d, offset:%d, val:%d\n", verifier_reg->ssid, verifier_reg->op_type, verifier_reg->offset, verifier_reg->value);
	TZV_LOG_VERBOSE("%s sess:%d shmid:%d verifier_reg:%x end\n", __func__, session_id, shm_id, verifier_reg);
	return 0;
}
EXPORT_SYMBOL(tz_verifier_reg_command);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("JLQ trustzone verifier driver");
