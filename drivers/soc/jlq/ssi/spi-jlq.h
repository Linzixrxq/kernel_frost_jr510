/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __SPI_JLQ_H
#define __SPI_JLQ_H

#define SSI_CTRL0			(0x00)
#define SSI_CTRL1			(0x04)
#define SSI_EN				(0x08)
#define SSI_SE				(0x10)
#define SSI_BAUD			(0x14)
#define SSI_TXFTL			(0x18)
#define SSI_RXFTL			(0x1c)
#define SSI_TXFL			(0x20)
#define SSI_RXFL			(0x24)
#define SSI_STS				(0x28)
#define SSI_IE				(0x2c)
#define SSI_IS				(0x30)
#define SSI_RIS				(0x34)
#define SSI_TXOIC			(0x38)
#define SSI_RXOIC			(0x3c)
#define SSI_RXUIC			(0x40)
#define SSI_IC				(0x48)
#define SSI_DMAC			(0x4c)
#define SSI_DMATDL			(0x50)
#define SSI_DMARDL			(0x54)
#define SSI_DATA			(0x60)

/* SSI_CTRL0. */
#define SSI_SRL				(1 << 11)
#define SSI_RTX				(0 << 8)
#define SSI_TX				(1 << 8)
#define SSI_RX				(2 << 8)
#define SSI_CPOL			(1 << 7)
#define SSI_CPHA			(1 << 6)
#define SSI_MOTOROLA_SPI		(0 << 4)
#define SSI_TI_SSP			(1 << 4)
#define SSI_RTX_BIT			(8)
#define SSI_RTX_MASK			(0x3)
#define SSI_DFS_BIT			(0)
#define SSI_DFS_MASK			(0xf)

/* SSI_RIS. */
#define SSI_RFF				(1 << 4)
#define SSI_RFNE			(1 << 3)
#define SSI_TFE				(1 << 2)
#define SSI_TFNF			(1 << 1)
#define SSI_BUSY			(1 << 0)

#define JLQ_SPI_CS_MAX		(2)

struct jlq_spi_platform_data {
	unsigned int ssi_cfg;
	void __iomem *ssi_mode_base;
	unsigned int num_chipselect;
	int cs_gpios;
	int (*cs_state)(int chipselect, int idx, int level);
};

#define JLQ_SPI_DEBUG
#ifdef JLQ_SPI_DEBUG
#define SSI_DUMP_REGS(a, b, c)		jlq_spi_dump_regs(a, b, c)
#define SSI_PRINT(fmt, args...)		pr_err(fmt, ##args)
#else
#define SSI_DUMP_REGS(a, b, c)
#define SSI_PRINT(fmt, args...)		pr_debug(fmt, ##args)
#endif

/* Version. */
#define SSI_VERSION			"1.0.0"

/* Frame length. */
#define SSI_DFS_MIN			(4)
#define SSI_DFS_MAX			(16)

/* DMA transfer. */
#define JSPI_DMA_TX_BUFFER_SIZE            SZ_64K
#define JSPI_DMA_RX_BUFFER_SIZE            JSPI_DMA_TX_BUFFER_SIZE

#define SSI_DMA_MIN_BYTES		(512)
#define SSI_DMA_MAX_BYTES		(JSPI_DMA_TX_BUFFER_SIZE)

/* TX dummy data . */
#define SSI_TX_DUMMY_DATA		(0)

/* Tranfser timeout. */
#define SSI_TIMEOUT			msecs_to_jiffies(1000)

#define JLQ_DMA_MASK	(DMA_BIT_MASK(32))

#endif /* __SPI_JLQ_H */
