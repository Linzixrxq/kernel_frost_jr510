// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/time.h>
#include <linux/atomic.h>
#include <linux/mm.h>

#include <asm/ioctls.h>

#include <linux/memory.h>

#include <sound/compress_params.h>

#include <dsp/msm_audio_ion.h>
#include <dsp/apr_audio-v2.h>
#include <dsp/audio_cal_utils.h>
#include <dsp/q6asm-v2.h>
#include <dsp/q6audio-v2.h>
#include <dsp/q6common.h>
#include <dsp/q6core.h>
#include "adsp_err.h"

#define TIMEOUT_MS  1000
#define TRUE        0x01
#define FALSE       0x00
#define SESSION_MAX 8

#define ENC_FRAMES_PER_BUFFER 0x01

enum {
	ASM_TOPOLOGY_CAL = 0,
	ASM_CUSTOM_TOP_CAL,
	ASM_AUDSTRM_CAL,
	ASM_RTAC_APR_CAL,
	ASM_MAX_CAL_TYPES
};

union asm_token_struct {
	struct {
		u8 stream_id;
		u8 session_id;
		u8 buf_index;
		u8 flags;
	} _token;
	u32 token;
} __packed;

enum {
	ASM_DIRECTION_OFFSET,
	ASM_CMD_NO_WAIT_OFFSET,
	/*
	 * Offset is limited to 7 because flags is stored in u8
	 * field in asm_token_structure defined above. The offset
	 * starts from 0.
	 */
	ASM_MAX_OFFSET = 7,
};

enum {
	WAIT_CMD,
	NO_WAIT_CMD
};

#define ASM_SET_BIT(n, x)	(n |= 1 << x)
#define ASM_TEST_BIT(n, x)	((n >> x) & 1)

/* TODO, combine them together */
static DEFINE_MUTEX(session_lock);
struct asm_mmap {
	atomic_t ref_cnt;
	void *apr;
};

static struct asm_mmap this_mmap;

struct audio_session {
	struct audio_client *ac;
	spinlock_t session_lock;
	struct mutex mutex_lock_per_session;
};
/* session id: 0 reserved */
static struct audio_session session[ASM_ACTIVE_STREAMS_ALLOWED + 1];

struct asm_buffer_node {
	struct list_head list;
	phys_addr_t buf_phys_addr;
	uint32_t  mmap_hdl;
};
static int32_t q6asm_callback(struct apr_client_data *data, void *priv);
static void q6asm_add_hdr(struct audio_client *ac, struct apr_hdr *hdr,
			uint32_t pkt_size, uint32_t cmd_flg);
static void q6asm_add_hdr_async(struct audio_client *ac, struct apr_hdr *hdr,
			uint32_t pkt_size, uint32_t cmd_flg);
int q6asm_memory_map_regions(struct audio_client *ac, int dir,
				uint32_t bufsz, uint32_t bufcnt,
				bool is_contiguous);
int q6asm_memory_unmap_regions(struct audio_client *ac, int dir);
static void q6asm_reset_buf_state(struct audio_client *ac);

static int q6asm_is_valid_session(struct apr_client_data *data, void *priv);

/* for ASM custom topology */
static struct audio_buffer common_buf[2];
static struct audio_client common_client;
static int set_custom_topology;

struct generic_get_data_ {
	int valid;
	int is_inband;
	int size_in_ints;
	int ints[];
};

#define OUT_BUFFER_SIZE 56
#define IN_BUFFER_SIZE 24

static inline void q6asm_set_flag_in_token(union asm_token_struct *asm_token,
					   int flag, int flag_offset)
{
	if (flag)
		ASM_SET_BIT(asm_token->_token.flags, flag_offset);
}

static inline int q6asm_get_flag_from_token(union asm_token_struct *asm_token,
					    int flag_offset)
{
	return ASM_TEST_BIT(asm_token->_token.flags, flag_offset);
}

static inline void q6asm_update_token(u32 *token, u8 session_id, u8 stream_id,
				      u8 buf_index, u8 dir, u8 nowait_flag)
{
	union asm_token_struct asm_token;

	asm_token.token = 0;
	asm_token._token.session_id = session_id;
	asm_token._token.stream_id = stream_id;
	asm_token._token.buf_index = buf_index;
	q6asm_set_flag_in_token(&asm_token, dir, ASM_DIRECTION_OFFSET);
	q6asm_set_flag_in_token(&asm_token, nowait_flag,
				  ASM_CMD_NO_WAIT_OFFSET);
	*token = asm_token.token;
}

static inline uint32_t q6asm_get_pcm_format_id(uint32_t media_format_block_ver)
{
	uint32_t pcm_format_id;

	switch (media_format_block_ver) {
	case PCM_MEDIA_FORMAT_V5:
		pcm_format_id = ASM_MEDIA_FMT_MULTI_CHANNEL_PCM_V5;
		break;
	case PCM_MEDIA_FORMAT_V4:
		pcm_format_id = ASM_MEDIA_FMT_MULTI_CHANNEL_PCM_V4;
		break;
	case PCM_MEDIA_FORMAT_V3:
		pcm_format_id = ASM_MEDIA_FMT_MULTI_CHANNEL_PCM_V3;
		break;
	case PCM_MEDIA_FORMAT_V2:
	default:
		pcm_format_id = ASM_MEDIA_FMT_MULTI_CHANNEL_PCM_V2;
		break;
	}
	return pcm_format_id;
}

/*
 * q6asm_get_buf_index_from_token:
 *       Retrieve buffer index from token.
 *
 * @token: token value sent to ASM service on q6.
 * Returns buffer index in the read/write commands.
 */
uint8_t q6asm_get_buf_index_from_token(uint32_t token)
{
	union asm_token_struct asm_token;

	asm_token.token = token;
	return asm_token._token.buf_index;
}
EXPORT_SYMBOL(q6asm_get_buf_index_from_token);

/*
 * q6asm_get_stream_id_from_token:
 *       Retrieve stream id from token.
 *
 * @token: token value sent to ASM service on q6.
 * Returns stream id.
 */
uint8_t q6asm_get_stream_id_from_token(uint32_t token)
{
	union asm_token_struct asm_token;

	asm_token.token = token;
	return asm_token._token.stream_id;
}
EXPORT_SYMBOL(q6asm_get_stream_id_from_token);

static int q6asm_session_alloc(struct audio_client *ac)
{
	int n;

	for (n = 1; n <= ASM_ACTIVE_STREAMS_ALLOWED; n++) {
		if (!(session[n].ac)) {
			session[n].ac = ac;
			return n;
		}
	}
	pr_err("%s: session not available\n", __func__);
	return -ENOMEM;
}

static int q6asm_get_session_id_from_audio_client(struct audio_client *ac)
{
	int n;

	for (n = 1; n <= ASM_ACTIVE_STREAMS_ALLOWED; n++) {
		if (session[n].ac == ac)
			return n;
	}

	pr_debug("%s: cannot find matching audio client. ac = %pK\n",
		__func__, ac);

	return 0;
}

static bool q6asm_is_valid_audio_client(struct audio_client *ac)
{
	return q6asm_get_session_id_from_audio_client(ac) ? 1 : 0;
}

static void q6asm_session_free(struct audio_client *ac)
{
	int session_id;
	unsigned long flags = 0;

	pr_debug("%s: sessionid[%d]\n", __func__, ac->session);
	session_id = ac->session;
	mutex_lock(&session[session_id].mutex_lock_per_session);
	rtac_remove_popp_from_adm_devices(ac->session);
	spin_lock_irqsave(&(session[session_id].session_lock), flags);
	session[ac->session].ac = NULL;
	ac->session = -1;
	ac->session_type = -1;
	ac->stream_id = LEGACY_PCM_MODE;
	ac->fptr_cache_ops = NULL;
	ac->cb = NULL;
	ac->priv = NULL;
	kfree(ac);
	ac = NULL;
	spin_unlock_irqrestore(&(session[session_id].session_lock), flags);
	mutex_unlock(&session[session_id].mutex_lock_per_session);
}

static uint32_t q6asm_get_next_buf(struct audio_client *ac,
		uint32_t curr_buf, uint32_t max_buf_cnt)
{
	dev_vdbg(ac->dev, "%s: curr_buf = %d, max_buf_cnt = %d\n",
		 __func__, curr_buf, max_buf_cnt);
	curr_buf += 1;
	return (curr_buf >= max_buf_cnt) ? 0 : curr_buf;
}

int q6asm_audio_client_buf_free(unsigned int dir,
			struct audio_client *ac)
{
	struct audio_port_data *port;
	int cnt = 0;
	int rc = 0;

	pr_debug("%s: Session id %d\n", __func__, ac->session);
	mutex_lock(&ac->cmd_lock);
	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[dir];
		if (!port->buf) {
			pr_err("%s: buf NULL\n", __func__);
			mutex_unlock(&ac->cmd_lock);
			return 0;
		}
		cnt = port->max_buf_cnt - 1;

		while (cnt >= 0) {
			if (port->buf[cnt].data) {
				if (!rc || atomic_read(&ac->reset))
					msm_audio_ion_free(
						port->buf[cnt].dma_buf);

				port->buf[cnt].dma_buf = NULL;
				port->buf[cnt].data = NULL;
				port->buf[cnt].phys = 0;
				--(port->max_buf_cnt);
			}
			--cnt;
		}
		kfree(port->buf);
		port->buf = NULL;
	}
	mutex_unlock(&ac->cmd_lock);
	return 0;
}

/**
 * q6asm_audio_client_buf_free_contiguous -
 *       frees the memory buffers for ASM
 *
 * @dir: RX or TX direction
 * @ac: audio client handle
 *
 * Returns 0 on success or error on failure
 */
int q6asm_audio_client_buf_free_contiguous(unsigned int dir,
			struct audio_client *ac)
{
	struct audio_port_data *port;
	int cnt = 0;
	int rc = 0;

	pr_debug("%s: Session id %d\n", __func__, ac->session);
	mutex_lock(&ac->cmd_lock);
	port = &ac->port[dir];
	if (!port->buf) {
		mutex_unlock(&ac->cmd_lock);
		return 0;
	}
	cnt = port->max_buf_cnt - 1;

	if (port->buf[0].data) {
/*
		pr_debug("%s: data[%pK], phys[%pK], dma_buf[%pK]\n",
			__func__,
			port->buf[0].data,
			&port->buf[0].phys,
			port->buf[0].dma_buf);
*/
		if (!rc || atomic_read(&ac->reset)){
			
			
#ifdef AUDIO_TEST
		dma_free_coherent(ac->dev, port->bytes_to_alloc,
				port->buf[0].data, port->buf[0].phys);
#else
		msm_audio_ion_free(port->buf[0].dma_buf);
#endif

		}
		port->buf[0].dma_buf = NULL;
	}

	while (cnt >= 0) {
		port->buf[cnt].data = NULL;
		port->buf[cnt].phys = 0;
		cnt--;
	}
	port->max_buf_cnt = 0;
	kfree(port->buf);
	port->buf = NULL;
	mutex_unlock(&ac->cmd_lock);
	return 0;
}
EXPORT_SYMBOL(q6asm_audio_client_buf_free_contiguous);

/**
 * q6asm_audio_client_buf_alloc -
 *       Allocs memory from ION for ASM
 *
 * @dir: RX or TX direction
 * @ac: Audio client handle
 * @bufsz: size of each buffer
 * @bufcnt: number of buffers to alloc
 *
 * Returns 0 on success or error on failure
 */
int q6asm_audio_client_buf_alloc(unsigned int dir,
			struct audio_client *ac,
			unsigned int bufsz,
			uint32_t bufcnt)
{
	int cnt = 0;
	int rc = 0;
	struct audio_buffer *buf;
	size_t len;

	if (!(ac) || !(bufsz) || ((dir != IN) && (dir != OUT))) {
		pr_err("%s: ac %pK bufsz %d dir %d\n", __func__, ac, bufsz,
			dir);
		return -EINVAL;
	}

	pr_debug("%s: session[%d]bufsz[%d]bufcnt[%d]\n", __func__, ac->session,
		bufsz, bufcnt);

	if (ac->session <= 0 || ac->session > 8) {
		pr_err("%s: Session ID is invalid, session = %d\n", __func__,
			ac->session);
		goto fail;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		if (ac->port[dir].buf) {
			pr_debug("%s: buffer already allocated\n", __func__);
			return 0;
		}
		mutex_lock(&ac->cmd_lock);
		if (bufcnt > (U32_MAX/sizeof(struct audio_buffer))) {
			pr_err("%s: Buffer size overflows", __func__);
			mutex_unlock(&ac->cmd_lock);
			goto fail;
		}
		buf = kzalloc(((sizeof(struct audio_buffer))*bufcnt),
				GFP_KERNEL);

		if (!buf) {
			mutex_unlock(&ac->cmd_lock);
			goto fail;
		}

		ac->port[dir].buf = buf;

		while (cnt < bufcnt) {
			if (bufsz > 0) {
				if (!buf[cnt].data) {
					rc = msm_audio_ion_alloc(
					      &buf[cnt].dma_buf,
					      bufsz,
					      &buf[cnt].phys,
					      &len,
					      &buf[cnt].data);
					if (rc) {
						pr_err("%s: ION Get Physical for AUDIO failed, rc = %d\n",
							__func__, rc);
						mutex_unlock(&ac->cmd_lock);
					goto fail;
					}

					buf[cnt].used = 1;
					buf[cnt].size = bufsz;
					buf[cnt].actual_size = bufsz;
/*
					pr_debug("%s: data[%pK]phys[%pK][%pK]\n",
						__func__,
					   buf[cnt].data,
					   &buf[cnt].phys,
					   &buf[cnt].phys);
*/
					cnt++;
				}
			}
		}
		ac->port[dir].max_buf_cnt = cnt;

		mutex_unlock(&ac->cmd_lock);
		rc = q6asm_memory_map_regions(ac, dir, bufsz, cnt, 0);
		if (rc < 0) {
			pr_err("%s: CMD Memory_map_regions failed %d for size %d\n",
				__func__, rc, bufsz);
			goto fail;
		}
	}
	return 0;
fail:
	q6asm_audio_client_buf_free(dir, ac);
	return -EINVAL;
}
EXPORT_SYMBOL(q6asm_audio_client_buf_alloc);

/**
 * q6asm_audio_client_buf_alloc_contiguous -
 *       Alloc contiguous memory from ION for ASM
 *
 * @dir: RX or TX direction
 * @ac: Audio client handle
 * @bufsz: size of each buffer
 * @bufcnt: number of buffers to alloc
 *
 * Returns 0 on success or error on failure
 */
int q6asm_audio_client_buf_alloc_contiguous(unsigned int dir,
			struct audio_client *ac,
			unsigned int bufsz,
			unsigned int bufcnt)
{
	int cnt = 0;
	int rc = 0;
	struct audio_buffer *buf;
	size_t len;
	int bytes_to_alloc;

	if (!(ac) || ((dir != IN) && (dir != OUT))) {
		pr_err("%s: ac %pK dir %d\n", __func__, ac, dir);
		return -EINVAL;
	}

	pr_info("%s: session[%d]bufsz[%d]bufcnt[%d]\n",
			__func__, ac->session,
			bufsz, bufcnt);

	if (ac->session < 0 || ac->session > ASM_ACTIVE_STREAMS_ALLOWED) {
		pr_err("%s: Session ID is invalid, session = %d\n", __func__,
			ac->session);
		goto fail;
	}

	if (ac->port[dir].buf) {
		pr_err("%s: buffer already allocated\n", __func__);
		return 0;
	}
	mutex_lock(&ac->cmd_lock);
	buf = kzalloc(((sizeof(struct audio_buffer))*bufcnt),
			GFP_KERNEL);

	if (!buf) {
		pr_err("%s: buffer allocation failed\n", __func__);
		mutex_unlock(&ac->cmd_lock);
		goto fail;
	}

	ac->port[dir].buf = buf;

	/* check for integer overflow */
	if ((bufcnt > 0) && ((INT_MAX / bufcnt) < bufsz)) {
		pr_err("%s: integer overflow\n", __func__);
		mutex_unlock(&ac->cmd_lock);
		goto fail;
	}
	bytes_to_alloc = bufsz * bufcnt;

	/* The size to allocate should be multiple of 4K bytes */
	bytes_to_alloc = PAGE_ALIGN(bytes_to_alloc);
	ac->port[dir].bytes_to_alloc = bytes_to_alloc;

#ifdef AUDIO_TEST
    len = 0;
	buf[0].data = dma_alloc_coherent(
		ac->dev, bytes_to_alloc, &buf[0].phys, GFP_KERNEL);
#else
	rc = msm_audio_ion_alloc(&buf[0].dma_buf,
		bytes_to_alloc,
		&buf[0].phys, &len,
		&buf[0].data);
#endif
	if (rc) {
		pr_err("%s: Audio ION alloc is failed, rc = %d\n",
			__func__, rc);
		mutex_unlock(&ac->cmd_lock);
		goto fail;
	}
/*
	pr_info("%s: data[0x%px]phys[0x%x]\n",
					__func__,
					buf[0].data,
					(uint32_t)buf[0].phys);
*/

	buf[0].used = dir ^ 1;
	buf[0].size = bufsz;
	buf[0].actual_size = bufsz;
	cnt = 1;
	while (cnt < bufcnt) {
		if (bufsz > 0) {
			buf[cnt].data =  buf[0].data + (cnt * bufsz);
			buf[cnt].phys =  buf[0].phys + (cnt * bufsz);
			if (!buf[cnt].data) {
				pr_err("%s: Buf alloc failed\n",
							__func__);
				mutex_unlock(&ac->cmd_lock);
				goto fail;
			}
			buf[cnt].used = dir ^ 1;
			buf[cnt].size = bufsz;
			buf[cnt].actual_size = bufsz;
/*
			pr_info("%s: data[0x%px]phys[0x%x]\n",
					__func__,
					buf[cnt].data,
					(uint32_t)buf[cnt].phys);
*/
		}
		cnt++;
	}
	ac->port[dir].max_buf_cnt = cnt;
	mutex_unlock(&ac->cmd_lock);

	return 0;
fail:
	q6asm_audio_client_buf_free_contiguous(dir, ac);
	return -EINVAL;
}
EXPORT_SYMBOL(q6asm_audio_client_buf_alloc_contiguous);

/**
 * q6asm_audio_client_free -
 *       frees the audio client for ASM
 *
 * @ac: audio client handle
 *
 */
void q6asm_audio_client_free(struct audio_client *ac)
{
	int loopcnt;
	struct audio_port_data *port;

	if (!ac) {
		pr_err("%s: ac %pK\n", __func__, ac);
		return;
	}
	if (ac->session < 0) {
		pr_err("%s: ac session invalid\n", __func__);
		return;
	}

	mutex_lock(&session_lock);

	pr_debug("%s: Session id %d\n", __func__, ac->session);
	if (ac->io_mode & SYNC_IO_MODE) {
		for (loopcnt = 0; loopcnt <= OUT; loopcnt++) {
			port = &ac->port[loopcnt];
			if (!port->buf)
				continue;
			pr_debug("%s: loopcnt = %d\n",
				__func__, loopcnt);
			q6asm_audio_client_buf_free(loopcnt, ac);
		}
	}

	rtac_set_asm_handle(ac->session, NULL);
	//apr_deregister(ac->apr2);
	apr_deregister(ac->apr);
	ac->apr2 = NULL;
	ac->apr = NULL;
	ac->mmap_apr = NULL;
	q6asm_session_free(ac);

	pr_debug("%s: APR De-Register\n", __func__);

/*done:*/
	mutex_unlock(&session_lock);
}
EXPORT_SYMBOL(q6asm_audio_client_free);

/**
 * q6asm_audio_client_alloc -
 *       Alloc audio client for ASM
 *
 * @cb: callback fn
 * @priv: private data
 *
 * Returns ac pointer on success or NULL on failure
 */
struct audio_client *q6asm_audio_client_alloc(app_cb cb, void *priv,int session_type,int stream_id)
{
	struct audio_client *ac;
	int n;
	int lcnt = 0;

	ac = kzalloc(sizeof(struct audio_client), GFP_KERNEL);
	if (!ac)
		return NULL;

	mutex_lock(&session_lock);
	n = q6asm_session_alloc(ac);
	if (n <= 0) {
		pr_err("%s: ASM Session alloc fail n=%d\n", __func__, n);
		mutex_unlock(&session_lock);
		kfree(ac);
		goto fail_session;
	}
	ac->session = n;
	ac->session_type = session_type;
	ac->stream_id = stream_id;
	ac->cb = cb;
	ac->path_delay = UINT_MAX;
	ac->priv = priv;
	ac->io_mode = SYNC_IO_MODE;
	ac->fptr_cache_ops = NULL;
	ac->apr = apr_register("ADSP", "ASM",
			(apr_fn)q6asm_callback,
			((ac->session_type) << 8 | ac->stream_id),
			ac);

	if (ac->apr == NULL) {
		pr_err("%s: Registration with APR failed\n", __func__);
		mutex_unlock(&session_lock);
		goto fail_apr1;
	}
	rtac_set_asm_handle(n, ac->apr);

#if 0
	ac->apr2 = apr_register("ADSP", "ASM",
			(apr_fn)q6asm_callback,
			((ac->session) << 8 | 0x0002),
			ac);

	if (ac->apr2 == NULL) {
		pr_err("%s: Registration with APR-2 failed\n", __func__);
		mutex_unlock(&session_lock);
		goto fail_apr2;
	}

    pr_debug("%s: Registering the common port with APR\n", __func__);
	ac->mmap_apr = q6asm_mmap_apr_reg();
	if (ac->mmap_apr == NULL) {
		mutex_unlock(&session_lock);
		goto fail_mmap;
	}
#endif
	init_waitqueue_head(&ac->cmd_wait);
	init_waitqueue_head(&ac->time_wait);
	init_waitqueue_head(&ac->mem_wait);
	atomic_set(&ac->time_flag, 1);
	atomic_set(&ac->reset, 0);
	INIT_LIST_HEAD(&ac->port[0].mem_map_handle);
	INIT_LIST_HEAD(&ac->port[1].mem_map_handle);
	pr_debug("%s: mem_map_handle list init'ed\n", __func__);
	mutex_init(&ac->cmd_lock);
	for (lcnt = 0; lcnt <= OUT; lcnt++) {
		mutex_init(&ac->port[lcnt].lock);
		spin_lock_init(&ac->port[lcnt].dsp_lock);
	}
	atomic_set(&ac->cmd_state, 0);
	atomic_set(&ac->cmd_state_pp, 0);
	atomic_set(&ac->mem_state, 0);

	pr_debug("%s: session[%d]\n", __func__, ac->session);

	mutex_unlock(&session_lock);

	return ac;

fail_apr1:
	q6asm_session_free(ac);
fail_session:
	return NULL;
}
EXPORT_SYMBOL(q6asm_audio_client_alloc);

/**
 * q6asm_get_audio_client -
 *       Retrieve audio client for ASM
 *
 * @session_id: ASM session id
 *
 * Returns valid pointer on success or NULL on failure
 */
struct audio_client *q6asm_get_audio_client(int session_id)
{
	if (session_id == ASM_CONTROL_SESSION)
		return &common_client;

	if ((session_id <= 0) || (session_id > ASM_ACTIVE_STREAMS_ALLOWED)) {
		pr_err("%s: invalid session: %d\n", __func__, session_id);
		goto err;
	}

	if (!(session[session_id].ac)) {
		pr_err("%s: session not active: %d\n", __func__, session_id);
		goto err;
	}
	return session[session_id].ac;
err:
	return NULL;
}
EXPORT_SYMBOL(q6asm_get_audio_client);

int get_buf_id(uint32_t buf_addr,struct audio_port_data *port){
    int i;
	int buf_id = -1;
	uint32_t addr = 0;

	for(i=0; i< port->max_buf_cnt; i++){
		addr = lower_32_bits((uint64_t)port->buf[i].phys);
		if(buf_addr == addr){
			buf_id = i;
			break;
		}
	}
	return buf_id;
}

static int32_t q6asm_callback(struct apr_client_data *data, void *priv)
{
	struct audio_client *ac = (struct audio_client *)priv;
	unsigned long dsp_flags = 0;
	uint32_t *payload;
	int32_t  ret = 0;
	int buf_index = 0;
	unsigned long flags = 0;
	int session_id;

	if (ac == NULL) {
		pr_err("%s: ac NULL\n", __func__);
		return -EINVAL;
	}
	if (data == NULL) {
		pr_err("%s: data NULL\n", __func__);
		return -EINVAL;
	}

	session_id = q6asm_get_session_id_from_audio_client(ac);
	if (session_id <= 0 || session_id > ASM_ACTIVE_STREAMS_ALLOWED) {
		pr_err("%s: Session ID is invalid, session = %d\n", __func__,
			session_id);
		return -EINVAL;
	}
	pr_debug("%s: session_id %d \n", __func__,session_id);
	spin_lock_irqsave(&(session[session_id].session_lock), flags);

	if (!q6asm_is_valid_audio_client(ac)) {
		pr_err("%s: audio client pointer is invalid, ac = %pK\n",
				__func__, ac);
		spin_unlock_irqrestore(
			&(session[session_id].session_lock), flags);
		return -EINVAL;
	}

	payload = data->payload;
	if (data->opcode == RESET_EVENTS) {
		atomic_set(&ac->reset, 1);
		pr_debug("%s: Reset event is received: %d %d apr[%p]\n",
			__func__,
			data->reset_event, data->reset_proc, ac->apr);
		if (ac->cb)
			ac->cb(data->opcode, data->token,
				(uint32_t *)data->payload, ac->priv);
		apr_reset(ac->apr);
		ac->apr = NULL;
		atomic_set(&ac->time_flag, 0);
		atomic_set(&ac->cmd_state, 0);
		atomic_set(&ac->mem_state, 0);
		atomic_set(&ac->cmd_state_pp, 0);
		wake_up(&ac->time_wait);
		wake_up(&ac->cmd_wait);
		wake_up(&ac->mem_wait);
		spin_unlock_irqrestore(
			&(session[session_id].session_lock), flags);
		return 0;
	}

	pr_debug("%s: session[%d]opcode[0x%X] token[0x%x]payload_size[%d] src[%d] dest[%d]\n",
		 __func__,
		ac->session, data->opcode,
		data->token, data->payload_size, data->src_port,
		data->dest_port);
	if ((data->opcode != ASM_DATA_EVENT_RENDERED_EOS) &&
	    (data->opcode != ASM_DATA_EVENT_EOS) &&
	    (data->opcode != ASM_SESSION_EVENTX_OVERFLOW) &&
	    (data->opcode != ASM_SESSION_EVENT_RX_UNDERFLOW) &&
	    (data->opcode != ASM_VT_EVENT)) {
		if (payload == NULL) {
			pr_err("%s: payload is null\n", __func__);
			spin_unlock_irqrestore(
				&(session[session_id].session_lock), flags);
			return -EINVAL;
		}
		if(data->payload_size >= 2 * sizeof(uint32_t))
			dev_vdbg(ac->dev, "%s: Payload = [0x%x] status[0x%x] opcode 0x%x\n",
				__func__, payload[0], payload[1], data->opcode);
		else
			dev_vdbg(ac->dev, "%s: Payload size of %d is less than expected.\n",
				__func__, data->payload_size);
	}
	if (data->opcode == APR_BASIC_RSP_RESULT) {
		switch (payload[0]) {
		case ASM_SESSION_CMD_PAUSE:
		case ASM_SESSION_CMD_SUSPEND:
		case ASM_DATA_CMD_EOS:
		case ASM_STREAM_CMD_CLOSE:
		case ASM_STREAM_CMD_FLUSH:
		case ASM_SESSION_CMD_RUN_V2:
		case ASM_STREAM_CMD_FLUSH_READBUFS:

			ret = q6asm_is_valid_session(data, priv);
			if (ret != 0) {
				pr_err("%s: session invalid %d\n", __func__, ret);
				spin_unlock_irqrestore(
					&(session[session_id].session_lock), flags);
				return ret;
			}
		case ASM_STREAM_CMD_OPEN_READ_V3:
		case ASM_STREAM_CMD_OPEN_WRITE_V3:
		case ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2:
		case ASM_STREAM_CMD_OPEN_READ_COMPRESSED:
		case ASM_STREAM_CMD_OPEN_WRITE_COMPRESSED:
		case ASM_VT_OPEN:
		case ASM_VT_HW_CONFIG:
		case ASM_VT_CLOSE:
		case ASM_VT_SET_MODE:
		case ASM_VT_RUN:
		case ASM_VT_PAUSE:
			if (data->payload_size >= 2 * sizeof(uint32_t)) {
				pr_debug("%s: session %d opcode 0x%x token 0x%x Payload = [0x%X] status 0x%x src %d dest %d\n",
					__func__, ac->session,
					data->opcode, data->token,
					payload[0], payload[1],
					data->src_port, data->dest_port);
			} else {
				pr_err("%s: payload size of %x is less than expected.\n",
					__func__, data->payload_size);
			}

			atomic_set(&ac->cmd_state,payload[1]);
			wake_up(&ac->cmd_wait);

			if (ac->cb)
				ac->cb(data->opcode, data->token,
					(uint32_t *)data->payload, ac->priv);
			break;
		default:
			pr_debug("%s: command[0x%x] not expecting rsp\n",
							__func__, payload[0]);
			break;
		}

		spin_unlock_irqrestore(
			&(session[session_id].session_lock), flags);
		return 0;
	}

	switch (data->opcode) {
	case ASM_DATA_EVENT_WRITE_DONE_V2:{		
		struct audio_port_data *port = &ac->port[IN];
		if (data->payload_size >= 2 * sizeof(uint32_t))
			dev_vdbg(ac->dev, "%s: Rxed status[0x%x] addr[0x%x] token[0x%x]",
					__func__, payload[0], payload[1],data->token);
		else
			dev_err(ac->dev, "%s: payload size of %x is less than expected.\n",
				__func__, data->payload_size);
				
		if (ac->io_mode & SYNC_IO_MODE) {
			if (port->buf == NULL) {
				pr_err("%s: Unexpected Write Done\n",
								__func__);
				spin_unlock_irqrestore(
					&(session[session_id].session_lock),
					flags);
				return -EINVAL;
			}
			spin_lock_irqsave(&port->dsp_lock, dsp_flags);

			buf_index = get_buf_id(payload[1],port);
			if (buf_index < 0 ) {
				pr_err("%s: Invalid buffer index %u bufaddr[0x%x]\n",
					__func__, buf_index,payload[1]);
				spin_unlock_irqrestore(&port->dsp_lock,
								dsp_flags);
				spin_unlock_irqrestore(
					&(session[session_id].session_lock),
					flags);
				return -EINVAL;
			}

			port->buf[buf_index].used = 1;
			spin_unlock_irqrestore(&port->dsp_lock, dsp_flags);

            dev_dbg(ac->dev, "%s: Rxed status[0x%x] addr[0x%x] buf_index[%d]",
                __func__, payload[0], payload[1],buf_index);

		}
		
		break;
	}
	case ASM_DATA_EVENT_READ_DONE_V2:
	case ASM_VT_READ_DONE:{

		struct audio_port_data *port = &ac->port[OUT];

		if (ac->io_mode & SYNC_IO_MODE) {
			if (port->buf == NULL) {
				pr_err("%s: Unexpected Read Done\n", __func__);
				spin_unlock_irqrestore(
					&(session[session_id].session_lock),
					flags);
				return -EINVAL;
			}
			spin_lock_irqsave(&port->dsp_lock, dsp_flags);
			
			buf_index = get_buf_id(payload[1],port);
			
			if (buf_index < 0) {
				pr_debug("%s: Invalid buffer index %u\n",
					__func__, buf_index);
				dev_dbg(ac->dev, "%s: Rxed status[0x%x] addr[0x%x] size[%d] token[0x%x] \n",
					__func__, payload[0], payload[1],payload[2]);
				spin_unlock_irqrestore(&port->dsp_lock,
								dsp_flags);
				spin_unlock_irqrestore(
					&(session[session_id].session_lock),
					flags);
				return -EINVAL;
			}
			port->buf[buf_index].used = 0;

			port->buf[buf_index].actual_size =
				payload[2];
			spin_unlock_irqrestore(&port->dsp_lock, dsp_flags);

            dev_dbg(ac->dev, "%s: Rxed status[0x%x] addr[0x%x] act_size[%d] buf_index[%d]",
                    __func__, payload[0], payload[1],payload[2],buf_index);
		}
		break;
	}
	case ASM_VT_EVENT:
		pr_debug("%s: ASM_VT_EVENT received: rxed session %d opcode 0x%x token 0x%x src %d dest %d\n",
				__func__, ac->session,
				data->opcode, data->token,
				data->src_port, data->dest_port);
		break;
	case ASM_DATA_EVENT_EOS:
	case ASM_DATA_EVENT_RENDERED_EOS:
		pr_debug("%s: EOS ACK received: rxed session %d opcode 0x%x token 0x%x src %d dest %d\n",
				__func__, ac->session,
				data->opcode, data->token,
				data->src_port, data->dest_port);
		break;
	}

	q6asm_update_token(&data->token,
		   0, /* Session ID is NA */
		   0, /* Stream ID is NA */
		   buf_index,
		   0, /* Direction flag is NA */
		   NO_WAIT_CMD);
	
	if (ac->cb)
		ac->cb(data->opcode, data->token,
			data->payload, ac->priv);

	spin_unlock_irqrestore(
		&(session[session_id].session_lock), flags);
	
	return 0;
}

/**
 * q6asm_is_cpu_buf_avail -
 *       retrieve next CPU buf avail
 *
 * @dir: RX or TX direction
 * @ac: Audio client handle
 * @size: size pointer to be updated with size of buffer
 * @index: index pointer to be updated with
 * 	CPU buffer index available
 *
 * Returns buffer pointer on success or NULL on failure
 */
void *q6asm_is_cpu_buf_avail(int dir, struct audio_client *ac, uint32_t *size,
				uint32_t *index)
{
	void *data;
	unsigned char idx;
	struct audio_port_data *port;

	if (!ac || ((dir != IN) && (dir != OUT))) {
		pr_err("%s: ac %pK dir %d\n", __func__, ac, dir);
		return NULL;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[dir];

		mutex_lock(&port->lock);
		idx = port->cpu_buf;
		if (port->buf == NULL) {
			pr_err("%s: Buffer pointer null\n", __func__);
			mutex_unlock(&port->lock);
			return NULL;
		}
		/* dir 0: used = 0 means buf in use
		 * dir 1: used = 1 means buf in use
		 */
		if (port->buf[idx].used == dir) {
			/* To make it more robust, we could loop and get the
			 * next avail buf, its risky though
			 */
			pr_debug("%s: Next buf idx[0x%x] not available, dir[%d]\n",
			 __func__, idx, dir);
			mutex_unlock(&port->lock);
			return NULL;
		}
		*size = port->buf[idx].actual_size;
		*index = port->cpu_buf;
		data = port->buf[idx].data;
		dev_vdbg(ac->dev, "%s: session[%d]index[%d] data[%pK]size[%d]\n",
						__func__,
						ac->session,
						port->cpu_buf,
						data, *size);
		/* By default increase the cpu_buf cnt
		 * user accesses this function,increase cpu
		 * buf(to avoid another api)
		 */
		port->buf[idx].used = dir;
		port->cpu_buf = q6asm_get_next_buf(ac, port->cpu_buf,
						   port->max_buf_cnt);
		mutex_unlock(&port->lock);
		return data;
	}
	return NULL;
}
EXPORT_SYMBOL(q6asm_is_cpu_buf_avail);

/**
 * q6asm_cpu_buf_release -
 *       releases cpu buffer for ASM
 *
 * @dir: RX or TX direction
 * @ac: Audio client handle
 *
 * Returns 0 on success or error on failure
 */
int q6asm_cpu_buf_release(int dir, struct audio_client *ac)
{
	struct audio_port_data *port;
	int ret = 0;
	int idx;

	if (!ac || ((dir != IN) && (dir != OUT))) {
		pr_err("%s: ac %pK dir %d\n", __func__, ac, dir);
		ret = -EINVAL;
		goto exit;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[dir];
		mutex_lock(&port->lock);
		idx = port->cpu_buf;
		if (port->cpu_buf == 0) {
			port->cpu_buf = port->max_buf_cnt - 1;
		} else if (port->cpu_buf < port->max_buf_cnt) {
			port->cpu_buf = port->cpu_buf - 1;
		} else {
			pr_err("%s: buffer index(%d) out of range\n",
			       __func__, port->cpu_buf);
			ret = -EINVAL;
			mutex_unlock(&port->lock);
			goto exit;
		}
		port->buf[port->cpu_buf].used = dir ^ 1;
		mutex_unlock(&port->lock);
	}
exit:
	return ret;
}
EXPORT_SYMBOL(q6asm_cpu_buf_release);

/**
 * q6asm_is_cpu_buf_avail_nolock -
 *       retrieve next CPU buf avail without lock acquire
 *
 * @dir: RX or TX direction
 * @ac: Audio client handle
 * @size: size pointer to be updated with size of buffer
 * @index: index pointer to be updated with
 * 	CPU buffer index available
 *
 * Returns buffer pointer on success or NULL on failure
 */
void *q6asm_is_cpu_buf_avail_nolock(int dir, struct audio_client *ac,
					uint32_t *size, uint32_t *index)
{
	void *data;
	unsigned char idx;
	struct audio_port_data *port;

	if (!ac || ((dir != IN) && (dir != OUT))) {
		pr_err("%s: ac %pK dir %d\n", __func__, ac, dir);
		return NULL;
	}

	port = &ac->port[dir];

	idx = port->cpu_buf;
	if (port->buf == NULL) {
		pr_err("%s: Buffer pointer null\n", __func__);
		return NULL;
	}
	/*
	 * dir 0: used = 0 means buf in use
	 * dir 1: used = 1 means buf in use
	 */
	if (port->buf[idx].used == dir) {
		/*
		 * To make it more robust, we could loop and get the
		 * next avail buf, its risky though
		 */
		pr_err("%s: Next buf idx[0x%x] not available, dir[%d]\n",
		 __func__, idx, dir);
		return NULL;
	}
	*size = port->buf[idx].actual_size;
	*index = port->cpu_buf;
	data = port->buf[idx].data;
	dev_vdbg(ac->dev, "%s: session[%d]index[%d] data[%pK]size[%d]\n",
		__func__, ac->session, port->cpu_buf,
		data, *size);
	/*
	 * By default increase the cpu_buf cnt
	 * user accesses this function,increase cpu
	 * buf(to avoid another api)
	 */
	port->buf[idx].used = dir;
	port->cpu_buf = q6asm_get_next_buf(ac, port->cpu_buf,
					   port->max_buf_cnt);
	return data;
}
EXPORT_SYMBOL(q6asm_is_cpu_buf_avail_nolock);

int q6asm_is_dsp_buf_avail(int dir, struct audio_client *ac)
{
	int ret = -1;
	struct audio_port_data *port;
	uint32_t idx;

	if (!ac || (dir != OUT)) {
		pr_err("%s: ac %pK dir %d\n", __func__, ac, dir);
		return ret;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[dir];

		mutex_lock(&port->lock);
		idx = port->dsp_buf;

		if (port->buf[idx].used == (dir ^ 1)) {
			/* To make it more robust, we could loop and get the
			 * next avail buf, its risky though
			 */
			pr_err("%s: Next buf idx[0x%x] not available, dir[%d]\n",
				__func__, idx, dir);
			mutex_unlock(&port->lock);
			return ret;
		}
		dev_vdbg(ac->dev, "%s: session[%d]dsp_buf=%d cpu_buf=%d\n",
			__func__,
			ac->session, port->dsp_buf, port->cpu_buf);
		ret = ((port->dsp_buf != port->cpu_buf) ? 0 : -1);
		mutex_unlock(&port->lock);
	}
	return ret;
}

static void __q6asm_add_hdr(struct audio_client *ac, struct apr_hdr *hdr,
			uint32_t pkt_size, uint32_t cmd_flg)
{
	unsigned long flags = 0;

	dev_vdbg(ac->dev, "%s: pkt_size=%d cmd_flg=%d session=%d stream_id=%d\n",
			__func__, pkt_size, cmd_flg, ac->session, ac->stream_id);
	mutex_lock(&ac->cmd_lock);
	spin_lock_irqsave(&(session[ac->session].session_lock), flags);
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL", __func__);
		spin_unlock_irqrestore(
			&(session[ac->session].session_lock), flags);
		mutex_unlock(&ac->cmd_lock);
		return;
	}

	hdr->hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
			APR_HDR_LEN(sizeof(struct apr_hdr)),
			APR_PKT_VER);
	hdr->src_svc = ((struct apr_svc *)ac->apr)->id;
	hdr->src_domain = APR_DOMAIN_APPS;
	hdr->dest_svc = APR_SVC_ASM;
	hdr->dest_domain = APR_DOMAIN_ADSP;
	hdr->src_port = ((ac->session_type << 8) & 0xFF00) | (ac->stream_id);
	hdr->dest_port = ((ac->session_type << 8) & 0xFF00) | (ac->stream_id);
	if (cmd_flg)
		q6asm_update_token(&hdr->token,
				   ac->session,
				   0, /* Stream ID is NA */
				   0, /* Buffer index is NA */
				   0, /* Direction flag is NA */
				   WAIT_CMD);

	hdr->pkt_size  = pkt_size;
	spin_unlock_irqrestore(
		&(session[ac->session].session_lock), flags);
	mutex_unlock(&ac->cmd_lock);
}

static void q6asm_add_hdr(struct audio_client *ac, struct apr_hdr *hdr,
			uint32_t pkt_size, uint32_t cmd_flg)
{
	__q6asm_add_hdr(ac, hdr, pkt_size, cmd_flg);
}

static void q6asm_stream_add_hdr(struct audio_client *ac, struct apr_hdr *hdr,
			uint32_t pkt_size, uint32_t cmd_flg)
{
	__q6asm_add_hdr(ac, hdr, pkt_size, cmd_flg);
}

static void __q6asm_add_hdr_async(struct audio_client *ac, struct apr_hdr *hdr,
				  uint32_t pkt_size, uint32_t cmd_flg,u8 no_wait_flag)
{
	dev_vdbg(ac->dev, "%s: pkt_size = %d, cmd_flg = %d, session_type = %d stream_id=%d\n",
			__func__, pkt_size, cmd_flg, ac->session_type, ac->stream_id);
	hdr->hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
			APR_HDR_LEN(sizeof(struct apr_hdr)),
			APR_PKT_VER);
	if (ac->apr == NULL) {
		pr_err("%s: AC APR is NULL", __func__);
		return;
	}
	hdr->src_svc = ((struct apr_svc *)ac->apr)->id;
	hdr->src_domain = APR_DOMAIN_APPS;
	hdr->dest_svc = APR_SVC_ASM;
	hdr->dest_domain = APR_DOMAIN_ADSP;
	hdr->src_port = ((ac->session_type << 8) & 0xFF00) | (ac->stream_id);
	hdr->dest_port = ((ac->session_type << 8) & 0xFF00) | (ac->stream_id);
	if (cmd_flg) {
		q6asm_update_token(&hdr->token,
				   ac->session,
				   0, /* Stream ID is NA */
				   0, /* Buffer index is NA */
				   0, /* Direction flag is NA */
				   no_wait_flag);

	}
	hdr->pkt_size  = pkt_size;
}

static void q6asm_add_hdr_async(struct audio_client *ac, struct apr_hdr *hdr,
				uint32_t pkt_size, uint32_t cmd_flg)
{
	__q6asm_add_hdr_async(ac, hdr, pkt_size, cmd_flg,WAIT_CMD);
}

static void q6asm_stream_add_hdr_async(struct audio_client *ac,
					struct apr_hdr *hdr, uint32_t pkt_size,
					uint32_t cmd_flg)
{
	__q6asm_add_hdr_async(ac, hdr, pkt_size, cmd_flg,NO_WAIT_CMD);
}

static void q6asm_add_mmaphdr(struct audio_client *ac, struct apr_hdr *hdr,
			u32 pkt_size, int dir)
{
	pr_debug("%s: pkt size=%d\n",
		__func__, pkt_size);
	hdr->hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
	hdr->src_port = 0;
	hdr->dest_port = 0;
	q6asm_update_token(&hdr->token,
			   ac->session,
			   0, /* Stream ID is NA */
			   0, /* Buffer index is NA */
			   dir,
			   WAIT_CMD);
	hdr->pkt_size  = pkt_size;
}

static int __q6asm_open_read(struct audio_client *ac, uint32_t format,
		    struct asm_pcm_cfg_param cfg_param)
{
	int rc = 0x00;
	struct asm_stream_cmd_open open;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	pr_info( "%s: session[%d] format[0x%x] stream_id[%d], rate(%d) channels(%d) bits(%d)\n",
               __func__, ac->session, format,ac->stream_id,
               cfg_param.sample_rate, cfg_param.channel_num,cfg_param.bit_width);

	q6asm_add_hdr(ac, &open.hdr, sizeof(open), TRUE);
	atomic_set(&ac->cmd_state, -1);
	open.hdr.opcode = ASM_STREAM_CMD_OPEN_READ_V3;

	switch (format) {
		case FORMAT_LINEAR_PCM:
			open.fmt_id = ASM_MEDIA_FMT_PCM;
			open.cfg_param = cfg_param;
			break;
		default:
			pr_err("%s: Invalid format 0x%x\n",
				__func__, format);
			rc = -EINVAL;
			goto fail_cmd;
	}

	rc = apr_send_pkt(ac->apr, (uint32_t *) &open);
	if (rc < 0) {
		pr_err("%s: open failed op[0x%x]rc[%d]\n",
				__func__, open.hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}
	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0),
			msecs_to_jiffies(TIMEOUT_MS));
	rc = 1;
	if (!rc) {
		pr_err("%s: timeout. waited for open read\n",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) > 0) {
		pr_err("%s: DSP returned error[%s]\n",
				__func__, adsp_err_get_err_str(
				atomic_read(&ac->cmd_state)));
		rc = adsp_err_get_lnx_err_code(
				atomic_read(&ac->cmd_state));
		goto fail_cmd;
	}

	ac->io_mode |= TUN_READ_IO_MODE;

	return 0;
fail_cmd:
	return rc;
}

/*
 * asm_open_read_v5 - Opens audio capture session
 *
 * @ac: Client session handle
 * @format: encoder format
 * @bits_per_sample: bit width of capture session
 * @ts_mode: timestamp mode
 */
int q6asm_open_read_v5(struct audio_client *ac, uint32_t format,
			uint16_t bits_per_sample,uint32_t rate, uint32_t channels,
			uint32_t interleave)
{
    struct asm_pcm_cfg_param pcm_cfg;

	pcm_cfg.sample_rate = rate;
	pcm_cfg.channel_num = channels;
	pcm_cfg.bit_width   = bits_per_sample;
	pcm_cfg.interleave  = interleave;
	
	return __q6asm_open_read(ac, format, pcm_cfg);
}
EXPORT_SYMBOL(q6asm_open_read_v5);

static int __q6asm_open_write(struct audio_client *ac, uint32_t format,
		    struct asm_pcm_cfg_param cfg_param)
{
	int rc = 0x00;
	struct asm_stream_cmd_open open;

	/*if adsp state down*/
	if(apr_get_q6_state() == APR_SUBSYS_DOWN) {
		pr_err("%s: adsp state is DOWN\n", __func__);
		return -EINVAL;
	}

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	q6asm_stream_add_hdr(ac, &open.hdr, sizeof(open), TRUE);
	atomic_set(&ac->cmd_state, -1);
	/*
	 * Updated the token field with stream/session for compressed playback
	 * Platform driver must know the the stream with which the command is
	 * associated
	 */
	if (ac->io_mode & COMPRESSED_STREAM_IO)
		q6asm_update_token(&open.hdr.token,
				   ac->session,
				   ac->stream_id,
				   0, /* Buffer index is NA */
				   0, /* Direction flag is NA */
				   WAIT_CMD);

	pr_info("%s: token(0x%x) stream_id(%d) session(0x%x),rate(%d) channels(%d) bits(%d)\n",
			__func__, open.hdr.token, ac->stream_id, ac->session,
			cfg_param.sample_rate,cfg_param.channel_num,cfg_param.bit_width);

	open.hdr.opcode = ASM_STREAM_CMD_OPEN_WRITE_V3;
	if (ac->stream_id == LOW_LATENCY_PCM_MODE){
		pr_debug("%s: low latency playback stream_id 1\n", __func__);
	}else {
		pr_debug("%s: deepbuffer playback stream_id 0\n", __func__);
	}

	switch (format) {
		case FORMAT_LINEAR_PCM:
			open.fmt_id = ASM_MEDIA_FMT_PCM;
			break;
		default:
			pr_err("%s: Invalid format 0x%x\n", __func__, format);
			rc = -EINVAL;
			goto fail_cmd;
	}

	open.cfg_param = cfg_param;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &open);
	if (rc < 0) {
		pr_err("%s: open failed op[0x%x]rc[%d]\n",
				__func__, open.hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}
	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0),
			msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for open write\n", __func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) > 0) {
		pr_err("%s: DSP returned error[%s]\n",
				__func__, adsp_err_get_err_str(
				atomic_read(&ac->cmd_state)));
		rc = adsp_err_get_lnx_err_code(
				atomic_read(&ac->cmd_state));
		goto fail_cmd;
	}
	ac->io_mode |= TUN_WRITE_IO_MODE;

	return 0;
fail_cmd:
	return rc;
}


/*
 * q6asm_open_write_v5 - Opens audio playback session
 *
 * @ac: Client session handle
 * @format: decoder format
 * @bits_per_sample: bit width of playback session
 */
int q6asm_open_write_v5(struct audio_client *ac, uint32_t format,
			uint16_t bits_per_sample,uint32_t rate, uint32_t channels,
			uint32_t interleave)
{
    struct asm_pcm_cfg_param pcm_cfg;

	pcm_cfg.sample_rate = rate;
	pcm_cfg.channel_num = channels;
	pcm_cfg.bit_width = bits_per_sample;
	pcm_cfg.interleave  = interleave;
	
	return __q6asm_open_write(ac, format, pcm_cfg);
}
EXPORT_SYMBOL(q6asm_open_write_v5);

/**
 * q6asm_run -
 *       command to set ASM to run state
 *
 * @ac: Audio client handle
 * @flags: Flags for session
 * @msw_ts: upper 32bits timestamp
 * @lsw_ts: lower 32bits timestamp
 *
 * Returns 0 on success or error on failure
 */
int q6asm_run(struct audio_client *ac, uint32_t flags,
		uint32_t msw_ts, uint32_t lsw_ts)
{
	struct asm_session_cmd_run_v2 run;
	int rc;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_info("%s: session[%d]\n", __func__, ac->session);

	q6asm_add_hdr(ac, &run.hdr, sizeof(run), TRUE);
	atomic_set(&ac->cmd_state, -1);

	run.hdr.opcode = ASM_SESSION_CMD_RUN_V2;
	run.flags    = flags;
	run.time_lsw = lsw_ts;
	run.time_msw = msw_ts;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &run);
	if (rc < 0) {
		pr_err("%s: Commmand run failed[%d]",
				__func__, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0),
			msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for run success",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) > 0) {
		pr_err("%s: DSP returned error[%s]\n",
				__func__, adsp_err_get_err_str(
				atomic_read(&ac->cmd_state)));
		rc = adsp_err_get_lnx_err_code(
				atomic_read(&ac->cmd_state));
		goto fail_cmd;
	}

	return 0;
fail_cmd:
	return rc;
}
EXPORT_SYMBOL(q6asm_run);

static int __q6asm_run_nowait(struct audio_client *ac, uint32_t flags,
		uint32_t msw_ts, uint32_t lsw_ts)
{
	struct asm_session_cmd_run_v2 run;
	int rc;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: session[%d]\n", __func__, ac->session);

	q6asm_stream_add_hdr_async(ac, &run.hdr, sizeof(run), TRUE);
	atomic_set(&ac->cmd_state, 1);
	run.hdr.opcode = ASM_SESSION_CMD_RUN_V2;
	run.flags    = flags;
	run.time_lsw = lsw_ts;
	run.time_msw = msw_ts;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &run);
	if (rc < 0) {
		pr_err("%s: Commmand run failed[%d]", __func__, rc);
		return -EINVAL;
	}
	return 0;
}

/**
 * q6asm_run_nowait -
 *       command to set ASM to run state with no wait for ack
 *
 * @ac: Audio client handle
 * @flags: Flags for session
 * @msw_ts: upper 32bits timestamp
 * @lsw_ts: lower 32bits timestamp
 *
 * Returns 0 on success or error on failure
 */
int q6asm_run_nowait(struct audio_client *ac, uint32_t flags,
			uint32_t msw_ts, uint32_t lsw_ts)
{
	return __q6asm_run_nowait(ac, flags, msw_ts, lsw_ts);
}
EXPORT_SYMBOL(q6asm_run_nowait);

/**
 * q6asm_memory_map -
 *       command to send memory map for ASM
 *
 * @ac: Audio client handle
 * @buf_add: buffer address to map
 * @dir: RX or TX session
 * @bufsz: size of each buffer
 * @bufcnt: buffer count
 *
 * Returns 0 on success or error on failure
 */
int q6asm_memory_map(struct audio_client *ac, phys_addr_t buf_add, int dir,
				uint32_t bufsz, uint32_t bufcnt)
{
	struct avs_cmd_shared_mem_map_regions *mmap_regions = NULL;
	struct avs_shared_map_region_payload  *mregions = NULL;
	struct audio_port_data *port = NULL;
	void	*mmap_region_cmd = NULL;
	void	*payload = NULL;
	struct asm_buffer_node *buffer_node = NULL;
	int	rc = 0;
	int	cmd_size = 0;

	if (!ac) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->mmap_apr == NULL) {
		pr_err("%s: mmap APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: Session[%d]\n", __func__, ac->session);

	buffer_node = kmalloc(sizeof(struct asm_buffer_node), GFP_KERNEL);
	if (!buffer_node)
		return -ENOMEM;

	cmd_size = sizeof(struct avs_cmd_shared_mem_map_regions)
			+ sizeof(struct avs_shared_map_region_payload) * bufcnt;

	mmap_region_cmd = kzalloc(cmd_size, GFP_KERNEL);
	if (mmap_region_cmd == NULL) {
		rc = -EINVAL;
		kfree(buffer_node);
		return rc;
	}
	mmap_regions = (struct avs_cmd_shared_mem_map_regions *)
							mmap_region_cmd;
	q6asm_add_mmaphdr(ac, &mmap_regions->hdr, cmd_size, dir);
	atomic_set(&ac->mem_state, -1);
	mmap_regions->hdr.opcode = ASM_CMD_SHARED_MEM_MAP_REGIONS;
	mmap_regions->mem_pool_id = ADSP_MEMORY_MAP_SHMEM8_4K_POOL;
	mmap_regions->num_regions = bufcnt & 0x00ff;
	mmap_regions->property_flag = 0x00;
	payload = ((u8 *) mmap_region_cmd +
		sizeof(struct avs_cmd_shared_mem_map_regions));
	mregions = (struct avs_shared_map_region_payload *)payload;

	ac->port[dir].tmp_hdl = 0;
	port = &ac->port[dir];
	pr_debug("%s: buf_add 0x%pK, bufsz: %d\n", __func__,
		&buf_add, bufsz);
	mregions->shm_addr_lsw = lower_32_bits(buf_add);
	mregions->shm_addr_msw = msm_audio_populate_upper_32_bits(buf_add);
	mregions->mem_size_bytes = bufsz;
	++mregions;

	rc = apr_send_pkt(ac->mmap_apr, (uint32_t *) mmap_region_cmd);
	if (rc < 0) {
		pr_err("%s: mmap op[0x%x]rc[%d]\n", __func__,
					mmap_regions->hdr.opcode, rc);
		rc = -EINVAL;
		kfree(buffer_node);
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->mem_wait,
			(atomic_read(&ac->mem_state) >= 0 &&
			 ac->port[dir].tmp_hdl),
			msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for memory_map\n", __func__);
		rc = -ETIMEDOUT;
		kfree(buffer_node);
		goto fail_cmd;
	}
	if (atomic_read(&ac->mem_state) > 0) {
		pr_err("%s: DSP returned error[%s] for memory_map\n",
			__func__, adsp_err_get_err_str(
			atomic_read(&ac->mem_state)));
		rc = adsp_err_get_lnx_err_code(
			atomic_read(&ac->mem_state));
		kfree(buffer_node);
		goto fail_cmd;
	}
	buffer_node->buf_phys_addr = buf_add;
	buffer_node->mmap_hdl = ac->port[dir].tmp_hdl;
	list_add_tail(&buffer_node->list, &ac->port[dir].mem_map_handle);
	ac->port[dir].tmp_hdl = 0;
	rc = 0;

fail_cmd:
	kfree(mmap_region_cmd);
	return rc;
}
EXPORT_SYMBOL(q6asm_memory_map);

/**
 * q6asm_memory_unmap -
 *       command to send memory unmap for ASM
 *
 * @ac: Audio client handle
 * @buf_add: buffer address to unmap
 * @dir: RX or TX session
 *
 * Returns 0 on success or error on failure
 */
int q6asm_memory_unmap(struct audio_client *ac, phys_addr_t buf_add, int dir)
{
	struct avs_cmd_shared_mem_unmap_regions mem_unmap;
	struct asm_buffer_node *buf_node = NULL;
	struct list_head *ptr, *next;

	int rc = 0;

	if (!ac) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (this_mmap.apr == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: Session[%d]\n", __func__, ac->session);

	q6asm_add_mmaphdr(ac, &mem_unmap.hdr,
			sizeof(struct avs_cmd_shared_mem_unmap_regions),
			dir);
	atomic_set(&ac->mem_state, -1);
	mem_unmap.hdr.opcode = ASM_CMD_SHARED_MEM_UNMAP_REGIONS;
	mem_unmap.mem_map_handle = 0;
	list_for_each_safe(ptr, next, &ac->port[dir].mem_map_handle) {
		buf_node = list_entry(ptr, struct asm_buffer_node,
						list);
		if (buf_node->buf_phys_addr == buf_add) {
			pr_debug("%s: Found the element\n", __func__);
			mem_unmap.mem_map_handle = buf_node->mmap_hdl;
			break;
		}
	}
	pr_debug("%s: mem_unmap-mem_map_handle: 0x%x\n",
		__func__, mem_unmap.mem_map_handle);

	if (mem_unmap.mem_map_handle == 0) {
		pr_err("%s: Do not send null mem handle to DSP\n", __func__);
		rc = 0;
		goto fail_cmd;
	}
	rc = apr_send_pkt(ac->mmap_apr, (uint32_t *) &mem_unmap);
	if (rc < 0) {
		pr_err("%s: mem_unmap op[0x%x]rc[%d]\n", __func__,
					mem_unmap.hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->mem_wait,
			(atomic_read(&ac->mem_state) >= 0),
			msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for memory_unmap of handle 0x%x\n",
			__func__, mem_unmap.mem_map_handle);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	} else if (atomic_read(&ac->mem_state) > 0) {
		pr_err("%s DSP returned error [%s] map handle 0x%x\n",
			__func__, adsp_err_get_err_str(
			atomic_read(&ac->mem_state)),
			mem_unmap.mem_map_handle);
		rc = adsp_err_get_lnx_err_code(
			atomic_read(&ac->mem_state));
		goto fail_cmd;
	} else if (atomic_read(&ac->unmap_cb_success) == 0) {
		pr_err("%s: Error in mem unmap callback of handle 0x%x\n",
			__func__, mem_unmap.mem_map_handle);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = 0;
fail_cmd:
	list_for_each_safe(ptr, next, &ac->port[dir].mem_map_handle) {
		buf_node = list_entry(ptr, struct asm_buffer_node,
						list);
		if (buf_node->buf_phys_addr == buf_add) {
			list_del(&buf_node->list);
			kfree(buf_node);
			break;
		}
	}
	return rc;
}
EXPORT_SYMBOL(q6asm_memory_unmap);

/**
 * q6asm_memory_map_regions -
 *       command to send memory map regions for ASM
 *
 * @ac: Audio client handle
 * @dir: RX or TX session
 * @bufsz: size of each buffer
 * @bufcnt: buffer count
 * @is_contiguous: alloc contiguous mem or not
 *
 * Returns 0 on success or error on failure
 */
int q6asm_memory_map_regions(struct audio_client *ac, int dir,
				uint32_t bufsz, uint32_t bufcnt,
				bool is_contiguous)
{
	struct avs_cmd_shared_mem_map_regions *mmap_regions = NULL;
	struct avs_shared_map_region_payload  *mregions = NULL;
	struct audio_port_data *port = NULL;
	struct audio_buffer *ab = NULL;
	void	*mmap_region_cmd = NULL;
	void	*payload = NULL;
	struct asm_buffer_node *buffer_node = NULL;
	int	rc = 0;
	int    i = 0;
	uint32_t cmd_size = 0;
	uint32_t bufcnt_t;
	uint32_t bufsz_t;

	if (!ac) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->mmap_apr == NULL) {
		pr_err("%s: mmap APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: Session[%d]\n", __func__, ac->session);

	bufcnt_t = (is_contiguous) ? 1 : bufcnt;
	bufsz_t = (is_contiguous) ? (bufsz * bufcnt) : bufsz;

	if (is_contiguous) {
		/* The size to memory map should be multiple of 4K bytes */
		bufsz_t = PAGE_ALIGN(bufsz_t);
	}

	if (bufcnt_t > (UINT_MAX
			- sizeof(struct avs_cmd_shared_mem_map_regions))
			/ sizeof(struct avs_shared_map_region_payload)) {
		pr_err("%s: Unsigned Integer Overflow. bufcnt_t = %u\n",
				__func__, bufcnt_t);
		return -EINVAL;
	}

	cmd_size = sizeof(struct avs_cmd_shared_mem_map_regions)
			+ (sizeof(struct avs_shared_map_region_payload)
							* bufcnt_t);


	if (bufcnt > (UINT_MAX / sizeof(struct asm_buffer_node))) {
		pr_err("%s: Unsigned Integer Overflow. bufcnt = %u\n",
				__func__, bufcnt);
		return -EINVAL;
	}

	buffer_node = kzalloc(sizeof(struct asm_buffer_node) * bufcnt,
				GFP_KERNEL);
	if (!buffer_node)
		return -ENOMEM;

	mmap_region_cmd = kzalloc(cmd_size, GFP_KERNEL);
	if (mmap_region_cmd == NULL) {
		rc = -EINVAL;
		kfree(buffer_node);
		return rc;
	}
	mmap_regions = (struct avs_cmd_shared_mem_map_regions *)
							mmap_region_cmd;
	q6asm_add_mmaphdr(ac, &mmap_regions->hdr, cmd_size, dir);
	atomic_set(&ac->mem_state, -1);
	pr_debug("%s: mmap_region=0x%pK token=0x%x\n", __func__,
		mmap_regions, ((ac->session << 8) | dir));

	mmap_regions->hdr.opcode = ASM_CMD_SHARED_MEM_MAP_REGIONS;
	mmap_regions->mem_pool_id = ADSP_MEMORY_MAP_SHMEM8_4K_POOL;
	mmap_regions->num_regions = bufcnt_t; /*bufcnt & 0x00ff; */
	mmap_regions->property_flag = 0x00;
	pr_debug("%s: map_regions->nregions = %d\n", __func__,
		mmap_regions->num_regions);
	payload = ((u8 *) mmap_region_cmd +
		sizeof(struct avs_cmd_shared_mem_map_regions));
	mregions = (struct avs_shared_map_region_payload *)payload;

	ac->port[dir].tmp_hdl = 0;
	port = &ac->port[dir];
	for (i = 0; i < bufcnt_t; i++) {
		ab = &port->buf[i];
		mregions->shm_addr_lsw = lower_32_bits(ab->phys);
		mregions->shm_addr_msw =
				msm_audio_populate_upper_32_bits(ab->phys);
		mregions->mem_size_bytes = bufsz_t;
		++mregions;
	}

	rc = apr_send_pkt(ac->mmap_apr, (uint32_t *) mmap_region_cmd);
	if (rc < 0) {
		pr_err("%s: mmap_regions op[0x%x]rc[%d]\n", __func__,
					mmap_regions->hdr.opcode, rc);
		rc = -EINVAL;
		kfree(buffer_node);
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->mem_wait,
			(atomic_read(&ac->mem_state) >= 0 &&
			 ac->port[dir].tmp_hdl),
			msecs_to_jiffies(TIMEOUT_MS));
	rc = 1;
	if (!rc) {
		pr_err("%s: timeout. waited for memory_map\n", __func__);
		rc = -ETIMEDOUT;
		kfree(buffer_node);
		goto fail_cmd;
	}
	if (atomic_read(&ac->mem_state) > 0) {
		pr_err("%s DSP returned error for memory_map [%s]\n",
			__func__, adsp_err_get_err_str(
			atomic_read(&ac->mem_state)));
		rc = adsp_err_get_lnx_err_code(
			atomic_read(&ac->mem_state));
		kfree(buffer_node);
		goto fail_cmd;
	}
	mutex_lock(&ac->cmd_lock);

	for (i = 0; i < bufcnt; i++) {
		ab = &port->buf[i];
		buffer_node[i].buf_phys_addr = ab->phys;
		buffer_node[i].mmap_hdl = ac->port[dir].tmp_hdl;
		list_add_tail(&buffer_node[i].list,
			&ac->port[dir].mem_map_handle);
		pr_debug("%s: i=%d, bufadd[i] = 0x%pK, maphdl[i] = 0x%x\n",
			__func__, i, &buffer_node[i].buf_phys_addr,
			buffer_node[i].mmap_hdl);
	}
	ac->port[dir].tmp_hdl = 0;
	mutex_unlock(&ac->cmd_lock);
	rc = 0;
fail_cmd:
	kfree(mmap_region_cmd);
	return rc;
}
EXPORT_SYMBOL(q6asm_memory_map_regions);

/**
 * q6asm_memory_unmap_regions -
 *       command to send memory unmap regions for ASM
 *
 * @ac: Audio client handle
 * @dir: RX or TX session
 *
 * Returns 0 on success or error on failure
 */
int q6asm_memory_unmap_regions(struct audio_client *ac, int dir)
{
	struct avs_cmd_shared_mem_unmap_regions mem_unmap;
	struct audio_port_data *port = NULL;
	struct asm_buffer_node *buf_node = NULL;
	struct list_head *ptr, *next;
	phys_addr_t buf_add;
	int	rc = 0;
	int	cmd_size = 0;

	if (!ac) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->mmap_apr == NULL) {
		pr_err("%s: mmap APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: Session[%d]\n", __func__, ac->session);

	cmd_size = sizeof(struct avs_cmd_shared_mem_unmap_regions);
	q6asm_add_mmaphdr(ac, &mem_unmap.hdr, cmd_size, dir);
	atomic_set(&ac->mem_state, -1);
	port = &ac->port[dir];
	buf_add = port->buf->phys;
	mem_unmap.hdr.opcode = ASM_CMD_SHARED_MEM_UNMAP_REGIONS;
	mem_unmap.mem_map_handle = 0;
	list_for_each_safe(ptr, next, &ac->port[dir].mem_map_handle) {
		buf_node = list_entry(ptr, struct asm_buffer_node,
						list);
		if (buf_node->buf_phys_addr == buf_add) {
			pr_debug("%s: Found the element\n", __func__);
			mem_unmap.mem_map_handle = buf_node->mmap_hdl;
			break;
		}
	}

	pr_debug("%s: mem_unmap-mem_map_handle: 0x%x\n",
			__func__, mem_unmap.mem_map_handle);

	if (mem_unmap.mem_map_handle == 0) {
		pr_err("%s: Do not send null mem handle to DSP\n", __func__);
		rc = 0;
		goto fail_cmd;
	}
	rc = apr_send_pkt(ac->mmap_apr, (uint32_t *) &mem_unmap);
	if (rc < 0) {
		pr_err("mmap_regions op[0x%x]rc[%d]\n",
				mem_unmap.hdr.opcode, rc);
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->mem_wait,
			(atomic_read(&ac->mem_state) >= 0),
			msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for memory_unmap of handle 0x%x\n",
			__func__, mem_unmap.mem_map_handle);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	} else if (atomic_read(&ac->mem_state) > 0) {
		pr_err("%s: DSP returned error[%s]\n",
				__func__, adsp_err_get_err_str(
				atomic_read(&ac->mem_state)));
		rc = adsp_err_get_lnx_err_code(
				atomic_read(&ac->mem_state));
		goto fail_cmd;
	} else if (atomic_read(&ac->unmap_cb_success) == 0) {
		pr_err("%s: Error in mem unmap callback of handle 0x%x\n",
			__func__, mem_unmap.mem_map_handle);
		rc = -EINVAL;
		goto fail_cmd;
	}
	rc = 0;

fail_cmd:
	list_for_each_safe(ptr, next, &ac->port[dir].mem_map_handle) {
		buf_node = list_entry(ptr, struct asm_buffer_node,
						list);
		if (buf_node->buf_phys_addr == buf_add) {
			list_del(&buf_node->list);
			kfree(buf_node);
			break;
		}
	}
	return rc;
}
EXPORT_SYMBOL(q6asm_memory_unmap_regions);

static int __q6asm_read(struct audio_client *ac, bool is_custom_len_reqd,
			int len)
{
	struct asm_data_cmd_read_v2 read;
	struct audio_buffer        *ab;
	int dsp_buf;
	struct audio_port_data     *port;
	int rc;
	
	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[OUT];

		q6asm_add_hdr(ac, &read.hdr, sizeof(read), FALSE);

		mutex_lock(&port->lock);

		dsp_buf = port->dsp_buf;
		if (port->buf == NULL) {
			pr_err("%s: buf is NULL\n", __func__);
			mutex_unlock(&port->lock);
			return -EINVAL;
		}
		ab = &port->buf[dsp_buf];

		read.hdr.opcode = ASM_DATA_CMD_READ_V2;
		read.buf_addr = lower_32_bits((uint64_t)ab->phys);
		read.buf_size = is_custom_len_reqd ? len : ab->size;
		q6asm_update_token(&read.hdr.token,
				   0, /* Session ID is NA */
				   0, /* Stream ID is NA */
				   port->dsp_buf,
				   0, /* Direction flag is NA */
				   NO_WAIT_CMD);
		port->dsp_buf = q6asm_get_next_buf(ac, port->dsp_buf,
						   port->max_buf_cnt);
		mutex_unlock(&port->lock);

        if(ac->data_rw_logprint_count < DATA_RW_LOG_PRINT_THRESHOLD){
            pr_info("%s: bufadd[0x%x]bufid[0x%x]bufsize[0x%x] token[0x%x]"
                , __func__,
                read.buf_addr,
                dsp_buf,
                read.buf_size,
                read.hdr.token);
            ac->data_rw_logprint_count++;
        } else {
            pr_debug("%s: bufadd[0x%x]bufid[0x%x]bufsize[0x%x] token[0x%x]"
                , __func__,
                read.buf_addr,
                dsp_buf,
                read.buf_size,
                read.hdr.token);
        }
		rc = apr_send_pkt(ac->apr, (uint32_t *) &read);
		if (rc < 0) {
			pr_err("%s: read op[0x%x]rc[%d]\n",
					__func__, read.hdr.opcode, rc);
			goto fail_cmd;
		}
		return 0;
	}
fail_cmd:
	return -EINVAL;
}

/**
 * q6asm_read -
 *       command to read buffer data from DSP
 *
 * @ac: Audio client handle
 *
 * Returns 0 on success or error on failure
 */
int q6asm_read(struct audio_client *ac)
{
	return __q6asm_read(ac, false/*is_custom_len_reqd*/, 0);
}
EXPORT_SYMBOL(q6asm_read);

static int __q6asm_vt_read(struct audio_client *ac, bool is_custom_len_reqd,
			int len)
{
	struct asm_data_cmd_read_v2 read;
	struct audio_buffer        *ab;
	int dsp_buf;
	struct audio_port_data     *port;
	int rc;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[OUT];

		q6asm_add_hdr(ac, &read.hdr, sizeof(read), FALSE);

		mutex_lock(&port->lock);

		dsp_buf = port->dsp_buf;
		if (port->buf == NULL) {
			pr_err("%s: buf is NULL\n", __func__);
			mutex_unlock(&port->lock);
			return -EINVAL;
		}
		ab = &port->buf[dsp_buf];

		read.hdr.opcode = ASM_VT_READ;
		read.buf_addr = lower_32_bits((uint64_t)ab->phys);
		read.buf_size = is_custom_len_reqd ? len : ab->size;
		q6asm_update_token(&read.hdr.token,
				   0, /* Session ID is NA */
				   0, /* Stream ID is NA */
				   port->dsp_buf,
				   0, /* Direction flag is NA */
				   NO_WAIT_CMD);
		port->dsp_buf = q6asm_get_next_buf(ac, port->dsp_buf,
						   port->max_buf_cnt);
		mutex_unlock(&port->lock);

		pr_debug("%s: bufadd[0x%x]bufid[0x%x]bufsize[0x%x] token[0x%x]"
			, __func__,
			read.buf_addr,
			dsp_buf,
			read.buf_size,
			read.hdr.token);
				
		rc = apr_send_pkt(ac->apr, (uint32_t *) &read);
		if (rc < 0) {
			pr_err("%s: read op[0x%x]rc[%d]\n",
					__func__, read.hdr.opcode, rc);
			goto fail_cmd;
		}
		return 0;
	}
fail_cmd:
	return -EINVAL;
}

/**
 * q6asm_read -
 *       command to read buffer data from DSP
 *
 * @ac: Audio client handle
 *
 * Returns 0 on success or error on failure
 */
int q6asm_vt_read(struct audio_client *ac)
{
	return __q6asm_vt_read(ac, false/*is_custom_len_reqd*/, 0);
}
EXPORT_SYMBOL(q6asm_vt_read);

/**
 * q6asm_read -
 *       command to read buffer data from DSP
 *
 * @ac: Audio client handle
 *
 * Returns 0 on success or error on failure
 */
int q6asm_vt_read_nolock(struct audio_client *ac)
{
	struct asm_data_cmd_read_v2 read;
	struct audio_buffer 	   *ab;
	int dsp_buf;
	struct audio_port_data	   *port;
	int rc;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[OUT];
	
		q6asm_add_hdr_async(ac, &read.hdr, sizeof(read), FALSE);
	
		dsp_buf = port->dsp_buf;
		if (port->buf == NULL) {
			pr_err("%s: buf is NULL\n", __func__);
			mutex_unlock(&port->lock);
			return -EINVAL;
		}
		ab = &port->buf[dsp_buf];

		read.hdr.opcode = ASM_VT_READ;
		read.buf_addr = lower_32_bits((uint64_t)ab->phys);
		read.buf_size = ab->size;
		q6asm_update_token(&read.hdr.token,
				   0, /* Session ID is NA */
				   0, /* Stream ID is NA */
				   port->dsp_buf,
				   0, /* Direction flag is NA */
				   NO_WAIT_CMD);
		port->dsp_buf = q6asm_get_next_buf(ac, port->dsp_buf,
						   port->max_buf_cnt);
		
		pr_debug("%s: bufadd[0x%x]bufid[0x%x]bufsize[0x%x] token[0x%x]"
			, __func__,
			read.buf_addr,
			dsp_buf,
			read.buf_size,
			read.hdr.token);

		rc = apr_send_pkt(ac->apr, (uint32_t *) &read);
		if (rc < 0) {
			pr_err("%s: read op[0x%x]rc[%d]\n",
					__func__, read.hdr.opcode, rc);
			goto fail_cmd;
		}
		return 0;
	}
fail_cmd:
	return -EINVAL;

}
EXPORT_SYMBOL(q6asm_vt_read_nolock);


static int __q6asm_vt_open(struct audio_client *ac,
			uint16_t bits_per_sample,uint32_t rate, uint32_t channels,
			uint32_t vt_buf,uint32_t buf_size)
{
	int rc = 0x00;
	struct asm_vt_open vt_open;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	pr_info("%s:sample_rate %d bit_width %d channel_num %d \n", __func__,rate,bits_per_sample,channels);
	pr_info("%s: vt_buf 0x%x buf_size %d\n", __func__,vt_buf,buf_size);

	q6asm_add_hdr(ac, &vt_open.hdr, sizeof(vt_open), TRUE);
	atomic_set(&ac->cmd_state, -1);
	vt_open.hdr.opcode = ASM_VT_OPEN;
	vt_open.buf_addr = vt_buf;
	vt_open.buf_size = buf_size;
	vt_open.cfg_param.channel_num = channels;
	vt_open.cfg_param.bit_width = bits_per_sample;
	vt_open.cfg_param.sample_rate = rate;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &vt_open);
	if (rc < 0) {
		pr_err("%s: open failed op[0x%x]rc[%d]\n",
				__func__, vt_open.hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0), msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for open read\n",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n",
				__func__);
		rc = -EINVAL;
		goto fail_cmd;
	}

	ac->io_mode |= TUN_READ_IO_MODE;

	return 0;
fail_cmd:
	return rc;
}

int q6asm_vt_open(struct audio_client *ac,
			uint16_t bits_per_sample,uint32_t rate, uint32_t channels,
			uint32_t vt_buf,uint32_t buf_size)
{
	int rc = 0;

	rc = __q6asm_vt_open(ac, bits_per_sample,rate,
		                 channels,vt_buf,buf_size);
	return rc;
}
EXPORT_SYMBOL(q6asm_vt_open);

static int __q6asm_vt_close(struct audio_client *ac)
{
	int rc = 0x00;
	struct asm_vt_close vt_close;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	pr_info("%s: \n", __func__);

	q6asm_add_hdr(ac, &vt_close.hdr, sizeof(vt_close), TRUE);
	atomic_set(&ac->cmd_state, -1);
	vt_close.hdr.opcode = ASM_VT_CLOSE;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &vt_close);
	if (rc < 0) {
		pr_err("%s: open failed op[0x%x]rc[%d]\n",
				__func__, vt_close.hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0), msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for vt close\n",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n",
				__func__);
		rc = -EINVAL;
		goto fail_cmd;
	}

	ac->io_mode |= TUN_READ_IO_MODE;

	return 0;
fail_cmd:
	return rc;
}

int q6asm_vt_close(struct audio_client *ac)
{
	int rc = 0;

	rc = __q6asm_vt_close(ac);
	return rc;
}
EXPORT_SYMBOL(q6asm_vt_close);

int q6asm_vt_set_mode(struct audio_client *ac,uint32_t vt_mode)
{
	int rc = 0x00;
	struct asm_vt_set_mode vt_set_mode;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_info("%s: vt_mode %d\n", __func__,vt_mode);

	q6asm_add_hdr(ac, &vt_set_mode.hdr, sizeof(vt_set_mode), TRUE);
	atomic_set(&ac->cmd_state, -1);
	vt_set_mode.hdr.opcode = ASM_VT_SET_MODE;
	vt_set_mode.vt_mode = vt_mode;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &vt_set_mode);
	if (rc < 0) {
		pr_err("%s: open failed op[0x%x]rc[%d]\n",
				__func__, vt_set_mode.hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0), msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for set mode\n",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n",
				__func__);
		rc = -EINVAL;
		goto fail_cmd;
	}

	ac->io_mode |= TUN_READ_IO_MODE;

	return 0;
fail_cmd:
	return rc;
}
EXPORT_SYMBOL(q6asm_vt_set_mode);

int q6asm_vt_run(struct audio_client *ac)
{
	int rc = 0x00;
	struct asm_vt_run run;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: session[%d]\n", __func__, ac->session);

	q6asm_add_hdr(ac, &run.hdr, sizeof(run), TRUE);
	atomic_set(&ac->cmd_state, -1);
	run.hdr.opcode = ASM_VT_RUN;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &run);
	if (rc < 0) {
		pr_err("%s: ASM_VT_RUN failed rc[%d]\n",__func__, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0), msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for run\n",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n",
				__func__);
		rc = -EINVAL;
		goto fail_cmd;
	}

	return 0;
fail_cmd:
	return rc;
}
EXPORT_SYMBOL(q6asm_vt_run);

int q6asm_vt_pause(struct audio_client *ac)
{
	int rc = 0x00;
	struct asm_vt_pause pause;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: session[%d]\n", __func__, ac->session);

	q6asm_add_hdr(ac, &pause.hdr, sizeof(pause), TRUE);
	atomic_set(&ac->cmd_state, -1);
	pause.hdr.opcode = ASM_VT_PAUSE;

	rc = apr_send_pkt(ac->apr, (uint32_t *) &pause);
	if (rc < 0) {
		pr_err("%s: ASM_VT_PAUSE failed rc[%d]\n",__func__, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}

	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0), msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for open read\n",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n",
				__func__);
		rc = -EINVAL;
		goto fail_cmd;
	}

	return 0;
fail_cmd:
	return rc;
}
EXPORT_SYMBOL(q6asm_vt_pause);

static int __q6asm_vt_hw_config(struct audio_client *ac, uint8_t flag,
				uint8_t port, uint32_t rate, uint32_t channels,
				uint16_t bits_per_sample)
{
	int rc = 0x00;
	struct asm_vt_hw_config vt_hw_cfg;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	pr_info("%s: flag[%d], port[%d], bitwidth[%d], channel_num[%d], sample_rate[%d],\n",
		__func__, flag, port, bits_per_sample, channels, bits_per_sample);

	q6asm_add_hdr(ac, &vt_hw_cfg.hdr, sizeof(vt_hw_cfg), TRUE);
	atomic_set(&ac->cmd_state, -1);
	vt_hw_cfg.hdr.opcode = ASM_VT_HW_CONFIG;

	vt_hw_cfg.flags = flag;
	vt_hw_cfg.port = port;
	if (flag == OPEN_VOICE_WAKEUP_PORT || flag == OPEN_VOICE_RECOGNITION_PORT) {
		vt_hw_cfg.bit_width = bits_per_sample;
		vt_hw_cfg.channel_num = channels;
		vt_hw_cfg.sample_rate = bits_per_sample;
	}

	rc = apr_send_pkt(ac->apr, (uint32_t *) &vt_hw_cfg);
	if (rc < 0) {
		pr_err("%s: open failed op[0x%x]rc[%d]\n",
				__func__, vt_hw_cfg.hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}
	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0), msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for open read\n",
				__func__);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(&ac->cmd_state) != 0) {
		pr_err("%s: DSP returned error.\n",
				__func__);
		rc = -EINVAL;
		goto fail_cmd;
	}

	ac->io_mode |= TUN_READ_IO_MODE;

	return 0;
fail_cmd:
	return rc;
}

int q6asm_vt_hw_config(struct audio_client *ac, uint8_t flag,
			uint8_t port, uint32_t rate, uint32_t channels,
			uint16_t bits_per_sample)
{
	return __q6asm_vt_hw_config(ac, flag, port,
		rate, channels, bits_per_sample);
}
EXPORT_SYMBOL(q6asm_vt_hw_config);

/**
 * q6asm_read_nolock -
 *       command to read buffer data from DSP
 *       with no wait for ack.
 *
 * @ac: Audio client handle
 *
 * Returns 0 on success or error on failure
 */
int q6asm_read_nolock(struct audio_client *ac)
{
	struct asm_data_cmd_read_v2 read;
	struct audio_buffer        *ab;
	int dsp_buf;
	struct audio_port_data     *port;
	int rc;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[OUT];

		q6asm_add_hdr_async(ac, &read.hdr, sizeof(read), FALSE);

		dsp_buf = port->dsp_buf;
		ab = &port->buf[dsp_buf];

		read.hdr.opcode = ASM_DATA_CMD_READ_V2;
		read.buf_addr = lower_32_bits((uint64_t)ab->phys);
		read.buf_size = ab->size;
		q6asm_update_token(&read.hdr.token,
				   0, /* Session ID is NA */
				   0, /* Stream ID is NA */
				   port->dsp_buf,
				   0, /* Direction flag is NA */
				   NO_WAIT_CMD);
		port->dsp_buf = q6asm_get_next_buf(ac, port->dsp_buf,
						   port->max_buf_cnt);

        if(ac->data_rw_logprint_count < DATA_RW_LOG_PRINT_THRESHOLD){
            pr_info("%s: bufadd[0x%x]bufid[0x%x]bufsize[0x%x] token[0x%x]"
                , __func__,
                read.buf_addr,
                dsp_buf,
                read.buf_size,
                read.hdr.token);
            ac->data_rw_logprint_count++;
        } else {
            pr_debug("%s: bufadd[0x%x]bufid[0x%x]bufsize[0x%x] token[0x%x]"
                , __func__,
                read.buf_addr,
                dsp_buf,
                read.buf_size,
                read.hdr.token);
        }

		rc = apr_send_pkt(ac->apr, (uint32_t *) &read);
		if (rc < 0) {
			pr_err("%s: read op[0x%x]rc[%d]\n",
					__func__, read.hdr.opcode, rc);
			goto fail_cmd;
		}
		return 0;
	}
fail_cmd:
	return -EINVAL;
}
EXPORT_SYMBOL(q6asm_read_nolock);

/**
 * q6asm_write -
 *       command to write buffer data to DSP
 *
 * @ac: Audio client handle
 * @len: buffer size
 * @msw_ts: upper 32bits of timestamp
 * @lsw_ts: lower 32bits of timestamp
 * @flags: Flags for timestamp mode
 *
 * Returns 0 on success or error on failure
 */
int q6asm_write(struct audio_client *ac, uint32_t len, uint32_t msw_ts,
		uint32_t lsw_ts, uint32_t flags)
{
	int rc = 0;
	struct asm_data_cmd_write_v2 write;
	struct audio_port_data *port;
	struct audio_buffer    *ab;
	int dsp_buf = 0;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	pr_debug("%s: session[%d] len=%d\n",
			__func__, ac->session, len);
	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[IN];

		q6asm_add_hdr(ac, &write.hdr, sizeof(write),
				FALSE);
		mutex_lock(&port->lock);

		dsp_buf = port->dsp_buf;
		ab = &port->buf[dsp_buf];

		q6asm_update_token(&write.hdr.token,
				   0, /* Session ID is NA */
				   0, /* Stream ID is NA */
				   port->dsp_buf,
				   0, /* Direction flag is NA */
				   NO_WAIT_CMD);
		write.hdr.opcode = ASM_DATA_CMD_WRITE_V2;
		write.buf_addr = lower_32_bits((uint64_t)ab->phys);
		write.buf_size = len;
		port->dsp_buf = q6asm_get_next_buf(ac, port->dsp_buf,
						   port->max_buf_cnt);
        if(ac->data_rw_logprint_count < DATA_RW_LOG_PRINT_THRESHOLD){
            pr_info("%s: bufadd[0x%x]dsp_buf[0x%x]buf_size[0x%x] token[0x%x]"
                , __func__,
                write.buf_addr,
                dsp_buf,
                write.buf_size,
                write.hdr.token);
            ac->data_rw_logprint_count++;
        } else {
            pr_debug("%s: bufadd[0x%x]dsp_buf[0x%x]buf_size[0x%x] token[0x%x]"
                , __func__,
                write.buf_addr,
                dsp_buf,
                write.buf_size,
                write.hdr.token);
        }
		mutex_unlock(&port->lock);

		rc = apr_send_pkt(ac->apr, (uint32_t *) &write);
		if (rc < 0) {
			pr_err("%s: write op[0x%x]rc[%d]\n",
					__func__, write.hdr.opcode, rc);
			goto fail_cmd;
		}
		return 0;
	}
fail_cmd:
	return -EINVAL;
}
EXPORT_SYMBOL(q6asm_write);

/**
 * q6asm_write_nolock -
 *       command to write buffer data to DSP
 *       with no wait for ack.
 *
 * @ac: Audio client handle
 * @len: buffer size
 * @msw_ts: upper 32bits of timestamp
 * @lsw_ts: lower 32bits of timestamp
 * @flags: Flags for timestamp mode
 *
 * Returns 0 on success or error on failure
 */
int q6asm_write_nolock(struct audio_client *ac, uint32_t len, uint32_t msw_ts,
			uint32_t lsw_ts, uint32_t flags)
{
	int rc = 0;
	struct asm_data_cmd_write_v2 write;
	struct audio_port_data *port;
	struct audio_buffer    *ab;
	int dsp_buf = 0;

	if (ac == NULL) {
		pr_err("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}

	pr_debug("%s: session[%d] len=%d\n",
			__func__, ac->session, len);
	if (ac->io_mode & SYNC_IO_MODE) {
		port = &ac->port[IN];

		q6asm_add_hdr_async(ac, &write.hdr, sizeof(write),
				FALSE);

		dsp_buf = port->dsp_buf;
		ab = &port->buf[dsp_buf];

		q6asm_update_token(&write.hdr.token,
				   0, /* Session ID is NA */
				   0, /* Stream ID is NA */
				   port->dsp_buf,
				   0, /* Direction flag is NA */
				   NO_WAIT_CMD);

		write.hdr.opcode = ASM_DATA_CMD_WRITE_V2;
		write.buf_addr = lower_32_bits((uint64_t)ab->phys);
		write.buf_size = len;
		port->dsp_buf = q6asm_get_next_buf(ac, port->dsp_buf,
						   port->max_buf_cnt);

        if(ac->data_rw_logprint_count < DATA_RW_LOG_PRINT_THRESHOLD){
            pr_info("%s: bufadd[0x%x]dsp_buf[0x%x]buf_size[0x%x] token[0x%x]"
                , __func__,
                write.buf_addr,
                dsp_buf,
                write.buf_size,
                write.hdr.token);
            ac->data_rw_logprint_count++;
        } else {
            pr_debug("%s: bufadd[0x%x]dsp_buf[0x%x]buf_size[0x%x] token[0x%x]"
                , __func__,
                write.buf_addr,
                dsp_buf,
                write.buf_size,
                write.hdr.token);
       }

		rc = apr_send_pkt(ac->apr, (uint32_t *) &write);
		if (rc < 0) {
			pr_err("%s: write op[0x%x]rc[%d]\n",
					__func__, write.hdr.opcode, rc);
			goto fail_cmd;
		}
		return 0;
	}
fail_cmd:
	return -EINVAL;
}
EXPORT_SYMBOL(q6asm_write_nolock);

static int __q6asm_cmd(struct audio_client *ac, int cmd)
{
	struct apr_hdr hdr;
	int rc;
	atomic_t *state;
	int cnt = 0;

	if (!ac) {
		pr_err_ratelimited("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err_ratelimited("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	q6asm_stream_add_hdr(ac, &hdr, sizeof(hdr), TRUE);
	atomic_set(&ac->cmd_state, -1);
	/*
	 * Updated the token field with stream/session for compressed playback
	 * Platform driver must know the the stream with which the command is
	 * associated
	 */
	if (ac->io_mode & COMPRESSED_STREAM_IO)
		q6asm_update_token(&hdr.token,
				   ac->session,
				   ac->stream_id,
				   0, /* Buffer index is NA */
				   0, /* Direction flag is NA */
				   WAIT_CMD);
	pr_debug("%s: token = 0x%x, stream_id  %d, session 0x%x\n",
			__func__, hdr.token, ac->stream_id, ac->session);
	switch (cmd) {
	case CMD_PAUSE:
		pr_info("%s: CMD_PAUSE\n", __func__);
		hdr.opcode = ASM_SESSION_CMD_PAUSE;
		state = &ac->cmd_state;
		break;
	case CMD_SUSPEND:
		pr_info("%s: CMD_SUSPEND\n", __func__);
		hdr.opcode = ASM_SESSION_CMD_SUSPEND;
		state = &ac->cmd_state;
		break;
	case CMD_FLUSH:
		pr_info("%s: CMD_FLUSH\n", __func__);
		hdr.opcode = ASM_STREAM_CMD_FLUSH;
		state = &ac->cmd_state;
		break;
	case CMD_OUT_FLUSH:
		pr_info("%s: CMD_OUT_FLUSH\n", __func__);
		hdr.opcode = ASM_STREAM_CMD_FLUSH_READBUFS;
		state = &ac->cmd_state;
		break;
	case CMD_EOS:
		pr_info("%s: CMD_EOS\n", __func__);
		hdr.opcode = ASM_DATA_CMD_EOS;
		atomic_set(&ac->cmd_state, 0);
		state = &ac->cmd_state;
		break;
	case CMD_CLOSE:
		pr_info("%s: CMD_CLOSE\n", __func__);
		hdr.opcode = ASM_STREAM_CMD_CLOSE;
		state = &ac->cmd_state;
		break;
	default:
		pr_err("%s: Invalid format[%d]\n", __func__, cmd);
		rc = -EINVAL;
		goto fail_cmd;
	}
	pr_debug("%s: session[%d]opcode[0x%x]\n", __func__,
			ac->session,
			hdr.opcode);
	rc = apr_send_pkt(ac->apr, (uint32_t *) &hdr);
	if (rc < 0) {
		pr_err("%s: Commmand 0x%x failed %d\n",
				__func__, hdr.opcode, rc);
		rc = -EINVAL;
		goto fail_cmd;
	}
	rc = wait_event_timeout(ac->cmd_wait, (atomic_read(state) >= 0),
				msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for response opcode[0x%x]\n",
				__func__, hdr.opcode);
		rc = -ETIMEDOUT;
		goto fail_cmd;
	}
	if (atomic_read(state) > 0) {
		pr_err("%s: DSP returned error[%s] opcode %d\n",
					__func__, adsp_err_get_err_str(
					atomic_read(state)),
					hdr.opcode);
		rc = adsp_err_get_lnx_err_code(atomic_read(state));
		goto fail_cmd;
	}

	if (cmd == CMD_FLUSH)
		q6asm_reset_buf_state(ac);
	if (cmd == CMD_CLOSE) {
		/* check if DSP return all buffers */
		if (ac->port[IN].buf) {
			for (cnt = 0; cnt < ac->port[IN].max_buf_cnt;
					cnt++) {
				if (ac->port[IN].buf[cnt].used == IN) {
					dev_vdbg(ac->dev, "Write Buf[%d] not returned\n",
							cnt);
				}
			}
		}
		if (ac->port[OUT].buf) {
			for (cnt = 0; cnt < ac->port[OUT].max_buf_cnt; cnt++) {
				if (ac->port[OUT].buf[cnt].used == OUT) {
					dev_vdbg(ac->dev, "Read Buf[%d] not returned\n",
							cnt);
				}
			}
		}
	}
	return 0;
fail_cmd:
	return rc;
}

/**
 * q6asm_cmd -
 *       Function used to send commands for
 *       ASM with wait for ack.
 *
 * @ac: Audio client handle
 * @cmd: command to send
 *
 * Returns 0 on success or error on failure
 */
int q6asm_cmd(struct audio_client *ac, int cmd)
{
	return __q6asm_cmd(ac, cmd);
}
EXPORT_SYMBOL(q6asm_cmd);

/**
 * q6asm_cmd_nowait -
 *       Function used to send commands for
 *       ASM stream without wait for ack.
 *
 * @ac: Audio client handle
 * @cmd: command to send
 * @stream_id: Stream ID
 *
 * Returns 0 on success or error on failure
 */
static int __q6asm_cmd_nowait(struct audio_client *ac, int cmd)
{
	struct apr_hdr hdr;
	int rc;

	if (!ac) {
		pr_err_ratelimited("%s: APR handle NULL\n", __func__);
		return -EINVAL;
	}
	if (ac->apr == NULL) {
		pr_err_ratelimited("%s: AC APR handle NULL\n", __func__);
		return -EINVAL;
	}
	q6asm_stream_add_hdr_async(ac, &hdr, sizeof(hdr), TRUE);
	atomic_set(&ac->cmd_state, 1);
	/*
	 * Updated the token field with stream/session for compressed playback
	 * Platform driver must know the the stream with which the command is
	 * associated
	 */
	if (ac->io_mode & COMPRESSED_STREAM_IO)
		q6asm_update_token(&hdr.token,
				   ac->session,
				   ac->stream_id,
				   0, /* Buffer index is NA */
				   0, /* Direction flag is NA */
				   NO_WAIT_CMD);

	pr_debug("%s: token = 0x%x, stream_id  %d, session 0x%x\n",
			__func__, hdr.token, ac->stream_id, ac->session);
	switch (cmd) {
	case CMD_PAUSE:
		pr_debug("%s: CMD_PAUSE\n", __func__);
		hdr.opcode = ASM_SESSION_CMD_PAUSE;
		break;
	case CMD_EOS:
		pr_debug("%s: CMD_EOS\n", __func__);
		hdr.opcode = ASM_DATA_CMD_EOS;
		break;
	case CMD_CLOSE:
		pr_debug("%s: CMD_CLOSE\n", __func__);
		hdr.opcode = ASM_STREAM_CMD_CLOSE;
		break;
	default:
		pr_err("%s: Invalid format[%d]\n", __func__, cmd);
		goto fail_cmd;
	}
	pr_debug("%s: session[%d]opcode[0x%x]\n", __func__,
			ac->session,
			hdr.opcode);

	rc = apr_send_pkt(ac->apr, (uint32_t *) &hdr);
	if (rc < 0) {
		pr_err("%s: Commmand 0x%x failed %d\n",
				__func__, hdr.opcode, rc);
		goto fail_cmd;
	}
	return 0;
fail_cmd:
	return -EINVAL;
}

int q6asm_cmd_nowait(struct audio_client *ac, int cmd)
{
	return __q6asm_cmd_nowait(ac, cmd);
}
EXPORT_SYMBOL(q6asm_cmd_nowait);

static void q6asm_reset_buf_state(struct audio_client *ac)
{
	int cnt = 0;
	int loopcnt = 0;
	int used;
	struct audio_port_data *port = NULL;

	if (ac->io_mode & SYNC_IO_MODE) {
		used = (ac->io_mode & TUN_WRITE_IO_MODE ? 1 : 0);
		mutex_lock(&ac->cmd_lock);
		for (loopcnt = 0; loopcnt <= OUT; loopcnt++) {
			port = &ac->port[loopcnt];
			cnt = port->max_buf_cnt - 1;
			port->dsp_buf = 0;
			port->cpu_buf = 0;
			while (cnt >= 0) {
				if (!port->buf)
					continue;
				port->buf[cnt].used = used;
				cnt--;
			}
		}
		mutex_unlock(&ac->cmd_lock);
	}
}

/*
 * q6asm_get_path_delay() - get the path delay for an audio session
 * @ac: audio client handle
 *
 * Retrieves the current audio DSP path delay for the given audio session.
 *
 * Return: 0 on success, error code otherwise
 */
int q6asm_get_path_delay(struct audio_client *ac)
{
	int rc = 0;
	struct apr_hdr hdr;

	if (!ac || ac->apr == NULL) {
		pr_err("%s: invalid audio client\n", __func__);
		return -EINVAL;
	}

	hdr.opcode = ASM_SESSION_CMD_GET_PATH_DELAY_V2;
	q6asm_add_hdr(ac, &hdr, sizeof(hdr), TRUE);
	atomic_set(&ac->cmd_state, -1);

	rc = apr_send_pkt(ac->apr, (uint32_t *) &hdr);
	if (rc < 0) {
		pr_err("%s: Commmand 0x%x failed %d\n", __func__,
				hdr.opcode, rc);
		return rc;
	}

	rc = wait_event_timeout(ac->cmd_wait,
			(atomic_read(&ac->cmd_state) >= 0),
			msecs_to_jiffies(TIMEOUT_MS));
	if (!rc) {
		pr_err("%s: timeout. waited for response opcode[0x%x]\n",
				__func__, hdr.opcode);
		return -ETIMEDOUT;
	}

	if (atomic_read(&ac->cmd_state) > 0) {
		pr_err("%s: DSP returned error[%s]\n",
				__func__, adsp_err_get_err_str(
				atomic_read(&ac->cmd_state)));
		rc = adsp_err_get_lnx_err_code(
				atomic_read(&ac->cmd_state));
		return rc;
	}

	return 0;
}
EXPORT_SYMBOL(q6asm_get_path_delay);

int q6asm_get_apr_service_id(int session_id)
{
	int service_id;

	pr_debug("%s:\n", __func__);

	if (session_id <= 0 || session_id > ASM_ACTIVE_STREAMS_ALLOWED) {
		pr_err("%s: invalid session_id = %d\n", __func__, session_id);
		return -EINVAL;
	}
	mutex_lock(&session[session_id].mutex_lock_per_session);
	if (session[session_id].ac != NULL)
		if ((session[session_id].ac)->apr != NULL) {
			service_id = ((struct apr_svc *)(session[session_id].ac)->apr)->id;
			mutex_unlock(&session[session_id].mutex_lock_per_session);
			return service_id;
	}
	mutex_unlock(&session[session_id].mutex_lock_per_session);
	return -EINVAL;
}

uint8_t q6asm_get_asm_stream_id(int session_id)
{
	uint8_t stream_id = 1;
	pr_debug("%s:\n", __func__);

	if (session_id <= 0 || session_id > ASM_ACTIVE_STREAMS_ALLOWED) {
		pr_err("%s: invalid session_id = %d\n", __func__, session_id);
		goto done;
	}
	if (session[session_id].ac == NULL) {
		pr_err("%s: session not created for session id = %d\n",
		       __func__, session_id);
		goto done;
	}
	stream_id = (session[session_id].ac)->stream_id;

done:
	return stream_id;
}

int q6asm_get_asm_topology(int session_id)
{
	int topology = -EINVAL;

	if (session_id <= 0 || session_id > ASM_ACTIVE_STREAMS_ALLOWED) {
		pr_err("%s: invalid session_id = %d\n", __func__, session_id);
		goto done;
	}
	if (session[session_id].ac == NULL) {
		pr_err("%s: session not created for session id = %d\n",
		       __func__, session_id);
		goto done;
	}
	topology = (session[session_id].ac)->topology;
done:
	return topology;
}

int q6asm_get_asm_app_type(int session_id)
{
	int app_type = -EINVAL;

	if (session_id <= 0 || session_id > ASM_ACTIVE_STREAMS_ALLOWED) {
		pr_err("%s: invalid session_id = %d\n", __func__, session_id);
		goto done;
	}
	if (session[session_id].ac == NULL) {
		pr_err("%s: session not created for session id = %d\n",
		       __func__, session_id);
		goto done;
	}
	app_type = (session[session_id].ac)->app_type;
done:
	return app_type;
}

static int q6asm_is_valid_session(struct apr_client_data *data, void *priv)
{
	struct audio_client *ac = (struct audio_client *)priv;
	union asm_token_struct asm_token;

	asm_token.token = data->token;
	if (asm_token._token.session_id != ac->session) {
		pr_err("%s: Invalid session[%d] rxed expected[%d]",
			__func__, asm_token._token.session_id, ac->session);
		return -EINVAL;
	}
	return 0;
}

int __init q6asm_init(void)
{
	int lcnt;

	pr_debug("%s:\n", __func__);

	memset(session, 0, sizeof(struct audio_session) *
		(ASM_ACTIVE_STREAMS_ALLOWED + 1));
	for (lcnt = 0; lcnt <= ASM_ACTIVE_STREAMS_ALLOWED; lcnt++) {
		spin_lock_init(&(session[lcnt].session_lock));
		mutex_init(&(session[lcnt].mutex_lock_per_session));
	}

	set_custom_topology = 1;

	/*setup common client used for cal mem map */
	common_client.session = ASM_CONTROL_SESSION;
	common_client.port[0].buf = &common_buf[0];
	common_client.port[1].buf = &common_buf[1];
	init_waitqueue_head(&common_client.cmd_wait);
	init_waitqueue_head(&common_client.time_wait);
	init_waitqueue_head(&common_client.mem_wait);
	atomic_set(&common_client.time_flag, 1);
	INIT_LIST_HEAD(&common_client.port[0].mem_map_handle);
	INIT_LIST_HEAD(&common_client.port[1].mem_map_handle);
	mutex_init(&common_client.cmd_lock);
	for (lcnt = 0; lcnt <= OUT; lcnt++) {
		mutex_init(&common_client.port[lcnt].lock);
		spin_lock_init(&common_client.port[lcnt].dsp_lock);
	}
	atomic_set(&common_client.cmd_state, 0);
	atomic_set(&common_client.mem_state, 0);

	return 0;
}

void q6asm_exit(void)
{
}
