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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/spi/spi.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <linux/delay.h>
#include <soc/jlq/jr510/dmas.h>
#include <asm/dma.h>
#include "spi-jlq.h"
#include <linux/dma-mapping.h>

#define JLQ_SPI_NAME "jlq-spi"

static u64 dma_mask = JLQ_DMA_MASK;

struct jlq_ssi {
	/* Base. */
	struct device *dev;
	struct jlq_spi_platform_data *pdata;
	struct platform_device *pdev;
	struct spi_master *master;
	struct resource *res;
	void __iomem *base;
	struct clk *clk;
	struct clk *pclk;
	unsigned int clk_input;
	unsigned int pclk_input;
	int id;
	int irq;

	/* DMA. */
	int dma_supported;
	int dma_rx_channel;
	int dma_tx_channel;
	struct dmas_ch_cfg dma_rx_cfg;
	struct dmas_ch_cfg dma_tx_cfg;
	struct completion dma_rx_completion;
	struct completion dma_tx_completion;

	/* Driver message queue */
	struct workqueue_struct *workqueue;
	struct work_struct work;
	struct list_head queue;
	spinlock_t lock;

	/* SPI config. */
	unsigned int speed_hz;
	unsigned char bits_per_word;
	unsigned char chip_select;
	unsigned char mode;
	unsigned int tx_dummy_data;
	unsigned int rtx_flag;
	unsigned int rx_len;
	int cs_level;
	int cs_gpios;

	void *rx_dma_buf;
	void *tx_dma_buf;

	dma_addr_t rx_dma;
	dma_addr_t tx_dma;
};

static void jlq_spi_cs_set(struct jlq_ssi *ssi, int cs_active);

static inline void jlq_spi_write_reg(struct jlq_ssi *ssi,
		int idx, unsigned int val)
{
	writel(val, ssi->base + idx);
}

static inline unsigned int jlq_spi_read_reg(struct jlq_ssi *ssi, int idx)
{
	return readl(ssi->base + idx);
}

#ifdef JLQ_SPI_DEBUG
static void jlq_spi_dump_regs(struct jlq_ssi *ssi, int is_dma, int is_rx)
{
	SSI_PRINT("SSI%d driver version : %s\n", ssi->id, SSI_VERSION);
	SSI_PRINT("SSI CS level : %d\n", ssi->cs_level);
	SSI_PRINT("SSI_CTRL0 : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_CTRL0));
	SSI_PRINT("SSI_CTRL1 : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_CTRL1));
	SSI_PRINT("SSI_EN : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_EN));
	SSI_PRINT("SSI_SE : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_SE));
	SSI_PRINT("SSI_BAUD : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_BAUD));
	SSI_PRINT("SSI_TXFTL : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_TXFTL));
	SSI_PRINT("SSI_RXFTL : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_RXFTL));
	SSI_PRINT("SSI_TXFL : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_TXFL));
	SSI_PRINT("SSI_RXFL : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_RXFL));
	SSI_PRINT("SSI_STS : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_STS));
	SSI_PRINT("SSI_IE : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_IE));
	SSI_PRINT("SSI_IS : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_IS));
	SSI_PRINT("SSI_RIS : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_RIS));
	SSI_PRINT("SSI_DMAC : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_DMAC));
	SSI_PRINT("SSI_DMATDL : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_DMATDL));
	SSI_PRINT("SSI_DMARDL : 0x%08x\n", jlq_spi_read_reg(ssi, SSI_DMARDL));
	if (is_dma) {
		if (is_rx) {
			SSI_PRINT("rx dma : %d\n", ssi->dma_rx_channel);
			jlq_dmas_dump(ssi->dma_rx_channel);
		} else {
			SSI_PRINT("tx dma : %d\n", ssi->dma_tx_channel);
			jlq_dmas_dump(ssi->dma_tx_channel);
		}
	}
}
#endif

static int jlq_spi_for_reg_bit_clean(struct jlq_ssi *ssi, int idx,
			unsigned int bit)
{
	unsigned long timeout;

	timeout = jiffies + SSI_TIMEOUT;
	while (jlq_spi_read_reg(ssi, idx) & bit) {
		if (time_after(jiffies, timeout)) {
			dev_err(&ssi->pdev->dev,
				"wait reg bit clean timeout,reg=0x%x,bit=0x%x\n",
				idx, bit);
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	return 0;
}

static int jlq_spi_for_reg_bit(struct jlq_ssi *ssi, int idx,
			unsigned long bit)
{
	unsigned long timeout;

	timeout = jiffies + SSI_TIMEOUT;
	while (!(jlq_spi_read_reg(ssi, idx) & bit)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	return 0;
}

static void jlq_spi_hw_init(struct jlq_ssi *ssi)
{
	/* Disable all interrupts. */
	jlq_spi_write_reg(ssi, SSI_IE, 0);

	/* Disable SSI. */
	jlq_spi_write_reg(ssi, SSI_EN, 0);

	/* Config SSI. */
	jlq_spi_write_reg(ssi, SSI_CTRL0, SSI_RTX | SSI_MOTOROLA_SPI);
	jlq_spi_write_reg(ssi, SSI_CTRL1, 8);

	/* Set threshold of TX and RX. */
	jlq_spi_write_reg(ssi, SSI_TXFTL, 0);
	jlq_spi_write_reg(ssi, SSI_RXFTL, 0);

	if (ssi->dma_supported) {
		/* Disable DMA mode. */
		jlq_spi_write_reg(ssi, SSI_DMAC, 0);

		/* Set DMA threshold of TX and RX. */
		jlq_spi_write_reg(ssi, SSI_DMATDL, 4);
		jlq_spi_write_reg(ssi, SSI_DMARDL, 0);
	}

	/*set cs pin*/
	jlq_spi_cs_set(ssi, 0);
}

static void jlq_spi_hw_uninit(struct jlq_ssi *ssi)
{
	/* Disable all interrupts. */
	jlq_spi_write_reg(ssi, SSI_IE, 0);

	/* Disable SSI. */
	jlq_spi_write_reg(ssi, SSI_EN, 0);
}

static void jlq_spi_enable(struct jlq_ssi *ssi, int enable)
{
	jlq_spi_write_reg(ssi, SSI_EN, enable ? 1 : 0);
}

static void jlq_spi_dma_enable(struct jlq_ssi *ssi,
		int is_rx, int enable)
{
	unsigned int bit = is_rx ? 0 : 1;
	unsigned int val;

	val = jlq_spi_read_reg(ssi, SSI_DMAC);
	if (enable)
		val |= (1 << bit);
	else
		val &= ~(1 << bit);

	jlq_spi_write_reg(ssi, SSI_DMAC, val);
}

static void jlq_spi_cs_set(struct jlq_ssi *ssi, int cs_active)
{
	int level = (ssi->mode & SPI_CS_HIGH) ? cs_active : !cs_active;

	ssi->cs_level = level;
	if (ssi->pdata->cs_state)
		ssi->pdata->cs_state(ssi->chip_select, ssi->cs_gpios, level);
}
static void jlq_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct jlq_ssi *ssi = spi_master_get_devdata(spi->master);

	jlq_spi_cs_set(ssi, enable);
}

static inline void jlq_spi_dma_tx_irq(int irq, int type, void *dev_id)
{
	struct jlq_ssi *ssi = dev_id;

	jlq_spi_dma_enable(ssi, 0, 0);
	complete(&ssi->dma_tx_completion);
}

static inline void jlq_spi_dma_rx_irq(int irq, int type, void *dev_id)
{
	struct jlq_ssi *ssi = dev_id;

	jlq_spi_dma_enable(ssi, 1, 0);
	complete(&ssi->dma_rx_completion);
}

static int jlq_spi_dma_init(struct jlq_ssi *ssi)
{
	struct dmas_ch_cfg *cfg;
	int ret;

	cfg = &ssi->dma_tx_cfg;
	cfg->flags = DMAS_CFG_ALL;
	cfg->block_size = 0;
	cfg->src_addr = 0;
	cfg->dst_addr = (unsigned int)(ssi->res->start + SSI_DATA);
	cfg->priority = DMAS_CH_PRI_DEFAULT;
	cfg->bus_width = DMAS_DEV_WIDTH_8BIT;
	cfg->tx_trans_mode = DMAS_TRANS_NORMAL;
	cfg->tx_fix_value = 0;
	cfg->tx_block_mode = DMAS_SINGLE_BLOCK;
	cfg->irq_en = DMAS_INT_DONE;
	cfg->irq_handler = jlq_spi_dma_tx_irq;
	cfg->irq_data = ssi;
	ret = jlq_dmas_request((char *)ssi->res->name, ssi->dma_tx_channel);
	if (ret)
		return ret;

	ret = jlq_dmas_config(ssi->dma_tx_channel, cfg);
	if (ret)
		return ret;

	cfg = &ssi->dma_rx_cfg;
	cfg->flags = DMAS_CFG_ALL;
	cfg->block_size = 0;
	cfg->src_addr = (unsigned int)(ssi->res->start + SSI_DATA);
	cfg->dst_addr = 0;
	cfg->priority = DMAS_CH_PRI_DEFAULT;
	cfg->bus_width = DMAS_DEV_WIDTH_8BIT;
	cfg->rx_trans_type = DMAS_TRANS_BLOCK;
	cfg->rx_timeout = 0;
	cfg->irq_en = DMAS_INT_DONE;
	cfg->irq_handler = jlq_spi_dma_rx_irq;
	cfg->irq_data = ssi;
	ret = jlq_dmas_request((char *)ssi->res->name, ssi->dma_rx_channel);
	if (ret)
		return ret;

	ret = jlq_dmas_config(ssi->dma_rx_channel, cfg);
	if (ret)
		return ret;

	init_completion(&ssi->dma_rx_completion);
	init_completion(&ssi->dma_tx_completion);

	return ret;
}

static void jlq_spi_dma_uninit(struct jlq_ssi *ssi)
{
	jlq_dmas_free(ssi->dma_tx_channel);
	jlq_dmas_free(ssi->dma_rx_channel);
}

static void jlq_spi_dma_start(struct jlq_ssi *ssi,
		int is_rx, dma_addr_t buf, unsigned int count)
{
	struct dmas_ch_cfg *cfg;
	int ret = 0;

	if (is_rx) {
		cfg = &ssi->dma_rx_cfg;
		cfg->flags = DMAS_CFG_BLOCK_SIZE | DMAS_CFG_DST_ADDR;
		cfg->dst_addr = buf;
		cfg->block_size = count;
		ret = jlq_dmas_config(ssi->dma_rx_channel, cfg);
		if (ret < 0)
			dev_err(&ssi->pdev->dev, "dma rx cfg err\n");
		jlq_dmas_start(ssi->dma_rx_channel);
	} else {
		cfg = &ssi->dma_tx_cfg;
		cfg->flags = DMAS_CFG_BLOCK_SIZE | DMAS_CFG_SRC_ADDR;
		cfg->src_addr = buf;
		cfg->block_size = count;
		ret = jlq_dmas_config(ssi->dma_tx_channel, cfg);
		if (ret < 0)
			dev_err(&ssi->pdev->dev, "dma tx cfg err\n");

		jlq_dmas_start(ssi->dma_tx_channel);
	}
}

static int jlq_spi_config(struct spi_device *spi,
		struct spi_message *msg, struct spi_transfer *xfer)
{
	struct jlq_ssi *ssi = spi_master_get_devdata(spi->master);

	unsigned int rtx_flag = SSI_RTX;
	unsigned int enable = 0;
	unsigned int val = 0;
	unsigned int div = 0;
	int ret;

	if (xfer) {
		if (!xfer->rx_buf)
			rtx_flag = SSI_TX;

		if (!xfer->tx_buf)
			rtx_flag = SSI_RX;

		if ((!xfer->bits_per_word
			|| (xfer->bits_per_word == ssi->bits_per_word))
			&& (!xfer->speed_hz
			|| (xfer->speed_hz == ssi->speed_hz))
			&& (rtx_flag == ssi->rtx_flag)
			&& !((rtx_flag == SSI_RX)
			&& (xfer->len != ssi->rx_len)))
			return 0;

		ssi->rtx_flag = rtx_flag;
		if (xfer->bits_per_word)
			ssi->bits_per_word = xfer->bits_per_word;

		if (xfer->speed_hz)
			ssi->speed_hz = xfer->speed_hz;
	} else {
		if (spi->chip_select >= ssi->pdata->num_chipselect) {
			dev_err(&ssi->pdev->dev,
				"invalid chip select %d\n", spi->chip_select);
			return -EINVAL;
		}

		if ((spi->bits_per_word == ssi->bits_per_word)
			&& (spi->max_speed_hz == ssi->speed_hz)
			&& (spi->chip_select == ssi->chip_select)
			&& (spi->mode == ssi->mode))
			return 0;

		ssi->mode = spi->mode;
		ssi->chip_select = spi->chip_select;
		ssi->bits_per_word = spi->bits_per_word;
		ssi->speed_hz = spi->max_speed_hz;
	}

	if ((ssi->bits_per_word < SSI_DFS_MIN)
		|| (ssi->bits_per_word > SSI_DFS_MAX)) {
		dev_err(&ssi->pdev->dev,
			"invalid word length %d\n", ssi->bits_per_word);
		return -EINVAL;
	}

	if (ssi->speed_hz) {
		div = ssi->clk_input / ssi->speed_hz;
		if (div < 2 || div > 65534) {
			dev_err(&ssi->pdev->dev,
				"invalid speed_hz %d\n", ssi->speed_hz);
			return -EINVAL;
		}
		div = ((div + 1) / 2) * 2;
	}

	ret = jlq_spi_for_reg_bit_clean(ssi, SSI_STS, SSI_BUSY);
	if (ret)
		return ret;
	/* Save the SSI enable flag and disable SSI. */
	enable = jlq_spi_read_reg(ssi, SSI_EN);
	jlq_spi_write_reg(ssi, SSI_EN, 0);

	/* Set the buadrate. */
	jlq_spi_write_reg(ssi, SSI_BAUD, div);

	/* CS. */
	jlq_spi_write_reg(ssi, SSI_SE, 1 << ssi->chip_select);

	val = jlq_spi_read_reg(ssi, SSI_CTRL0);

	/* Word length. */
	val &= ~(SSI_DFS_MASK << SSI_DFS_BIT);
	val |= (ssi->bits_per_word - 1) << SSI_DFS_BIT;

	/* RTX. */
	if (xfer) {
		if (rtx_flag == SSI_RX) {
			ssi->rx_len = xfer->len;
			jlq_spi_write_reg(ssi, SSI_CTRL1, ssi->rx_len - 1);
		} else {
			ssi->rx_len = 0;
			jlq_spi_write_reg(ssi, SSI_CTRL1, 0);
		}

		val &= ~(SSI_RTX_MASK << SSI_RTX_BIT);
		val |= rtx_flag;
	}

	/* Set SPI mode. */
	if (spi->mode & SPI_CPOL)
		val |= SSI_CPOL;
	else
		val &= ~SSI_CPOL;

	if (spi->mode & SPI_CPHA)
		val |= SSI_CPHA;
	else
		val &= ~SSI_CPHA;

	if (spi->mode & SPI_LOOP)
		val |= SSI_SRL;
	else
		val &= ~SSI_SRL;

	jlq_spi_write_reg(ssi, SSI_CTRL0, val);

	/* Restore the SSI enable flag. */
	jlq_spi_write_reg(ssi, SSI_EN, enable);

	return 0;
}

static int jlq_spi_transfer_dma(struct jlq_ssi *ssi,
		struct spi_message *msg, struct spi_transfer *xfer)
{
	unsigned long timeout;
	unsigned int count;
	int ret;
	u8 *rx;
	const u8 *tx;

	count = xfer->len;
	rx = xfer->rx_buf;
	tx = xfer->tx_buf;
	ret = count;

	if (rx != NULL) {
		init_completion(&ssi->dma_rx_completion);
		jlq_spi_dma_enable(ssi, 1, 1);
		jlq_spi_dma_start(ssi, 1, xfer->rx_dma, count);
	}

	if (tx != NULL) {
		init_completion(&ssi->dma_tx_completion);
		jlq_spi_dma_enable(ssi, 0, 1);
		jlq_spi_dma_start(ssi, 0, xfer->tx_dma, count);
	} else {
		/* RX_ONLY mode needs dummy data in TX reg. */
		jlq_spi_write_reg(ssi, SSI_DATA, ssi->tx_dummy_data);
	}

	if (tx != NULL) {
		timeout = wait_for_completion_timeout(&ssi->dma_tx_completion,
						      SSI_TIMEOUT);
		if (!timeout && !ssi->dma_tx_completion.done) {
			dev_err(&ssi->pdev->dev, "DMA TXS timed out\n");
			SSI_DUMP_REGS(ssi, 1, 0);
			jlq_spi_dma_enable(ssi, 0, 0);
			jlq_dmas_stop(ssi->dma_tx_channel);
			ret = -EIO;
		}
	}

	if (rx != NULL) {
		if (ret == count) {
			timeout = wait_for_completion_timeout(
					&ssi->dma_rx_completion, SSI_TIMEOUT);
			if (!timeout && !ssi->dma_rx_completion.done) {
				dev_err(&ssi->pdev->dev, "DMA RXS timed out\n");
				SSI_DUMP_REGS(ssi, 1, 1);
				jlq_spi_dma_enable(ssi, 1, 0);
				jlq_dmas_stop(ssi->dma_rx_channel);
				ret = -EIO;
			}
		} else {
			jlq_spi_dma_enable(ssi, 1, 0);
			jlq_dmas_stop(ssi->dma_rx_channel);
		}

		if (xfer->rx_dma)
			memcpy(xfer->rx_buf, ssi->rx_dma_buf, count);
	}

	return ret;
}

static inline unsigned int swap_bit(unsigned int val, int bit_len)
{
	int i;
	unsigned int ret = 0;

	for (i = 0; i < bit_len; i++)
		ret |= ((val >> i) & 1) << (bit_len - 1 - i);

	return ret;
}

static int jlq_spi_transfer_pio(struct jlq_ssi *ssi,
		struct spi_message *msg, struct spi_transfer *xfer)
{
	unsigned int count;
	unsigned int c;
	unsigned int val;
	unsigned int step;
	u8 *rx;
	const u8 *tx;

	count = xfer->len;
	rx = xfer->rx_buf;
	tx = xfer->tx_buf;
	c = count;

	if (ssi->bits_per_word <= 8)
		step = 1;
	else if (ssi->bits_per_word <= 16)
		step = 2;
	else
		return -EINVAL;

	if ((ssi->rtx_flag == SSI_RX) && (rx != NULL))
		jlq_spi_write_reg(ssi, SSI_DATA, 0xa5);

	while (c) {
		if (tx != NULL) {
			memcpy(&val, tx, step);
			tx += step;
			if (ssi->mode & SPI_LSB_FIRST)
				val = swap_bit(val, ssi->bits_per_word);
		} else {
			val = ssi->tx_dummy_data;
		}
		if (tx != NULL) {
			if (jlq_spi_for_reg_bit(ssi, SSI_STS, SSI_TFNF) < 0) {
				dev_err(&ssi->pdev->dev, "TXS timed out\n");
				SSI_DUMP_REGS(ssi, 0, 0);
				goto out;
			}

			jlq_spi_write_reg(ssi, SSI_DATA, val);
		}
		if (rx != NULL) {
			if (jlq_spi_for_reg_bit(ssi, SSI_STS, SSI_RFNE) < 0) {
				dev_err(&ssi->pdev->dev, "RXS timed out\n");
				SSI_DUMP_REGS(ssi, 0, 1);
				goto out;
			}

			val = jlq_spi_read_reg(ssi, SSI_DATA);
		}
		if (rx != NULL) {
			if (ssi->mode & SPI_LSB_FIRST)
				val = swap_bit(val, ssi->bits_per_word);
			memcpy(rx, &val, step);
			rx += step;
		}

		c -= step;
	}

	/* for TX_ONLY mode, be sure all words have shifted out */
	if (xfer->rx_buf == NULL) {
		if (jlq_spi_for_reg_bit(ssi, SSI_STS, SSI_TFE) < 0)
			dev_err(&ssi->pdev->dev, "TXS timed out1\n");
	}

out:
	return count - c;
}

static int jlq_spi_one_transfer(struct jlq_ssi *ssi,
		struct spi_message *msg,
		struct spi_transfer *xfer)
{
	unsigned int count, reg_val;
	int	cs_active = 0;
	int ret = 0;

	jlq_spi_enable(ssi, 1);

	if (!cs_active) {
		jlq_spi_cs_set(ssi, 1);
		cs_active = 1;
	}

	if (xfer) {
		if (xfer->tx_buf == NULL && xfer->rx_buf == NULL && xfer->len) {
			ret = -EINVAL;
			goto clean;
		}

		ret = jlq_spi_config(msg->spi, msg, xfer);
		if (ret)
			goto clean;

		if ((msg->is_dma_mapped
			|| (xfer->len >= SSI_DMA_MIN_BYTES && xfer->len <= SSI_DMA_MAX_BYTES))
				&& !(ssi->mode & SPI_LSB_FIRST)
				&& ssi->dma_supported) {
			if (xfer->tx_buf != NULL) {
				if (xfer->len > JSPI_DMA_TX_BUFFER_SIZE) {
					dev_err(ssi->dev, "tx size %d > %d\n",
						xfer->len, JSPI_DMA_TX_BUFFER_SIZE);
					ret = -EINVAL;
					goto clean;
				}
				memcpy(ssi->tx_dma_buf, xfer->tx_buf, xfer->len);
				xfer->tx_dma = ssi->tx_dma;
			}

			if (xfer->rx_buf != NULL) {
				if (xfer->len > JSPI_DMA_RX_BUFFER_SIZE) {
					dev_err(ssi->dev, "rx size %d > %d\n",
						xfer->len, JSPI_DMA_RX_BUFFER_SIZE);
					ret = -EINVAL;
					goto clean;
				}

				xfer->rx_dma = ssi->rx_dma;
			}
			count = jlq_spi_transfer_dma(ssi, msg, xfer);
		} else {
			count = jlq_spi_transfer_pio(ssi, msg, xfer);
			msg->actual_length += count;
			if (count != xfer->len) {
				ret = -EIO;
				goto clean;
			}
		}

		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);

		if (xfer->cs_change) {
			jlq_spi_cs_set(ssi, 0);
			cs_active = 0;
		}
	}
	reg_val = jlq_spi_read_reg(ssi, SSI_CTRL0);
	if ((reg_val & (SSI_RTX_MASK << SSI_RTX_BIT)) != SSI_RX)
		jlq_spi_for_reg_bit_clean(ssi, SSI_STS, SSI_BUSY);

clean:
	if (cs_active)
		jlq_spi_cs_set(ssi, 0);

	jlq_spi_enable(ssi, 0);

	msg->status = ret;
	return 0;
}

static int jlq_spi_transfer_one_message(struct spi_master *master,
		struct spi_message *msg)
{
	struct jlq_ssi *ssi = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;
	struct spi_transfer *xfer = NULL;
	unsigned int count, reg_val;
	int	cs_active = 0;
	int ret = 0;

	u8 *rx;
	const u8 *tx;

	if (!spi->dev.dma_mask) {
		spi->dev.dma_mask = &dma_mask;
		msg->spi->dev.dma_mask = spi->dev.dma_mask;
	}

	if (!spi->dev.coherent_dma_mask) {
		spi->dev.coherent_dma_mask = dma_mask;
		msg->spi->dev.coherent_dma_mask = spi->dev.coherent_dma_mask;
	}

	msg->status = 0;
	msg->actual_length = 0;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {

		ret = jlq_spi_one_transfer(ssi, msg, xfer);

		if (ret)
			goto msg_done;

	}

msg_done:
	spi_finalize_current_message(master);


	return 0;
}

static int jlq_spi_setup(struct spi_device *spi)
{
	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	return jlq_spi_config(spi, NULL, NULL);
}

static void jlq_spi_cleanup(struct spi_device *spi)
{

}

static void jlq_spi_protocol_select(volatile void __iomem *addr, u32 mode)
{

	u32 val = 0;

	val = readl(addr);
	val |= mode;
	writel(val, addr);
}

static int jlq_spi_cs_state(int chipselect, int idx, int level)
{
	gpio_request(idx, "JLQ SPI CS");
	gpio_direction_output(idx, !!level);
	gpio_free(idx);

	return 0;
}

static struct jlq_spi_platform_data *jlq_spi_dt_to_pdata(
		struct platform_device *pdev)
{
	struct jlq_spi_platform_data *pdata;
	struct device_node *node = pdev->dev.of_node;
	u32 num_cs = 0, mode = 0; /* default number of chipselect */

	dev_dbg(&pdev->dev, "dt_to_pdata\n");

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	/* select spi protocol */
	of_property_read_u32(node, "ssi_protocol", &mode);
	pdata->ssi_cfg = mode;


	of_property_read_u32(node, "num_chipselect", &num_cs);
	pdata->num_chipselect = num_cs;

	if (of_property_read_bool(node, "cs_state")) {
		pdata->cs_gpios = of_get_named_gpio(node, "cs-gpios", 0);
		if (pdata->cs_gpios < 0) {
			dev_err(&pdev->dev, "cs-gpios failed\n");
			return ERR_PTR(pdata->cs_gpios);
		}
		pdata->cs_state = jlq_spi_cs_state;
	}

	return pdata;
}

static int jlq_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jlq_spi_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct spi_master *master;
	struct jlq_ssi *ssi;
	struct resource *r, *r1;
	int dma_tx_channel = 0;
	int dma_rx_channel = 0;
	int irq = 0;
	int ret = 0;
	int id = 0;

	if (pdev->dev.of_node) {
		pdata = jlq_spi_dt_to_pdata(pdev);
		pdev->dev.platform_data = pdata;
		id = of_alias_get_id(pdev->dev.of_node, "spi");
		if (id < 0) {
			dev_err(&pdev->dev,
				"failed to get spi alias id (%d)\n", id);
			return id;
		}
		pdev->id = id;

		dma_rx_channel = of_get_named_dmas_channel(pdev->dev.of_node,
				"rx_dma_channel", 0);
		dma_tx_channel = of_get_named_dmas_channel(pdev->dev.of_node,
				"tx_dma_channel", 0);

		if (dma_rx_channel < 0 || dma_tx_channel < 0) {
			dev_err(&pdev->dev, "failed to get spi dmas channel\n");
			return -EPROBE_DEFER;
		}

		r = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "ssi");
		if (!r) {
			dev_err(&pdev->dev, "Invalid spi mem.\n");
			return -ENXIO;
		}

		r1 = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "ap_ctrl");
		if (!r1) {
			dev_err(&pdev->dev, "Invalid ap_ctrl men.\n");
			return -ENXIO;
		}

		/*select spi mode*/
		pdata->ssi_mode_base = ioremap(r1->start, 1);
		if (!pdata->ssi_mode_base) {
			ret = -ENOMEM;
			dev_err(&pdev->dev,
				"cannot remap ap_ctrl io 0x%llx\n", r1->start);
			return ret;
		}
		jlq_spi_protocol_select(pdata->ssi_mode_base, pdata->ssi_cfg);
	} else {
		r = platform_get_resource(pdev, IORESOURCE_DMA, 0);
		if (!r)
			return -ENXIO;

		dma_rx_channel = r->start;

		r = platform_get_resource(pdev, IORESOURCE_DMA, 1);
		if (!r)
			return -ENXIO;

		dma_tx_channel = r->start;

		r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!r) {
			dev_err(&pdev->dev, "Invalid spi mem.\n");
			return -ENXIO;
		}
		r1 = r;
		id = pdev->id;
	}

	if (!pdata || (pdata->num_chipselect > JLQ_SPI_CS_MAX)) {
		dev_err(&pdev->dev, "Invalid spi platform data.\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Invalid spi irq.\n");
		return -ENXIO;
	}

	r = request_mem_region(r->start, r->end - r->start + 1, r->name);
	if (!r) {
		dev_err(&pdev->dev, "Invalid spi request_mem.\n");
		return -EBUSY;
	}

	/* Allocate master with space for drv_data and null dma buffer. */
	master = spi_alloc_master(dev, sizeof(struct jlq_ssi));
	if (!master) {
		dev_err(&pdev->dev, "cannot alloc spi_master\n");
		ret = -ENOMEM;
		goto out;
	}

	master->bus_num = pdev->id;
	master->num_chipselect = pdata->num_chipselect;
	master->mode_bits = SPI_MODE_3 | SPI_LOOP|
				SPI_CS_HIGH | SPI_LSB_FIRST |
				SPI_RX_DUAL | SPI_RX_QUAD |
				SPI_TX_DUAL | SPI_TX_QUAD;

	master->bits_per_word_mask = SPI_BPW_MASK(32) | SPI_BPW_MASK(16) |
					SPI_BPW_MASK(8);
	master->set_cs = jlq_spi_set_cs;
	master->cleanup = jlq_spi_cleanup;
	master->setup = jlq_spi_setup;
	master->transfer_one_message = jlq_spi_transfer_one_message;
	if (pdev->dev.of_node)
		master->dev.of_node = pdev->dev.of_node;

	dev_set_drvdata(&pdev->dev, master);
	ssi = spi_master_get_devdata(master);
	ssi->cs_gpios = pdata->cs_gpios;
	ssi->master = master;
	ssi->pdata = pdata;
	ssi->pdev = pdev;
	ssi->dev = &pdev->dev;
	ssi->res = r;
	ssi->irq = irq;
	ssi->id = pdev->id;
	ssi->speed_hz = 0;
	ssi->bits_per_word = 0;
	ssi->chip_select = 0;
	ssi->mode = 0;
	ssi->rx_len = 0;
	ssi->rtx_flag = 0;
	ssi->tx_dummy_data = SSI_TX_DUMMY_DATA;
	ssi->dma_tx_channel = dma_tx_channel;
	ssi->dma_rx_channel = dma_rx_channel;
	ssi->base = ioremap(r->start, r->end - r->start + 1);
	if (!ssi->base) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "cannot remap io 0x%llx\n", r->start);
		goto out_ioremap;
	}

	if ((dma_tx_channel >= DMAS_CH_MAX) || (dma_rx_channel >= DMAS_CH_MAX))
		ssi->dma_supported = 0;
	else
		ssi->dma_supported = 1;

	ssi->clk = clk_get(&pdev->dev, "ssi_clk");
	if (IS_ERR(ssi->clk)) {
		ret = PTR_ERR(ssi->clk);
		ssi->clk = NULL;
		dev_err(&pdev->dev, "cannot get ssi clk\n");
		goto out_clk;
	}

	ssi->pclk = clk_get(&pdev->dev, "ssi_pclkgt");
	if (IS_ERR(ssi->pclk)) {
		ret = PTR_ERR(ssi->pclk);
		ssi->pclk = NULL;
		dev_err(&pdev->dev, "cannot get ssi pclk\n");
		goto out_clk;
	}

	ssi->clk_input = clk_get_rate(ssi->clk);

	ssi->pclk_input = clk_get_rate(ssi->pclk);
	/* Enable SSI clock. */
	clk_prepare_enable(ssi->clk);
	master->max_speed_hz = clk_get_rate(ssi->clk)/2;
	/* Enable SSI ssi_pclkgt. */
	clk_prepare_enable(ssi->pclk);
	/* Load default SSI configuration. */
	jlq_spi_hw_init(ssi);

	/* Initial SSI DMA. */
	if (ssi->dma_supported) {
		ret = jlq_spi_dma_init(ssi);
		if (ret)
			goto out_master;
	}

	spin_lock_init(&ssi->lock);

	/* Register with the SPI framework. */
	platform_set_drvdata(pdev, ssi);
	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "problem registering ssi master\n");
		goto out_master;
	}

	ssi->tx_dma_buf = dma_alloc_coherent(ssi->dev, JSPI_DMA_TX_BUFFER_SIZE,
			&ssi->tx_dma, GFP_KERNEL);
	if (!ssi->tx_dma_buf) {
		dev_err(ssi->dev, "tx dma buf alloc failed\n");
		goto out_master;
	}

	ssi->rx_dma_buf = dma_alloc_coherent(ssi->dev, JSPI_DMA_RX_BUFFER_SIZE,
			&ssi->rx_dma, GFP_KERNEL);
	if (!ssi->rx_dma_buf) {
		dev_err(ssi->dev, "rx dma buf alloc failed\n");
		goto out_master;
	}

	return ret;

out_master:
	clk_disable_unprepare(ssi->clk);
	clk_disable_unprepare(ssi->pclk);
	clk_put(ssi->clk);
	clk_put(ssi->pclk);
out_clk:
	iounmap(ssi->base);
out_ioremap:
	spi_master_put(master);
out:
	release_mem_region(r->start, r->end - r->start + 1);
	return ret;
}

static int jlq_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct jlq_ssi *ssi = spi_master_get_devdata(master);

	if (!ssi)
		return 0;

	dma_free_coherent(ssi->dev, JSPI_DMA_TX_BUFFER_SIZE,
		ssi->tx_dma_buf, ssi->tx_dma);

	dma_free_coherent(ssi->dev, JSPI_DMA_RX_BUFFER_SIZE,
		ssi->rx_dma_buf, ssi->rx_dma);

	/* Release SSI hardware. */
	jlq_spi_hw_uninit(ssi);

	/* Release IO. */
	iounmap(ssi->base);

	/* Release CLK. */
	clk_disable_unprepare(ssi->clk);
	clk_disable_unprepare(ssi->pclk);
	clk_put(ssi->clk);
	clk_put(ssi->pclk);
	/* Release DMA. */
	if (ssi->dma_supported)
		jlq_spi_dma_uninit(ssi);

	/* Disconnect from the SPI framework. */
	spi_unregister_master(ssi->master);

	/* Prevent double remove. */
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int jlq_spi_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct jlq_ssi *ssi = platform_get_drvdata(pdev);

	clk_disable_unprepare(ssi->clk);
	//clk_disable_unprepare(ssi->pclk);
	return 0;
}

static int jlq_spi_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct jlq_ssi *ssi = platform_get_drvdata(pdev);

	clk_prepare_enable(ssi->clk);
	//clk_prepare_enable(ssi->pclk);
	return 0;
}

const static struct dev_pm_ops jlq_spi_pm_ops = {
	.suspend_noirq = jlq_spi_suspend_noirq,
	.resume_noirq = jlq_spi_resume_noirq,
};
#endif

const static struct of_device_id of_spi_match_table[] = {
	{ .compatible = "jlq,jlq-spi", },
	{}
};

static struct platform_driver jlq_spi_driver = {
	.probe	= jlq_spi_probe,
	.remove = jlq_spi_remove,
	.driver = {
		.name = JLQ_SPI_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &jlq_spi_pm_ops,
#endif
		.of_match_table = of_spi_match_table,
	},
};

module_platform_driver(jlq_spi_driver);

MODULE_ALIAS("platform:" JLQ_SPI_NAME);
MODULE_DESCRIPTION("JLQ SSP SPI Controller driver");
MODULE_AUTHOR("Yucailiu <yucailiu@jlq.com>");
MODULE_LICENSE("GPL");

