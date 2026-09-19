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
//#define DEBUG
#include <linux/printk.h>

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/vmalloc.h>
#include "teei_keymaster.h"
#include "teei_client_transfer_data.h"

#include "soc/jlq/jlq_verifier.h"

#define TZ_DBG_NO_TEE

#define TZ_VERIFIER_SESS_MAX		10
#define TZ_VERIFIER_SHM_MAX			10

static struct TEEC_Context tz_context;
static int context_initialized;
static DEFINE_MUTEX(tz_lock);

static struct TEEC_Session teec_session_array[TZ_VERIFIER_SESS_MAX];
static struct TEEC_SharedMemory teec_shm_array[TZ_VERIFIER_SHM_MAX];


struct tz_verifier {
	struct TEEC_Session *session;
	struct TEEC_SharedMemory *sharedmem;
};

const static struct TEEC_UUID tz_verifier_uuid = {0x4c4a23f2, 0x3300, 0x11ec,
					{0x8a, 0x5a, 0x17, 0x34, 0x8e, 0x13, 0x7f, 0xdd}};


void tz_verifier_destroy(void)
{
	mutex_lock(&tz_lock);
	if (context_initialized)
		ut_pf_gp_finalize_context(&tz_context);
	context_initialized = 0;
	mutex_unlock(&tz_lock);
}

static int tz_find_free_session(int start)
{
	TZV_LOG_VERBOSE("%s start:%d\n", __func__, start);
	for (; start < TZ_VERIFIER_SESS_MAX; start++) {
		if (!teec_session_array[start].ctx)
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
		if (!teec_shm_array[start].priv) {
			break;
		} else {
			TZV_LOG_VERBOSE("%s [%d]:%x\n", __func__, start, teec_shm_array[start].priv);
		}
	}

	TZV_LOG_VERBOSE("%s found:%d end\n", __func__, start);
	if (start < TZ_VERIFIER_SHM_MAX)
		return start;

	return -1;
}

static inline struct TEEC_Session *tz_get_session(int id)
{
	if (id < 0)
		return NULL;

	if (id < TZ_VERIFIER_SESS_MAX)
		return &teec_session_array[id];

	return NULL;
}

static inline struct TEEC_SharedMemory *tz_get_shm(int id)
{
	if (id < 0)
		return NULL;

	if (id < TZ_VERIFIER_SHM_MAX)
		return &teec_shm_array[id];

	return NULL;
}

int tz_verifier_init(void)
{
	struct TEEC_Session *session;
	int session_id;
	TEEC_Result result;
	uint32_t returnOrigin = 0;
	int ret = 0;

	TZV_LOG_VERBOSE("%s\n", __func__);

	mutex_lock(&tz_lock);
	if (!context_initialized) {
		context_initialized = 1;
		TZV_LOG_DBG("%s context init\n", __func__);
		//memset(&tz_context, 0, sizeof(tz_context));
		ret = ut_pf_gp_initialize_context(&tz_context);
		if (ret) {
			TZV_LOG_ERR("Failed to initialize tz context, err: %x", ret);
			ret = -EIO;
			goto err_out;
		}
	}

	session_id = tz_find_free_session(0);
	if (session_id < 0) {
		TZV_LOG_ERR("%s: get session fail\n", __func__);
		ret = -EBUSY;
		goto err_out;
	}

	session = tz_get_session(session_id);

	result = TEEC_OpenSession(&tz_context, session, &tz_verifier_uuid, TEEC_LOGIN_PUBLIC,
			NULL, NULL, &returnOrigin);
	if (result != TEEC_SUCCESS) {
		TZV_LOG_ERR("Failed to open session,err: %x", result);
		ret = -EIO;
		goto err_out;
	}

	mutex_unlock(&tz_lock);

	TZV_LOG_VERBOSE("%s end\n", __func__);
	return session_id;

err_out:
	mutex_unlock(&tz_lock);
	return ret;
}
EXPORT_SYMBOL(tz_verifier_init);

void tz_verifier_deinit(int session_id, int destroy)
{
	struct TEEC_Session *session = tz_get_session(session_id);

	TZV_LOG_VERBOSE("%s session_id:%d destroy:%d\n", __func__, session_id, destroy);

	mutex_lock(&tz_lock);
	TEEC_CloseSession(session);
	if (session)
		session->ctx = NULL;
	mutex_unlock(&tz_lock);

	if (destroy)
		tz_verifier_destroy();

	TZV_LOG_VERBOSE("%s session_id:%d destroy:%d end\n", __func__, session_id, destroy);
}
EXPORT_SYMBOL(tz_verifier_deinit);

const unsigned char *tz_verifier_get_raw_data(const unsigned char *image, size_t size)
{
	int header_size = VERIFIER_HEADER_SIZE;
	const unsigned char *raw_data = image;
	size_t raw_size;
	const u8 header[4] = {0x49, 0x4d, 0x2a, 0x48};

	if (size <= header_size)
		return raw_data;

	if (!memcmp(image, header, sizeof(header))) {
		raw_data = image + header_size;
	}

	return raw_data;
}
EXPORT_SYMBOL(tz_verifier_get_raw_data);

int tz_verifier_alloc_shm(unsigned long size)
{
	struct TEEC_SharedMemory *sharedmem;
	int shm_id;
	TEEC_Result result;
	int ret;

	TZV_LOG_VERBOSE("%s\n", __func__);

	if (size < 1) {
		return -EIO;
	}

	mutex_lock(&tz_lock);

	shm_id = tz_find_free_shm(0);
	if (shm_id < 0) {
		TZV_LOG_ERR("%s: get shm fail\n", __func__);
		ret = -EBUSY;
		goto err_out;
	}

	sharedmem = tz_get_shm(shm_id);

	sharedmem->size = size;
	sharedmem->flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
	result = TEEC_AllocateSharedMemory(&tz_context, sharedmem);
	if (result != TEEC_SUCCESS) {
		TZV_LOG_ERR("Failed to register %d shared memory,err: %x",
		(unsigned int)size, result);
		ret = -EIO;
		goto err_out;
	}

	mutex_unlock(&tz_lock);

	TZV_LOG_VERBOSE("%s end\n", __func__);
	return shm_id;

err_out:
	mutex_unlock(&tz_lock);
	return ret;
}
EXPORT_SYMBOL(tz_verifier_alloc_shm);

int tz_verifier_get_shm_info(int shm_id, char **buffer, size_t *size)
{
	struct TEEC_SharedMemory *shm = tz_get_shm(shm_id);

	TZV_LOG_VERBOSE("%s\n", __func__);

	if (!shm)
		return -EINVAL;

	if (buffer)
		*buffer = shm->buffer;

	if (size)
		*size = shm->alloced_size;

	TZV_LOG_VERBOSE("%s shm_id:%d 0x%x@%d\n", __func__, shm_id, shm->buffer, shm->alloced_size);

	if (!shm->buffer)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(tz_verifier_get_shm_info);

void tz_verifier_free_shm(int shm_id)
{
	struct TEEC_SharedMemory *shm = tz_get_shm(shm_id);

	TEEC_ReleaseSharedMemory(shm);
	TZV_LOG_VERBOSE("%s id:%d %x end\n", __func__, shm_id, shm);
}
EXPORT_SYMBOL(tz_verifier_free_shm);

int tz_verifier_invode_command(int session_id, int shm_id, unsigned int command, int cmdsize)
{
	struct TEEC_Session *session = tz_get_session(session_id);
	struct TEEC_SharedMemory *shm = tz_get_shm(shm_id);
	struct TEEC_Operation operation;
	TEEC_Result result;
	uint32_t error_orgin;

	TZV_LOG_VERBOSE("%s sess:%d shmid:%d cmd:%d@%d\n", __func__, session_id, shm_id, command, cmdsize);

	if (!session || !shm)
		return -EINVAL;

	memset(&operation, 0x00, sizeof(operation));
	operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_PARTIAL_INOUT,
				TEEC_NONE, TEEC_NONE, TEEC_NONE);
	operation.started = 1;
	operation.params[0].memref.parent = shm;
	operation.params[0].memref.offset = 0;
	if (cmdsize < 0)
		operation.params[0].memref.size = shm->size;
	else
		operation.params[0].memref.size = cmdsize;

	//print_hex_dump(KERN_DEBUG, "shm:", DUMP_PREFIX_ADDRESS, 16, 1, shm->buffer,
	//			operation.params[0].memref.size, false);

	mutex_lock(&tz_lock);
	result = TEEC_InvokeCommand(session, command, &operation, &error_orgin);
	mutex_unlock(&tz_lock);
	if (result != TEEC_SUCCESS) {
		pr_err("Failed to invoke command %d, err: %x, orgin:%x\n", command, result, error_orgin);
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
	}

	offset = offsetof(struct verifier_reg_op, value);
	memcpy(buffer, verifier_reg, argsize);

	//print_hex_dump(KERN_DEBUG, "regshm:", DUMP_PREFIX_ADDRESS, 16, 1, buffer,
	//			argsize, false);

	ret = tz_verifier_invode_command(session_id, shm_id, TZCMD_OP_REG, argsize);
	if (ret) {
		pr_err("%s read reg fail! ret:%d\n", ret);
		return ret;
	}

	if (verifier_reg->op_type == TZCMD_REG_READ) {
		memcpy(&verifier_reg->value, &buffer[offset], sizeof(unsigned int));
	}

	TZV_LOG_VERBOSE("verifier_reg id:%d, op:%d, offset:%d, val:%d\n", verifier_reg->ssid, verifier_reg->op_type, verifier_reg->offset, verifier_reg->value);
	TZV_LOG_VERBOSE("%s sess:%d shmid:%d verifier_reg:%x end\n", __func__, session_id, shm_id, verifier_reg);
	return 0;
}
EXPORT_SYMBOL(tz_verifier_reg_command);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("JLQ trustzone verifier driver");
