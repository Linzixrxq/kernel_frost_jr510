/*
 * drivers/platform/jlq/base/dmas/-dmas.c
 *
 * Copyright (c) 2019-2021  JLQ Tech  Co.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <soc/jlq/jr510/dmas.h>
#include <dt-bindings/jlq/jr510/jlq_dmas.h>

#include "jlq-dmas.h"

static DEFINE_SPINLOCK(dmas_lock);
static struct channel_desc channel_desc[DMAS_NR_CHANNELS];
static LIST_HEAD(dmas_chips);

static irqreturn_t jlq_dmas_intr_handle(int irq, void *dev_id);

static struct dmas_chip *dmaschip_find(void *data,
				int (*match)(struct dmas_chip *chip,
					     void *data))
{
	struct dmas_chip *chip;
	unsigned long flags;

	spin_lock_irqsave(&dmas_lock, flags);
	list_for_each_entry(chip, &dmas_chips, list)
		if (match(chip, data))
			break;

	if (&chip->list == &dmas_chips)
		chip = NULL;
	spin_unlock_irqrestore(&dmas_lock, flags);

	return chip;
}

static inline bool channel_is_valid(int number)
{
	return number >= 0 && number < DMAS_NR_CHANNELS;
}

static struct channel_desc *channel_to_desc(unsigned int channel)
{
	if (WARN(!channel_is_valid(channel), "invalid channel %d\n", channel))
		return NULL;
	else
		return &channel_desc[channel];
}

static bool channel_is_rx_channel(int number)
{
	struct channel_desc *desc;

	desc = channel_to_desc(number);

	if (desc->index >= DMAS_RX_CHANNEL_BEGIN)
		return true;
	else
		return false;
}

static struct dmas_chip *desc_to_chip(struct channel_desc *desc)
{
	return desc ? desc->chip : NULL;
}

struct dmas_chip *channel_to_chip(unsigned int channel)
{
	return desc_to_chip(channel_to_desc(channel));
}

static int dmaschip_find_base(int nchannel)
{
	struct dmas_chip *chip;
	int base = DMAS_NR_CHANNELS - nchannel;

	list_for_each_entry_reverse(chip, &dmas_chips, list) {
		if (chip->base + chip->nchannel > base)
			base = chip->base - nchannel;
		else
			break;
	}

	if (channel_is_valid(base))
		pr_debug("%s: found new base at %d\n", __func__, base);
	else {
		pr_err("%s: cannot find free range\n", __func__);
		return -ENOSPC;
	}

	return base;
}

static int dmaschip_add_to_list(struct dmas_chip *chip)
{
	struct list_head *pos = &dmas_chips;
	struct dmas_chip *_chip;
	int err = 0;

	list_for_each(pos, &dmas_chips) {
		_chip = list_entry(pos, struct dmas_chip, list);
		if (_chip->base >= chip->base + chip->nchannel)
			break;
	}

	if (pos != &dmas_chips && pos->prev != &dmas_chips) {
		_chip = list_entry(pos->prev, struct dmas_chip, list);
		if (_chip->base + _chip->nchannel > chip->base) {
			dev_err(chip->dev,
			       "Channel integer space overlap, cannot add chip\n");
			err = -EBUSY;
		}
	}

	if (!err)
		list_add_tail(&chip->list, pos);

	return err;
}

static int dmaschip_add(struct dmas_chip *chip)
{
	unsigned long	flags;
	int		ret = 0;
	unsigned int	id;
	int		base = chip->base;
	struct clk	*clk;
	unsigned long	freq;
	unsigned long	val;

	if ((!channel_is_valid(base)
			|| !channel_is_valid(base + chip->nchannel - 1))
			&& base >= 0) {
		dev_err(chip->dev, "dmas chip parameter is invalid:\n"
				"base(%d), nchannel(%d)\n",
				base, chip->nchannel);
		ret = -EINVAL;
		goto fail;
	}

	spin_lock_irqsave(&dmas_lock, flags);

	if (base < 0) {
		base = dmaschip_find_base(chip->nchannel);
		if (base < 0) {
			ret = base;
			goto unlock;
		}
		chip->base = base;
	}

	ret = dmaschip_add_to_list(chip);

	if (ret == 0) {
		chip->desc = &channel_desc[chip->base];

		for (id = 0; id < chip->nchannel; id++) {
			struct channel_desc *desc = &chip->desc[id];

			desc->chip = chip;
			desc->index = id;
			desc->regbase = DMAS_CH_REG(chip, id);
			writel(id, DMAS_REG(chip, DMAS_CLR));
		}
	}

	if (chip->special_channel >= 0)
		chip->desc[chip->special_channel].flags =
						DMAS_CH_BLK_SIZE_UNIT_WORD;

	spin_unlock_irqrestore(&dmas_lock, flags);

	/* Clear the DMAS interrupt. */
	writel(0xffffffff, DMAS_REG(chip, DMAS_INT_RAW0));
	writel(0xffffffff, DMAS_REG(chip, DMAS_INT_CLR0));
	writel(0xffffffff, DMAS_REG(chip, DMAS_INT_RAW1));
	writel(0xffffffff, DMAS_REG(chip, DMAS_INT_CLR1));

	/* Get DMAS mclk frequency. */
	clk = devm_clk_get(chip->dev, "dmas_clk");
	if (IS_ERR(clk)) {
		freq = 208000000;
		dev_warn(chip->dev,
				"failed to get bus clock. Use default %ld\n",
				freq);
	} else
		freq = clk_get_rate(clk);
	devm_clk_put(chip->dev, clk);

	/* Timeout unit is 1us. */
	val = ((freq / 1000000) * DMAS_TIMEOUT_UNIT);
	if (val > 255)
		val = 255;
	else if (val > 0)
		val--;
	writel(val, DMAS_REG(chip, DMAS_INTV_UNIT));

	ret = request_irq(chip->irq, jlq_dmas_intr_handle,
			  0, chip->name, (void *)chip);
	if (ret) {
		dev_err(chip->dev,
				"Wow! Can't register IRQ for DMAS\n");
		goto fail;
	}

	return 0;

unlock:
	spin_unlock_irqrestore(&dmas_lock, flags);
fail:
	dev_err(chip->dev, "add dmas chip failed(%d)\n", ret);

	return ret;
}

static int of_dmaschip_find_and_xlate(struct dmas_chip *dc, void *data)
{
	struct of_dmas_data *of_dmas_data = data;
	int ret;

	if ((dc->of_node != of_dmas_data->dmasspec.np) ||
	    (dc->of_dmas_n_cells != of_dmas_data->dmasspec.args_count) ||
	    (!dc->of_xlate))
		return false;

	ret = dc->of_xlate(dc, &of_dmas_data->dmasspec, of_dmas_data->flags);
	if (ret < 0) {
		/* We've found the dmas chip, but the translation failed.
		 * Return true to stop looking and return the translation
		 * error via out_channel
		 */
		of_dmas_data->out_channel = ret;
		return true;
	}

	of_dmas_data->out_channel = ret + dc->base;

	return true;
}

int of_get_named_dmas_flags(struct device_node *np, const char *propname,
			   int index, enum of_dmas_flags *flags)
{
	struct of_dmas_data of_dmas_data = {
		.flags = flags,
		.out_channel = -EPROBE_DEFER };
	int ret;

	if (flags)
		*flags = 0;

	ret = of_parse_phandle_with_args(np, propname, "#dmas-cells", index,
					 &of_dmas_data.dmasspec);
	if (ret) {
		pr_debug("%s: can't parse dmas property\n", __func__);
		return ret;
	}

	dmaschip_find(&of_dmas_data, of_dmaschip_find_and_xlate);

	of_node_put(of_dmas_data.dmasspec.np);
	pr_debug("%s exited with status %d\n", __func__,
		 of_dmas_data.out_channel);
	return of_dmas_data.out_channel;
}
EXPORT_SYMBOL(of_get_named_dmas_flags);

static int of_dmas_simple_xlate(struct dmas_chip *dc,
			 const struct of_phandle_args *dmasspec, u32 *flags)
{
	if (dc->of_dmas_n_cells < 2) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (WARN_ON(dmasspec->args_count < dc->of_dmas_n_cells))
		return -EINVAL;

	if (dmasspec->args[0] >= dc->nchannel)
		return -EINVAL;

	if (flags)
		*flags = dmasspec->args[1];

	return dmasspec->args[0];
}

static void of_dmaschip_add(struct dmas_chip *chip)
{
	if ((!chip->of_node) && (chip->dev))
		chip->of_node = chip->dev->of_node;

	if (!chip->of_node)
		return;

	if (!chip->of_xlate) {
		chip->of_dmas_n_cells = 2;
		chip->of_xlate = of_dmas_simple_xlate;
	}

	of_node_get(chip->of_node);
}

static int __jlq_dmas_config(unsigned int channel, struct dmas_ch_cfg *cfg)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned int reg_ctl1;
	unsigned int block_size = cfg->block_size;
	unsigned int flags = cfg->flags;
	unsigned int int_en;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	reg_ctl1 = desc->reg_ctl1;

	if (flags & DMAS_CFG_BLOCK_SIZE) {
		if (desc->flags & DMAS_CH_BLK_SIZE_UNIT_WORD)
			block_size /= 4;

		writel(block_size, DMAS_CH_CTL0(desc));
	}

	if (flags & DMAS_CFG_SRC_ADDR)
		writel(cfg->src_addr, DMAS_CH_SAR(desc));

	if (flags & DMAS_CFG_DST_ADDR)
		writel(cfg->dst_addr, DMAS_CH_DAR(desc));

	if (flags & DMAS_CFG_MATCH_ADDR)
		writel(cfg->match_addr, DMAS_CH_INTA(desc));

	if (flags & DMAS_CFG_BUS_WIDTH)
		reg_ctl1 = (reg_ctl1 & (~0x3)) | cfg->bus_width;

	if (flags & DMAS_CFG_PRIORITY)
		DMAS_BIT_VALUE(reg_ctl1, 0xf, 8, cfg->priority);

	if (channel_is_rx_channel(channel)) {
		if (flags & DMAS_CFG_RX_TRANS_TYPE)
			DMAS_BIT_VALUE(reg_ctl1, 0x1, 3, cfg->rx_trans_type);

		if (cfg->rx_trans_type == DMAS_TRANS_WRAP) {
			if (flags & DMAS_CFG_RX_TIMEOUT)
				DMAS_BIT_VALUE(reg_ctl1, 0xfff, 16,
					       cfg->rx_timeout);
			reg_ctl1 |= (0x1 << 2);
			writel(0xff, DMAS_REG(chip, DMAS_INTV_UNIT));
		} else {
			reg_ctl1 &= ~(0x1 << 2);
			reg_ctl1 &= ~(0xfff << 16);
		}
	} else {
		if (flags & DMAS_CFG_TX_BLOCK_MODE)
			DMAS_BIT_VALUE(reg_ctl1, 0x1, 16, cfg->tx_block_mode);

		if (flags & DMAS_CFG_TX_TRANS_MODE)
			DMAS_BIT_VALUE(reg_ctl1, 0x1, 17, cfg->tx_trans_mode);

		if ((cfg->tx_trans_mode == DMAS_TRANS_SPECIAL)
			&& (flags & DMAS_CFG_TX_FIX_VALUE))
			writel(cfg->tx_fix_value,
			       DMAS_CH_WD(chip, desc->index));
	}

	/* For CP will also write this reg. */
	desc->reg_ctl1 = readl(DMAS_CH_CTL1(desc));
	if (reg_ctl1 != desc->reg_ctl1) {
		desc->reg_ctl1 = reg_ctl1;
		writel(reg_ctl1, DMAS_CH_CTL1(desc));
	}

	if (flags & DMAS_CFG_IRQ_EN) {
		if (cfg->irq_en & (DMAS_INT_DONE | DMAS_INT_HBLK_FLUSH)) {
			int_en = readl(DMAS_REG(chip, chip->int_en0_reg));
			if (cfg->irq_en & DMAS_INT_DONE)
				int_en |= (1 << desc->index);
			if (cfg->irq_en & DMAS_INT_HBLK_FLUSH)
				int_en |= (1 << (desc->index + 16));
			writel(int_en, DMAS_REG(chip, chip->int_en0_reg));
		}

		if (cfg->irq_en & DMAS_INT_MATCH) {
			int_en = readl(DMAS_REG(chip, chip->int_en1_reg));
			if (cfg->irq_en & DMAS_INT_MATCH)
				int_en |= (1 << desc->index);
			writel(int_en, DMAS_REG(chip, chip->int_en1_reg));
		}
	}

	if (flags & DMAS_CFG_IRQ_HANDLER)
		desc->irq_handler = cfg->irq_handler;

	if (flags & DMAS_CFG_IRQ_DATA)
		desc->irq_data = cfg->irq_data;

	return 0;
}

static irqreturn_t jlq_dmas_intr_handle(int irq, void *dev_id)
{
	struct dmas_chip *chip = (struct dmas_chip *)dev_id;
	struct channel_desc *desc;
	unsigned long flags;
	unsigned long status;
	unsigned int type;
	unsigned int index;
	unsigned int channel;
	unsigned int i;

	if (!chip)
		return IRQ_HANDLED;

	spin_lock_irqsave(&chip->lock, flags);

	status = readl(DMAS_REG(chip, chip->int0_reg));

	for_each_set_bit(i, &status, 32) {
		/* Clear interrupt. */
		writel(i, DMAS_REG(chip, DMAS_INT_CLR0));

		index = DMAS_INT_CHANNEL(i);
		type = DMAS_INT_TYPE(i);
		channel = index + chip->base;
		desc = &chip->desc[index];
		spin_unlock_irqrestore(&chip->lock, flags);
		if (desc->irq_handler)
			desc->irq_handler(channel, type, desc->irq_data);
		else
			dev_warn(chip->dev,
					"Spurious irq for dmas channel%d\n",
					channel);
		spin_lock_irqsave(&chip->lock, flags);
	}

	status = readl(DMAS_REG(chip, chip->int1_reg));

	for_each_set_bit(i, &status, 16) {
		/* Clear interrupt. */
		writel(i, DMAS_REG(chip, DMAS_INT_CLR1));

		index = DMAS_INT_CHANNEL(i);
		type = DMAS_INT_MATCH;
		channel = index + chip->base;
		desc = &chip->desc[index];
		spin_unlock_irqrestore(&chip->lock, flags);
		if (desc->irq_handler)
			desc->irq_handler(channel, type, desc->irq_data);
		else
			dev_warn(chip->dev,
					"Spurious irq for dmas channel%d\n",
					channel);
		spin_lock_irqsave(&chip->lock, flags);
	}

	spin_unlock_irqrestore(&chip->lock, flags);

	return IRQ_HANDLED;
}

int jlq_dmas_request(const char *name, unsigned int channel)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	if (desc->flags & DMAS_CH_REQUESTED) {
		ret = -EEXIST;
		dev_err(chip->dev, "channel%d has been occupied", channel);
		goto out;
	}

	desc->name = name;
	desc->flags |= DMAS_CH_REQUESTED;

out:
	spin_unlock_irqrestore(&chip->lock, flags);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_request);

int jlq_dmas_free(unsigned int channel)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	if (!(desc->flags & DMAS_CH_REQUESTED)) {
		ret = -EEXIST;
		dev_err(chip->dev, "channel%d has been free", channel);
		goto out;
	}

	writel(desc->index, DMAS_REG(chip, DMAS_CLR));

	desc->flags &= ~(DMAS_CH_REQUESTED | DMAS_CH_CONFIGURED);

out:
	spin_unlock_irqrestore(&chip->lock, flags);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_free);

int jlq_dmas_config(unsigned int channel, struct dmas_ch_cfg *cfg)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	if (IS_ERR_OR_NULL(cfg))
		return -EINVAL;

	if (desc->flags & DMAS_CH_BLK_SIZE_UNIT_WORD) {
		if (cfg->block_size > 4 * chip->block_size_max)
			return -EINVAL;
	} else {
		if (cfg->block_size > chip->block_size_max)
			return -EINVAL;
	}

	if ((cfg->src_addr & 0x3) || (cfg->dst_addr & 0x3))
		return -EINVAL;

	if (channel_is_rx_channel(channel)) {
		if (cfg->rx_trans_type >= DMAS_TRANS_TYPE_MAX)
			return -EINVAL;

		if (cfg->rx_trans_type == DMAS_TRANS_WRAP)
			if ((cfg->block_size & 0x3f)
			    || (cfg->rx_timeout > 0xfff))
				return -EINVAL;
	} else {
		if (cfg->tx_trans_mode >= DMAS_BLOCK_MODE_MAX)
			return -EINVAL;

		if (cfg->tx_block_mode >= DMAS_TRANS_MODE_MAX)
			return -EINVAL;
	}

	spin_lock_irqsave(&chip->lock, flags);

	if (!(desc->flags & DMAS_CH_REQUESTED)) {
		ret = -EEXIST;
		goto out;
	}

	if (readl(DMAS_REG(chip, DMAS_STA)) & (1 << desc->index)) {
		writel(desc->index, DMAS_REG(chip, DMAS_CLR));
		dev_warn(chip->dev, "Trying to configure active channel%d\n",
				channel);
	}

	ret = __jlq_dmas_config(channel, cfg);
	if (ret)
		goto out;

	desc->flags |= DMAS_CH_CONFIGURED;

out:
	spin_unlock_irqrestore(&chip->lock, flags);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_config);

int jlq_dmas_get(unsigned int channel, unsigned int *addr)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	if (!addr)
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	*addr = readl(DMAS_CH_CA(desc));

	spin_unlock_irqrestore(&chip->lock, flags);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_get);

int jlq_dmas_state(unsigned int channel)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	ret = !!(readl(DMAS_REG(chip, DMAS_STA)) & (1 << desc->index));

	spin_unlock_irqrestore(&chip->lock, flags);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_state);

int jlq_dmas_start(unsigned int channel)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	pr_debug("%s %s %d\n", __func__, desc->name, desc->index);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	if (!(desc->flags & DMAS_CH_REQUESTED)) {
		ret = -EEXIST;
		goto out;
	}

	if (!(desc->flags & DMAS_CH_CONFIGURED)) {
		ret = -ENODEV;
		goto out;
	}

	writel(desc->index, DMAS_REG(chip, DMAS_EN));

out:
	spin_unlock_irqrestore(&chip->lock, flags);

	if (ret)
		dev_err(chip->dev, "Start channel%d failed(%d)\n",
				channel, ret);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_start);

int jlq_dmas_stop(unsigned int channel)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	if (!(desc->flags & DMAS_CH_REQUESTED)) {
		ret = -EEXIST;
		goto out;
	}

	if (!(desc->flags & DMAS_CH_CONFIGURED)) {
		ret = -ENODEV;
		goto out;
	}

	writel(desc->index, DMAS_REG(chip, DMAS_CLR));

out:
	spin_unlock_irqrestore(&chip->lock, flags);

	if (ret)
		dev_err(chip->dev, "Stop channel%d failed(%d)\n",
				channel, ret);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_stop);

int jlq_dmas_intr_enable(unsigned int channel, unsigned int intr)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned int int_en;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	if (!(desc->flags & DMAS_CH_REQUESTED)) {
		ret = -EEXIST;
		goto out;
	}

	if (!(desc->flags & DMAS_CH_CONFIGURED)) {
		ret = -ENODEV;
		goto out;
	}

	if (intr & (DMAS_INT_DONE | DMAS_INT_HBLK_FLUSH)) {
		int_en = readl(DMAS_REG(chip, chip->int_en0_reg));
		if (intr & DMAS_INT_DONE)
			int_en |= (1 << desc->index);
		if (intr & DMAS_INT_HBLK_FLUSH)
			int_en |= (1 << (desc->index + 16));
		writel(int_en, DMAS_REG(chip, chip->int_en0_reg));
	}

	if (intr & DMAS_INT_MATCH) {
		int_en = readl(DMAS_REG(chip, chip->int_en1_reg));
		if (intr & DMAS_INT_MATCH)
			int_en |= (1 << desc->index);
		writel(int_en, DMAS_REG(chip, chip->int_en1_reg));
	}

out:
	spin_unlock_irqrestore(&chip->lock, flags);

	if (ret)
		dev_err(chip->dev, "Enable channel%d's interrupt failed(%d)\n",
				channel, ret);

	return ret;
}

int jlq_dmas_intr_disable(unsigned int channel, unsigned int intr)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;
	unsigned int int_en;
	unsigned long flags;
	int ret = 0;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	if (!(desc->flags & DMAS_CH_REQUESTED)) {
		ret = -EEXIST;
		goto out;
	}

	if (!(desc->flags & DMAS_CH_CONFIGURED)) {
		ret = -ENODEV;
		goto out;
	}

	if (intr & (DMAS_INT_DONE | DMAS_INT_HBLK_FLUSH)) {
		int_en = readl(DMAS_REG(chip, chip->int_en0_reg));
		if (intr & DMAS_INT_DONE)
			int_en &= ~(1 << desc->index);
		if (intr & DMAS_INT_HBLK_FLUSH)
			int_en &= ~(1 << (desc->index + 16));
		writel(int_en, DMAS_REG(chip, chip->int_en0_reg));
	}

	if (intr & DMAS_INT_MATCH) {
		int_en = readl(DMAS_REG(chip, chip->int_en1_reg));
		if (intr & DMAS_INT_MATCH)
			int_en &= ~(1 << desc->index);
		writel(int_en, DMAS_REG(chip, chip->int_en1_reg));
	}

out:
	spin_unlock_irqrestore(&chip->lock, flags);

	if (ret)
		dev_err(chip->dev, "Disable channel%d's interrupt failed(%d)\n",
				channel, ret);

	return ret;
}
EXPORT_SYMBOL(jlq_dmas_intr_disable);

void jlq_dmas_dump(unsigned int channel)
{
	struct channel_desc *desc = NULL;
	struct dmas_chip *chip = NULL;

	desc = channel_to_desc(channel);
	if (IS_ERR_OR_NULL(desc))
		return;

	chip = desc_to_chip(desc);
	if (IS_ERR_OR_NULL(desc))
		return;

	dev_info(chip->dev, "Channel%d\n", channel);
	dev_info(chip->dev, "DMAS_STA: 0x%08x\n",
		readl(DMAS_REG(chip, DMAS_STA)));
	dev_info(chip->dev, "DMAS_INT_RAW0: 0x%08x\n",
		readl(DMAS_REG(chip, DMAS_INT_RAW0)));
	dev_info(chip->dev, "DMAS_INT_EN0: 0x%08x\n",
		readl(DMAS_REG(chip, chip->int_en0_reg)));
	dev_info(chip->dev, "DMAS_INT0: 0x%08x\n",
		readl(DMAS_REG(chip, chip->int0_reg)));
	dev_info(chip->dev, "DMAS_INT_RAW1: 0x%08x\n",
		readl(DMAS_REG(chip, DMAS_INT_RAW1)));
	dev_info(chip->dev, "DMAS_INT_EN1: 0x%08x\n",
		readl(DMAS_REG(chip, chip->int_en1_reg)));
	dev_info(chip->dev, "DMAS_INT1: 0x%08x\n",
		readl(DMAS_REG(chip, chip->int1_reg)));
	dev_info(chip->dev, "DMAS_INTV_UNIT: 0x%08x\n",
		readl(DMAS_REG(chip, DMAS_INTV_UNIT)));
	dev_info(chip->dev, "DMAS_CH_CA: 0x%08x\n", readl(DMAS_CH_CA(desc)));
	dev_info(chip->dev, "DMAS_CH_SAR: 0x%08x\n", readl(DMAS_CH_SAR(desc)));
	dev_info(chip->dev, "DMAS_CH_DAR: 0x%08x\n", readl(DMAS_CH_DAR(desc)));
	dev_info(chip->dev, "DMAS_CH_CTL0: 0x%08x\n",
			readl(DMAS_CH_CTL0(desc)));
	dev_info(chip->dev, "DMAS_CH_CTL1: 0x%08x\n",
			readl(DMAS_CH_CTL1(desc)));
}
EXPORT_SYMBOL(jlq_dmas_dump);

static const struct of_device_id jlq_dmas_match[] = {
	{ .compatible = "jlq,jlq-dmas", },
	{}
};
MODULE_DEVICE_TABLE(of, jlq_dmas_match);

static int jlq_dmas_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct dmas_chip *chip;
	static unsigned int channel;
	int ret = 0;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct dmas_chip),
			GFP_KERNEL);

	chip->regbase = of_iomap(node, 0);
	if (!chip->regbase)
		return -ENOMEM;

	of_property_read_u32(pdev->dev.of_node, "nr_channel", &chip->nchannel);
	of_property_read_u32(pdev->dev.of_node, "reg_map_type",
			     &chip->reg_map_type);
	of_property_read_u32(pdev->dev.of_node, "block_size_max",
			     &chip->block_size_max);
	if (of_find_property(pdev->dev.of_node, "word_unit_channel", NULL))
		of_property_read_u32(pdev->dev.of_node, "word_unit_channel",
				     &chip->special_channel);
	else
		chip->special_channel = -1;

	chip->irq = irq_of_parse_and_map(node, 0);
	chip->dev = &pdev->dev;
	chip->base = channel;
	channel += chip->nchannel;
	chip->name = dev_name(&pdev->dev);

	if (chip->reg_map_type == DMAS_REG_MAP_TYPE0) {
		chip->int_en0_reg = DMAS_INT_EN0_0;
		chip->int0_reg = DMAS_INT0_0;
		chip->int_en1_reg = DMAS_INT_EN1_0;
		chip->int1_reg = DMAS_INT1_0;
	} else if (chip->reg_map_type == DMAS_REG_MAP_TYPE1) {
		chip->int_en0_reg = DMAS_INT_EN0_1;
		chip->int0_reg = DMAS_INT0_1;
		chip->int_en1_reg = DMAS_INT_EN1_1;
		chip->int1_reg = DMAS_INT1_1;
	};

	spin_lock_init(&chip->lock);

	ret = dmaschip_add(chip);
	if (ret)
		return ret;

	of_dmaschip_add(chip);

	dev_info(&pdev->dev, "JLQ dmas driver initialized\n");
	return 0;
}

static struct platform_driver jlq_dmas_driver = {
	.probe = jlq_dmas_probe,
	.driver = {
		.name = "jlq-dmas",
		.owner = THIS_MODULE,
		.of_match_table = jlq_dmas_match,
	},
};

static int __init jlq_dmas_init(void)
{
	return platform_driver_register(&jlq_dmas_driver);
}

#ifdef MODULE
module_init(jlq_dmas_init)
#else
arch_initcall(jlq_dmas_init);
#endif

MODULE_DESCRIPTION("JLQ dmas driver");
MODULE_LICENSE("GPL");
