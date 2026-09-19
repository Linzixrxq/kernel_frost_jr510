/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/of.h>
#include <linux/nmi.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <soc/jlq/jr510/dmas.h>
#include <linux/of_gpio.h>

#include <linux/ipc_logging.h>
#include <linux/debugfs.h>

#define JLQ_UART_TX_USE_DMA                     0x00000001
#define JLQ_UART_RX_USE_DMA                     0x00000002
#define JLQ_UART_SUPPORT_IRDA                   0x00000004
#define JLQ_UART_SUPPORT_MCTRL                  0x00000008
#define JLQ_UART_DISABLE_MSI                    0x00000010
#define JLQ_UART_USE_WORKQUEUE                  0x00000100
#define JLQ_UART_RX_USE_GPIO_IRQ                0x00010000
#define JLQ_UART_CODEC_RELATIVE                 0x00100000
#define JLQ_UART_QCOM_BT_SUPPORT                0x01000000
#define UART_VALID              0x0
#define UART_INVALID            0x1

#define PMU_WKUP_UART_CTL_INTR_CLR   (1UL << 0)
#define PMU_WKUP_UART_CTL_INTR_STAT  (1UL << 1)

#if defined(CONFIG_SERIAL_JLQ_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#define JLQ_UART_DEBUG
#ifdef JLQ_UART_DEBUG
#define UART_PRINT(fmt, args...) printk(KERN_ERR "[UART]" fmt, ##args)
#else
#define UART_PRINT(fmt, args...) printk(KERN_DEBUG "[UART]" fmt, ##args)
#endif

/*
 * The Leadcore COMIP on-chip UARTs define these bits
 */
#define UART_USR			31		/* UART Status Register. */
#define UART_USR_BUSY			0x01	/* UART Busy. */
#define UART_LSR_RX_FIFOE		0x80	/* receive FIFO error */
#define UART_MCR_SIRE			0x40	/* Enable IRDA */
#define UART_IER_PTIME			0x80	/* Enable write THRE. */
#define UART_FCR_RX_T1			0x00	/* receive FIFO threshold = 1 */
#define UART_FCR_RX_T4			0x40	/* receive FIFO threshold = 4 */
#define UART_FCR_RX_T8			0x80	/* receive FIFO threshold = 8 */
#define UART_FCR_RX_T14			0xc0	/* receive FIFO threshold = 14 */
#define UART_FCR_TX_T0			0x00	/* send FIFO threshold = 0 */
#define UART_FCR_TX_T2			0x10	/* send FIFO threshold = 2 */
#define UART_FCR_TX_T4			0x20	/* send FIFO threshold = 4 */
#define UART_FCR_TX_T8			0x30	/* send FIFO threshold = 8 */

#define UART_JLQ_NR_PORTS			(4)
#define UART_JLQ_TX_DMA_BUF_SIZE	(UART_XMIT_SIZE)
#define UART_JLQ_RX_DMA_BUF_SIZE	(64 * 1024)
#define UART_JLQ_RX_CHECK_TIMEOUT	(20 * 1000000)	/* 20 ms. */
#define UART_JLQ_RX_DATA_TIMEOUT	(2 * 1000000)	/* 2 ms. */
#define UART_JLQ_WAIT_IDLE_TIMEOUT	(100)			/* ms. */
#define UART_JLQ_TX_DMAS_WAIT_IDLE_TIMEOUT	(200)	/* ms. */
#define UART_JLQ_CLK_NORMAL			(115200 * 16)
#define UART_JLQ_CLK_EMULATOR		(115200 * 16)
#define UART_JLQ_CLK_1M				(1000000 * 16UL)
#define UART_JLQ_CLK_2M				(2000000 * 16UL)
#define UART_JLQ_CLK_3M				(3000000 * 16UL)
#define UART_JLQ_CLK_4M				(4000000 * 16UL)

#define IPC_LOG_PWR_PAGES	(10)
#define IPC_LOG_MISC_PAGES	(30)
#define IPC_LOG_TX_RX_PAGES	(30)
#define DATA_BYTES_PER_LINE	(32)
#define GET_DEV_PORT(uport) \
	container_of(uport, struct uart_jlq_port, port)

struct jlq_uart_platform_data {
        unsigned int flags;
        unsigned int rx_gpio;
        unsigned int de_gpio;
        unsigned int re_gpio;
        struct pinctrl *pinctrl;
        struct pinctrl_state *rs485_pin_state;
        int (*power)(void *param, int onoff);
        void (*pin_switch)(int enable);
};
struct uart_jlq_port {
	struct uart_port port;
	struct hrtimer rx_hrtimer;
	unsigned int id;
	unsigned int flags;
	unsigned int ier;
	unsigned char lcr;
	unsigned int mcr;
	unsigned int lsr_break_flag;
	struct clk *uart_clk;
	struct clk *apb_pclk;
	char name[10];
	struct jlq_uart_platform_data *mach;

	unsigned int txdma;
	unsigned int rxdma;
	void *txdma_addr;
	void *rxdma_addr;
	void *rxdma_start_addr;
	dma_addr_t txdma_addr_phys;
	dma_addr_t rxdma_addr_phys;
	u32 rxdma_addr_last;
	struct dmas_ch_cfg dma_rx_cfg;
	struct dmas_ch_cfg dma_tx_cfg;
	int tx_stop;
	int rx_stop;
	struct tasklet_struct tklet;
	struct workqueue_struct *wq;
	struct work_struct buffer_work;
	unsigned int fcr;

	struct dentry *dbg;
	void *ipc_log_tx;
	void *ipc_log_rx;
	void *ipc_log_pwr;
	void *ipc_log_misc;
	void *console_log;
	int bt_wkup_irq;
	void __iomem *bt_wakeup_reg;
};

extern bool btpower_get_onoff_status(void);

#define IPC_LOG_MSG(ctx, x...) do { \
	if (ctx) \
		ipc_log_string(ctx, x); \
} while (0)

static void dump_ipc(void *ipc_ctx, char *prefix, char *string, u64 addr, int size)
{
	char buf[DATA_BYTES_PER_LINE * 2];
	int len = 0;

	if (!ipc_ctx)
		return;
	len = min(size, DATA_BYTES_PER_LINE);
	hex_dump_to_buffer(string, len, DATA_BYTES_PER_LINE, 1, buf,
	sizeof(buf), false);
	ipc_log_string(ipc_ctx, "%s[0x%.10x:%d] : %s", prefix,
		(unsigned int)addr, size, buf);
}

static void jlq_uart_debug_init(struct uart_port *uport, bool console)
{
	struct uart_jlq_port *sport = GET_DEV_PORT(uport);
	char name[30];

	sport->dbg = debugfs_create_dir(dev_name(uport->dev), NULL);
	if (IS_ERR_OR_NULL(sport->dbg))
		dev_err(uport->dev, "Failed to create dbg dir\n");

	if (!console) {
		memset(name, 0, sizeof(name));
		if (!sport->ipc_log_rx) {
			scnprintf(name, sizeof(name), "%s%s",
					dev_name(uport->dev), "_rx");
			sport->ipc_log_rx = ipc_log_context_create(
					IPC_LOG_TX_RX_PAGES, name, 0);
			if (!sport->ipc_log_rx)
				dev_info(uport->dev, "Err in Rx IPC Log\n");
		}
		memset(name, 0, sizeof(name));
		if (!sport->ipc_log_tx) {
			scnprintf(name, sizeof(name), "%s%s",
					dev_name(uport->dev), "_tx");
			sport->ipc_log_tx = ipc_log_context_create(
					IPC_LOG_TX_RX_PAGES, name, 0);
			if (!sport->ipc_log_tx)
				dev_info(uport->dev, "Err in Tx IPC Log\n");
		}
		memset(name, 0, sizeof(name));
		if (!sport->ipc_log_pwr) {
			scnprintf(name, sizeof(name), "%s%s",
					dev_name(uport->dev), "_pwr");
			sport->ipc_log_pwr = ipc_log_context_create(
						IPC_LOG_PWR_PAGES, name, 0);
			if (!sport->ipc_log_pwr)
				dev_info(uport->dev, "Err in Pwr IPC Log\n");
		}
		memset(name, 0, sizeof(name));
		if (!sport->ipc_log_misc) {
			scnprintf(name, sizeof(name), "%s%s",
					dev_name(uport->dev), "_misc");
			sport->ipc_log_misc = ipc_log_context_create(
					IPC_LOG_MISC_PAGES, name, 0);
			if (!sport->ipc_log_misc)
				dev_info(uport->dev, "Err in Misc IPC Log\n");
		}
	} else {
		memset(name, 0, sizeof(name));
		if (!sport->console_log) {
			scnprintf(name, sizeof(name), "%s%s",
						dev_name(uport->dev), "_console");
			sport->console_log = ipc_log_context_create(
						IPC_LOG_MISC_PAGES, name, 0);
		if (!sport->console_log)
			dev_info(uport->dev, "Err in Misc IPC Log\n");
		}
	}
}

#ifdef CONFIG_BRCM_BLUETOOTH
extern int brcm_bt_uart_register(void* host, int on);
#endif
static u8 set_console_options = 0;

static inline unsigned int serial_in(struct uart_jlq_port *up, int offset)
{
	offset <<= 2;
	return readl(up->port.membase + offset);
}

static inline void serial_out(struct uart_jlq_port *up,
			int offset, int value)
{
	offset <<= 2;
	writel(value, up->port.membase + offset);
}

static void serial_jlq_wait_for_idle(struct uart_jlq_port *up)
{
	unsigned long timeout =
		jiffies + (msecs_to_jiffies(UART_JLQ_WAIT_IDLE_TIMEOUT));
	unsigned int fcr_clr =
		UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT;

	while (serial_in(up, UART_USR)) {
		if (time_after(jiffies, timeout)){
			serial_out(up, UART_FCR, fcr_clr);
			UART_PRINT("%d wait for idle timeout MSR = 0x%08x\n",
					up->id, serial_in(up, UART_MSR));
			return;
		}
	}

	if (time_after(jiffies, timeout))
		serial_out(up, UART_FCR, up->fcr);
}

static void serial_jlq_enable_ms(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	if (!(up->flags & (JLQ_UART_SUPPORT_MCTRL |
				JLQ_UART_RX_USE_DMA | JLQ_UART_TX_USE_DMA)))
		return;

	if (up->flags & JLQ_UART_DISABLE_MSI)
		return;

	up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
}

static void serial_jlq_stop_tx(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	if (up->flags & JLQ_UART_TX_USE_DMA)
		up->tx_stop = 1;
	else {
		if (up->ier & UART_IER_THRI) {
			up->ier &= ~UART_IER_THRI;
			serial_out(up, UART_IER, up->ier);
		}
	}
}

static void serial_jlq_stop_rx(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	if (up->flags & JLQ_UART_RX_USE_DMA) {
		jlq_dmas_stop(up->rxdma);
		up->rx_stop = 1;
	} else {
		up->ier &= ~UART_IER_RLSI;
		up->port.read_status_mask &= ~UART_LSR_DR;
		serial_out(up, UART_IER, up->ier);
	}
}

static void serial_jlq_flip_buffer_push_wq(struct work_struct *work)
{
	struct uart_jlq_port *up =
		container_of(work, struct uart_jlq_port, buffer_work);
	struct tty_struct *tty = NULL;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (up && up->port.state)
		tty = up->port.state->port.tty;
	spin_unlock_irqrestore(&up->port.lock, flags);

	if (tty) {
		tty->port->low_latency = 1;
		tty_flip_buffer_push(tty->port);
	}
}

static void serial_jlq_flip_buffer_push(struct uart_jlq_port *up)
{
	struct tty_struct *tty = up->port.state->port.tty;

	if (up->flags & JLQ_UART_USE_WORKQUEUE)
		queue_work(up->wq, &up->buffer_work);
	else
		tty_flip_buffer_push(tty->port);
}

static inline void serial_jlq_receive_chars(struct uart_jlq_port *up, int *status)
{
	unsigned int ch, flag;
	int max_count = 256;

	do {
		ch = serial_in(up, UART_RX);
		flag = TTY_NORMAL;
		up->port.icount.rx++;

		if (unlikely(*status & (UART_LSR_BI | UART_LSR_PE |
					UART_LSR_FE | UART_LSR_OE))) {
			/*
			 * For statistics only
			 */

			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				up->port.icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				//if (uart_handle_break(&up->port))
				//	goto ignore_char;
			} else if (*status & UART_LSR_PE)
				up->port.icount.parity++;
			else if (*status & UART_LSR_FE)
				up->port.icount.frame++;
			if (*status & UART_LSR_OE)
				up->port.icount.overrun++;

			/*
			 * Mask off conditions which should be ignored.
			 */
			*status &= up->port.read_status_mask;

#ifdef CONFIG_SERIAL_JLQ_CONSOLE
			if (up->port.line == up->port.cons->index) {
				/* Recover the break flag from console xmit */
				*status |= up->lsr_break_flag;
				up->lsr_break_flag = 0;
			}
#endif
			if (*status & UART_LSR_BI) {
				flag = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(&up->port, ch))
			goto ignore_char;

		uart_insert_char(&up->port, *status, UART_LSR_OE, ch, flag);

ignore_char:
		*status = serial_in(up, UART_LSR);
	} while ((*status & UART_LSR_DR) && (max_count-- > 0));

	serial_jlq_flip_buffer_push(up);
}

static void serial_jlq_transmit_chars(struct uart_jlq_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count;

	if (up->port.x_char) {
		serial_out(up, UART_TX, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		serial_jlq_stop_tx(&up->port);
		return;
	}

	count = up->port.fifosize;
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		if (uart_circ_empty(xmit)) {
			serial_jlq_stop_tx(&up->port);
			break;
		}
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		serial_jlq_stop_tx(&up->port);
}

static void serial_jlq_start_tx(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	if (up->flags & JLQ_UART_TX_USE_DMA) {
		up->tx_stop = 0;
		tasklet_schedule(&up->tklet);
	} else {
		if (!(up->ier & UART_IER_THRI)) {
			up->ier |= UART_IER_THRI;
			serial_out(up, UART_IER, up->ier);
		}
	}
}

static inline void serial_jlq_check_modem_status(struct uart_jlq_port *up)
{
	int status;
	status = serial_in(up, UART_MSR);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if ((up->ier & UART_IER_MSI) &&
		!(up->mcr & UART_MCR_AFE) &&
		(status & UART_MSR_DCTS))
		uart_handle_cts_change(&up->port, status & UART_MSR_CTS);

	wake_up_interruptible(&up->port.state->port.delta_msr_wait);
}

static unsigned int serial_jlq_dma_rx(struct uart_jlq_port *up)
{
	struct tty_struct *tty = up->port.state->port.tty;
	unsigned char *end_addr;
	unsigned char *transfer_addr;
	unsigned char *buf;
	unsigned int data_len = 0;
	unsigned int count;
	unsigned int packet_size;
	u32 addr_t;
	u32 addr;
	int ret;

	ret = jlq_dmas_get(up->rxdma, &addr);
	if (!ret && (addr != up->rxdma_addr_last)) {
		while (addr != up->rxdma_addr_last) {
			addr_t = (up->rxdma_addr_last -
					(u32)up->rxdma_addr_phys);
			buf = (unsigned char*)up->rxdma_addr + addr_t;

			if (addr < up->rxdma_addr_last) {
				count = UART_JLQ_RX_DMA_BUF_SIZE - addr_t;
				up->rxdma_addr_last = (u32)up->rxdma_addr_phys;
			} else {
				count = addr - up->rxdma_addr_last;
				up->rxdma_addr_last = addr;
			}

			transfer_addr = buf;
			end_addr = buf + count;
			while (transfer_addr != end_addr) {
				if ((end_addr - transfer_addr) >
					PAGE_SIZE)
					packet_size = PAGE_SIZE;
				else
					packet_size = end_addr - transfer_addr;

				tty_insert_flip_string(tty->port,
							transfer_addr,
							packet_size);
				transfer_addr += packet_size;
			}
			up->port.icount.rx += count;
			data_len += count;
			if (up->flags & JLQ_UART_QCOM_BT_SUPPORT)
				dump_ipc(up->ipc_log_rx, "DMA Rx", (char *)buf,
					 0, count);//add ipc log
		}

		tty_flip_buffer_push(tty->port);
	}

	return data_len;
}

static inline void serial_jlq_dma_tx_irq(int irq, int type, void *dev_id)
{
	struct uart_jlq_port *up = dev_id;
	struct circ_buf *xmit = &up->port.state->xmit;

	/* if tx stop, stop transmit DMA and return */
	if (up->tx_stop)
		return;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (!uart_circ_empty(xmit))
		tasklet_schedule(&up->tklet);
}

static inline void serial_jlq_dma_rx_irq(int irq, int type, void *dev_id)
{
	struct uart_jlq_port *up = dev_id;
	unsigned int data_len = 0;

	if (!up->rx_stop && (type & DMAS_INT_HBLK_FLUSH)) {
		data_len = serial_jlq_dma_rx(up);
		if (!hrtimer_callback_running(&up->rx_hrtimer)) {
			if (data_len > 0)
				hrtimer_start(&up->rx_hrtimer,
					ktime_set(0, UART_JLQ_RX_DATA_TIMEOUT),
					HRTIMER_MODE_REL_PINNED);
			else
				hrtimer_start(&up->rx_hrtimer,
					ktime_set(0, UART_JLQ_RX_CHECK_TIMEOUT),
					HRTIMER_MODE_REL_PINNED);
		}
	}
}

static void serial_jlq_config_dma_rx(struct uart_jlq_port *up)
{
	struct dmas_ch_cfg *cfg = &up->dma_rx_cfg;

	cfg->flags = DMAS_CFG_ALL;
	cfg->block_size = UART_JLQ_RX_DMA_BUF_SIZE;
	cfg->src_addr = (unsigned int)(up->port.mapbase + UART_RX);
	cfg->dst_addr = (unsigned int)up->rxdma_addr_phys;
	cfg->priority = DMAS_CH_PRI_DEFAULT;
	cfg->bus_width = DMAS_DEV_WIDTH_8BIT;
	cfg->rx_trans_type = DMAS_TRANS_WRAP;
	cfg->rx_timeout = 50;
	cfg->irq_en = DMAS_INT_HBLK_FLUSH;
	cfg->irq_handler = serial_jlq_dma_rx_irq;
	cfg->irq_data = up;
	jlq_dmas_config(up->rxdma, cfg);
}

static void serial_jlq_config_dma_tx(struct uart_jlq_port *up)
{
	struct dmas_ch_cfg *cfg = &up->dma_tx_cfg;

	cfg->flags = DMAS_CFG_ALL;
	cfg->block_size = UART_JLQ_TX_DMA_BUF_SIZE;
	cfg->src_addr = (unsigned int)up->txdma_addr_phys;
	cfg->dst_addr = (unsigned int)(up->port.mapbase + UART_TX);
	cfg->priority = DMAS_CH_PRI_DEFAULT;
	cfg->bus_width = DMAS_DEV_WIDTH_8BIT;
	cfg->tx_trans_mode = DMAS_TRANS_NORMAL;
	cfg->tx_fix_value = 0;
	cfg->tx_block_mode = DMAS_SINGLE_BLOCK;
	cfg->irq_en = DMAS_INT_DONE;
	cfg->irq_handler = serial_jlq_dma_tx_irq;
	cfg->irq_data = up;
	jlq_dmas_config(up->txdma, cfg);
}

static void serial_jlq_start_dma_rx(struct uart_jlq_port *up)
{
	struct dmas_ch_cfg *cfg = &up->dma_rx_cfg;

	cfg->flags = DMAS_CFG_BLOCK_SIZE;
	cfg->block_size = UART_JLQ_RX_DMA_BUF_SIZE;
	jlq_dmas_config(up->rxdma, cfg);
	jlq_dmas_start(up->rxdma);
}

static void serial_jlq_start_dma_tx(struct uart_jlq_port *up, int count)
{
	struct dmas_ch_cfg *cfg = &up->dma_tx_cfg;

	cfg->flags = DMAS_CFG_BLOCK_SIZE;
	cfg->block_size = count;
	jlq_dmas_config(up->txdma, cfg);
	jlq_dmas_start(up->txdma);
}

static enum hrtimer_restart serial_jlq_rx_hrtimer_fn(struct hrtimer *hrtimer)
{
	struct uart_jlq_port *up = container_of(hrtimer,
					struct uart_jlq_port, rx_hrtimer);
	unsigned int data_len = 0;

	if (!up->rx_stop && !jlq_dmas_state(up->rxdma)) {
		UART_PRINT("Invalid uart%d dma%d state.\n", up->id, up->rxdma);
		serial_jlq_start_dma_rx(up);
		goto restart_timer;
	}

	data_len = serial_jlq_dma_rx(up);

	if (up->rx_stop)
		return HRTIMER_NORESTART;

restart_timer:
	if (data_len > 0) {
		hrtimer_forward_now(hrtimer,
			ktime_set(0, UART_JLQ_RX_DATA_TIMEOUT));
		return HRTIMER_RESTART;
	} else {
		if (up->flags & JLQ_UART_RX_USE_GPIO_IRQ) {
			enable_irq(gpio_to_irq(up->mach->rx_gpio));
			return HRTIMER_NORESTART;
		} else {
			hrtimer_forward_now(hrtimer,
				ktime_set(0, UART_JLQ_RX_CHECK_TIMEOUT));
			return HRTIMER_RESTART;
		}
	}
}

static irqreturn_t serial_jlq_rx_gpio_irq(int irq, void *dev_id)
{
	struct uart_jlq_port *up = dev_id;

	disable_irq_nosync(gpio_to_irq(up->mach->rx_gpio));

	if (!hrtimer_callback_running(&up->rx_hrtimer))
		hrtimer_start(&up->rx_hrtimer,
			ktime_set(0, UART_JLQ_RX_DATA_TIMEOUT),
			HRTIMER_MODE_REL_PINNED);

	return IRQ_HANDLED;
}

static inline irqreturn_t serial_jlq_irq(int irq, void *dev_id)
{
	struct uart_jlq_port *up = dev_id;
	unsigned int iir, lsr;
	unsigned long flags;

	iir = serial_in(up, UART_IIR) & 0xf;
	if (iir == UART_IIR_NO_INT)
		return IRQ_NONE;

	spin_lock_irqsave(&up->port.lock, flags);

	if (iir == UART_IIR_BUSY)
		serial_in(up, UART_USR);

	lsr = serial_in(up, UART_LSR);
	if (!(up->flags & JLQ_UART_RX_USE_DMA)) {
		if ((lsr & UART_LSR_DR))
			serial_jlq_receive_chars(up, &lsr);
#if !defined(CONFIG_JLQ_EVB) && !defined(CONFIG_JLQ_ANVIZ)
		/* Avoid lc1860 wrong rx timeout interrupt bug. */
		if ((iir & UART_IIR_RX_TIMEOUT) && !(lsr & UART_LSR_DR))
			serial_in(up, UART_RX);
#endif
	}

	if (up->flags & JLQ_UART_SUPPORT_MCTRL)
		serial_jlq_check_modem_status(up);

	if (!(up->flags & JLQ_UART_TX_USE_DMA)) {
		if (lsr & UART_LSR_THRE) {
			serial_jlq_transmit_chars(up);
		}
	}

	uart_unlock_and_check_sysrq(&up->port, flags);
	return IRQ_HANDLED;
}

static unsigned int serial_jlq_tx_empty(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	if (up->flags & JLQ_UART_TX_USE_DMA)
		ret = !jlq_dmas_state(up->txdma);
	else
		ret = serial_in(up, UART_LSR) & UART_LSR_THRE;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return (ret ? TIOCSER_TEMT : 0);
}

static unsigned int serial_jlq_get_mctrl(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;
	unsigned char status;
	unsigned int ret = 0;

	if (!(up->flags & JLQ_UART_SUPPORT_MCTRL))
		return 0;

	status = serial_in(up, UART_MSR);

	if ((status & UART_MSR_CTS) || (up->mcr & UART_MCR_AFE))
		ret |= TIOCM_CTS;

	return ret;
}

static void serial_jlq_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;
	unsigned int val = 0;

	if (!(up->flags & JLQ_UART_SUPPORT_MCTRL))
		return;

	val = serial_in(up, UART_MCR);

	if ((mctrl & TIOCM_RTS))
		val |= (UART_MCR_AFE | UART_MCR_RTS);
	else
		val &= ~(UART_MCR_AFE | UART_MCR_RTS);

	serial_out(up, UART_MCR, val);
}

static void serial_jlq_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;
	unsigned long flags;
	unsigned long timeout;

	if (!(up->flags & JLQ_UART_SUPPORT_MCTRL))
		return;

	timeout = jiffies +
		(msecs_to_jiffies(UART_JLQ_TX_DMAS_WAIT_IDLE_TIMEOUT));

	if (up->flags & JLQ_UART_TX_USE_DMA) {
		while (jlq_dmas_state(up->txdma) > 0) {
			if (time_after(jiffies, timeout)) {
				UART_PRINT("%d break ctl wait for tx dma",
						up->id);
				UART_PRINT("idle timeout. MSR = 0x%08x\n",
						serial_in(up, UART_MSR));
				break;
			}
		}
	}

	serial_jlq_wait_for_idle(up);

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	serial_out(up, UART_LCR, up->lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void serial_jlq_task_action(unsigned long data)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)data;
	struct circ_buf *xmit = &up->port.state->xmit;
	unsigned char *tmp = up->txdma_addr;
	unsigned long flags;
	int count = 0, c;

	/* if the tx is stop, just return. */
	if (up->tx_stop || !tmp)
		return;

	if (jlq_dmas_state(up->txdma) > 0)
		return;

	spin_lock_irqsave(&up->port.lock, flags);
	while (1) {
		c = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
		if (c <= 0)
			break;

		memcpy(tmp, xmit->buf + xmit->tail, c);
		xmit->tail = (xmit->tail + c) & (UART_XMIT_SIZE - 1);
		tmp += c;
		count += c;
		up->port.icount.tx += c;
	}
	spin_unlock_irqrestore(&up->port.lock, flags);

	if (count) {
		up->tx_stop = 0;
		if (up->flags & JLQ_UART_QCOM_BT_SUPPORT)
			dump_ipc(up->ipc_log_tx, "DMA Tx", (char *)up->txdma_addr,
				 0, count);// add ipc log
		serial_jlq_start_dma_tx(up, count);
	}
}

static int serial_jlq_rx_gpio_irq_init(struct uart_jlq_port *up)
{
	int ret;
	char irq_name[20];
	unsigned int rx_gpio = up->mach->rx_gpio;

	ret = gpio_request(rx_gpio, up->name);
	if (ret < 0) {
		UART_PRINT("Failed to request UART%d GPIO.\n", up->id);
		return ret;
	}

	ret = gpio_direction_input(rx_gpio);
	if (ret < 0) {
		UART_PRINT("Failed to set UART%d GPIO.\n", up->id);
		goto exit;
	}

	/* Request irq. */
	sprintf(irq_name, "uart%d rx irq", up->id);
	ret = request_irq(gpio_to_irq(rx_gpio), serial_jlq_rx_gpio_irq,
			  IRQF_TRIGGER_LOW, irq_name, up);
	if (ret) {
		UART_PRINT("%d IRQ already in use.\n", up->id);
		goto exit;
	}

	/*
	 * Note : If RX GPIO use the irq function,
	 * it must disable the debounce opetation.
	 */
	gpio_set_debounce(rx_gpio, 0);

	return 0;

exit:
	gpio_free(rx_gpio);
	return ret;
}

static int serial_jlq_rx_gpio_irq_uninit(struct uart_jlq_port *up)
{
	unsigned int rx_gpio = up->mach->rx_gpio;

	/* Free irq. */
	disable_irq_nosync(gpio_to_irq(rx_gpio));
	free_irq(gpio_to_irq(rx_gpio), up);
	gpio_free(rx_gpio);

	return 0;
}

static int serial_jlq_dma_init(struct uart_jlq_port *up)
{
	int ret = -ENOMEM;

	if (up->flags & JLQ_UART_TX_USE_DMA) {
		jlq_dmas_request(up->name, up->txdma);
		if (!up->txdma_addr) {
			up->txdma_addr = dma_alloc_coherent(up->port.dev,
						UART_JLQ_TX_DMA_BUF_SIZE,
						&(up->txdma_addr_phys),
						GFP_KERNEL);
			if (!up->txdma_addr)
				goto txdma_err_alloc;
		}

		up->tx_stop = 0;
		serial_jlq_config_dma_tx(up);
		tasklet_init(&up->tklet,
			serial_jlq_task_action, (unsigned long)up);
	}

	if (up->flags & JLQ_UART_RX_USE_DMA) {
		jlq_dmas_request(up->name, up->rxdma);
		up->rxdma_addr = up->rxdma_start_addr;
		if (!up->rxdma_addr)
			goto rxdma_err_alloc;
		up->rxdma_addr_last = up->rxdma_addr_phys;

		serial_jlq_config_dma_rx(up);
		serial_jlq_start_dma_rx(up);

		hrtimer_init(&up->rx_hrtimer, CLOCK_MONOTONIC,
			HRTIMER_MODE_REL_PINNED);
		up->rx_hrtimer.function = serial_jlq_rx_hrtimer_fn;

		if (up->flags & JLQ_UART_RX_USE_GPIO_IRQ) {
			ret = serial_jlq_rx_gpio_irq_init(up);
			if (ret)
				goto rxirq_err_init;
		}

		up->rx_stop = 0;
	}

	return 0;

rxirq_err_init:
	up->rxdma_addr = NULL;
rxdma_err_alloc:
	dma_free_coherent(up->port.dev, UART_JLQ_TX_DMA_BUF_SIZE,
		up->txdma_addr, up->txdma_addr_phys);
	up->txdma_addr = NULL;
	tasklet_kill(&up->tklet);
txdma_err_alloc:
	return ret;
}

static void serial_jlq_dma_uninit(struct uart_jlq_port *up)
{
	if (up->flags & JLQ_UART_TX_USE_DMA) {
		tasklet_kill(&up->tklet);

		up->tx_stop = 1;
		jlq_dmas_stop(up->txdma);
		jlq_dmas_free(up->txdma);
		if (up->txdma_addr != NULL) {
			dma_free_coherent(up->port.dev, UART_JLQ_TX_DMA_BUF_SIZE,
				up->txdma_addr, up->txdma_addr_phys);
			up->txdma_addr = NULL;
		}
	}

	if (up->flags & JLQ_UART_RX_USE_DMA) {
		up->rx_stop = 1;
		jlq_dmas_stop(up->rxdma);
		jlq_dmas_free(up->rxdma);

		if (up->flags & JLQ_UART_RX_USE_GPIO_IRQ)
			serial_jlq_rx_gpio_irq_uninit(up);

		hrtimer_cancel(&up->rx_hrtimer);
		up->rxdma_addr = NULL;
	}
}

static int serial_jlq_startup(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;
	int ret;

	/*
	 * Enable UART power.
	 */
	if (up->mach && up->mach->power)
		up->mach->power(port, 1);

	/*
	 * Enable UART clock.
	 */
	clk_prepare_enable(up->apb_pclk);
	clk_prepare_enable(up->uart_clk);

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO |
		   UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	serial_out(up, UART_FCR, 0);
	up->fcr = 0;
	/*
	 * Allocate the IRQ
	 */
	serial_out(up, UART_IER, 0);
	ret = request_irq(up->port.irq, serial_jlq_irq,
			0, up->name, up);
	if (ret) {
		UART_PRINT("Failed to uart%d irq %d\n", up->id, up->port.irq);
		return ret;
	}

	/*
	 * Set the modem ctrl.
	 */
	up->mcr = 0;
	if (up->flags & JLQ_UART_SUPPORT_MCTRL)
		up->mcr |= UART_MCR_AFE | UART_MCR_RTS;
	if (up->flags & JLQ_UART_SUPPORT_IRDA)
		up->mcr |= UART_MCR_SIRE;

	/*
	 * Clear the interrupt registers.
	 */
	(void)serial_in(up, UART_LSR);
	(void)serial_in(up, UART_RX);
	(void)serial_in(up, UART_IIR);
	(void)serial_in(up, UART_MSR);

	/*
	 * Now, initialize the UART
	 */
	serial_out(up, UART_LCR, UART_LCR_WLEN8);

	/*
	 * Initialize the DMA.
	 */
	ret = serial_jlq_dma_init(up);
	if (ret)
		return ret;

	/*
	 * Finally, enable interrupts.	Note: Modem status interrupts
	 * are set via set_termios(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 */
	up->ier = UART_IER_RLSI;
	if (!(up->flags & JLQ_UART_RX_USE_DMA))
		up->ier |= UART_IER_RDI;
	serial_out(up, UART_IER, up->ier);

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void)serial_in(up, UART_LSR);
	(void)serial_in(up, UART_RX);
	(void)serial_in(up, UART_IIR);
	(void)serial_in(up, UART_MSR);

	return 0;
}

static void serial_jlq_shutdown(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	/*
	 * Disable interrupts from this port
	 */
	up->ier = 0;
	serial_out(up, UART_IER, 0);

	/*
	 * Free the IRQ
	 */
	free_irq(up->port.irq, up);

	/*
	 * Disable break condition and FIFOs
	 */
	serial_out(up, UART_LCR, serial_in(up, UART_LCR) & ~UART_LCR_SBC);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO |
		   UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	serial_out(up, UART_FCR, 0);
	up->fcr = 0;

	(void)serial_in(up, UART_LSR);
	(void)serial_in(up, UART_RX);
	(void)serial_in(up, UART_IIR);
	(void)serial_in(up, UART_MSR);

	serial_jlq_dma_uninit(up);

	/*
	 * Disable UART clock.
	 */
	clk_disable_unprepare(up->uart_clk);
	clk_disable_unprepare(up->apb_pclk);

	/*
	 * Disable UART power.
	 */
	if (up->mach && up->mach->power)
		up->mach->power(port, 0);
}

static void
serial_jlq_set_termios(struct uart_port *port, struct ktermios *termios,
			 struct ktermios *old)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;
	unsigned char cval = 0;
	unsigned long flags;
	unsigned int baud, quot;

	if (set_console_options) {
		set_console_options = 0;
	}

	if ((termios->c_cflag == 0) && up->port.cons &&
			(up->port.line == up->port.cons->index))
		termios->c_cflag = B115200 | CS8 | CSTOPB;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		cval = UART_LCR_WLEN5;
		break;
	case CS6:
		cval = UART_LCR_WLEN6;
		break;
	case CS7:
		cval = UART_LCR_WLEN7;
		break;
	default:
		cval = UART_LCR_WLEN8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		cval |= UART_LCR_STOP;
	if (termios->c_cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(termios->c_cflag & PARODD))
		cval |= UART_LCR_EPAR;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, UART_JLQ_CLK_4M / 16);

	quot = uart_get_divisor(port, baud);

	if (up->flags & JLQ_UART_RX_USE_DMA)
		up->fcr = UART_FCR_ENABLE_FIFO | UART_FCR_RX_T8;
	else
		up->fcr = UART_FCR_ENABLE_FIFO
			  | UART_FCR_RX_T8
			  | UART_FCR_TX_T8;

	/* Clear the FIFO buffers of console. */
	if (up->port.cons && (up->port.cons->index == up->port.line))
		serial_out(up, UART_FCR, up->fcr | UART_FCR_CLEAR_RCVR |
				UART_FCR_CLEAR_XMIT);

	/*
	 * Before configuring uart, make sure its status is not busy.
	 */
	serial_jlq_wait_for_idle(up);

	/*
	 * MCR-based auto flow control.
	 */
	if (termios->c_cflag & CRTSCTS)
		up->mcr |= UART_MCR_AFE;

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);
	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	up->ier &= ~UART_IER_MSI;
	if ((up->flags & JLQ_UART_SUPPORT_MCTRL)
		&& !(up->flags & JLQ_UART_DISABLE_MSI)
		&& UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->ier |= UART_IER_MSI;

	serial_out(up, UART_IER, up->ier);

	if (up->id != 1) { //set uart regs except com_uart
		serial_out(up, UART_LCR, cval | UART_LCR_DLAB); /* set DLAB */
		serial_out(up, UART_DLL, quot & 0xff);	/* LS of divisor */
		serial_out(up, UART_DLM, quot >> 8);	/* MS of divisor */
		serial_out(up, UART_LCR, cval); /* reset DLAB */
		/*delay (2*DIV*16) uart_mclk after bandrate changed as spec acquired*/
		udelay(2UL * 1000 * 1000 / baud + 1);

		up->lcr = cval;	/* Save LCR */
		serial_out(up, UART_MCR, up->mcr);
		serial_out(up, UART_FCR, up->fcr);
	}
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void serial_jlq_pm(struct uart_port *port, unsigned int state,
		unsigned int oldstate)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	if (!state) {
		clk_prepare_enable(up->uart_clk);
		clk_prepare_enable(up->apb_pclk);
	} else {
		clk_disable_unprepare(up->apb_pclk);
		clk_disable_unprepare(up->uart_clk);
	}
}

static void serial_jlq_release_port(struct uart_port *port)
{
}

static int serial_jlq_request_port(struct uart_port *port)
{
	return 0;
}

static void serial_jlq_config_port(struct uart_port *port, int flags)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	up->port.type = PORT_16550;
}

static int
serial_jlq_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}

static const char *serial_jlq_type(struct uart_port *port)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	return up->name;
}

static struct uart_jlq_port serial_jlq_ports[UART_JLQ_NR_PORTS];
static struct uart_driver serial_jlq_reg;
struct uart_ops serial_jlq_pops = {
	.tx_empty = serial_jlq_tx_empty,
	.set_mctrl = serial_jlq_set_mctrl,
	.get_mctrl = serial_jlq_get_mctrl,
	.stop_tx = serial_jlq_stop_tx,
	.start_tx = serial_jlq_start_tx,
	.stop_rx = serial_jlq_stop_rx,
	.enable_ms = serial_jlq_enable_ms,
	.break_ctl = serial_jlq_break_ctl,
	.startup = serial_jlq_startup,
	.shutdown = serial_jlq_shutdown,
	.set_termios = serial_jlq_set_termios,
	.pm = serial_jlq_pm,
	.type = serial_jlq_type,
	.release_port = serial_jlq_release_port,
	.request_port = serial_jlq_request_port,
	.config_port = serial_jlq_config_port,
	.verify_port = serial_jlq_verify_port,
};

static struct jlq_uart_platform_data *serial_jlq_dt_to_pdata(
		struct platform_device *pdev)
{
	struct jlq_uart_platform_data *pdata;

	dev_dbg(&pdev->dev, "dt_to_pdata\n");

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev,
				"allocate memory for platform data failed\n");
		return ERR_PTR(-ENOMEM);
	}

	if (of_property_read_bool(pdev->dev.of_node, "tx_use_dma"))
		pdata->flags |= JLQ_UART_TX_USE_DMA;
	if (of_property_read_bool(pdev->dev.of_node, "rx_use_dma"))
		pdata->flags |= JLQ_UART_RX_USE_DMA;
	if (of_property_read_bool(pdev->dev.of_node, "support_irda"))
		pdata->flags |= JLQ_UART_SUPPORT_IRDA;
	if (of_property_read_bool(pdev->dev.of_node, "support_mctrl"))
		pdata->flags |= JLQ_UART_SUPPORT_MCTRL;
	if (of_property_read_bool(pdev->dev.of_node, "disable_msi"))
		pdata->flags |= JLQ_UART_DISABLE_MSI;
	if (of_property_read_bool(pdev->dev.of_node, "use_workqueue"))
		pdata->flags |= JLQ_UART_USE_WORKQUEUE;
	if (of_property_read_bool(pdev->dev.of_node, "rx_use_gpio_irq"))
		pdata->flags |= JLQ_UART_RX_USE_GPIO_IRQ;
	if (of_property_read_bool(pdev->dev.of_node, "support_qcom_bt"))
		pdata->flags |= JLQ_UART_QCOM_BT_SUPPORT;
	of_property_read_u32(pdev->dev.of_node, "rx_gpio", &pdata->rx_gpio);

#ifdef CONFIG_BRCM_BLUETOOTH
	if (of_property_read_bool(pdev->dev.of_node, "brcm_bt_callback"))
		pdata->power = brcm_bt_uart_register;
#endif

	return pdata;
}

static int __init serial_jlq_init_port(struct uart_jlq_port *sport,
					  struct platform_device *dev)
{
	struct uart_port *port = &sport->port;
	struct jlq_uart_platform_data *data = dev->dev.platform_data;
	struct resource *mmres, *irqres;
	void __iomem *base;
	int id;

	if (dev->dev.of_node) {
		data = serial_jlq_dt_to_pdata(dev);
		dev->dev.platform_data = data;
		id = of_alias_get_id(dev->dev.of_node, "serial");
		if (id < 0) {
			dev_err(&dev->dev, "failed to get alias id (%d)\n", id);
			return id;
		}
		dev->id = id;

		sport->rxdma = of_get_named_dmas_channel(dev->dev.of_node,
				"rx_dma_channel", 0);
		sport->txdma = of_get_named_dmas_channel(dev->dev.of_node,
				"tx_dma_channel", 0);
		if (sport->rxdma < 0 || sport->txdma < 0) {
			dev_err(&dev->dev, "failed to get dmas channel\n");
			return -EFAULT;
		}

		of_property_read_u32(dev->dev.of_node, "fifo_size",
				&port->fifosize);
	} else {
		struct resource *dmarxres, *dmatxres;
		dmarxres = platform_get_resource(dev, IORESOURCE_DMA, 0);
		dmatxres = platform_get_resource(dev, IORESOURCE_DMA, 1);
		if (!dmarxres || !dmatxres)
			return -ENODEV;
		sport->txdma = dmatxres->start;
		sport->rxdma = dmarxres->start;
#ifdef CONFIG_CPU_LC1860
		if (dev->id == 3)
			port->fifosize = 16;
		else
			port->fifosize = 32;
#else
		port->fifosize = 32;
#endif
		id = dev->id;
	}

	mmres = platform_get_resource(dev, IORESOURCE_MEM, 0);
	irqres = platform_get_resource(dev, IORESOURCE_IRQ, 0);
	if (!mmres || !irqres)
		return -ENODEV;

	base = devm_ioremap(&dev->dev, mmres->start,
			    resource_size(mmres));
	if (!base) {
		return -ENOMEM;
	}

	port->type = PORT_16550;
	port->iotype = UPIO_MEM;
	port->flags = UPF_BOOT_AUTOCONF;
	port->ops = &serial_jlq_pops;
	port->line = id;
	port->dev = &dev->dev;
	port->irq = irqres->start;
	port->mapbase = mmres->start;
	port->membase = base;
	sport->txdma_addr = NULL;
	sport->rxdma_addr = NULL;
	sport->rxdma_start_addr = NULL;
	sport->txdma_addr_phys = 0;
	sport->rxdma_addr_phys = 0;
	sport->rxdma_addr_last = 0;
	sport->tx_stop = 1;
	sport->rx_stop = 1;
	sport->mach = data;
	sport->id = id;

	if (data)
		sport->flags = data->flags;
	else
		sport->flags = 0;

	sprintf(sport->name, "uart%d", sport->id);
	sport->uart_clk = devm_clk_get(port->dev, "uart_clk");
	if (IS_ERR(sport->uart_clk)) {
		dev_err(port->dev, "uart clock not available\n");
	}

	sport->apb_pclk = devm_clk_get(port->dev, "apb_pclk");
	if (IS_ERR(sport->apb_pclk)) {
		dev_err(port->dev, "abp clock not available\n");
	}

	if (sport->id != 1) //set uartclk except com_uart
		port->uartclk = clk_get_rate(sport->uart_clk);

#if defined(CONFIG_JLQ_EMULATOR) || defined(CONFIG_JLQ_HAPS)
	return 0;
#else
	if (IS_ERR(sport->uart_clk))
		return PTR_ERR(sport->uart_clk);
	if (IS_ERR(sport->apb_pclk))
		return PTR_ERR(sport->apb_pclk);
#endif

	return 0;
}

#ifdef CONFIG_SERIAL_JLQ_CONSOLE

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct uart_jlq_port *up)
{
	unsigned int status;
	unsigned long delay;

	/* Wait up to 10ms for the character(s) to be sent. */
	delay = jiffies;
	do {
		status = serial_in(up, UART_LSR);

		if (status & UART_LSR_BI)
			up->lsr_break_flag = UART_LSR_BI;

		if (time_after(jiffies, delay + 1))
			break;
	} while ((status & BOTH_EMPTY) != BOTH_EMPTY);

	/* Wait up to 1s for flow control if necessary */
	delay = jiffies;
	if (up->port.flags & UPF_CONS_FLOW) {
		while ((serial_in(up, UART_MSR) & UART_MSR_CTS) == 0) {
			if (time_after(jiffies, delay + 1*HZ))
				break;
			}
	}
}

static void serial_jlq_console_putchar(struct uart_port *port, int ch)
{
	struct uart_jlq_port *up = (struct uart_jlq_port *)port;

	wait_for_xmitr(up);
	serial_out(up, UART_TX, ch);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void
serial_jlq_console_write(struct console *co, const char *s,
			   unsigned int count)
{
	struct uart_jlq_port *up = &(serial_jlq_ports[co->index]);
	struct uart_port *port = &up->port;
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	touch_nmi_watchdog();
#if defined(CONFIG_SERIAL_CORE_CONSOLE) || defined(SUPPORT_SYSRQ)
	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
#else
	if (oops_in_progress)
#endif
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	/*
	 *	First save the IER then disable the interrupts
	 */
	ier = serial_in(up, UART_IER);
	serial_out(up, UART_IER, 0);

	uart_console_write(&up->port, s, count, serial_jlq_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up);
	serial_out(up, UART_IER, ier);

	/*
	 *	The receive handling will happen properly because the
	 *	receive ready bit will still be set; it is not cleared
	 *	on read.  However, modem control will not, we must
	 *	call it if we have saved something in the saved flags
	 *	while processing with interrupts off.
	 */

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);

}

static int __init serial_jlq_console_setup(struct console *co, char *options)
{
	struct uart_jlq_port *up;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret = 0;

	if (co->index == -1 || co->index >= serial_jlq_reg.nr)
		co->index = 0;

	up = &(serial_jlq_ports[co->index]);
	if (!up->port.iobase && !up->port.membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	set_console_options = 1;

	ret = uart_set_options(&up->port, co, baud, parity, bits, flow);

	return ret;
}

static struct console serial_jlq_console = {
	.name = "jlqttyS",
	.write = serial_jlq_console_write,
	.device = uart_console_device,
	.setup = serial_jlq_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &serial_jlq_reg,
};

#define JLQ_CONSOLE	&serial_jlq_console
#else
#define JLQ_CONSOLE	NULL
#endif

static struct uart_driver serial_jlq_reg = {
	.owner = THIS_MODULE,
	.driver_name = "jlq-serial",
	.dev_name = "jlqttyS",
	.major = TTY_MAJOR,
	.minor = 100,
	.nr = UART_JLQ_NR_PORTS,
	.cons = JLQ_CONSOLE,
};

static irqreturn_t serial_jlq_btwakeup_isr(int irq, void *dev_id)
{
	struct uart_jlq_port *up = dev_id;

	IPC_LOG_MSG(up->ipc_log_misc, "enter uart1 wakeup isr\n");

	if (up->bt_wakeup_reg) {
		IPC_LOG_MSG(up->ipc_log_misc, "###clr uart1 rx int\n");
		writel(PMU_WKUP_UART_CTL_INTR_CLR, up->bt_wakeup_reg);
	}

	return IRQ_HANDLED;
}

static int serial_jlq_btwkup_init(struct uart_jlq_port *sport,
					  struct platform_device *dev)
{
	int ret;
	struct resource *lpmres;

	jlq_uart_debug_init(&sport->port, false);
	sport->bt_wkup_irq = platform_get_irq(dev, 1);
	if (!sport->bt_wkup_irq) {
		pr_err("%s: Failed to get bt-wakeup irq resource\n", __func__);
		return -EINVAL;
	}

	ret = devm_request_irq(&dev->dev, sport->bt_wkup_irq,
				serial_jlq_btwakeup_isr, 0, "Uart1 Rx Wakeup", sport);
	if (ret) {
		pr_err("%s: Failed to request bt-wakeup irq\n", __func__);
		return -ENOMEM;
	}

	irq_set_irq_wake(sport->bt_wkup_irq, 1);
	lpmres = platform_get_resource_byname(dev, IORESOURCE_MEM, "wkup_mem");
	if (!lpmres) {
		pr_err("No bt lpm mem physical address resource found.\n");
		return -EINVAL;
	}

	sport->bt_wakeup_reg = devm_ioremap_resource(&dev->dev, lpmres);
	if (IS_ERR(sport->bt_wakeup_reg)) {
		dev_err(&dev->dev, "Failed to IO map bt lpmres registers.\n");
		return -ENOMEM;
	}

	return 0;
}

#ifdef CONFIG_PM
static int serial_jlq_suspend(struct platform_device *dev, pm_message_t state)
{
	struct uart_jlq_port *sport = platform_get_drvdata(dev);
	unsigned long flags;

	if (sport) {
		if(sport->mach->pin_switch)
			sport->mach->pin_switch(1);

		if ((sport->flags & JLQ_UART_TX_USE_DMA) && sport->txdma_addr) {
			spin_lock_irqsave(&(sport->port.lock),flags);
			sport->tx_stop = 1;
			spin_unlock_irqrestore(&(sport->port.lock),flags);
		}

		if ((sport->flags & JLQ_UART_RX_USE_DMA) && sport->rxdma_addr) {
			spin_lock_irqsave(&(sport->port.lock),flags);
			sport->rx_stop = 1;
			spin_unlock_irqrestore(&(sport->port.lock),flags);
			jlq_dmas_intr_disable(sport->rxdma, DMAS_INT_HBLK_FLUSH);
			hrtimer_cancel(&sport->rx_hrtimer);
			serial_jlq_dma_rx(sport);
			jlq_dmas_stop(sport->rxdma);
		}
		uart_suspend_port(&serial_jlq_reg, &sport->port);
	}

	if ((sport->flags & JLQ_UART_QCOM_BT_SUPPORT) && btpower_get_onoff_status()) {
		int rts_gpio_no;

		IPC_LOG_MSG(sport->ipc_log_misc, "enter uart1 suspend\n");
		pinctrl_pm_select_sleep_state(&dev->dev);
		if (dev->dev.of_node) {
			rts_gpio_no = of_get_named_gpio(dev->dev.of_node, "rts-gpio", 0);
			devm_gpio_request_one(&dev->dev, rts_gpio_no,
					GPIOF_OUT_INIT_LOW, "uart1_rts");
		} else
			IPC_LOG_MSG(sport->ipc_log_misc, "bt suspend, no dev ofnode exist!\n");
		if (sport->bt_wakeup_reg) {
			IPC_LOG_MSG(sport->ipc_log_misc, "enable uart1 rx int\n");
			writel(0, sport->bt_wakeup_reg);
		}
	}

	return 0;
}

static int serial_jlq_resume(struct platform_device *dev)
{
	struct uart_jlq_port *sport = platform_get_drvdata(dev);

	if (sport) {
		uart_resume_port(&serial_jlq_reg, &sport->port);

		if ((sport->flags & JLQ_UART_TX_USE_DMA) && sport->txdma_addr) {
			sport->tx_stop = 0;
			tasklet_schedule(&sport->tklet);
		}
	}

	if(sport->mach->pin_switch)
		sport->mach->pin_switch(0);

	if (sport->flags & JLQ_UART_QCOM_BT_SUPPORT) {
		IPC_LOG_MSG(sport->ipc_log_misc, "enter uart1 resume\n");
		pinctrl_pm_select_default_state(&dev->dev);
		if (sport->bt_wakeup_reg) {
			IPC_LOG_MSG(sport->ipc_log_misc, "clr uart1 rx int\n");
			writel(PMU_WKUP_UART_CTL_INTR_CLR, sport->bt_wakeup_reg);
		}
	}

	return 0;
}
#else
#define serial_jlq_suspend	NULL
#define serial_jlq_resume	NULL
#endif

static struct of_device_id serial_jlq_match_table[] = {
	{ .compatible = "jlq,jlq-uart", },
	{}
};

static int __init serial_jlq_probe(struct platform_device *dev)
{
	struct uart_jlq_port *sport;
	int ret;

#ifdef CONFIG_OF
	int id = 0;
	if (dev->dev.of_node) {
		id = of_alias_get_id(dev->dev.of_node, "serial");
		if (id < 0) {
			dev_err(&dev->dev, "serial not found, please check dtsi\n");
			return id;
		}
	}
	sport = &(serial_jlq_ports[id]);
#else
	sport = &(serial_jlq_ports[dev->id]);
#endif

	ret = serial_jlq_init_port(sport, dev);
	if (ret)
		return ret;

	if (sport->flags & JLQ_UART_USE_WORKQUEUE) {
		sport->wq = alloc_ordered_workqueue(
				"jlq serial%d", 0, sport->id);
		if (!sport->wq)
			return -ENOMEM;

		INIT_WORK(&sport->buffer_work, serial_jlq_flip_buffer_push_wq);
	}

	if ((sport->flags & JLQ_UART_RX_USE_DMA) && !sport->rxdma_start_addr) {
		sport->rxdma_start_addr = dma_alloc_coherent(sport->port.dev,
			UART_JLQ_RX_DMA_BUF_SIZE,
			&(sport->rxdma_addr_phys),
			GFP_KERNEL);
		if (!sport->rxdma_start_addr) {
			ret = -ENOMEM;
			goto out_wq;
		}
	}
	uart_add_one_port(&serial_jlq_reg, &sport->port);

	platform_set_drvdata(dev, sport);

	if (sport->flags & JLQ_UART_QCOM_BT_SUPPORT) {
		ret = serial_jlq_btwkup_init(sport, dev);
		if (ret)
			return ret;
	}

	return 0;

out_wq:
	if (sport->flags & JLQ_UART_USE_WORKQUEUE)
		destroy_workqueue(sport->wq);

	return ret;
}

static int __exit serial_jlq_remove(struct platform_device *dev)
{
	struct uart_jlq_port *sport = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if (sport->rxdma_start_addr) {
		dma_free_coherent(&dev->dev, UART_JLQ_RX_DMA_BUF_SIZE,
				sport->rxdma_start_addr, sport->rxdma_addr_phys);
		sport->rxdma_start_addr = NULL;
	}

	if (sport->flags & JLQ_UART_USE_WORKQUEUE)
		destroy_workqueue(sport->wq);

	uart_remove_one_port(&serial_jlq_reg, &sport->port);
	clk_put(sport->uart_clk);

	return 0;
}

static struct platform_driver serial_jlq_driver = {
	.remove = __exit_p(serial_jlq_remove),
	.suspend = serial_jlq_suspend,
	.resume = serial_jlq_resume,
	.driver = {
		.name = "jlq-uart",
		.owner = THIS_MODULE,
		.of_match_table = serial_jlq_match_table,
	},
};

int __init serial_jlq_init(void)
{
	int ret;

#ifdef CONFIG_SERIAL_JLQ_CONSOLE
	register_console(&serial_jlq_console);
#endif
	ret = uart_register_driver(&serial_jlq_reg);
	if (ret != 0) {
		printk("%s register failed ret = %d\n", __func__, ret);
		goto out;
	}

	ret = platform_driver_probe(&serial_jlq_driver, serial_jlq_probe);
	if (ret != 0) {
		printk("register uart driver failed\n");
		uart_unregister_driver(&serial_jlq_reg);
	}

out:
	return ret;
}

void __exit serial_jlq_exit(void)
{
	platform_driver_unregister(&serial_jlq_driver);
	uart_unregister_driver(&serial_jlq_reg);
}

module_init(serial_jlq_init);
module_exit(serial_jlq_exit);

MODULE_AUTHOR("jlqer <jlq@jlq.com>");
MODULE_DESCRIPTION("JLQ serial driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jlq-uart");
