// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2014, 2016-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <soc/scm.h>
#include <soc/snd_event.h>
#include <dsp/apr_audio-v2.h>
#include <dsp/audio_notifier.h>
#include <ipc/apr.h>
#include <ipc/apr_tal.h>

#define APR_PKT_IPC_LOG_PAGE_CNT 2

static struct apr_q6 q6;
static struct apr_client client[APR_DEST_MAX][APR_CLIENT_MAX];
static wait_queue_head_t modem_wait;
static bool is_modem_up;
static char *subsys_name = NULL;
/* Subsystem restart: QDSP6 data, functions */
static struct workqueue_struct *apr_reset_workqueue;
//#define JR510_AP_TEST 1
#ifdef JR510_AP_TEST
#define USE_MSG_QUEUE 1
struct task_struct* adsp_thread;
int read_count = 0;
struct message_queue dsp_message_queue;
wait_queue_head_t dsp_message_wait;
struct file_info {
	struct file *file;
	loff_t pos;
};
struct file_info playback_file;
struct file_info record_file;
static struct work_struct vt_event_work;
void *vt_handle;

struct apr_packet_t {
	struct apr_hdr hdr;
	void *payload;
};

struct apr_default_rsp_packet_payload_t {
	uint32_t rspCmdOpcode;
	uint32_t status;
};

struct apr_default_rsp_packet_t {
	struct apr_hdr hdr;
	struct apr_default_rsp_packet_payload_t payload;
};

struct apr_write_done_packet_t {
	struct apr_hdr hdr;
	struct asm_data_event_write_done_v2 payload;
};

struct apr_read_done_packet_t {
	struct apr_hdr hdr;
	struct asm_data_event_read_done_v2 payload;
};

static struct apr_default_rsp_packet_t aprVtEventPacket;
static void vt_event_work_do(struct work_struct *_work)
{
	pr_debug("%s: enter\n", __func__);
	apr_cb_func(&aprVtEventPacket, aprVtEventPacket.hdr.pkt_size, vt_handle);
}

#endif
static void apr_reset_deregister(struct work_struct *work);
static void dispatch_event(unsigned long code, uint16_t proc);

struct apr_reset_work {
	void *handle;
	struct work_struct work;
};

struct apr_chld_device {
	struct platform_device *pdev;
	struct list_head node;
};

struct apr_private {
	struct device *dev;
	spinlock_t apr_lock;
	bool is_initial_boot;
	struct work_struct add_chld_dev_work;
};

static struct apr_private *apr_priv;

struct apr_svc_table {
	char name[64];
	int idx;
	int id;
	int client_id;
};

static const struct apr_svc_table svc_tbl_qdsp6[] = {
	{
		.name = "AFE",
		.idx = 0,
		.id = APR_SVC_AFE,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "ASM",
		.idx = 1,
		.id = APR_SVC_ASM,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "ADM",
		.idx = 2,
		.id = APR_SVC_ADM,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "CORE",
		.idx = 3,
		.id = APR_SVC_ADSP_CORE,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "TEST",
		.idx = 4,
		.id = APR_SVC_TEST_CLIENT,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "MVM",
		.idx = 5,
		.id = APR_SVC_ADSP_MVM,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "CVS",
		.idx = 6,
		.id = APR_SVC_ADSP_CVS,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "CVP",
		.idx = 7,
		.id = APR_SVC_ADSP_CVP,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "USM",
		.idx = 8,
		.id = APR_SVC_USM,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "VIDC",
		.idx = 9,
		.id = APR_SVC_VIDC,
	},
	{
		.name = "LSM",
		.idx = 10,
		.id = APR_SVC_LSM,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "TinyLog",
		.idx = 11,
		.id = APR_SVC_TINYLOG,
		.client_id = APR_CLIENT_AUDIO,
	},
	{
		.name = "PSY",
		.idx = 12,
		.id = APR_SVC_PSY,
		.client_id = APR_CLIENT_AUDIO,
	},

};

static struct apr_svc_table svc_tbl_voice[] = {
	{
		.name = "VSM",
		.idx = 0,
		.id = APR_SVC_VSM,
		.client_id = APR_CLIENT_VOICE,
	},
	{
		.name = "VPM",
		.idx = 1,
		.id = APR_SVC_VPM,
		.client_id = APR_CLIENT_VOICE,
	},
	{
		.name = "MVS",
		.idx = 2,
		.id = APR_SVC_MVS,
		.client_id = APR_CLIENT_VOICE,
	},
	{
		.name = "MVM",
		.idx = 3,
		.id = APR_SVC_MVM,
		.client_id = APR_CLIENT_VOICE,
	},
	{
		.name = "CVS",
		.idx = 4,
		.id = APR_SVC_CVS,
		.client_id = APR_CLIENT_VOICE,
	},
	{
		.name = "CVP",
		.idx = 5,
		.id = APR_SVC_CVP,
		.client_id = APR_CLIENT_VOICE,
	},
	{
		.name = "SRD",
		.idx = 6,
		.id = APR_SVC_SRD,
		.client_id = APR_CLIENT_VOICE,
	},
	{
		.name = "TEST",
		.idx = 7,
		.id = APR_SVC_TEST_CLIENT,
		.client_id = APR_CLIENT_VOICE,
	},
};

/**
 * apr_get_modem_state:
 *
 * Returns current modem load status
 *
 */
enum apr_subsys_state apr_get_modem_state(void)
{
	return atomic_read(&q6.modem_state);
}
EXPORT_SYMBOL(apr_get_modem_state);

/**
 * apr_set_modem_state - Update modem load status.
 *
 * @state: State to update modem load status
 *
 */
void apr_set_modem_state(enum apr_subsys_state state)
{
	atomic_set(&q6.modem_state, state);
}
EXPORT_SYMBOL(apr_set_modem_state);

enum apr_subsys_state apr_cmpxchg_modem_state(enum apr_subsys_state prev,
					      enum apr_subsys_state new)
{
	return atomic_cmpxchg(&q6.modem_state, prev, new);
}

static void apr_modem_down(unsigned long opcode)
{
	apr_set_modem_state(APR_SUBSYS_DOWN);
	dispatch_event(opcode, APR_DEST_MODEM);
}

static void apr_modem_up(void)
{
	if (apr_cmpxchg_modem_state(APR_SUBSYS_DOWN, APR_SUBSYS_UP) ==
							APR_SUBSYS_DOWN)
		wake_up(&modem_wait);
	is_modem_up = 1;
}

enum apr_subsys_state apr_get_q6_state(void)
{
	atomic_set(&q6.q6_state, APR_SUBSYS_LOADED);
	return atomic_read(&q6.q6_state);
}
EXPORT_SYMBOL(apr_get_q6_state);

int apr_set_q6_state(enum apr_subsys_state state)
{
	pr_debug("%s: setting adsp state %d\n", __func__, state);
	if (state < APR_SUBSYS_DOWN || state > APR_SUBSYS_LOADED)
		return -EINVAL;
	atomic_set(&q6.q6_state, state);
	return 0;
}
EXPORT_SYMBOL(apr_set_q6_state);

static void apr_ssr_disable(struct device *dev, void *data)
{
	apr_set_q6_state(APR_SUBSYS_DOWN);
}

static const struct snd_event_ops apr_ssr_ops = {
	.disable = apr_ssr_disable,
};

void jlq_apr_adsp_down(void)
{
    pr_info("%s: adsp is power down\n", __func__);
    apr_set_q6_state(APR_SUBSYS_DOWN);
    dispatch_event(AUDIO_NOTIFIER_SERVICE_DOWN, APR_DEST_ADSP);
}
EXPORT_SYMBOL_GPL(jlq_apr_adsp_down);

void jlq_apr_adsp_up(void)
{
    pr_info("%s: adsp is power up\n", __func__);
    apr_set_q6_state(APR_SUBSYS_LOADED);
}
EXPORT_SYMBOL_GPL(jlq_apr_adsp_up);

static void apr_adsp_down(unsigned long opcode)
{
	pr_info("%s: Q6 is Down\n", __func__);
	snd_event_notify(apr_priv->dev, SND_EVENT_DOWN);
	apr_set_q6_state(APR_SUBSYS_DOWN);
	dispatch_event(opcode, APR_DEST_ADSP);
}

static void apr_add_child_devices(struct work_struct *work)
{
	int ret;

	ret = of_platform_populate(apr_priv->dev->of_node,
			NULL, NULL, apr_priv->dev);
	if (ret)
		dev_err(apr_priv->dev, "%s: failed to add child nodes, ret=%d\n",
			__func__, ret);
}

static void apr_adsp_up(void)
{
	pr_err("%s: Q6 is Up\n", __func__);
	apr_set_q6_state(APR_SUBSYS_LOADED);

	spin_lock(&apr_priv->apr_lock);
	if (apr_priv->is_initial_boot)
		schedule_work(&apr_priv->add_chld_dev_work);
	spin_unlock(&apr_priv->apr_lock);
	snd_event_notify(apr_priv->dev, SND_EVENT_UP);
}

int apr_load_adsp_image(void)
{
	int rc = 0;

#if 0
	mutex_lock(&q6.lock);
	if (apr_get_q6_state() == APR_SUBSYS_UP) {
		q6.pil = subsystem_get("adsp");
		if (IS_ERR(q6.pil)) {
			rc = PTR_ERR(q6.pil);
			pr_err("APR: Unable to load q6 image, error:%d\n", rc);
		} else {
			apr_set_q6_state(APR_SUBSYS_LOADED);
			pr_debug("APR: Image is loaded, stated\n");
		}
	} else if (apr_get_q6_state() == APR_SUBSYS_LOADED) {
		pr_debug("APR: q6 image already loaded\n");
	} else {
		pr_debug("APR: cannot load state %d\n", apr_get_q6_state());
	}
	mutex_unlock(&q6.lock);
#endif
	return rc;
}

struct apr_client *apr_get_client(int dest_id, int client_id)
{
	return &client[dest_id][client_id];
}

/**
 * apr_send_pkt - Clients call to send packet
 * to destination processor.
 *
 * @handle: APR service handle
 * @buf: payload to send to destination processor.
 *
 * Returns Bytes(>0)pkt_size on success or error on failure.
 */
#ifndef JR510_AP_TEST
int apr_send_pkt(void *handle, uint32_t *buf)
{
	struct apr_svc *svc = handle;
	struct apr_client *clnt;
	struct apr_hdr *hdr;
	uint16_t dest_id;
	uint16_t client_id;
	uint16_t w_len;
	int rc;
	unsigned long flags;

	if (!handle || !buf) {
		pr_err("APR: Wrong parameters for %s\n",
				!handle ? "handle" : "buf");
		return -EINVAL;
	}
	if (svc->need_reset) {
		pr_err_ratelimited("apr: send_pkt service need reset\n");
		return -ENETRESET;
	}
	
	if ((svc->dest_id == APR_DEST_ADSP) &&
	    (apr_get_q6_state() != APR_SUBSYS_LOADED)) {
		pr_err_ratelimited("%s: Still dsp is not Up\n", __func__);
		return -ENETRESET;
	} else if ((svc->dest_id == APR_DEST_MODEM) &&
		   (apr_get_modem_state() == APR_SUBSYS_DOWN)) {
		pr_err("apr: Still Modem is not Up\n");
		return -ENETRESET;
	}
    
	spin_lock_irqsave(&svc->w_lock, flags);
	dest_id = svc->dest_id;
	client_id = svc->client_id;
	clnt = &client[dest_id][client_id];

	
	if (!client[dest_id][client_id].handle) {
		pr_err_ratelimited("APR: Still service is not yet opened\n");
		spin_unlock_irqrestore(&svc->w_lock, flags);
		return -EINVAL;
	}
	hdr = (struct apr_hdr *)buf;

	hdr->src_domain = APR_DOMAIN_APPS;
	hdr->src_svc = svc->id;
	hdr->dest_domain = svc->dest_domain;
	hdr->dest_svc = svc->id;

	pr_debug("%s:  pkt_size:%d\n",__func__,hdr->pkt_size);

	rc = apr_tal_write(clnt->handle, buf,
			hdr->pkt_size);
	
	if (rc >= 0) {
		w_len = rc;
		if (w_len != hdr->pkt_size) {
			pr_err("%s: Unable to write whole APR pkt successfully: %d\n",
			       __func__, rc);
			rc = -EINVAL;
		}
	} else {
		pr_err_ratelimited("%s: Write APR pkt failed with error %d\n",
			__func__, rc);
		if (rc == -ECONNRESET) {
			pr_err_ratelimited("%s: Received reset error from tal\n",
					__func__);
			rc = -ENETRESET;
		}
	}
	spin_unlock_irqrestore(&svc->w_lock, flags);

	return rc;
}
EXPORT_SYMBOL_GPL(apr_send_pkt);

#else
message_queue_enum message_queue_init(struct message_queue *queue, uint32_t queue_size, uint32_t message_size)
{
	uint32_t i;

	queue->head = 0;
	queue->tail = 0;
	atomic_set(&queue->number, 0);
	queue->size = queue_size;

	for (i = 0; i < queue_size; i++) {
		queue->message[i].len = 0;
		memset(queue->message[i].buf, 0, message_size);
	}
	return MESSAGE_QUEUE_OK;
}

message_queue_enum message_queue_enter(struct message_queue *queue, struct dsp_message *message)
{
	if (atomic_read(&queue->number) < queue->size) {
		memcpy(queue->message[queue->tail].buf, message->buf, message->len);
		queue->message[queue->tail].len = message->len;
		queue->tail++;
		if (queue->tail == queue->size) {
			queue->tail = 0;
		}
		atomic_inc(&queue->number);

		return MESSAGE_QUEUE_OK;
	}

	return MESSAGE_QUEUE_FULL;
}

message_queue_enum message_queue_delete(struct message_queue *queue)
{
	if (atomic_read(&queue->number) > 0) {
		memset(queue->message[queue->head].buf, 0, queue->size);
		queue->message[queue->head].len = 0;
		queue->head++;
		if (queue->head == queue->size) {
			queue->head = 0;
		}
		atomic_dec(&queue->number);

		return MESSAGE_QUEUE_OK;
	}

	return MESSAGE_QUEUE_EMPTY;
}

static bool is_message_queue_empty(struct message_queue *queue)
{
	return (atomic_read(&queue->number) > 0) ? 0 : 1;
}

#if USE_MSG_QUEUE
static bool is_message_queue_full(struct message_queue *queue)
{
	return (atomic_read(&queue->number) < queue->size) ? 0 : 1;
}
#endif

int apr_adsp_cb_process(void *data) {
	struct dsp_message *message;
	struct message_queue *queue = &dsp_message_queue;
	struct apr_hdr *hdr;
	//struct apr_write_done_packet_t *aprWriteDonePacket;
	//struct apr_read_done_packet_t *aprReadDonePacket;
	//int ret = 0;
	//char *wrtieBuf;
	//char *readBuf;

	for (;;) {
		wait_event_interruptible(dsp_message_wait, atomic_read(&queue->number));
		udelay(10000);
		if (is_message_queue_empty(queue)) {
			pr_err("%s: message queue is empty.\n", __func__);
			continue;
		}

		message = &queue->message[queue->head];
		hdr = (struct apr_hdr *)message->buf;
		pr_debug("%s: opcode 0x%x\n", __func__,hdr->opcode);
#if 0
		if(hdr->opcode == ASM_DATA_EVENT_WRITE_DONE_V2) {
			aprWriteDonePacket = (struct apr_write_done_packet_t *)message->buf;
			wrtieBuf = (char *)((uint64_t)aprWriteDonePacket->payload.buf_addr << 32) |
						aprWriteDonePacket->payload.buf_addr);
			if (playback_file.file) {
				ret = kernel_write(playback_file.file,
						(void *)wrtieBuf, aprWriteDonePacket->payload.status,
						&playback_file.pos);
			}
			aprWriteDonePacket->payload.status = 0;
		}else if (hdr->opcode == ASM_DATA_EVENT_READ_DONE_V2 ||
			hdr->opcode == ASM_VT_READ_DONE) {

			aprReadDonePacket = (struct apr_read_done_packet_t *)message->buf;
			readBuf = (char *)(((uint64_t)aprReadDonePacket->payload.buf_addr << 32) |
						aprReadDonePacket->payload.buf_addr);
			if (record_file.file) {
				ret = kernel_read(record_file.file, (void *)readBuf,
					aprReadDonePacket->payload.buf_size, &record_file.pos);
			}
		}
#endif
		apr_cb_func(message->buf, message->len, NULL);
		message_queue_delete(queue);
	}
	return 0;
}

int apr_send_pkt(void *handle, uint32_t *buf)
{
	struct apr_default_rsp_packet_t aprRspPacket;
	struct apr_write_done_packet_t aprWriteDonePacket;
	struct apr_read_done_packet_t aprReadDonePacket;
	struct apr_default_rsp_packet_t eosRenderedPacket;
	struct apr_packet_t *aprPacket = (struct apr_packet_t *)buf;
	int ret = 0;
#if USE_MSG_QUEUE
	struct dsp_message message;
	struct message_queue *queue = &dsp_message_queue;
#endif
	char file_name[100];

	switch (aprPacket->hdr.opcode) {
		case ASM_STREAM_CMD_OPEN_WRITE_V3:
		case ASM_SESSION_CMD_RUN_V2:
		case ASM_STREAM_CMD_OPEN_READ_V3:
		case AFE_PORT_CMD_DEVICE_OPEN:
		case AFE_PORT_CMD_DEVICE_CLOSE:
		case AFE_PORT_CMD_DEVICE_START:
		case AFE_PORT_CMD_DEVICE_STOP:
		case AFE_PORT_CMD_SET_PARAM_V2:
		case ASM_SESSION_CMD_PAUSE:
		case ASM_SESSION_CMD_SUSPEND:
		case ASM_STREAM_CMD_FLUSH:
		case ASM_STREAM_CMD_FLUSH_READBUFS:
		case ASM_STREAM_CMD_CLOSE:
		case ASM_VT_OPEN:
		case ASM_VT_HW_CONFIG:
		case ASM_VT_CLOSE:
		case ASM_VT_SET_MODE:
		case ASM_VT_RUN:
		case ASM_VT_PAUSE:
		case VSS_IMVM_CMD_OPEN_VOICE:
		case VSS_IMVM_CMD_START_VOICE:
		case VSS_IMVM_CMD_STOP_VOICE:
		case VSS_IVOLUME_CMD_SET_STEP:
		case VSS_IVOLUME_CMD_MUTE_V2:
		case ADM_CMD_MATRIX_MAP_ROUTINGS_V5:
		case ADM_IVOLUME_CMD_FM_SET_STEP:
		case ADM_STREAM_FM_OPEN:
		case ADM_STREAM_FM_RUN:
		case ADM_STREAM_FM_CLOSE:
		case ADM_CMD_AUDIO_CAL_UPDATED:
		{
			uint8_t tempSvc    = aprPacket->hdr.dest_svc;
			uint8_t tempDomain = aprPacket->hdr.dest_domain;
			uint16_t tempPort  = aprPacket->hdr.dest_port;
			aprRspPacket.payload.rspCmdOpcode = aprPacket->hdr.opcode;
			aprRspPacket.payload.status       = 0;
			aprRspPacket.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_CMD_RSP,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
			aprRspPacket.hdr.pkt_size   = sizeof(struct apr_default_rsp_packet_t);
			aprRspPacket.hdr.dest_svc    = aprPacket->hdr.src_svc;
			aprRspPacket.hdr.dest_domain = aprPacket->hdr.src_domain;
			aprRspPacket.hdr.dest_port   = aprPacket->hdr.src_port;
			aprRspPacket.hdr.src_svc     = tempSvc;
			aprRspPacket.hdr.src_domain  = tempDomain;
			aprRspPacket.hdr.src_port    = tempPort;
			aprRspPacket.hdr.token       = aprPacket->hdr.token;
			aprRspPacket.hdr.opcode     = APR_BASIC_RSP_RESULT;
			apr_cb_func(&aprRspPacket, aprRspPacket.hdr.pkt_size, handle);

			if(aprPacket->hdr.opcode == AFE_PORT_CMD_SET_PARAM_V2 ||
			   aprPacket->hdr.opcode == AFE_PORT_CMD_DEVICE_START ||
			   aprPacket->hdr.opcode == AFE_PORT_CMD_DEVICE_STOP  ||
			   aprPacket->hdr.opcode == AFE_PORT_CMD_DEVICE_OPEN  || 
			   aprPacket->hdr.opcode == AFE_PORT_CMD_DEVICE_CLOSE)
				ret = 1;
			if(aprPacket->hdr.opcode == ASM_STREAM_CMD_OPEN_WRITE_V3) {
				memset(file_name, 0, 100);
				playback_file.pos = 0;
				sprintf(file_name, "/data/playback_rec.bin");
				playback_file.file = filp_open(file_name, O_RDWR | O_CREAT | O_TRUNC, 0600);
				if (IS_ERR_OR_NULL(playback_file.file)) {
					pr_err("%s: playback filp open error.\n", __func__);
					playback_file.file = NULL;
				}
			} else if(aprPacket->hdr.opcode == ASM_STREAM_CMD_OPEN_READ_V3 ||
					  aprPacket->hdr.opcode == ASM_VT_OPEN) {
				memset(file_name, 0, 100);
				record_file.pos = 0;
				sprintf(file_name, "/data/pri.wav");
				record_file.file = filp_open(file_name, O_RDONLY, 0);//filp_open(file_name, O_RDWR | O_CREAT | O_TRUNC, 0600);
				if (IS_ERR_OR_NULL(record_file.file)) {
					//pr_err("%s: record filp open error.\n", __func__);
					record_file.file = NULL;
				}
			} else if(aprPacket->hdr.opcode == ASM_STREAM_CMD_CLOSE) {
				if (playback_file.file) {
					filp_close(playback_file.file, NULL);
					playback_file.file = NULL;
					playback_file.pos = 0;
				}
				if (record_file.file) {
					filp_close(record_file.file, NULL);
					record_file.file = NULL;
					record_file.pos = 0;
				}
			} else if(aprPacket->hdr.opcode == ASM_VT_RUN){
					vt_handle = handle;
					aprVtEventPacket = aprRspPacket;
					aprVtEventPacket.hdr.opcode = ASM_VT_EVENT;

					INIT_WORK(&vt_event_work, vt_event_work_do);
					//schedule_work(&vt_event_work);
					//pr_info("%s: schedule vt_event_work.\n", __func__);

					pr_debug("%s: send ASM_VT_EVENT\n", __func__);
					apr_cb_func(&aprVtEventPacket, aprVtEventPacket.hdr.pkt_size, vt_handle);
			}
			break;
		}
		case ASM_DATA_CMD_WRITE_V2:
		{
			uint8_t tempSvc    = aprPacket->hdr.dest_svc;
			uint8_t tempDomain = aprPacket->hdr.dest_domain;
			uint16_t tempPort  = aprPacket->hdr.dest_port;
			struct asm_data_cmd_write_v2 *writePayload = (struct asm_data_cmd_write_v2*)buf;
			aprWriteDonePacket.payload.buf_addr = writePayload->buf_addr;
			aprWriteDonePacket.payload.status     = writePayload->buf_size;
			aprWriteDonePacket.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_EVENT,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
			aprWriteDonePacket.hdr.pkt_size    = sizeof(struct apr_write_done_packet_t);
			aprWriteDonePacket.hdr.dest_svc    = aprPacket->hdr.src_svc;
			aprWriteDonePacket.hdr.dest_domain = aprPacket->hdr.src_domain;
			aprWriteDonePacket.hdr.dest_port   = aprPacket->hdr.src_port;
			aprWriteDonePacket.hdr.src_svc     = tempSvc;
			aprWriteDonePacket.hdr.src_domain  = tempDomain;
			aprWriteDonePacket.hdr.src_port    = tempPort;
			aprWriteDonePacket.hdr.token       = aprPacket->hdr.token;
			aprWriteDonePacket.hdr.opcode      = ASM_DATA_EVENT_WRITE_DONE_V2;

#if USE_MSG_QUEUE
			message.len = aprWriteDonePacket.hdr.pkt_size;
			message.buf = (uint8_t *)&aprWriteDonePacket;
			if (is_message_queue_full(queue))
				pr_err("%s: write done: message queue is full.\n", __func__);
			else {
				message_queue_enter(queue, &message);
				wake_up(&dsp_message_wait);
			}
#else
			apr_cb_func(&aprWriteDonePacket, aprWriteDonePacket.hdr.pkt_size, handle);
#endif
			break;
		}
		case ASM_DATA_CMD_READ_V2:
		{
			uint8_t tempSvc    = aprPacket->hdr.dest_svc;
			uint8_t tempDomain = aprPacket->hdr.dest_domain;
			uint16_t tempPort  = aprPacket->hdr.dest_port;
			struct asm_data_cmd_read_v2 *readPayload = (struct asm_data_cmd_read_v2*)buf;
			aprReadDonePacket.payload.buf_addr = readPayload->buf_addr;
			aprReadDonePacket.payload.buf_size    = readPayload->buf_size;
			aprReadDonePacket.payload.status     = 0;
			aprReadDonePacket.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_EVENT,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
			aprReadDonePacket.hdr.pkt_size    = sizeof(struct apr_read_done_packet_t);
			aprReadDonePacket.hdr.dest_svc    = aprPacket->hdr.src_svc;
			aprReadDonePacket.hdr.dest_domain = aprPacket->hdr.src_domain;
			aprReadDonePacket.hdr.dest_port   = aprPacket->hdr.src_port;
			aprReadDonePacket.hdr.src_svc     = tempSvc;
			aprReadDonePacket.hdr.src_domain  = tempDomain;
			aprReadDonePacket.hdr.src_port    = tempPort;
			aprReadDonePacket.hdr.token       = aprPacket->hdr.token;
			aprReadDonePacket.hdr.opcode      = ASM_DATA_EVENT_READ_DONE_V2;

#if USE_MSG_QUEUE
			message.len = aprReadDonePacket.hdr.pkt_size;
			message.buf = (uint8_t *)&aprReadDonePacket;
			if (is_message_queue_full(queue))
				pr_err("%s: read done: message queue is full.\n", __func__);
			else {
				message_queue_enter(queue, &message);
				wake_up(&dsp_message_wait);
			}
#else
			apr_cb_func(&aprReadDonePacket, aprReadDonePacket.hdr.pkt_size, handle);
#endif
			break;
		}
		case ASM_VT_READ:
		{
			uint8_t tempSvc    = aprPacket->hdr.dest_svc;
			uint8_t tempDomain = aprPacket->hdr.dest_domain;
			uint16_t tempPort  = aprPacket->hdr.dest_port;
			struct asm_data_cmd_read_v2 *readPayload = (struct asm_data_cmd_read_v2*)buf;
			aprReadDonePacket.payload.buf_addr = readPayload->buf_addr;
			aprReadDonePacket.payload.buf_size    = readPayload->buf_size;
			aprReadDonePacket.payload.status     = 0;
			aprReadDonePacket.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_EVENT,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
			aprReadDonePacket.hdr.pkt_size    = sizeof(struct apr_read_done_packet_t);
			aprReadDonePacket.hdr.dest_svc    = aprPacket->hdr.src_svc;
			aprReadDonePacket.hdr.dest_domain = aprPacket->hdr.src_domain;
			aprReadDonePacket.hdr.dest_port   = aprPacket->hdr.src_port;
			aprReadDonePacket.hdr.src_svc     = tempSvc;
			aprReadDonePacket.hdr.src_domain  = tempDomain;
			aprReadDonePacket.hdr.src_port    = tempPort;
			aprReadDonePacket.hdr.token       = aprPacket->hdr.token;
			aprReadDonePacket.hdr.opcode      = ASM_VT_READ_DONE;

#if USE_MSG_QUEUE
			pr_info("%s: enter vt read msg,token %X \n", __func__,aprReadDonePacket.hdr.token);
			message.len = aprReadDonePacket.hdr.pkt_size;
			message.buf = (uint8_t *)&aprReadDonePacket;
			if (is_message_queue_full(queue))
				pr_err("%s: vt read done : message queue is full.\n", __func__);
			else {
				message_queue_enter(queue, &message);
				wake_up(&dsp_message_wait);
			}
#else
			apr_cb_func(&aprReadDonePacket, aprReadDonePacket.hdr.pkt_size, handle);
#endif
			
			break;
		}
		case ASM_DATA_CMD_EOS:
		{
			uint8_t tempSvc    = aprPacket->hdr.dest_svc;
			uint8_t tempDomain = aprPacket->hdr.dest_domain;
			uint16_t tempPort  = aprPacket->hdr.dest_port;
			eosRenderedPacket.payload.rspCmdOpcode = aprPacket->hdr.opcode;
			eosRenderedPacket.payload.status       = 0;
			eosRenderedPacket.hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_EVENT,
				APR_HDR_LEN(APR_HDR_SIZE), APR_PKT_VER);
			eosRenderedPacket.hdr.pkt_size   = sizeof(struct apr_default_rsp_packet_t);
			eosRenderedPacket.hdr.dest_svc    = aprPacket->hdr.src_svc;
			eosRenderedPacket.hdr.dest_domain = aprPacket->hdr.src_domain;
			eosRenderedPacket.hdr.dest_port   = aprPacket->hdr.src_port;
			eosRenderedPacket.hdr.src_svc     = tempSvc;
			eosRenderedPacket.hdr.src_domain  = tempDomain;
			eosRenderedPacket.hdr.src_port    = tempPort;
			eosRenderedPacket.hdr.token       = aprPacket->hdr.token;
			eosRenderedPacket.hdr.opcode      = ASM_DATA_EVENT_RENDERED_EOS;

#if USE_MSG_QUEUE
			message.len = eosRenderedPacket.hdr.pkt_size;
			message.buf = (uint8_t *)&eosRenderedPacket;
			if (is_message_queue_full(queue))
				pr_err("%s: write done: message queue is full.\n", __func__);
			else {
				message_queue_enter(queue, &message);
				wake_up(&dsp_message_wait);
			}
#else
			apr_cb_func(&eosRenderedPacket, eosRenderedPacket.hdr.pkt_size, handle);
#endif
			break;
		}
		default:
			break;
	}
	return ret;
}

EXPORT_SYMBOL_GPL(apr_send_pkt);
#endif
int apr_pkt_config(void *handle, struct apr_pkt_cfg *cfg)
{
	struct apr_svc *svc = (struct apr_svc *)handle;
	uint16_t dest_id;
	uint16_t client_id;
	struct apr_client *clnt;

	if (!handle) {
		pr_err("%s: Invalid handle\n", __func__);
		return -EINVAL;
	}

	if (svc->need_reset) {
		pr_err("%s: service need reset\n", __func__);
		return -ENETRESET;
	}

	svc->pkt_owner = cfg->pkt_owner;
	dest_id = svc->dest_id;
	client_id = svc->client_id;
	clnt = &client[dest_id][client_id];

	return apr_tal_rx_intents_config(clnt->handle,
		cfg->intents.num_of_intents, cfg->intents.size);
}

/**
 * apr_register - Clients call to register
 * to APR.
 *
 * @dest: destination processor
 * @svc_name: name of service to register as
 * @svc_fn: callback function to trigger when response
 *   ack or packets received from destination processor.
 * @src_port: Port number within a service
 * @priv: private data of client, passed back in cb fn.
 *
 * Returns apr_svc handle on success or NULL on failure.
 */
struct apr_svc *apr_register(char *dest, char *svc_name, apr_fn svc_fn,
				uint32_t src_port, void *priv)
{
	struct apr_client *clnt;
	int client_id = 0;
	int svc_idx = 0;
	int svc_id = 0;
	int dest_id = 0;
	int domain_id = 0;
	int temp_port = 0;
	struct apr_svc *svc = NULL;
	int rc = 0;
	bool can_open_channel = true;

	if (!dest || !svc_name || !svc_fn)
		return NULL;

	if (!strcmp(dest, "ADSP"))
		domain_id = APR_DOMAIN_ADSP;
	else if (!strcmp(dest, "MODEM")) {
		/* Don't request for SMD channels if destination is MODEM,
		 * as these channels are no longer used and these clients
		 * are to listen only for MODEM SSR events
		 */
		can_open_channel = false;
		domain_id = APR_DOMAIN_MODEM;
	} else {
		pr_err("APR: wrong destination\n");
		goto done;
	}

	dest_id = apr_get_dest_id(dest);

	if (dest_id == APR_DEST_ADSP) {
		if (apr_get_q6_state() != APR_SUBSYS_LOADED) {
			pr_err_ratelimited("%s: adsp not up\n", __func__);
			return NULL;
		}
		pr_debug("%s: adsp Up\n", __func__);
	} else if (dest_id == APR_DEST_MODEM) {
		if (apr_get_modem_state() == APR_SUBSYS_DOWN) {
			if (is_modem_up) {
				pr_err("%s: modem shutdown due to SSR, ret",
					__func__);
				return NULL;
			}
			pr_debug("%s: Wait for modem to bootup\n", __func__);
			rc = wait_event_interruptible_timeout(modem_wait,
						(apr_get_modem_state() == APR_SUBSYS_UP),
						(1 * HZ));
			if (rc == 0) {
				pr_err("%s: Modem is not Up\n", __func__);
				return NULL;
			}
		}
		pr_debug("%s: modem Up\n", __func__);
	}

	if (apr_get_svc(svc_name, domain_id, &client_id, &svc_idx, &svc_id)) {
		pr_err_ratelimited("%s: apr_get_svc failed\n", __func__);
		goto done;
	}

	clnt = &client[dest_id][client_id];
	mutex_lock(&clnt->m_lock);
	if (!clnt->handle && can_open_channel) {
		clnt->handle = apr_tal_open(client_id, dest_id,
				APR_DL_SMD, apr_cb_func, NULL);
		if (!clnt->handle) {
			//svc = NULL;
			pr_err_ratelimited("APR: Unable to open handle\n");
			//mutex_unlock(&clnt->m_lock);
		//	goto done;
		}
	}
	mutex_unlock(&clnt->m_lock);
	svc = &clnt->svc[svc_idx];
	mutex_lock(&svc->m_lock);
	clnt->id = client_id;
	if (svc->need_reset) {
		mutex_unlock(&svc->m_lock);
		pr_err_ratelimited("APR: Service needs reset\n");
		svc = NULL;
		goto done;
	}
	svc->id = svc_id;
	svc->dest_id = dest_id;
	svc->client_id = client_id;
	svc->dest_domain = domain_id;
	svc->pkt_owner = APR_PKT_OWNER_DRIVER;

	if (src_port != 0xFFFFFFFF) {
		temp_port = ((src_port >> 8) * 8) + (src_port & 0xFF);
		pr_debug("port = %d t_port = %d\n", src_port, temp_port);
		if (temp_port >= APR_MAX_PORTS || temp_port < 0) {
			pr_err("APR: temp_port out of bounds\n");
			mutex_unlock(&svc->m_lock);
			return NULL;
		}
		if (!svc->svc_cnt)
			clnt->svc_cnt++;
		svc->port_cnt++;
		svc->port_fn[temp_port] = svc_fn;
		svc->port_priv[temp_port] = priv;
		svc->svc_cnt++;
	} else {
		if (!svc->fn) {
			if (!svc->svc_cnt)
				clnt->svc_cnt++;
			svc->fn = svc_fn;
			svc->priv = priv;
			svc->svc_cnt++;
		}
	}

	mutex_unlock(&svc->m_lock);
done:
	return svc;
}
EXPORT_SYMBOL_GPL(apr_register);


void apr_cb_func(void *buf, int len, void *priv)
{
	struct apr_client_data data;
	struct apr_client *apr_client;
	struct apr_svc *c_svc;
	struct apr_hdr *hdr;
	uint16_t hdr_size;
	uint16_t msg_type;
	uint16_t ver;
	uint16_t src;
	uint16_t svc;
	uint16_t clnt;
	int i;
	int temp_port = 0;
	uint32_t *ptr;

	pr_debug("APR2: len = %d\n", len);
	ptr = buf;
	pr_debug("\n*****************\n");
	for (i = 0; i < len/4; i++)
		pr_debug("%x  ", ptr[i]);
	pr_debug("\n");
	pr_debug("\n*****************\n");

	if (!buf || len < APR_HDR_SIZE) {
		pr_err("APR: Improper apr pkt received:%pK %d\n", buf, len);
		return;
	}
	hdr = buf;

	ver = hdr->hdr_field;
	ver = (ver & 0x000F);
	if (ver > APR_PKT_VER + 1) {
		pr_err("APR: Wrong version: %d\n", ver);
		return;
	}

	hdr_size = hdr->hdr_field;
	hdr_size = ((hdr_size & 0x00F0) >> 0x4) * 4;
	if (hdr_size < APR_HDR_SIZE) {
		pr_err("APR: Wrong hdr size:%d\n", hdr_size);
		return;
	}

	if (hdr->pkt_size < APR_HDR_SIZE) {
		pr_err("APR: Wrong paket size\n");
		return;
	}

	if (hdr->pkt_size < hdr_size) {
		pr_err("APR: Packet size less than header size\n");
		return;
	}

	msg_type = hdr->hdr_field;
	msg_type = (msg_type >> 0x08) & 0x0003;
	if (msg_type >= APR_MSG_TYPE_MAX && msg_type != APR_BASIC_RSP_RESULT) {
		pr_err("APR: Wrong message type: %d\n", msg_type);
		return;
	}

	if (hdr->src_domain >= APR_DOMAIN_MAX ||
		hdr->dest_domain >= APR_DOMAIN_MAX ||
		hdr->src_svc >= APR_SVC_MAX ||
		hdr->dest_svc >= APR_SVC_MAX) {
		pr_err("APR: Wrong APR header\n");
		return;
	}

	svc = hdr->dest_svc;
	if (hdr->src_domain == APR_DOMAIN_MODEM) {
		if (svc == APR_SVC_MVS || svc == APR_SVC_MVM ||
		    svc == APR_SVC_CVS || svc == APR_SVC_CVP ||
		    svc == APR_SVC_TEST_CLIENT)
			clnt = APR_CLIENT_VOICE;
		else {
			pr_err("APR: Wrong svc :%d\n", svc);
			return;
		}
	} else if (hdr->src_domain == APR_DOMAIN_ADSP) {
		if (svc == APR_SVC_AFE || svc == APR_SVC_ASM ||
		    svc == APR_SVC_VSM || svc == APR_SVC_VPM ||
		    svc == APR_SVC_ADM || svc == APR_SVC_ADSP_CORE ||
		    svc == APR_SVC_USM ||
		    svc == APR_SVC_TEST_CLIENT || svc == APR_SVC_ADSP_MVM ||
		    svc == APR_SVC_ADSP_CVS || svc == APR_SVC_ADSP_CVP ||
		    svc == APR_SVC_LSM || svc == APR_SVC_TINYLOG ||
			svc == APR_SVC_PSY)
			clnt = APR_CLIENT_AUDIO;
		else if (svc == APR_SVC_VIDC)
			clnt = APR_CLIENT_AUDIO;
		else {
			pr_err("APR: Wrong svc :%d\n", svc);
			return;
		}
	} else {
		pr_err("APR: Pkt from wrong source: %d\n", hdr->src_domain);
		return;
	}
	pr_debug("dest_svc = %d src_domain = %d clnt = %d\n", svc,hdr->src_domain,clnt);

	src = apr_get_data_src(hdr);
	if (src == APR_DEST_MAX)
		return;

	pr_debug("src =%d clnt = %d\n", src, clnt);
	apr_client = &client[src][clnt];
	for (i = 0; i < APR_SVC_MAX; i++)
		if (apr_client->svc[i].id == svc) {
			pr_debug("%d\n", apr_client->svc[i].id);
			c_svc = &apr_client->svc[i];
			break;
		}

	if (i == APR_SVC_MAX) {
		pr_err("APR: service is not registered\n");
		return;
	}
	pr_debug("svc_idx = %d\n", i);
	pr_debug("token = 0x%x\n", hdr->token);
	pr_debug("%x %x %x %pK %pK\n", c_svc->id, c_svc->dest_id,
		 c_svc->client_id, c_svc->fn, c_svc->priv);
	data.payload_size = hdr->pkt_size - hdr_size;
	data.opcode = hdr->opcode;
	data.src = src;
	data.src_port = hdr->src_port;
	data.dest_port = hdr->dest_port;
	data.token = hdr->token;
	data.msg_type = msg_type;
	data.payload = NULL;
	if (data.payload_size > 0)
		data.payload = (char *)hdr + hdr_size;

	temp_port = ((data.dest_port >> 8) * 8) + (data.dest_port & 0xFF);
	if (((temp_port >= 0) && (temp_port < APR_MAX_PORTS))
		&& (c_svc->port_cnt && c_svc->port_fn[temp_port]))
		c_svc->port_fn[temp_port](&data,
			c_svc->port_priv[temp_port]);
	else if (c_svc->fn)
		c_svc->fn(&data, c_svc->priv);
	else
		pr_err("APR: Rxed a packet for NULL callback\n");
}

int apr_get_svc(const char *svc_name, int domain_id, int *client_id,
		int *svc_idx, int *svc_id)
{
	int i;
	int size;
	struct apr_svc_table *tbl;
	int ret = 0;

	if (domain_id == APR_DOMAIN_ADSP) {
		tbl = (struct apr_svc_table *)&svc_tbl_qdsp6;
		size = ARRAY_SIZE(svc_tbl_qdsp6);
	} else {
		tbl = (struct apr_svc_table *)&svc_tbl_voice;
		size = ARRAY_SIZE(svc_tbl_voice);
	}

	for (i = 0; i < size; i++) {
		if (!strcmp(svc_name, tbl[i].name)) {
			*client_id = tbl[i].client_id;
			*svc_idx = tbl[i].idx;
			*svc_id = tbl[i].id;
			break;
		}
	}

	pr_debug("%s: svc_name = %s c_id = %d domain_id = %d\n",
		 __func__, svc_name, *client_id, domain_id);
	if (i == size) {
		pr_err("%s: APR: Wrong svc name %s\n", __func__, svc_name);
		ret = -EINVAL;
	}

	return ret;
}

static void apr_reset_deregister(struct work_struct *work)
{
	struct apr_svc *handle = NULL;
	struct apr_reset_work *apr_reset =
			container_of(work, struct apr_reset_work, work);

	handle = apr_reset->handle;
	pr_debug("%s:handle[%pK]\n", __func__, handle);
	apr_deregister(handle);
	kfree(apr_reset);
}

/**
 * apr_start_rx_rt - Clients call to vote for thread
 * priority upgrade whenever needed.
 *
 * @handle: APR service handle
 *
 * Returns 0 on success or error otherwise.
 */
int apr_start_rx_rt(void *handle)
{
	int rc = 0;
	struct apr_svc *svc = handle;
	uint16_t dest_id = 0;
	uint16_t client_id = 0;

	if (!svc) {
		pr_err("%s: Invalid APR handle\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&svc->m_lock);
	dest_id = svc->dest_id;
	client_id = svc->client_id;

	if ((client_id >= APR_CLIENT_MAX) || (dest_id >= APR_DEST_MAX)) {
		pr_err("%s: %s invalid. client_id = %u, dest_id = %u\n",
		       __func__,
		       client_id >= APR_CLIENT_MAX ? "Client ID" : "Dest ID",
		       client_id, dest_id);
		rc = -EINVAL;
		goto exit;
	}

	if (!client[dest_id][client_id].handle) {
		pr_err("%s: Client handle is NULL\n", __func__);
		rc = -EINVAL;
		goto exit;
	}

	rc = apr_tal_start_rx_rt(client[dest_id][client_id].handle);
	if (rc)
		pr_err("%s: failed to set RT thread priority for APR RX. rc = %d\n",
			__func__, rc);

exit:
	mutex_unlock(&svc->m_lock);
	return rc;
}
EXPORT_SYMBOL(apr_start_rx_rt);

/**
 * apr_end_rx_rt - Clients call to unvote for thread
 * priority upgrade (perviously voted with
 * apr_start_rx_rt()).
 *
 * @handle: APR service handle
 *
 * Returns 0 on success or error otherwise.
 */
int apr_end_rx_rt(void *handle)
{
	int rc = 0;
	struct apr_svc *svc = handle;
	uint16_t dest_id = 0;
	uint16_t client_id = 0;

	if (!svc) {
		pr_err("%s: Invalid APR handle\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&svc->m_lock);
	dest_id = svc->dest_id;
	client_id = svc->client_id;

	if ((client_id >= APR_CLIENT_MAX) || (dest_id >= APR_DEST_MAX)) {
		pr_err("%s: %s invalid. client_id = %u, dest_id = %u\n",
		       __func__,
		       client_id >= APR_CLIENT_MAX ? "Client ID" : "Dest ID",
		       client_id, dest_id);
		rc = -EINVAL;
		goto exit;
	}

	if (!client[dest_id][client_id].handle) {
		pr_err("%s: Client handle is NULL\n", __func__);
		rc = -EINVAL;
		goto exit;
	}

	rc = apr_tal_end_rx_rt(client[dest_id][client_id].handle);
	if (rc)
		pr_err("%s: failed to reset RT thread priority for APR RX. rc = %d\n",
			__func__, rc);

exit:
	mutex_unlock(&svc->m_lock);
	return rc;
}
EXPORT_SYMBOL(apr_end_rx_rt);

/**
 * apr_deregister - Clients call to de-register
 * from APR.
 *
 * @handle: APR service handle to de-register
 *
 * Returns 0 on success or -EINVAL on error.
 */
int apr_deregister(void *handle)
{
	struct apr_svc *svc = handle;
	struct apr_client *clnt;
	uint16_t dest_id;
	uint16_t client_id;

	if (!handle)
		return -EINVAL;

	mutex_lock(&svc->m_lock);
	if (!svc->svc_cnt) {
		pr_err("%s: svc already deregistered. svc = %pK\n",
			__func__, svc);
		mutex_unlock(&svc->m_lock);
		return -EINVAL;
	}

	dest_id = svc->dest_id;
	client_id = svc->client_id;
	clnt = &client[dest_id][client_id];

	if (svc->svc_cnt > 0) {
		if (svc->port_cnt)
			svc->port_cnt--;
		svc->svc_cnt--;
		if (!svc->svc_cnt) {
			client[dest_id][client_id].svc_cnt--;
			pr_debug("%s: service is reset %pK\n", __func__, svc);
		}
	}

	if (!svc->svc_cnt) {
		svc->priv = NULL;
		svc->id = 0;
		svc->fn = NULL;
		svc->dest_id = 0;
		svc->client_id = 0;
		svc->need_reset = 0x0;
	}
	if (client[dest_id][client_id].handle &&
	    !client[dest_id][client_id].svc_cnt) {
		apr_tal_close(client[dest_id][client_id].handle);
		client[dest_id][client_id].handle = NULL;
	}

	mutex_unlock(&svc->m_lock);

	return 0;
}
EXPORT_SYMBOL(apr_deregister);

/**
 * apr_reset - sets up workqueue to de-register
 * the given APR service handle.
 *
 * @handle: APR service handle
 *
 */
void apr_reset(void *handle)
{
	struct apr_reset_work *apr_reset_worker = NULL;

	if (!handle)
		return;
	pr_debug("%s: handle[%pK]\n", __func__, handle);

	if (apr_reset_workqueue == NULL) {
		pr_err("%s: apr_reset_workqueue is NULL\n", __func__);
		return;
	}

	apr_reset_worker = kzalloc(sizeof(struct apr_reset_work),
							GFP_ATOMIC);

	if (apr_reset_worker == NULL) {
		pr_err("%s: mem failure\n", __func__);
		return;
	}

	apr_reset_worker->handle = handle;
	INIT_WORK(&apr_reset_worker->work, apr_reset_deregister);
	queue_work(apr_reset_workqueue, &apr_reset_worker->work);
}
EXPORT_SYMBOL(apr_reset);

/* Dispatch the Reset events to Modem and audio clients */
static void dispatch_event(unsigned long code, uint16_t proc)
{
	struct apr_client *apr_client;
	struct apr_client_data data;
	struct apr_svc *svc;
	uint16_t clnt;
	int i, j;

	memset(&data, 0, sizeof(data));
	data.opcode = RESET_EVENTS;
	data.reset_event = code;

	/* Service domain can be different from the processor */
	data.reset_proc = apr_get_reset_domain(proc);

	clnt = APR_CLIENT_AUDIO;
	apr_client = &client[proc][clnt];
	for (i = 0; i < APR_SVC_MAX; i++) {
		mutex_lock(&apr_client->svc[i].m_lock);
		if (apr_client->svc[i].fn) {
			//apr_client->svc[i].need_reset = 0x1;
			apr_client->svc[i].fn(&data, apr_client->svc[i].priv);
		}
		if (apr_client->svc[i].port_cnt) {
			svc = &(apr_client->svc[i]);
			svc->need_reset = 0x1;
			for (j = 0; j < APR_MAX_PORTS; j++)
				if (svc->port_fn[j])
					svc->port_fn[j](&data,
						svc->port_priv[j]);
		}
		mutex_unlock(&apr_client->svc[i].m_lock);
	}

	clnt = APR_CLIENT_VOICE;
	apr_client = &client[proc][clnt];
	for (i = 0; i < APR_SVC_MAX; i++) {
		mutex_lock(&apr_client->svc[i].m_lock);
		if (apr_client->svc[i].fn) {
			apr_client->svc[i].need_reset = 0x1;
			apr_client->svc[i].fn(&data, apr_client->svc[i].priv);
		}
		if (apr_client->svc[i].port_cnt) {
			svc = &(apr_client->svc[i]);
			svc->need_reset = 0x1;
			for (j = 0; j < APR_MAX_PORTS; j++)
				if (svc->port_fn[j])
					svc->port_fn[j](&data,
						svc->port_priv[j]);
		}
		mutex_unlock(&apr_client->svc[i].m_lock);
	}
}

static int apr_notifier_service_cb(struct notifier_block *this,
				   unsigned long opcode, void *data)
{
	struct audio_notifier_cb_data *cb_data = data;

	if (cb_data == NULL) {
		pr_err("%s: Callback data is NULL!\n", __func__);
		goto done;
	}

	pr_err("%s: Service opcode 0x%lx, domain %d\n",
		__func__, opcode, cb_data->domain);

	switch (opcode) {
	case AUDIO_NOTIFIER_SERVICE_DOWN:
		/*
		 * Use flag to ignore down notifications during
		 * initial boot. There is no benefit from error
		 * recovery notifications during initial boot
		 * up since everything is expected to be down.
		 */
		spin_lock(&apr_priv->apr_lock);
		if (apr_priv->is_initial_boot) {
			spin_unlock(&apr_priv->apr_lock);
			break;
		}
		spin_unlock(&apr_priv->apr_lock);
		if (cb_data->domain == AUDIO_NOTIFIER_MODEM_DOMAIN)
			apr_modem_down(opcode);
		else
			apr_adsp_down(opcode);
		break;
	case AUDIO_NOTIFIER_SERVICE_UP:
		if (cb_data->domain == AUDIO_NOTIFIER_MODEM_DOMAIN)
			apr_modem_up();
		else
			apr_adsp_up();
		spin_lock(&apr_priv->apr_lock);
		apr_priv->is_initial_boot = false;
		spin_unlock(&apr_priv->apr_lock);
		break;
	default:
		break;
	}
done:
	return NOTIFY_OK;
}

static struct notifier_block adsp_service_nb = {
	.notifier_call  = apr_notifier_service_cb,
	.priority = 0,
};

static struct notifier_block modem_service_nb = {
	.notifier_call  = apr_notifier_service_cb,
	.priority = 0,
};

static void apr_cleanup(void)
{
	int i, j, k;

	of_platform_depopulate(apr_priv->dev);
	subsys_notif_deregister(subsys_name);
	if (apr_reset_workqueue) {
		flush_workqueue(apr_reset_workqueue);
		destroy_workqueue(apr_reset_workqueue);
	}
	mutex_destroy(&q6.lock);
	for (i = 0; i < APR_DEST_MAX; i++) {
		for (j = 0; j < APR_CLIENT_MAX; j++) {
			mutex_destroy(&client[i][j].m_lock);
			for (k = 0; k < APR_SVC_MAX; k++)
				mutex_destroy(&client[i][j].svc[k].m_lock);
		}
	}
}

static int apr_probe(struct platform_device *pdev)
{
	int i, j, k, ret = 0;
#ifdef JR510_AP_TEST
	struct message_queue *queue = &dsp_message_queue;
#endif
	pr_err("%s: enter.\n", __func__);
	init_waitqueue_head(&modem_wait);

	apr_priv = devm_kzalloc(&pdev->dev, sizeof(*apr_priv), GFP_KERNEL);
	if (!apr_priv)
		return -ENOMEM;

	apr_priv->dev = &pdev->dev;
	spin_lock_init(&apr_priv->apr_lock);
	INIT_WORK(&apr_priv->add_chld_dev_work, apr_add_child_devices);

	for (i = 0; i < APR_DEST_MAX; i++)
		for (j = 0; j < APR_CLIENT_MAX; j++) {
			mutex_init(&client[i][j].m_lock);
			for (k = 0; k < APR_SVC_MAX; k++) {
				mutex_init(&client[i][j].svc[k].m_lock);
				spin_lock_init(&client[i][j].svc[k].w_lock);
			}
		}
	apr_set_subsys_state();
	mutex_init(&q6.lock);
	apr_reset_workqueue = create_singlethread_workqueue("apr_driver");
	if (!apr_reset_workqueue) {
		apr_priv = NULL;
		return -ENOMEM;
	}

	spin_lock(&apr_priv->apr_lock);
	apr_priv->is_initial_boot = true;
	spin_unlock(&apr_priv->apr_lock);
	ret = of_property_read_string(pdev->dev.of_node,
				      "jlq,subsys-name",
				      (const char **)(&subsys_name));
	if (ret) {
		pr_err("%s: missing subsys-name entry in dt node\n", __func__);
		return -EINVAL;
	}

	if (!strcmp(subsys_name, "apr_adsp")) {
		subsys_notif_register("apr_adsp",
				       AUDIO_NOTIFIER_ADSP_DOMAIN,
				       &adsp_service_nb);
	} else if (!strcmp(subsys_name, "apr_modem")) {
		subsys_notif_register("apr_modem",
				       AUDIO_NOTIFIER_MODEM_DOMAIN,
				       &modem_service_nb);
	} else {
		pr_err("%s: invalid subsys-name %s\n", __func__, subsys_name);
		return -EINVAL;
	}

	apr_tal_init();

	ret = snd_event_client_register(&pdev->dev, &apr_ssr_ops, NULL);
	if (ret) {
		pr_err("%s: Registration with SND event fwk failed ret = %d\n",
			__func__, ret);
		ret = 0;
	}

#ifdef JR510_AP_TEST
	for (i = 0; i < ADSP_MESSAGE_QUEUE_SIZE; i++)
		queue->message[i].buf = kmalloc(BRIDGE_BUF_SIZE, GFP_KERNEL);
	message_queue_init(&dsp_message_queue, ADSP_MESSAGE_QUEUE_SIZE, BRIDGE_BUF_SIZE);

	init_waitqueue_head(&dsp_message_wait);

	adsp_thread =
		kthread_create(apr_adsp_cb_process, NULL, "adsp-cb-thread");
	if (IS_ERR(adsp_thread)) {
		pr_err("%s: Could not create adsp cb thread.\n", __func__);
		return -ENODEV;
	} else {
		wake_up_process(adsp_thread);
	}

	playback_file.file = NULL;
	playback_file.pos = 0;
	record_file.file = NULL;
	record_file.pos = 0;
#endif

    return 0;
}

static int apr_remove(struct platform_device *pdev)
{
	snd_event_client_deregister(&pdev->dev);
	apr_cleanup();
	apr_tal_exit();
	apr_priv = NULL;
	return 0;
}

static const struct of_device_id apr_machine_of_match[]  = {
	{ .compatible = "jlq,msm-audio-apr", },
	{},
};

static struct platform_driver apr_driver = {
	.probe = apr_probe,
	.remove = apr_remove,
	.driver = {
		.name = "audio_apr",
		.owner = THIS_MODULE,
		.of_match_table = apr_machine_of_match,
		.suppress_bind_attrs = true,
	}
};

module_platform_driver(apr_driver);

MODULE_DESCRIPTION("APR DRIVER");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, apr_machine_of_match);
MODULE_SOFTDEP("pre: jlq_clk jlq-gpio spmi-pmic-arb jlq_slimbus");
MODULE_SOFTDEP("pre: jlq-regulator rpm-smd-regulator jlq qca_cld3_wlan");
MODULE_SOFTDEP("pre: i2c-designware-core i2c-dev i2c-designware-platform");
