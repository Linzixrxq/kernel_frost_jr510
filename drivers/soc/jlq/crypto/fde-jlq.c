/*
 * JLQ Inline Crypto Module
 *
 * Copyright 2018~2021 JLQ Technology Co.,
 * Ltd. or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define DEBUG
#include <linux/printk.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/device-mapper.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/bitops.h>

#include <soc/jlq/jr510/fde.h>
#include "fde-regs.h"

#if IS_ENABLED(CONFIG_TEE) && !IS_ENABLED(CONFIG_MICROTRUST_TEE_SUPPORT)
#define USE_OPTEE_KEYCONFIG
#endif

static struct fde_device *gs_fde_device;

int fde_debug_level;
EXPORT_SYMBOL(fde_debug_level);

#define FDE_INSTANCE_TYPE_LENGTH 12

int jlq_fde_hw_get_keylength(int keysize)
{
	if (keysize == 32) {
		return FDE_KEY_SIZE_256B;
	} else if (keysize == 16) {
		return FDE_KEY_SIZE_128B;
	} else if (keysize == 24) {
		return FDE_KEY_SIZE_192B;
	} else {
		return -1;
	}
}

int jlq_fde_hw_get_ivkeylength(int keysize)
{
	if (keysize == 32) {
		return FDE_IVKEY_SIZE_256B;
	} else if (keysize == 16) {
		return FDE_IVKEY_SIZE_128B;
	} else {
		return -1;
	}
}

#ifdef USE_OPTEE_KEYCONFIG

#define TA_CMD_SET_SLOT_KEYINFO		0
#define TA_CMD_SET_RESETBYPASS		1
#define TA_CMD_GET_RESETBYPASS		2
#define TA_CMD_DUMP_SLOT_KEYINFO	3

static int fde_tee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

static int fde_tee_init(struct fde_device *fde_dev)
{
	struct tee_ioctl_open_session_arg sess_arg;
	const uuid_t fde_uuid =
		UUID_INIT(0x98d73c4c, 0x220f, 0x4558,
			  0xbb, 0x14, 0x01, 0x7a, 0xba, 0x2c, 0xc3, 0x79);
	int ret;

	memset(&sess_arg, 0, sizeof(sess_arg));
	memcpy(sess_arg.uuid, fde_uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(fde_dev->ctx, &sess_arg, NULL);
	if ((ret < 0) || (sess_arg.ret != 0)) {
		pr_err("%s: tee_client_open_session failed, err=%x\n",
			__func__, sess_arg.ret);
		ret = -EINVAL;
		goto out;
	}
	fde_dev->session_id = sess_arg.session;

	fde_dev->shm = tee_shm_alloc(fde_dev->ctx,
				      FDE_SHM_SIZE,
				      TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(fde_dev->shm)) {
		pr_err("%s: tee_shm_alloc failed\n", __func__);
		ret = -ENOMEM;
		goto out_shm_alloc;
	}

	pr_info("%s {shm:0x%llx flags:0x%x} session_id:%d end\n", __func__, fde_dev->shm, fde_dev->shm->flags, fde_dev->session_id);

	return 0;

out_shm_alloc:
	tee_client_close_session(fde_dev->ctx, fde_dev->session_id);
out:

	return ret;

}

static int fde_tee_deinit(struct fde_device *fde_dev)
{
	/* Free the shared memory pool */
	tee_shm_free(fde_dev->shm);

	/* close the existing session with fTPM TA*/
	tee_client_close_session(fde_dev->ctx, fde_dev->session_id);

	/* close the context with TEE driver */
	tee_client_close_context(fde_dev->ctx);

	return 0;
}

static int fde_tee_set_resetbypass(struct fde_device *fde_dev,
				 int reset, int bypass)
{
	int ret = 0;
	//unsigned long flag;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];

	pr_info("%s reset:%d bypass:%d\n", __func__, reset, bypass);

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke TA_CMD_GET_ENTROPY function of Trusted App */
	inv_arg.func = TA_CMD_SET_RESETBYPASS;
	inv_arg.session = fde_dev->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	if (reset >= 0) {
		param[0].u.value.a = 1;
		param[0].u.value.b = reset;
	}

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	if (bypass >= 0) {
		param[1].u.value.a = 1;
		param[1].u.value.b = bypass;
	}

	//spin_lock_irqsave(&fde_dev->lock, flag);
	ret = tee_client_invoke_func(fde_dev->ctx, &inv_arg, param);
	//spin_unlock_irqrestore(&fde_dev->lock, flag);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		pr_err("TA_CMD_SET_RESETBYPASS invoke err: %d, arg.ret:%x\n",
			ret, inv_arg.ret);
		return -EIO;
	}

	return 0;
}

static int fde_tee_get_resetbypass(struct fde_device *fde_dev,
				 int *reset, int *bypass)
{
	int ret = 0;
	//unsigned long flag;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];

	pr_info("%s\n", __func__);

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke TA_CMD_GET_ENTROPY function of Trusted App */
	inv_arg.func = TA_CMD_GET_RESETBYPASS;
	inv_arg.session = fde_dev->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;

	//spin_lock_irqsave(&fde_dev->lock, flag);
	ret = tee_client_invoke_func(fde_dev->ctx, &inv_arg, param);
	//spin_unlock_irqrestore(&fde_dev->lock, flag);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		pr_err("TA_CMD_GET_RESETBYPASS invoke err: %d, arg.ret:%x\n",
			ret, inv_arg.ret);
		return -EIO;
	}

	if (reset) {
		*reset = param[0].u.value.b;
	}

	if (bypass) {
		*bypass = param[1].u.value.b;
	}

	return 0;
}

static void fde_shm_lock(struct fde_device *fde_dev)
{
	mutex_lock(&fde_dev->mutex_shm);
}

static void fde_shm_unlock(struct fde_device *fde_dev)
{
	mutex_unlock(&fde_dev->mutex_shm);
}

static int fde_tee_dump_slot_keyinfo(struct fde_device *fde_dev, int slot)
{
	int ret = 0;
	u32 *temp_buf = NULL;
	int i;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	int len = sizeof(struct fde_crypto_config);

	pr_devel("%s slot:%d entry\n", __func__, slot);

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke TA_CMD_GET_ENTROPY function of Trusted App */
	inv_arg.func = TA_CMD_DUMP_SLOT_KEYINFO;
	inv_arg.session = fde_dev->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = slot;

	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[1].u.memref.shm = fde_dev->shm;
	param[1].u.memref.size = fde_dev->shm->size;
	param[1].u.memref.shm_offs = 0;

	fde_shm_lock(fde_dev);
	ret = tee_client_invoke_func(fde_dev->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		fde_shm_unlock(fde_dev);
		pr_err("TA_CMD_DUMP_SLOT_KEYINFO invoke err: %d, arg.ret:%x\n",
			ret, inv_arg.ret);
		return -EIO;
	}

	temp_buf = tee_shm_get_va(fde_dev->shm, 0);
	if (IS_ERR(temp_buf)) {
		fde_shm_unlock(fde_dev);
		pr_err("%s: tee_shm_get_va failed for transmit\n",
			__func__);
		return PTR_ERR(temp_buf);
	}
	len = param[1].u.memref.size;
	len /= 4;

	for (i = 0; i < len;) {
		pr_devel("[%x]=%x\n", temp_buf[i], temp_buf[i+1]);
		i += 2;
	}

	fde_shm_unlock(fde_dev);

	return 0;
}


static int fde_tee_program_key(struct fde_device *fde_dev,
				 const struct fde_crypto_config *cfg, int slot)
{
	int ret = 0;
	u8 *temp_buf = NULL;
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	int len = sizeof(struct fde_crypto_config);

	pr_devel("%s slot:%d entry\n", __func__, slot);

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	inv_arg.func = TA_CMD_SET_SLOT_KEYINFO;
	inv_arg.session = fde_dev->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.shm = fde_dev->shm;
	param[0].u.memref.size = len;
	param[0].u.memref.shm_offs = 0;

	fde_shm_lock(fde_dev);
	temp_buf = tee_shm_get_va(fde_dev->shm, 0);
	if (IS_ERR(temp_buf)) {
		fde_shm_unlock(fde_dev);
		pr_err("%s: tee_shm_get_va failed for transmit\n",
			__func__);
		return PTR_ERR(temp_buf);
	}
	memset(temp_buf, 0, len);
	memcpy(temp_buf, cfg, len);

	ret = tee_client_invoke_func(fde_dev->ctx, &inv_arg, param);
	fde_shm_unlock(fde_dev);

	if ((ret < 0) || (inv_arg.ret != 0)) {
		pr_err("TA_CMD_SET_SLOT_KEYINFO invoke err: %d, arg.ret:%x\n",
			ret, inv_arg.ret);
		return -EIO;
	}
	return 0;
}
#else
#include <soc/jlq/jlq_sip.h>

int tee_invoke_interface_read(u32 base, u32 offset0, u32 *data0, u32 offset1, u32 *data1)
{
	u32 ret;

	ret = jlq_sip_tz_svcr(((offset0 != (u32)-1) ? (base + offset0):0),
				data0, ((offset1 != (u32)-1) ? (base + offset1) : 0), data1);

	return (int)ret;
}

int tee_invoke_interface_write(u32 base, u32 offset0, u32 data0, u32 offset1, u32 data1)
{
	u32 ret;

	ret = jlq_sip_tz_svcw(((offset0 != (u32)-1) ? (base + offset0) : 0),
				data0, ((offset1 != (u32)-1) ? (base + offset1) : 0), data1);

	return (int)ret;
}

// mode 0
// slot 0~31
int tee_invoke_interface_dump_fde_reg(u32 mode, u32 slot)
{
	u32 ret;

	ret = jlq_sip_tz_dbg(0, slot, 0, 0);

	return (int)ret;
}


static int fde_tee_set_resetbypass(struct fde_device *fde_dev,
				 int reset, int bypass)
{
	int ret;
	printk(KERN_INFO "@@@fde_tee_set_resetbypass: reset:%d bypass:%d\n", reset, bypass);

	spin_lock(&fde_dev->lock_fde);
	ret = tee_invoke_interface_write(fde_dev->paddr, ((reset != -1) ? FDE_SEC_RESET : -1), !!reset, ((bypass != -1) ? FDE_SEC_BYPASS : -1), bypass);
	tee_invoke_interface_write(fde_dev->paddr, FDE_REGS_AXUSER_SYNC_DISABLE, 1, FDE_AES_SYS_CONFIG, 1);
	wmb();
	spin_unlock(&fde_dev->lock_fde);

	return ret;
}

static int fde_tee_get_resetbypass(struct fde_device *fde_dev,
				 int *reset, int *bypass)
{
	u32 ret;
	u32 bypass_val;

	ret = tee_invoke_interface_read(fde_dev->paddr, FDE_SEC_BYPASS, &bypass_val, -1, NULL);

	if (bypass)
		*bypass = bypass_val;

	return 0;
}

static int fde_tee_dump_slot_keyinfo(struct fde_device *fde_dev, int slot)
{
	tee_invoke_interface_dump_fde_reg(0, slot);
	return 0;
}

static int fde_tee_program_key(struct fde_device *fde_dev,
				 const struct fde_crypto_config *cfg, int slot)
{
	u32 key_slot_key_size_mode = 0;
	u32 key_slot_sel = 0;
	int i;
	u32 *key_slot_key;
	int key_size;
	u32 aes_sys_config;
	int ivkeysize;
	int aeskeysize;

	key_size = cfg->key.key_slot_key_size / 2; // 64/2

	aeskeysize = jlq_fde_hw_get_keylength(key_size);
	if (unlikely(aeskeysize < 0)) {
		pr_err("key size error! key size:%dB\n", key_size);
		return -EINVAL;
	}

	ivkeysize = jlq_fde_hw_get_ivkeylength(key_size);
	if (unlikely(ivkeysize < 0)) {
		pr_err("ivkeysize size error! key size:%dB\n", key_size);
		return -EINVAL;
	}

	key_slot_sel = (slot & FDE_CONTEXT_SEL_CTX_SEL_MSK) << FDE_CONTEXT_SEL_CTX_SEL_OFFSET;
	key_slot_key_size_mode = (FDE_KEY_MODE_XTS_AES << FDE_SEC_KEY_SLOT_KEY_MODE_OFFSET) \
							| (aeskeysize << FDE_SEC_KEY_SLOT_KEY_SIZE_OFFSET);

	aes_sys_config = 1 | (ivkeysize << FDE_SEC_SYS_CONFIG_IVKEY_LENGTH_OFFSET);

	if (fde_debug_level & FDE_DBG_KEYINFO) {
		//print_hex_dump(KERN_DEBUG, "TEE AES KEY:", DUMP_PREFIX_ADDRESS, 16, 1, cfg->key.key_slot_key,
		//		sizeof(cfg->key.key_slot_key), false);
		pr_devel("slot:%d, key_size_mode:0x%x\n", key_slot_sel, key_slot_key_size_mode);
	}

	spin_lock(&fde_dev->lock_fde);

	//tee_invoke_interface_write(fde_dev->paddr, FDE_SEC_BYPASS, !!cfg->bypass, FDE_SEC_KEY_SLOT_SEL, key_slot_sel);
	tee_invoke_interface_write(fde_dev->paddr, FDE_SEC_KEY_SLOT_SEL, key_slot_sel, -1, -1);
	wmb();

	key_size /= sizeof(u32); // 8

	key_slot_key = (u32 *)cfg->key.key_slot_key;
	for (i = 0; i < key_size;) {
		tee_invoke_interface_write(fde_dev->paddr, FDE_SEC_KEY_SLOT_KEY + i * 4, key_slot_key[i], FDE_SEC_KEY_SLOT_KEY + (i + 1) * 4, key_slot_key[i + 1]);
		i += 2;
	}

	for (; i < 8;) {
		tee_invoke_interface_write(fde_dev->paddr, FDE_SEC_KEY_SLOT_KEY + i * 4, 0, FDE_SEC_KEY_SLOT_KEY + (i + 1) * 4, 0);
		i += 2;
	}

	key_slot_key = (u32 *)&cfg->key.key_slot_key[key_size];
	for (i = 0; i < key_size;) {
		tee_invoke_interface_write(fde_dev->paddr, FDE_SEC_KEY_SLOT_IV_KEY + i * 4, key_slot_key[i], FDE_SEC_KEY_SLOT_IV_KEY + (i + 1) * 4, key_slot_key[i + 1]);
		i += 2;
	}

	for (; i < 8;) {
		tee_invoke_interface_write(fde_dev->paddr, FDE_SEC_KEY_SLOT_IV_KEY + i * 4, 0, FDE_SEC_KEY_SLOT_IV_KEY + (i + 1) * 4, 0);
		i += 2;
	}

	tee_invoke_interface_write(fde_dev->paddr, FDE_SEC_KEY_SLOT_KEY_SIZE_MODE, key_slot_key_size_mode, -1, -1);
	wmb();

	spin_unlock(&fde_dev->lock_fde);

	return 0;
}

#endif

static void fde_context_dbgshow(struct fde_device *fde_dev, int w1r0, int context, int alloc)
{
	char ch = FDE_DBG_CONTEXT_ERR;

	if (context < 0 || context >= FDE_SLOT_BM_LEN)
		return;

	if (alloc) {
		if (w1r0 == 1)
			ch = FDE_DBG_CONTEXT_W;
		else if (!w1r0)
			ch = FDE_DBG_CONTEXT_R;
	} else {
		ch = FDE_DBG_CONTEXT_NOTUSED;
	}

	fde_dev->context_info[context] = ch;
	pr_devel("[slot_manage]%s\n", fde_dev->context_info);
}

static void fde_context_init(struct fde_device *fde_dev)
{
	unsigned long flag;
	unsigned long offset;
	if (fde_dev) {
		spin_lock_irqsave(&fde_dev->lock_context, flag);
		fde_dev->context_last_read = -1;
		fde_dev->context_last_write = -1;
		bitmap_zero(fde_dev->slot_used, FDE_SLOT_BM_LEN);
		__bitmap_or(fde_dev->slot_used, fde_dev->slot_used, &fde_dev->context_reserve_bits, sizeof(fde_dev->context_reserve_bits) * 8);
		if (fde_dev->context_reserve_bits2)
			__bitmap_or(fde_dev->slot_used, fde_dev->slot_used, &fde_dev->context_reserve_bits2, sizeof(fde_dev->context_reserve_bits2) * 8);

		memset(fde_dev->context_info, FDE_DBG_CONTEXT_NOTUSED, sizeof(fde_dev->context_info));
		for (offset = 0; offset < FDE_SLOT_BM_LEN; offset++) {
			if (test_bit(offset, fde_dev->slot_used)) {
				fde_dev->context_info[offset] = FDE_DBG_CONTEXT_RESV;
			}
		}
		fde_dev->context_info[sizeof(fde_dev->context_info) - 1] = '\0';
		spin_unlock_irqrestore(&fde_dev->lock_context, flag);
		pr_devel("[slot_manage init]%s\n", fde_dev->context_info);
	}
}

static int fde_context_alloc(struct platform_device *pdev, int *context, int w1r0)
{
	struct fde_device *fde_dev = platform_get_drvdata(pdev);
	int ret = 0;
	unsigned long flag;
	int slot_alloc = -1;
	unsigned long fzb = FDE_SLOT_BM_LEN;
	int notuse = -1;

	if (fde_debug_level & FDE_DBG_CONTEXT) {
		pr_devel("[slot_manage]%s w:%d\n", __func__, w1r0);
	}

	spin_lock_irqsave(&fde_dev->lock_context, flag);

	if (w1r0 == 1) {
		/*if (fde_dev->context_last_read >= 0) {
			slot_alloc = fde_dev->context_last_read;
			fde_dev->context_last_read = -1;
			printk("[slot_manage]%s context_last_read:%d\n", __func__, slot_alloc);
		}*/
		notuse = fde_dev->context_last_write;
	} else {
		/*if (fde_dev->context_last_write >= 0) {
			slot_alloc = fde_dev->context_last_write;
			fde_dev->context_last_write = -1;
			printk("[slot_manage]%s context_last_write:%d\n", __func__, slot_alloc);
		}*/
		notuse = fde_dev->context_last_read;
	}

	if (slot_alloc < 0) {
		if (notuse >= 0) {
			fzb = find_next_zero_bit(fde_dev->slot_used, FDE_SLOT_BM_LEN, notuse + 1);
		}
		if (fzb == FDE_SLOT_BM_LEN) {
			fzb = find_first_zero_bit(fde_dev->slot_used, FDE_SLOT_BM_LEN);
		}
		if (fzb < FDE_SLOT_BM_LEN) {
			slot_alloc = fzb;
		}
	}

	if (slot_alloc < 0) {
		if (fde_debug_level & FDE_DBG_CONTEXT)
			pr_devel("%s: context w:%d alloc fail!\n", __func__, w1r0);

		ret = -EBUSY;
	} else {
		*context = slot_alloc;
		__set_bit(fzb, fde_dev->slot_used);

		if (fde_debug_level & FDE_DBG_CONTEXT) {
			fde_context_dbgshow(fde_dev, w1r0, slot_alloc, 1);
		}
	}

	spin_unlock_irqrestore(&fde_dev->lock_context, flag);

	if (fde_debug_level & FDE_DBG_CONTEXT) {
		pr_devel("[slot_manage]%s alloc:%d slot_used:0x%lx, last w:%d,r:%d\n", __func__, slot_alloc, fde_dev->slot_used[0], fde_dev->context_last_write, fde_dev->context_last_read);
	}

	return ret;
}

static int fde_context_free(struct platform_device *pdev, int context, int w1r0)
{
	struct fde_device *fde_dev = platform_get_drvdata(pdev);
	int ret = 0;
	unsigned long flag;

	if (fde_debug_level & FDE_DBG_CONTEXT) {
		pr_devel("[slot_manage]%s context:%d(w:%d)\n", __func__, context, w1r0);
	}

	if (context < 0) {
		pr_info("[slot_manage]%s context:%d donothing\n", __func__, context);
		return 0;
	}

	spin_lock_irqsave(&fde_dev->lock_context, flag);
	if (w1r0) {
		fde_dev->context_last_write = context;
	} else {
		fde_dev->context_last_read = context;
	}

	if (fde_debug_level & FDE_DBG_CONTEXT) {
		if (unlikely(!test_bit(context, fde_dev->slot_used))) {
			pr_info("[slot_manage]%s: context %d(w:%d) has cleared\n", __func__, context, w1r0);
		}
	}

	clear_bit(context, fde_dev->slot_used);

	if (fde_debug_level & FDE_DBG_CONTEXT) {
		pr_devel("[slot_manage]%s slot_used:0x%lx, last w:%d,r:%d\n", __func__, fde_dev->slot_used[0], fde_dev->context_last_write, fde_dev->context_last_read);
		fde_context_dbgshow(fde_dev, w1r0, context, 0);
	}

	spin_unlock_irqrestore(&fde_dev->lock_context, flag);

	return ret;
}

int fde_hw_reset(struct platform_device *pdev, int reset)
{
	struct fde_device *fde_dev;
	//unsigned long flag;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}
	//spin_lock_irqsave(&fde_dev->lock, flag);
	fde_context_init(fde_dev);

	if (fde_tee_set_resetbypass(fde_dev, !!reset, -1)) {
		//spin_unlock_irqrestore(&fde_dev->lock, flag);
		return -EIO;
	}

	//spin_unlock_irqrestore(&fde_dev->lock, flag);

	return 0;
}

int fde_hw_bypass(struct platform_device *pdev, int bypass)
{
	struct fde_device *fde_dev;
	int ret = 0;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}
	if (fde_tee_set_resetbypass(fde_dev, -1, !!bypass)) {
		return -EIO;
	}

	return ret;
}

static const struct file_operations jlq_fde_fops = {
	.owner = THIS_MODULE,
};

static int fde_hw_register_fde_device(struct fde_device *fde_dev, const char *type)
{
	int rc = 0;
	unsigned int baseminor = 0;
	unsigned int count = 1;
	struct device *class_dev;

	if (!strcmp(type, SDHC_FDE_TYPE_STR))
		strlcpy(fde_dev->fde_instance_type, "fdesdhc", sizeof(fde_dev->fde_instance_type));
	else
		strlcpy(fde_dev->fde_instance_type, "fdeufs", sizeof(fde_dev->fde_instance_type));

	rc = alloc_chrdev_region(&fde_dev->device_no, baseminor, count,
			fde_dev->fde_instance_type);
	if (rc < 0) {
		pr_err("alloc_chrdev_region failed %d for %s\n", rc,
			fde_dev->fde_instance_type);
		return rc;
	}
	fde_dev->driver_class = class_create(THIS_MODULE, fde_dev->fde_instance_type);
	if (IS_ERR(fde_dev->driver_class)) {
		rc = -ENOMEM;
		pr_err("class_create failed %d for %s\n", rc, fde_dev->fde_instance_type);
		goto exit_unreg_chrdev_region;
	}
	class_dev = device_create(fde_dev->driver_class, NULL,
					fde_dev->device_no, NULL, fde_dev->fde_instance_type);

	if (!class_dev) {
		pr_err("class_device_create failed %d for %s\n", rc, fde_dev->fde_instance_type);
		rc = -ENOMEM;
		goto exit_destroy_class;
	}

	cdev_init(&fde_dev->cdev, &jlq_fde_fops);
	fde_dev->cdev.owner = THIS_MODULE;

	rc = cdev_add(&fde_dev->cdev, MKDEV(MAJOR(fde_dev->device_no), 0), 1);
	if (rc < 0) {
		pr_err("cdev_add failed %d for %s\n", rc, fde_dev->fde_instance_type);
		goto exit_destroy_device;
	}
	return  0;

exit_destroy_device:
	device_destroy(fde_dev->driver_class, fde_dev->device_no);

exit_destroy_class:
	class_destroy(fde_dev->driver_class);

exit_unreg_chrdev_region:
	unregister_chrdev_region(fde_dev->device_no, 1);
	return rc;
}

static int fde_dev_init(const char *type, struct platform_device *pdev, void *host_data)
{
	struct fde_device *fde_dev;
	int ret;

	pr_devel("%s: Registering FDE device\n", __func__);

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}

	ret = fde_hw_register_fde_device(fde_dev, type);
	if (ret) {
		pr_err("create character device failed.\n");
		goto err_fde_dev;
	}

	fde_dev->host = host_data;

	//fde_dev->need_reset = 1;
	fde_hw_reset(pdev, 1);
	fde_hw_bypass(pdev, 0);

	return 0;

err_fde_dev:
	return ret;

}

static int fde_hw_get_keyslot_num(struct platform_device *pdev)
{
	struct fde_device *fde_dev;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return 0;
	}

	return fde_dev->context_num;
}

static int fde_hw_set_context_reserved(struct platform_device *pdev, unsigned long reserved_bits)
{
	struct fde_device *fde_dev;
	int offset;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}

	fde_dev->context_reserve_bits2 = reserved_bits;

	if (fde_dev->context_reserve_bits2) {
		__bitmap_or(fde_dev->slot_used, fde_dev->slot_used, &fde_dev->context_reserve_bits2, FDE_SLOT_BM_LEN);
		for (offset = 0; offset < FDE_SLOT_BM_LEN; offset++) {
			if (test_bit(offset, &fde_dev->context_reserve_bits2)) {
				fde_dev->context_info[offset] = FDE_DBG_CONTEXT_RESV;
			}
		}
	}

	if (fde_debug_level & FDE_DBG_CONTEXT)
		pr_info("%s reserved_bits:%lx, set context reserved:0x%lx slot_used:0x%x\n",
			__func__, reserved_bits, fde_dev->context_reserve_bits2, fde_dev->slot_used[0]);

	return 0;
}


#if 0
static int fde_hw_check_ready(struct fde_device *fde_dev)
{
	if (!fde_dev->ctx) {

	}
	if (fde_dev->need_reset) {

	}
	return 0;
}
#endif

static int fde_hw_program_key(struct platform_device *pdev, const struct fde_crypto_config *cfg, int slot)
{
	struct fde_device *fde_dev;
	int key_size;
	int aeskeysize;
	int ivkeysize;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}

	key_size = cfg->key.key_slot_key_size / 2;
	aeskeysize = jlq_fde_hw_get_keylength(key_size);
	if (aeskeysize < 0) {
		pr_err("key size error! key size:%dB\n", key_size);
		return -EINVAL;
	}

	ivkeysize = jlq_fde_hw_get_ivkeylength(key_size);
	if (ivkeysize < 0) {
		pr_err("ivkeysize size error! key size:%dB\n", key_size);
		return -EINVAL;
	}

	if (fde_tee_program_key(fde_dev, cfg, slot)) {
		panic("program key fail!");
		return -EIO;
	}
	return 0;
}

static int fde_hw_get_dateunitsize(unsigned short size)
{
	if (size == 512) {
		return FDE_CONTEXT_WORD0_DATA_UNIT_SIZE_512;
	} else if (size == 1024) {
		return FDE_CONTEXT_WORD0_DATA_UNIT_SIZE_1024;
	} else if (size == 2048) {
		return FDE_CONTEXT_WORD0_DATA_UNIT_SIZE_2048;
	} else if (size == 4096) {
		return FDE_CONTEXT_WORD0_DATA_UNIT_SIZE_4096;
	} else {
		return FDE_CONTEXT_WORD0_DATA_UNIT_SIZE_INVALID;
	}
}

static int fde_hw_config_slot(struct platform_device *pdev, const struct fde_crypto_slot_config *cfg)
{
	struct fde_device *fde_dev;
	uint32_t val;
	int dataunitsize = 0;
	//unsigned long flag;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}

	if (cfg->context_sel > 31 || cfg->key_slot > 31) {
		pr_err("%s: invalid config, context_sel:%d, key_slot:%d\n", __func__, cfg->context_sel, cfg->key_slot);
		return -EINVAL;
	}

	dataunitsize = fde_hw_get_dateunitsize(cfg->data_unit_size);
	if (dataunitsize == FDE_CONTEXT_WORD0_DATA_UNIT_SIZE_INVALID) {
		pr_err("%s: invalid data unit size:%d\n", __func__, cfg->data_unit_size);
		return -EINVAL;
	}

	if (cfg->is_nonecq) {
		#if 1
		val = readl(fde_dev->hblk_sctrl + FDE_HBLK_EMMC_DMA_CONTEXT);
		if (cfg->iswrite) {
			val = FDE_HBLK_EMMC_DMA_CONTEXT_SET_ENC(val, cfg->context_sel);
		} else {
			val = FDE_HBLK_EMMC_DMA_CONTEXT_SET_DEC(val, cfg->context_sel);
		}
		#else
		val = FDE_HBLK_EMMC_DMA_CONTEXT_SET_DEC(cfg->context_sel, cfg->context_sel);
		#endif

		writel(val, fde_dev->hblk_sctrl + FDE_HBLK_EMMC_DMA_CONTEXT);
	} else {
		writel(0, fde_dev->hblk_sctrl + FDE_HBLK_EMMC_DMA_CONTEXT);
	}

	writel(cfg->context_sel, fde_dev->base + FDE_REGS_CONTEXT_SEL);
	wmb();

	// bypass, context_sel, dataunitsize
	val = cfg->key_slot << FDE_CONTEXT_WORD0_KEYSLOT_SEL_OFFSET;
	if (!cfg->slot_bypass) {
		val |= (FDE_CONTEXT_WORD0_NOBYPASS_MSK << FDE_CONTEXT_WORD0_NOBYPASS_OFFSET);
	}
	val |= (dataunitsize << FDE_CONTEXT_WORD0_DATA_UNIT_SIZE_OFFSET);
	writel(val, fde_dev->base + FDE_REGS_CONTEXT_WORD0);

	writel(cfg->start_sector_number, fde_dev->base + FDE_REGS_CONTEXT_WORD1);
	wmb();

	if (fde_debug_level & FDE_DBG_SLOTCFG) {
		pr_devel("%s set hblk fde context 0x%x\n", __func__, readl(fde_dev->hblk_sctrl + FDE_HBLK_EMMC_DMA_CONTEXT));
		pr_devel("%s set fde context sel 0x%x\n", __func__, readl(fde_dev->base + FDE_REGS_CONTEXT_SEL));
		pr_devel("%s set fde context word0 0x%x\n", __func__, readl(fde_dev->base + FDE_REGS_CONTEXT_WORD0));
		pr_devel("%s set fde context word1 0x%x\n", __func__, readl(fde_dev->base + FDE_REGS_CONTEXT_WORD1));
	}

	return 0;
}

static int fde_hw_config_bypassed_context(struct platform_device *pdev, struct fde_crypto_slot_config *cfg)
{
	struct fde_device *fde_dev;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}

	if (fde_dev->context_bypassed_context < 0) {
		pr_devel_once("%s NO default bypass context\n", __func__);
		return 0;
	}

	cfg->slot_bypass = 1;
	cfg->context_sel = fde_dev->context_bypassed_context;
	cfg->key_slot = FDE_DEFAULT_BYPASSED_SLOT;

	return fde_hw_config_slot(pdev, (const struct fde_crypto_slot_config *)cfg);
}

static int fde_runtime_level(struct platform_device *pdev, int level)
{
	struct fde_device *fde_dev;
	unsigned long current_freq;
	unsigned long target_freq_core = 0xffffffff;
	unsigned long target_freq_bus = 0xffffffff;
	int ret = 0;
	int ret_core = 0;

	fde_dev = platform_get_drvdata(pdev);
	if (!fde_dev) {
		pr_err("%s: invalid device\n", __func__);
		return -EINVAL;
	}

	if (level == FDE_RUNTIME_POWEROFF) {
		clk_disable_unprepare(fde_dev->fde_clk);
		clk_disable_unprepare(fde_dev->fde_bus_clk);
		fde_dev->runtime_level = FDE_RUNTIME_POWEROFF;
		return 0;
	} else {
		if (fde_dev->bus_clk_table_len > 1)
			target_freq_bus = fde_dev->bus_clk_table[fde_dev->bus_clk_table_len - 1];
		if (fde_dev->clk_table_len > 1)
			target_freq_core = fde_dev->clk_table[fde_dev->clk_table_len - 1];
	}

	if (fde_dev->runtime_level == FDE_RUNTIME_POWEROFF) {
		ret = clk_prepare_enable(fde_dev->fde_bus_clk);
		if (ret) {
			pr_err("clk_enable failed for fbe bus clock: %d\n", ret);
			return ret;
		}
		ret_core = clk_prepare_enable(fde_dev->fde_clk);
		if (ret_core) {
			pr_err("clk_enable failed for fbe clock: %d\n", ret_core);
			clk_disable_unprepare(fde_dev->fde_bus_clk);
			return ret_core;
		}
	}

	if (fde_debug_level & FDE_DBG_DVFS)
		pr_devel("%s lvl:%d bus:%ld, core:%ld\n", __func__, level, target_freq_bus, target_freq_core);

	if (target_freq_bus != 0xffffffff) {
		current_freq = clk_get_rate(fde_dev->fde_bus_clk);
		if (fde_debug_level & FDE_DBG_DVFS)
			pr_devel("old fde bus clk: (%d)\n", current_freq);

		if (target_freq_bus != current_freq)
			ret = clk_set_rate(fde_dev->fde_bus_clk, target_freq_bus);

		current_freq = clk_get_rate(fde_dev->fde_bus_clk);
		if (fde_debug_level & FDE_DBG_DVFS)
			pr_devel("new fde bus clk: (%d)\n", current_freq);
	}
	if (target_freq_core != 0xffffffff) {
		current_freq = clk_get_rate(fde_dev->fde_clk);
		if (fde_debug_level & FDE_DBG_DVFS)
			pr_devel("old fde clk: (%d)\n", current_freq);

		if (target_freq_core != current_freq)
			ret_core = clk_set_rate(fde_dev->fde_clk, target_freq_core);

		current_freq = clk_get_rate(fde_dev->fde_clk);
		if (fde_debug_level & FDE_DBG_DVFS)
			pr_devel("new fde bus clk: (%d)\n", current_freq);
	}
	fde_dev->runtime_level = level;

	if (ret)
		pr_warn("fde set core level %d fail %d\n", level, ret);

	if (ret_core)
		pr_warn("fde set bus level %d fail %d\n", level, ret_core);

	return ret + ret_core;
}


struct jlq_fde_variant_ops jlq_fde_ops = {
	.name				= "jlq-fde",
	.init				= fde_dev_init,
	.get_keyslot_num	= fde_hw_get_keyslot_num,
	.set_context_reserved	= fde_hw_set_context_reserved,
	.program_key		= fde_hw_program_key,
	.config_context		= fde_hw_config_slot,
	.config_default_bypass	= fde_hw_config_bypassed_context,
	.do_reset			= fde_hw_reset,
	.set_bypass			= fde_hw_bypass,
	.alloc_context		= fde_context_alloc,
	.free_context		= fde_context_free,
	.runtime_level		= fde_runtime_level
};

struct jlq_fde_variant_ops *jlq_fde_get_ops(void)
{
	return &jlq_fde_ops;
}
EXPORT_SYMBOL(jlq_fde_get_ops);

struct platform_device *jlq_fde_get_pdevice(struct device_node *node)
{
	struct platform_device *fde_pdev = NULL;

	if (!node) {
		pr_err("%s: invalid node %pX\n", __func__, node);
		goto out;
	}

	if (!of_device_is_available(node)) {
		pr_err("%s: device unavailable\n", __func__);
		goto out;
	}

	if (!gs_fde_device) {
		pr_err("%s: fde device not probed yet\n", __func__);
		fde_pdev = ERR_PTR(-EPROBE_DEFER);
		goto out;
	}

	if (gs_fde_device->dev->of_node == node) {
		pr_info("%s: found fde device 0x%x\n", __func__, gs_fde_device);
		fde_pdev = to_platform_device(gs_fde_device->dev);
	}

	if (fde_pdev)
		pr_info("%s: matching platform device 0x%x\n", __func__, fde_pdev);
out:
	return fde_pdev;
}
EXPORT_SYMBOL(jlq_fde_get_pdevice);

#if 0
static int dump_slot;
static ssize_t fde_reg_dump_slot_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t nbytes)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val < 0 || val > 31)
		return -EINVAL;

	dump_slot = val;

	return nbytes;
}

static ssize_t fde_reg_dump_slot_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	u32 val;
	if (!gs_fde_device) {
		printk("fde not valid\n");
		return -EIO;
	}
	val = readl(gs_fde_device->hblk_sctrl + FDE_HBLK_EMMC_DMA_CONTEXT);

	if (fde_tee_dump_slot_keyinfo(gs_fde_device, dump_slot)) {
		return -EIO;
	}
	return sprintf(buf, "slot:0x%x, hblk:0x%x\n", dump_slot, val);
}

static struct kobj_attribute fde_reg_dump_slot_attr =
	__ATTR(fde_reg_dump_slot, 0644, fde_reg_dump_slot_show, fde_reg_dump_slot_store);
#endif

static ssize_t fde_reg_fde_debug_level_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t nbytes)
{
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	fde_debug_level = val;

	return nbytes;
}

static ssize_t fde_reg_fde_debug_level_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "fde debug level:0x%x\n", fde_debug_level);
}

static struct kobj_attribute fde_reg_fde_debug_level_attr =
	__ATTR(fde_reg_fde_debug_level, 0644, fde_reg_fde_debug_level_show, fde_reg_fde_debug_level_store);

	
static ssize_t fde_reg_bypass_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t nbytes)
{
	int ret;
	unsigned long val;

	if (!gs_fde_device) {
		printk("fde not valid\n");
		return -EIO;
	}

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (fde_tee_set_resetbypass(gs_fde_device, -1, !!val)) {
		return -EIO;
	}

	return nbytes;
}

static ssize_t fde_reg_bypass_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	u32 val;
	if (!gs_fde_device) {
		printk("fde not valid\n");
		return -EIO;
	}
	if (fde_tee_get_resetbypass(gs_fde_device, NULL, &val)) {
		return -EIO;
	}
	return sprintf(buf, "fde bypass:%d\n", val);
}

static struct kobj_attribute fde_reg_bypass_attr =
	__ATTR(fde_reg_bypass, 0644, fde_reg_bypass_show, fde_reg_bypass_store);

static ssize_t fde_reg_reset_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t nbytes)
{
	int ret;
	unsigned long val;

	if (!gs_fde_device) {
		printk("fde not valid\n");
		return -EIO;
	}

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	if (val) {
		fde_context_init(gs_fde_device);
		if (fde_tee_set_resetbypass(gs_fde_device, 1, -1)) {
			return -EIO;
		}
	}

	return nbytes;
}

static struct kobj_attribute fde_reg_reset_attr =
	__ATTR(fde_reg_reset, 0644, NULL, fde_reg_reset_store);

static struct attribute *default_attrs[] = {
	//&fde_reg_dump_slot_attr.attr,
	&fde_reg_bypass_attr.attr,
	&fde_reg_reset_attr.attr,
	&fde_reg_fde_debug_level_attr.attr,
	NULL,
};

static const struct attribute_group debug_attr_group = {
	.attrs = default_attrs,
};

static int jlq_fde_sysfs_init(struct fde_device *fde_dev)
{
	int ret;
	fde_dev->kobj = kobject_create_and_add("debugmode", &fde_dev->dev->kobj);
	if (!fde_dev->kobj) {
		pr_err("failed to create sysfs\n");
		return -EINVAL;
	}

	ret = sysfs_create_group(fde_dev->kobj, &debug_attr_group);
	if (ret) {
		pr_err("failed to create attribute group\n");
		return ret;
	}

	return 0;
}

static int jlq_fde_sysfs_deinit(struct fde_device *fde_dev)
{
	sysfs_remove_group(fde_dev->kobj, &debug_attr_group);

	kobject_put(fde_dev->kobj);

	return 0;
}


static int jlq_fde_get_reserved_bits_count(unsigned int reserved_bits)
{
	int count = 0;

	while (reserved_bits > 0) {
		count++;
		reserved_bits &= reserved_bits - 1;
	}
	return count;
}

static int jlq_dt_get_array(struct device *dev, const char *prop_name,
				 u32 **out, int *len, u32 size)
{
	int ret = 0;
	struct device_node *np = dev->of_node;
	size_t sz;
	u32 *arr = NULL;

	if (!of_get_property(np, prop_name, len)) {
		ret = -EINVAL;
		goto out;
	}
	sz = *len = *len / sizeof(*arr);
	if (sz <= 0 || (size > 0 && (sz > size))) {
		pr_err("%s invalid size\n", prop_name);
		ret = -EINVAL;
		goto out;
	}

	arr = devm_kzalloc(dev, sz * sizeof(*arr), GFP_KERNEL);
	if (!arr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_property_read_u32_array(np, prop_name, arr, sz);
	if (ret < 0) {
		pr_err("%s failed reading array %d\n", prop_name, ret);
		goto out;
	}
	*out = arr;
out:
	if (ret)
		*len = 0;
	return ret;
}

static int jlq_fde_get_device_tree_data(struct platform_device *pdev,
		struct fde_device *fde_dev)
{
	struct device *dev = &pdev->dev;
	int ret = -1;
	int reserved_bits_num = 0;
	unsigned int reserve_bits;
	unsigned long current_freq;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fde_base");
	if (!res) {
		pr_err("fde_base not found\n");
		return -ENODEV;
	}
	fde_dev->paddr = res->start;
	pr_devel("res: start:0x%x, size:0x%x\n", res->start, resource_size(res));

	fde_dev->base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(fde_dev->base)) {
		ret = PTR_ERR(fde_dev->base);
		pr_err("%s: Error = %d mapping FDE io memory\n", __func__, ret);
		return -ENOMEM;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hblkctrl_base");
	if (!res) {
		pr_err("hblkctrl_base not found\n");
		return -ENODEV;
	}
	fde_dev->hblk_sctrl = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(fde_dev->hblk_sctrl)) {
		pr_err("hblkctrl_base ioremap failed\n");
		return -ENOMEM;
	}

	if (jlq_dt_get_array(&pdev->dev, "jlq,clk-rates",
			&fde_dev->clk_table, &fde_dev->clk_table_len, 0)) {
		pr_err("failed parsing supported clock rates\n");
		return -ENODEV;
	}
	if (!fde_dev->clk_table || !fde_dev->clk_table_len) {
		pr_err("Invalid clock table\n");
		return -ENODEV;
	}

	if (jlq_dt_get_array(&pdev->dev, "jlq,bus-clk-rates",
			&fde_dev->bus_clk_table, &fde_dev->bus_clk_table_len, 0)) {
		pr_err("failed parsing supported bus clock rates\n");
		return -ENODEV;
	}
	if (!fde_dev->bus_clk_table || !fde_dev->bus_clk_table_len) {
		pr_err("Invalid bus clock table\n");
		return -ENODEV;
	}

	ret = device_property_read_u32(&pdev->dev,
		"default-clk-rate", &fde_dev->clk_rate);
	if (ret || fde_dev->clk_rate > fde_dev->clk_table[fde_dev->clk_table_len - 1]) {
		fde_dev->clk_rate = 0xffffffff;
		pr_warning("unknown default-clk-rate\n");
	}

	ret = device_property_read_u32(&pdev->dev,
		"default-bus-clk-rate", &fde_dev->bus_clk_rate);
	if (ret || fde_dev->bus_clk_rate > fde_dev->bus_clk_table[fde_dev->bus_clk_table_len - 1]) {
		fde_dev->bus_clk_rate = 0xffffffff;
		pr_warning("unknown default-bus-clk-rate\n");
	}

	fde_dev->fde_clk = devm_clk_get(fde_dev->dev, "fbe_clk");
	if (IS_ERR(fde_dev->fde_clk)) {
		pr_err("base clk setup failed (%d)\n", ret);
		return -ENODEV;
	}

	ret = clk_prepare_enable(fde_dev->fde_clk);
	if (ret)
		goto out;

	fde_dev->fde_bus_clk = devm_clk_get(fde_dev->dev, "fbe_bus_clk");
	if (IS_ERR(fde_dev->fde_bus_clk)) {
		ret = -ENODEV;
		pr_err("bus clk setup failed (%d)\n", ret);
		goto fde_clk_disable;
	}

	ret = clk_prepare_enable(fde_dev->fde_bus_clk);
	if (ret)
		goto fde_clk_disable;

	if (fde_dev->clk_rate != 0xffffffff) {
		current_freq = clk_get_rate(fde_dev->fde_clk);
		pr_devel("old fde clk: (%d)\n", current_freq);

		ret = clk_set_rate(fde_dev->fde_clk, fde_dev->clk_rate);

		current_freq = clk_get_rate(fde_dev->fde_clk);
		pr_devel("new fde clk: (%d)\n", current_freq);
	}

	if (fde_dev->bus_clk_rate != 0xffffffff) {
		current_freq = clk_get_rate(fde_dev->fde_bus_clk);
		pr_devel("old fde bus clk: (%d)\n", current_freq);

		ret = clk_set_rate(fde_dev->fde_bus_clk, fde_dev->bus_clk_rate);

		current_freq = clk_get_rate(fde_dev->fde_bus_clk);
		pr_devel("new fde bus clk: (%d)\n", current_freq);
	}

	ret = device_property_read_u32(&pdev->dev,
		"context-resverdbits", &reserve_bits);
	fde_dev->context_reserve_bits = reserve_bits;

	if (ret || fde_dev->context_reserve_bits == FDE_CONTEXT_MSK_MAX) {
		fde_dev->context_reserve_bits = 0;
		pr_err("all context reserved?! set as NO reserved!\n");
	}
	reserved_bits_num = jlq_fde_get_reserved_bits_count(fde_dev->context_reserve_bits);
	if (reserved_bits_num) {
		fde_dev->context_bypassed_context = find_first_bit((const unsigned long *)&fde_dev->context_reserve_bits, sizeof(fde_dev->context_reserve_bits) * 8);
		if (fde_dev->context_bypassed_context != 0) {
			//WARN_ON(fde_dev->context_bypassed_context != 0);
			//fde_dev->context_bypassed_context = 0;
		}
	} else {
		fde_dev->context_bypassed_context = -1;
	}
	fde_dev->context_num = 32 - reserved_bits_num;
	pr_info("context_num:%d context_reserve_bits:0x%04x bypassed_context:%d\n", fde_dev->context_num, fde_dev->context_reserve_bits, fde_dev->context_bypassed_context);

	return 0;

//fde_bus_clk_disable:
//	clk_disable_unprepare(fde_dev->fde_bus_clk);

fde_clk_disable:
	clk_disable_unprepare(fde_dev->fde_clk);

out:
	return ret;
}

static int jlq_fde_probe(struct platform_device *pdev)
{
	struct fde_device *fde_dev;
	int ret = 0;
#ifdef USE_OPTEE_KEYCONFIG
	struct tee_context *ctx;
#endif

	if (!pdev) {
		pr_err("%s: Invalid platform_device passed\n",
			__func__);
		return -EINVAL;
	}

#ifdef USE_OPTEE_KEYCONFIG
	/* Open context with TEE driver */
	ctx = tee_client_open_context(NULL, fde_tee_ctx_match, NULL, NULL);
	if (IS_ERR(ctx)) {
		if (PTR_ERR(ctx) == -ENOENT)
			return -EPROBE_DEFER;
		pr_err("%s: tee_client_open_context failed\n", __func__);
		return PTR_ERR(ctx);
	}
#endif

	fde_dev = devm_kzalloc(&pdev->dev, sizeof(struct fde_device), GFP_KERNEL);

	if (!fde_dev) {
		ret = -ENOMEM;
		pr_err("%s: Error %d allocating memory for FDE device:\n", __func__, ret);
		goto err_fde_condex;
	}

#ifdef USE_OPTEE_KEYCONFIG
	fde_dev->ctx = ctx;
	ret = fde_tee_init(fde_dev);
	if (ret) {
		goto err_fde_condex;
	}
#endif

	fde_dev->dev = &pdev->dev;
	if (!fde_dev->dev) {
		ret = -EINVAL;
		pr_err("%s: Invalid device passed in platform_device\n", __func__);
		goto err_fde_dev;
	}

	if (pdev->dev.of_node)
		ret = jlq_fde_get_device_tree_data(pdev, fde_dev);
	else {
		ret = -EINVAL;
		pr_err("%s: FDE device node not found\n", __func__);
	}

	if (ret)
		goto err_fde_dev;

	platform_set_drvdata(pdev, fde_dev);

	mutex_init(&fde_dev->mutex_shm);
	spin_lock_init(&fde_dev->lock_context);
	spin_lock_init(&fde_dev->lock_fde);

	ret = jlq_fde_sysfs_init(fde_dev);
	if (ret) {
		pr_info("%s: Registering FDE sysfail:%d ignore it\n", __func__, ret);
		ret = 0;
	}

	pr_info("%s dev:0x%lx end\n", __func__, fde_dev);

	gs_fde_device = fde_dev;

	goto out;

err_fde_condex:
#ifdef USE_OPTEE_KEYCONFIG
	tee_client_close_context(fde_dev->ctx);
#endif
err_fde_dev:
	if (fde_dev)
		devm_kfree(&pdev->dev, fde_dev);
out:
	return ret;
}

static int jlq_fde_remove(struct platform_device *pdev)
{
	struct fde_device *fde_dev;

	fde_dev = (struct fde_device *)platform_get_drvdata(pdev);

	if (!fde_dev)
		return 0;

	jlq_fde_sysfs_deinit(fde_dev);

#ifdef USE_OPTEE_KEYCONFIG
	fde_tee_deinit(fde_dev);
#endif

	clk_disable_unprepare(fde_dev->fde_bus_clk);
	clk_disable_unprepare(fde_dev->fde_clk);

	if (fde_dev->base)
		iounmap(fde_dev->base);

	if (fde_dev)
		devm_kfree(&pdev->dev, fde_dev);

	return 0;
}


/* Following struct is required to match device with driver from dts file */
static const struct of_device_id jlq_fde_match[] = {
	{ .compatible = "jlq,fde" },
	{},
};
MODULE_DEVICE_TABLE(of, jlq_fde_match);

static struct platform_driver jlq_fde_driver = {
	.probe          = jlq_fde_probe,
	.remove         = jlq_fde_remove,
	.driver         = {
		.name   = "jlq_fde",
		.of_match_table = jlq_fde_match,
	},
};

static int __init jlq_fde_driver_init(void)
{
	return platform_driver_register(&(jlq_fde_driver));
}
fs_initcall(jlq_fde_driver_init);
static void __exit jlq_fde_driver_exit(void)
{
	platform_driver_unregister(&(jlq_fde_driver));
}
module_exit(jlq_fde_driver_exit);


MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("JLQ Inline Crypto Engine driver");
