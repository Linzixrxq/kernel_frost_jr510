// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/rpmsg.h>
#include <ipc/apr_tal.h>
#include <linux/of_device.h>
#include <soc/jlq/jr510/jlq-bridge.h>

enum apr_channel_state {
	APR_CH_DISCONNECTED,
	APR_CH_CONNECTED,
};

#define APR_MAXIMUM_NUM_OF_RETRIES 2

static struct apr_svc_ch_dev
	apr_svc_ch[APR_DL_MAX][APR_DEST_MAX][APR_CLIENT_MAX];

/**
 * apr_tal_write() - Write a message across to the remote processor
 * @apr_ch: apr channel handle
 * @data: buffer that needs to be transferred over the channel
 * @pkt_priv: private data of the packet
 * @len: length of the buffer
 *
 * Returns len of buffer successfully transferred on success
 * and an appropriate error value on failure.
 */
int __apr_tal_write(struct apr_svc_ch_dev *apr_ch, void *data,
			int len)
{
	int rc;
	
	rc = bridge_write_from_irq(apr_ch->ch, (char *)data, len);
	if (rc != len)
		pr_err("apr write unmatch");

	return rc;
}

int apr_tal_write(struct apr_svc_ch_dev *apr_ch, void *data, int len)
{
	int rc = 0, retries = 0;
	
	if (!apr_ch->ch)
		return -EINVAL;
	do {
		rc = __apr_tal_write(apr_ch, data, len);
		if (!rc)
			udelay(50);
	} while (!rc && retries++ < 5);

	if (!rc)
		pr_err("apr_tal: TIMEOUT for write\n");

	return rc;
}


/**
 * apr_tal_rx_intents_config() - Configure glink intents for remote processor
 * @apr_ch: apr channel handle
 * @num_of_intents: number of intents
 * @size: size of the intents
 *
 * This api is not supported with RPMSG. Returns 0 to indicate success
 */
int apr_tal_rx_intents_config(struct apr_svc_ch_dev *apr_ch,
			      int num_of_intents, uint32_t size)
{
	pr_debug("%s: NO-OP\n", __func__);
	return 0;
}
EXPORT_SYMBOL(apr_tal_rx_intents_config);

/**
 * apr_tal_start_rx_rt() - Set RT thread priority for APR RX transfer
 * @apr_ch: apr channel handle
 *
 * This api is not supported with RPMSG as message transfer occurs
 * in client's context. Returns 0 to indicate success.
 */
int apr_tal_start_rx_rt(struct apr_svc_ch_dev *apr_ch)
{
	pr_debug("%s: NO-OP\n", __func__);
	return 0;
}
EXPORT_SYMBOL(apr_tal_start_rx_rt);

/**
 * apr_tal_end_rx_rt() - Remove RT thread priority for APR RX transfer
 * @apr_ch: apr channel handle
 *
 * This api is not supported with RPMSG. Returns 0 to indicate success
 */
int apr_tal_end_rx_rt(struct apr_svc_ch_dev *apr_ch)
{
	pr_debug("%s: NO-OP\n", __func__);
	return 0;
}
EXPORT_SYMBOL(apr_tal_end_rx_rt);

void adsp_irq_handler(void *data, unsigned int irq)
{
	struct apr_svc_ch_dev *apr_ch = data;
	unsigned long flags;
	int data_len;
	int data_offset = 0;
	struct apr_hdr *hdr_head;

	//read data from bridge
	data_len = bridge_read_from_notify(apr_ch->ch,
					(char *)&apr_ch->data, 256);

	while (data_len > 0) {
		hdr_head = (struct apr_hdr *)&apr_ch->data[data_offset];
		if (hdr_head->pkt_size == 0 || hdr_head->pkt_size > data_len) {
			pr_err("%s: pkt size error, data_len = %d, pkt_size = %d.\n",
						__func__, data_len, hdr_head->pkt_size);
			break;
		}
		spin_lock_irqsave(&apr_ch->r_lock, flags);
		if (apr_ch->func)
			apr_ch->func(hdr_head, hdr_head->pkt_size,
				apr_ch->priv);
		spin_unlock_irqrestore(&apr_ch->r_lock, flags);
		data_len -= hdr_head->pkt_size;
		data_offset += hdr_head->pkt_size;
	}
}

/**
 * apr_tal_open() - Open a transport channel for data transfer
 * on remote processor.
 * @clnt: apr client, audio or voice
 * @dest: destination remote processor for which apr channel is requested for.
 * @dl: type of data link
 * @func: callback function to handle data transfer from remote processor
 * @priv: private data of the client
 *
 * Returns apr_svc_ch_dev handle on success and NULL on failure.
 */
struct apr_svc_ch_dev *apr_tal_open(uint32_t clnt, uint32_t dest, uint32_t dl,
				    apr_svc_cb_fn func, void *priv)
{
	int rc;
	struct bridge_channel *ch = NULL;
	struct apr_svc_ch_dev *apr_ch;

	if ((clnt >= APR_CLIENT_MAX) || (dest >= APR_DEST_MAX) ||
						(dl >= APR_DL_MAX)) {
		pr_err("apr_tal: Invalid params\n");
		return NULL;
	}

	apr_ch = (struct apr_svc_ch_dev *)&apr_svc_ch[dl][dest][clnt];
	if (apr_ch->ch) {
		pr_err("apr_tal: This channel alreday openend\n");
		return NULL;
	}

	if (dest == APR_DEST_ADSP) {
		//OPEN mailbox channel
		
		pr_debug("%s: bridge_name_open AUDIO\n",__func__);
		rc = bridge_name_open("AUDIO", 2, &ch, apr_ch,
			(void *)adsp_irq_handler);
		if (rc != 0) {
			pr_err("adsp bridge open fail\n");
			kfree(ch);
			apr_ch->ch = NULL;
			return NULL;
		}
		mutex_lock(&apr_ch->m_lock);
		apr_ch->ch = ch;
		apr_ch->func = func;
		apr_ch->priv = priv;
		mutex_unlock(&apr_ch->m_lock);

		pr_debug("%s: AUDIO bridge open success\n",__func__);

		return apr_ch;
	}

	return NULL;
}
EXPORT_SYMBOL(apr_tal_open);

/**
 * apr_tal_close() - Close transport channel on remote processor.
 * @apr_ch: apr channel handle
 *
 * Returns 0 on success and an appropriate error value on failure.
 */
int apr_tal_close(struct apr_svc_ch_dev *apr_ch)
{
	if (!apr_ch->ch)
		return -EINVAL;

	mutex_lock(&apr_ch->m_lock);
	bridge_release(apr_ch->ch);
	apr_ch->ch = NULL;
	apr_ch->func = NULL;
	apr_ch->priv = NULL;
	mutex_unlock(&apr_ch->m_lock);
	return 0;
}
EXPORT_SYMBOL(apr_tal_close);

int apr_tal_pd_clean(void)
{
	bridge_name_flush_for_cp_pd("AUDIO");
	return 0;
}
EXPORT_SYMBOL(apr_tal_pd_clean);

static int apr_tal_adsp_probe(struct platform_device *pdev)
{
	struct apr_svc_ch_dev *apr_ch = NULL;

	apr_ch = &apr_svc_ch[APR_DL_SMD][APR_DEST_ADSP][APR_CLIENT_AUDIO];
	apr_ch->handle = pdev;
	apr_ch->channel_state = APR_CH_CONNECTED;
	dev_set_drvdata(&pdev->dev, apr_ch);
	wake_up(&apr_ch->wait);

	return 0;
}

static int apr_tal_adsp_remove(struct platform_device *pdev)
{
	struct apr_svc_ch_dev *apr_ch = dev_get_drvdata(&pdev->dev);

	if (!apr_ch) {
		dev_err(&pdev->dev, "%s: Invalid apr_ch\n", __func__);
		return -EINVAL;
	}

	apr_ch->handle = NULL;
	apr_ch->channel_state = APR_CH_DISCONNECTED;
	dev_set_drvdata(&pdev->dev, NULL);
	return 0;
}

static struct platform_driver apr_tal_adsp_driver = {
	.probe = apr_tal_adsp_probe,
	.remove = apr_tal_adsp_remove,
	.driver = {
		.name = "apr_tal_adsp",
		.owner = THIS_MODULE,
	},
};

static int apr_tal_modem_probe(struct platform_device *pdev)
{
	struct apr_svc_ch_dev *apr_ch = NULL;

	apr_ch = &apr_svc_ch[APR_DL_SMD][APR_DEST_MODEM][APR_CLIENT_AUDIO];
	apr_ch->handle = pdev;
	apr_ch->channel_state = APR_CH_CONNECTED;
	dev_set_drvdata(&pdev->dev, apr_ch);
	wake_up(&apr_ch->wait);

	return 0;
}

static int apr_tal_modem_remove(struct platform_device *pdev)
{
	struct apr_svc_ch_dev *apr_ch = dev_get_drvdata(&pdev->dev);

	if (!apr_ch) {
		dev_err(&pdev->dev, "%s: Invalid apr_ch\n", __func__);
		return -EINVAL;
	}

	apr_ch->handle = NULL;
	apr_ch->channel_state = APR_CH_DISCONNECTED;
	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

static struct platform_driver apr_tal_modem_driver = {
	.probe = apr_tal_modem_probe,
	.remove = apr_tal_modem_remove,
	.driver = {
		.name = "apr_tal_modem",
		.owner = THIS_MODULE,
	},
};

/**
 * apr_tal_int() - Registers rpmsg driver with rpmsg framework.
 *
 * Returns 0 on success and an appropriate error value on failure.
 */

int apr_tal_init()
{
	int i, j, k;

	memset(apr_svc_ch, 0, sizeof(struct apr_svc_ch_dev));
	for (i = 0; i < APR_DL_MAX; i++) {
		for (j = 0; j < APR_DEST_MAX; j++) {
			for (k = 0; k < APR_CLIENT_MAX; k++) {
				init_waitqueue_head(&apr_svc_ch[i][j][k].wait);
				spin_lock_init(&apr_svc_ch[i][j][k].w_lock);
				spin_lock_init(&apr_svc_ch[i][j][k].r_lock);
				mutex_init(&apr_svc_ch[i][j][k].m_lock);
			}
		}
	}

	platform_driver_register(&apr_tal_adsp_driver);
	platform_driver_register(&apr_tal_modem_driver);

	return 0;
}
EXPORT_SYMBOL(apr_tal_init);

/**
 * apr_tal_exit() - De-register rpmsg driver with rpmsg framework.
 */
void apr_tal_exit(void)
{
	platform_driver_unregister(&apr_tal_adsp_driver);
	platform_driver_unregister(&apr_tal_modem_driver);
}
EXPORT_SYMBOL(apr_tal_exit);



