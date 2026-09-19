#ifndef _PMCTRL_H__
#define _PMCTRL_H__

#define PMCTL_DEBUG
#ifdef PMCTL_DEBUG
#define pmctl_debug(fmt, ...) printk(KERN_ERR fmt, ##__VA_ARGS__)
#else
#define pmctl_debug(fmt, ...)
#endif

#define SMD_EVENT_SUCCESS (0)
#define SMD_EVENT_ERR	  (-1)

#define SMD_MSG_TIMEOUT	  (500)

struct pmctl_msg
{
	unsigned int seq;
	unsigned int id;
	unsigned int action;
	unsigned int flags;
	unsigned int status;
	unsigned int data[4];
};

typedef struct
{
	int magic;
	int wr_index;
	int rd_index;
	int max_len;
	char *log_buf;
} PM_DEBUG_LOG;

struct pmctrl_data
{
	struct mutex lock;
	struct bridge_channel *ch;
	unsigned int initialized;
	wait_queue_head_t wait;
	atomic_t ack;
	struct pmctl_msg snd;
	struct pmctl_msg rcv;
	int logEnable;
	PM_DEBUG_LOG *debug_log;
	char *log_buffer;
	void __iomem *share_mem_base;
};

enum PMCTRL_MSG_ID
{
	PMCTL_MSG_GET_STATE = 0x1000001,
	DDR_AP_MSG          = 0x1000002,
	PMCTL_MSG_SET_OPP   = 0x1000003,
};

int pmctrl_send_nowait_ack_msg(unsigned int cmd, unsigned int id, unsigned int *data);
struct pmctl_msg *pmctrl_send_wait_ack_msg(unsigned int cmd, unsigned int id, unsigned int *data);

#endif


