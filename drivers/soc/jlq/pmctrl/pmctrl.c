#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/stat.h>
#include <linux/mutex.h>
#include <soc/jlq/jr510/jlq-bridge.h>
#include "pmctrl.h"

#define DEBUG_UNLOCK_MAGIC   (303)

static struct kobject *kobj;
static struct pmctrl_data *pmctrl = NULL;
static struct mutex pm_mutex;
#define ROOT_DIR "scp"
#define PMCTRL_BUF_SIZE  256
static char showBuffer[PMCTRL_BUF_SIZE];
static int  debug_log;

#define SHM_BASE_ADDRESS (0x98000000)
#define SHM_BASE_LEN	   (0x100000)
#define LOG_BASE_OFFSET	    (0x40000)
#define PM_DEBUG_RESERV_SIZE	(128)

static ssize_t alive_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buff)
{
	int count = 0;

	pmctl_debug("*****pmctrl******\n");

	return count;
}

static ssize_t alive_store(struct  kobject *kobj, struct kobj_attribute *attr,
		const char *buff, size_t count)
{
	unsigned long val;
	struct pmctl_msg *ack;

	if (kstrtoul(buff, 10, &val))
	{
		return -EINVAL;
	}

	if ((val < 0) && (val > 100))
	{
		pmctl_debug("Don't support this command!!!\n");
		return -EINVAL;;
	}

	ack = pmctrl_send_wait_ack_msg(PMCTL_MSG_GET_STATE, val, NULL);

	if (ack->status == SMD_EVENT_SUCCESS)
	{
		printk(KERN_ERR"scp ack id:%d successfully!!\n", ack->id);
		printk(KERN_ERR" data(hex):%x-%x-%x-%x\n", ack->data[0], ack->data[1],
				ack->data[2], ack->data[3]);
		printk(KERN_ERR" data(dig):%d-%d-%d-%d\n", ack->data[0], ack->data[1],
				ack->data[2], ack->data[3]);
	}
	else
	{
		printk(KERN_ERR"scp ack id:%d error!!\n", val);
	}

	return count;
}

static struct kobj_attribute alive_attribute =
{
	.attr =
	{
		.name = "state",
		.mode = 0644,
	},

	.show  = &alive_show,
	.store = &alive_store,
};

void log_copy(char *showbuf, PM_DEBUG_LOG *log, int roll)
{

	int len;

	while (1)
	{
		if (roll == 1)
		{
			len = log->rd_index - log->wr_index;
			if (len <= 0)
			{
				break;
			}
		}
		else
		{
			len = log->wr_index - log->rd_index;
			if (len <= 2)
			{
				break;
			}
		}

		if (len >= PMCTRL_BUF_SIZE)
		{
			memcpy(showbuf, log->log_buf + log->rd_index, PMCTRL_BUF_SIZE);
			showbuf[PMCTRL_BUF_SIZE - 1] = 0;
			log->rd_index += PMCTRL_BUF_SIZE;
		}
		else
		{
			memcpy(showbuf, log->log_buf + log->rd_index, len);
			showbuf[len - 1] = 0;
			log->rd_index += len;
		}
		printk(KERN_ERR"[scp] %s!!\n", showbuf);
	}
}

static ssize_t log_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff)
{
	int count = 0;
	int tmpwrindex;
	PM_DEBUG_LOG pmlog;
	char *buffer;

	if (pmctrl->logEnable == 0)
	{
		printk(KERN_ERR"scp dont support yet.!!\n");
		return count;
	}

	if (debug_log == 0)
	{
		return count;
	}

	memcpy(&pmlog, pmctrl->debug_log, sizeof(PM_DEBUG_LOG));
	pmlog.log_buf = pmctrl->log_buffer;
	buffer		  = showBuffer;

	pmctl_debug("before: wr: %x rd:%x\n", pmlog.wr_index, pmlog.rd_index);

	if (pmlog.rd_index <= pmlog.wr_index)
	{
		log_copy(buffer, &pmlog, 0);
	}
	else
	{
		tmpwrindex     = pmlog.wr_index;
		pmlog.wr_index = pmlog.max_len - 2;
		log_copy(buffer, &pmlog, 0);
		pmlog.wr_index	=  tmpwrindex;
		pmlog.rd_index	=  0;
		log_copy(buffer, &pmlog, 1);
	}

	pmctrl->debug_log->rd_index = pmlog.rd_index;
	pmctl_debug("after: wr: %x rd:%x\n", pmlog.wr_index, pmlog.rd_index);

	return count;
}

static ssize_t log_store(struct  kobject *kobj, struct kobj_attribute *attr,
		const char *buff, size_t count)
{

	unsigned long val;

	if (kstrtoul(buff, 10, &val))
	{
		return -EINVAL;
	}

	if (val == DEBUG_UNLOCK_MAGIC)
	{
		debug_log = 1;
	}
	else
	{
		debug_log = 0;
	}

	return count;
}

static struct kobj_attribute log_attribute =
{
	.attr =
	{
		.name = "log",
		.mode = 0644,
	},

	.show  = &log_show,
	.store = &log_store,
};

static struct attribute *attrs[] =
{
	&alive_attribute.attr,
	&log_attribute.attr,
	NULL,
};

static struct attribute_group attr_group =
{
	.attrs = attrs,
};

struct pmctl_msg *pmctrl_send_wait_ack_msg(unsigned int cmd, unsigned int id, unsigned int *data)
{
	int ret = 0;
	struct pmctl_msg *msg = &pmctrl->snd;
	struct pmctl_msg *ack = &pmctrl->rcv;

	mutex_lock(&pmctrl->lock);

	msg->seq++;
	msg->id = id;
	msg->action = cmd;
	msg->status = 0;
	if (cmd == DDR_AP_MSG)
		msg->data[0] = *data;
	if (cmd == PMCTL_MSG_SET_OPP)
		memcpy(msg->data, data, sizeof(msg->data));

	ack->status = SMD_EVENT_ERR;
	ret = bridge_write(pmctrl->ch, (const char *)msg, sizeof(*msg));

	while (1)
	{
		ret = wait_event_interruptible_timeout(pmctrl->wait,
				atomic_read(&pmctrl->ack), SMD_MSG_TIMEOUT);

		if (ret == 0)
		{
			pmctl_debug("scp ack timeout\n");
			mutex_unlock(&pmctrl->lock);
			return ack;
		}

		atomic_set(&pmctrl->ack, 0);
		ret = bridge_read(pmctrl->ch, (char *)ack, sizeof(*ack));

		if ((ret == sizeof(*ack)) && (ack->seq == msg->seq) &&
			 (ack->id == msg->id))
		{
			break;
		}
	}

	mutex_unlock(&pmctrl->lock);

	return ack;
}
EXPORT_SYMBOL(pmctrl_send_wait_ack_msg);

int pmctrl_send_nowait_ack_msg(unsigned int cmd, unsigned int id, unsigned int *data)
{
	int ret = 0;
	struct pmctl_msg *msg = &pmctrl->snd;

	mutex_lock(&pmctrl->lock);

	msg->seq++;
	msg->id = id;
	msg->action = cmd;
	msg->status = 0;
	ret = bridge_write(pmctrl->ch, (const char *)msg, sizeof(*msg));

	mutex_unlock(&pmctrl->lock);

	return ret;
}
EXPORT_SYMBOL(pmctrl_send_nowait_ack_msg);

static void pmctrl_read_notify(void *priv, unsigned int flags)
{

	if (flags == BG_EV_RX)
	{
		atomic_set(&pmctrl->ack, 1);
		/* wake_up implementation works as memory barrier */
		wake_up_interruptible_sync(&pmctrl->wait);
	}
}

int __init pmctl_init(void)
{
	int ret;
	int probe_status;

	pmctl_debug("%s-->%d enter\n", __func__, __LINE__);
	if (pmctrl && pmctrl->initialized)
	{
		return 0;
	}

	pmctrl = kzalloc(sizeof(struct pmctrl_data), GFP_KERNEL);
	if (!pmctrl)
	{
		probe_status = -ENOMEM;
		return probe_status;
	}

	if (!request_mem_region(SHM_BASE_ADDRESS, SHM_BASE_LEN, "scp ram"))
	{
		pmctl_debug("%s-->%d\n", __func__, __LINE__);
		pmctrl->share_mem_base = 0;
	}
	else
	{
		pmctrl->share_mem_base = ioremap_nocache(SHM_BASE_ADDRESS,
				SHM_BASE_LEN);
	}

	debug_log = 0;
	if (!pmctrl->share_mem_base)
	{
		pmctl_debug("%s-->%d out\n", __func__, __LINE__);
		pmctrl->logEnable = 0;
	}
	else
	{
		pmctrl->debug_log = (PM_DEBUG_LOG *)((char *)pmctrl->share_mem_base + LOG_BASE_OFFSET);
		pmctrl->logEnable = 1;
		if (pmctrl->debug_log->magic != 0xa3a4a5a6)
		{
			pmctl_debug("%s-->%d error\n", __func__, __LINE__);
			pmctrl->debug_log->magic = 0xa3a4a5a6;
			pmctrl->logEnable = 1;
		}
		else
		{
			pmctrl->log_buffer = (char *)((char *)pmctrl->share_mem_base + LOG_BASE_OFFSET +
						PM_DEBUG_RESERV_SIZE);
		}
	}

	mutex_init(&pmctrl->lock);
	init_waitqueue_head(&pmctrl->wait);

	probe_status = bridge_name_open("DDR", 5, &pmctrl->ch,
					pmctrl, pmctrl_read_notify);

	if (probe_status)
	{
		pmctl_debug("pmctrl open bridge channel(%d) error\n", probe_status);
		return 0;
	}

	kobj = kobject_create_and_add(ROOT_DIR, kernel_kobj);
	if (!kobj)
	{
		return -ENOMEM;
	}

	ret = sysfs_create_group(kobj, &attr_group);
	if (ret)
	{
		kobject_put(kobj);
	}

	pmctl_debug("%s-->%d out\n", __func__, __LINE__);

	return ret;
}

void __exit pmctl_exit(void)
{
	if (pmctrl->ch)
	{
		bridge_release(pmctrl->ch);
	}

	if (pmctrl->share_mem_base)
	{
		iounmap(pmctrl->share_mem_base);
		release_mem_region(SHM_BASE_ADDRESS, SHM_BASE_LEN);
	}

	kobject_put(kobj);
	mutex_destroy(&pmctrl->lock);
}
#ifdef MODULE
module_init(pmctl_init);
#else
fs_initcall(pmctl_init);
#endif
module_exit(pmctl_exit);
MODULE_LICENSE("GPL");
