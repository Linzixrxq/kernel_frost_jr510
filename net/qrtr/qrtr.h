/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QRTR_H_
#define __QRTR_H_

#include <linux/types.h>

struct sk_buff;

/* endpoint node id auto assignment */
#define QRTR_EP_NID_AUTO (-1)
#define QRTR_EP_NET_ID_AUTO (1)

#define QRTR_DEL_PROC_MAGIC	0xe111

/**
 * struct qrtr_endpoint - endpoint handle
 * @xmit: Callback for outgoing packets
 *
 * The socket buffer passed to the xmit function becomes owned by the endpoint
 * driver.  As such, when the driver is done with the buffer, it should
 * call kfree_skb() on failure, or consume_skb() on success.
 */
struct qrtr_endpoint {
	int (*xmit)(struct qrtr_endpoint *ep, struct sk_buff *skb);
	/* private: not for endpoint use */
	struct qrtr_node *node;
};

static const struct {
	unsigned int service;
	unsigned int ifilter;
	const char *name;
} common_names[] = {
	{ 0, 0, "Control service" },
	{ 1, 0, "Wireless Data Service" },
	{ 2, 0, "Device Management Service" },
	{ 3, 0, "Network Access Service" },
	{ 4, 0, "Quality Of Service service" },
	{ 5, 0, "Wireless Messaging Service" },
	{ 6, 0, "Position Determination Service" },
	{ 7, 0, "Authentication service" },
	{ 8, 0, "AT service" },
	{ 9, 0, "Voice service" },
	{ 10, 0, "Card Application Toolkit service (v2)" },
	{ 11, 0, "User Identity Module service" },
	{ 12, 0, "Phonebook Management service" },
	{ 13, 0, "QCHAT service" },
	{ 14, 0, "Remote file system service" },
	{ 15, 0, "Test service" },
	{ 16, 0, "Location service (~ PDS v2)" },
	{ 17, 0, "Specific absorption rate service" },
	{ 18, 0, "IMS settings service" },
	{ 19, 0, "Analog to digital converter driver service" },
	{ 20, 0, "Core sound driver service" },
	{ 21, 0, "Modem embedded file system service" },
	{ 22, 0, "Time service" },
	{ 23, 0, "Thermal sensors service" },
	{ 24, 0, "Thermal mitigation device service" },
	{ 25, 0, "Service access proxy service" },
	{ 26, 0, "Wireless data administrative service" },
	{ 27, 0, "TSYNC control service" },
	{ 28, 0, "Remote file system access service" },
	{ 29, 0, "Circuit switched videotelephony service" },
	{ 30, 0, "QTI mobile access point service" },
	{ 31, 0, "IMS presence service" },
	{ 32, 0, "IMS videotelephony service" },
	{ 33, 0, "IMS application service" },
	{ 34, 0, "Coexistence service" },
	{ 36, 0, "Persistent device configuration service" },
	{ 38, 0, "Simultaneous transmit service" },
	{ 39, 0, "Bearer independent transport service" },
	{ 40, 0, "IMS RTP service" },
	{ 41, 0, "RF radiated performance enhancement service" },
	{ 42, 0, "Data system determination service" },
	{ 43, 0, "Subsystem control service" },
	{ 49, 0, "IPA control service" },
	{ 51, 0, "CoreSight remote tracing service" },
	{ 52, 0, "Dynamic Heap Memory Sharing" },
	{ 64, 0, "Service registry locator service" },
	{ 66, 0, "Service registry notification service" },
	{ 69, 0, "ATH10k WLAN firmware service" },
	{ 224, 0, "Card Application Toolkit service (v1)" },
	{ 225, 0, "Remote Management Service" },
	{ 226, 0, "Open Mobile Alliance device management service" },
	{ 312, 0, "QBT1000 Ultrasonic Fingerprint Sensor service" },
	{ 769, 0, "SLIMbus control service" },
	{ 771, 0, "Peripheral Access Control Manager service" },
};

int qrtr_endpoint_register(struct qrtr_endpoint *ep, unsigned int net_id,
			   bool rt);

void qrtr_endpoint_unregister(struct qrtr_endpoint *ep);

int qrtr_endpoint_post(struct qrtr_endpoint *ep, const void *data, size_t len);

void qrtr_ns_init(void);

void qrtr_ns_remove(void);

int qrtr_peek_pkt_size(const void *data);

unsigned int qrtr_get_service_id(unsigned int node_id, unsigned int port_id);
#endif
