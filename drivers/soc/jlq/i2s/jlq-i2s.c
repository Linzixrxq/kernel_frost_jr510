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
 */
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <soc/jlq/jr510/jlq-i2s.h>
#include "adsp_subsys.h"


#define jlq_reg_read(addr)   readl(addr)
#define jlq_reg_write(val, addr)   writel((val), (addr))

//int jlq_i2s_config(u32 i2s_id);
//int jlq_i2s_trigger(u32 i2s_id, u32 direction, u32 cmd);
//int jlq_i2s_shutdown(u32 i2s_id);
//int jlq_i2s_set_param(u16 port_id, union jlq_i2s_port_config *i2s_port_config);

struct jlq_i2s_aif_config i2s_configs[PCM_I2S_MAX] = {
	{
		.playback_open_times = 0,
		.record_open_times = 0,
	},
	{
		.playback_open_times = 0,
		.record_open_times = 0,
	},
};

int jlq_i2s_config(u32 i2s_id)
{
	struct jlq_i2s_aif_config *config = (struct jlq_i2s_aif_config *)&i2s_configs[i2s_id];
	u32 value;
	u32 div;

#ifdef I2S_CLK_DISABLE
	pr_crit("%s: i2s[%d] clk has been disabled.\n", __func__, i2s_id);
	return -1;
#endif

	pr_info("%s: i2s_id=%d\n", __func__,i2s_id);
	pr_info("%s: format=%d,ms_mode=%d,mclk_rate=%d\n", __func__,
			config->format,config->ms_mode,config->mclk_rate);
	switch (config->format) {
	case DAIFMT_I2S:
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value &= ~(1 << I2S_MODE);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_MODE));

		/* in I2S mode: set data valid bits */
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_FSYNC_CFG));
		value &= ~FSYNC_BIT_MASK;
		switch (config->bits_per_sample) {
		case FORMAT_S16:
			value |= (0x00 << FSYNC_BIT);
			break;
		case FORMAT_S24:
			value |= (0x01 << FSYNC_BIT);
			break;
		case FORMAT_S32:
			value |= (0x10 << FSYNC_BIT);
			break;
		default:
			return -EINVAL;
		}
		jlq_reg_write(value, PCM_I2S_REG(config,
					PCM_I2S_FSYNC_CFG));
		break;
	case DAIFMT_PCM:
		/* in PCM mode: set slot num */
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value &= ~SLOT_NUM_MASK;
		value |= ((config->channels - 1) << PCM_SLOT_NUM);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_MODE));
		break;
	case DAIFMT_TDM:
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value |= (1 << I2S_MODE);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_MODE));

		/* in TDM mode: set channel num */
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_FSYNC_CFG));
		value &= ~CHANNEL_NUM_MASK;
		value |= ((config->channels - 1) << CHANNEL_NUM);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_FSYNC_CFG));
		break;
	default:
		return -EINVAL;
	}

	if (config->ms_mode == SLAVE_MODE) {
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value |= (1 << SLAVE_EN);
		jlq_reg_write(value, PCM_I2S_REG(config,
					PCM_I2S_MODE));
	} else {
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value &= ~(1 << SLAVE_EN);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_MODE));

		/* in master mode: set sclk & fsync div*/
		div = config->mclk_rate /
					(config->rate * config->channels * 32);
		value = (div << SCLK_DIV);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_SCLK_CFG));
		div = config->channels * 32;
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_FSYNC_CFG));
		value &= ~DIV_MASK;
		value |= (div << FSYNC_DIV);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_FSYNC_CFG));
	}

	if (config->dly_mode) {
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value |= (1 << DLY_MODE);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_MODE));
	} else {
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value &= ~(1 << DLY_MODE);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_MODE));
	}

	return 0;
}

int jlq_i2s_trigger(u32 i2s_id, u32 direction, u32 cmd)
{
	struct jlq_i2s_aif_config *config = (struct jlq_i2s_aif_config *)&i2s_configs[i2s_id];
	u32 value;

#ifdef I2S_CLK_DISABLE
	pr_crit("%s: i2s[%d] clk has been disabled.\n", __func__, i2s_id);
	return -1;
#endif

	mutex_lock(&config->i2s_lock);

	switch (cmd) {
	case TRIGGER_START:
		if (direction == PLAYBACK_STREAM) {
			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value |= (1 << FLUSH_TRAN_BUF);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));

			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value &= ~(1 << FLUSH_TRAN_BUF);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));

			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value |= (1 << TRAN_EN);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));
			config->playback_open_times++;
		} else {
			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value |= (1 << FLUSH_REC_BUF);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));

			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value &= ~(1 << FLUSH_REC_BUF);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));

			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value |= (1 << REC_EN);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));
			config->record_open_times++;
		}

		if (config->format == DAIFMT_PCM) {
			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_PCM_EN));
			value |= (1 << PCM_EN);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_PCM_EN));
		} else {
			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value |= (1 << I2S_EN);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));
		}
		clk_set_rate(config->clk, config->mclk_rate);
		clk_prepare_enable(config->clk);
		break;
	case TRIGGER_STOP:
		if (direction == PLAYBACK_STREAM &&
					config->playback_open_times > 0) {
			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value &= ~(1 << TRAN_EN);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));
			config->playback_open_times--;
		} else if (direction == CAPTURE_STREAM &&
					config->record_open_times > 0) {
			value = jlq_reg_read(PCM_I2S_REG(config,
						PCM_I2S_MODE));
			value &= ~(1 << REC_EN);
			jlq_reg_write(value, PCM_I2S_REG(config,
				PCM_I2S_MODE));
			config->record_open_times--;
		}

		break;
	default:
		mutex_unlock(&config->i2s_lock);
		return -EINVAL;
	}

	mutex_unlock(&config->i2s_lock);

	return 0;
}

int jlq_i2s_shutdown(u32 i2s_id)
{
	struct jlq_i2s_aif_config *config = (struct jlq_i2s_aif_config *)&i2s_configs[i2s_id];
	u32 value;

#ifdef I2S_CLK_DISABLE
	pr_crit("%s: i2s[%d] clk has been disabled.\n", __func__, i2s_id);
	return -1;
#endif

	mutex_lock(&config->i2s_lock);

	if (config->playback_open_times || config->record_open_times) {
		mutex_unlock(&config->i2s_lock);
		pr_info("%s: playback_open_times=%d, record_open_times=%d \n",
				__func__,config->playback_open_times, config->record_open_times);
		return 0;
	}

	if (config->format == DAIFMT_PCM) {
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_PCM_EN));
		value &= ~(1 << PCM_EN);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_PCM_EN));
	} else {
		value = jlq_reg_read(PCM_I2S_REG(config,
					PCM_I2S_MODE));
		value &= ~(1 << I2S_EN);
		jlq_reg_write(value, PCM_I2S_REG(config,
			PCM_I2S_MODE));
	}
	clk_disable_unprepare(config->clk);
	mutex_unlock(&config->i2s_lock);

	return 0;
}

/* description: set I2S aif param. option api.
 *
 */
int jlq_i2s_set_param(u16 port_id, u16 mode, union jlq_i2s_port_config *i2s_port_config)
{
	struct jlq_i2s_aif_config *config = NULL;

#ifdef I2S_CLK_DISABLE
	pr_crit("%s: i2s port_id[%d] clk has been disabled.\n", __func__, port_id);
	return -1;
#endif

	config = (struct jlq_i2s_aif_config *)&i2s_configs[port_id];
	switch (mode) {
	case DAIFMT_I2S:
		config->channels = i2s_port_config->i2s.num_channels;
		config->rate = i2s_port_config->i2s.sample_rate;
		break;
	case DAIFMT_PCM:
		config->channels = i2s_port_config->pcm.num_channels;
		config->rate = i2s_port_config->pcm.sample_rate;
		break;
	case DAIFMT_TDM:
		config->channels = i2s_port_config->tdm.num_channels;
		config->rate = i2s_port_config->tdm.sample_rate;
		break;
	default:
		pr_info("%s: port_id(%d) invalid. \n", __func__, port_id);
		return -EINVAL;
	}

	return 0;
}

static int __init jlq_i2s_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 id = 0;
	struct jlq_i2s_aif_config *config = NULL;

	ret = of_property_read_u32(pdev->dev.of_node, "i2s_id", &id);

	config = (struct jlq_i2s_aif_config *)&i2s_configs[id];

	config->reg_base = of_iomap(pdev->dev.of_node, 0);
	if (!config->reg_base) {
		pr_err("%s: get reg base error, config->reg_base = %p.\n",
			__func__, config->reg_base);
		return -ENOMEM;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"i2s_ms_mode", &config->ms_mode);
	if (ret < 0) {
		pr_err("%s: i2s[%d] get i2s_ms_mode error.\n", __func__, id);
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"i2s_delay_mode", &config->dly_mode);
	if (ret < 0) {
		pr_err("%s: i2s[%d] get i2s_delay_mode error.\n", __func__, id);
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"clock_rate", &config->mclk_rate);
	if (ret < 0) {
		pr_err("%s: i2s[%d] get clock_rate error.\n", __func__, id);
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"i2s_format", &config->format);
	if (ret < 0) {
		pr_err("%s: i2s[%d] get i2s_format error.\n", __func__, id);
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"i2s_bits_per_sample",
				&config->bits_per_sample);
	if (ret < 0) {
		pr_err("%s: i2s[%d] get i2s_bits_per_sample error.\n",
			__func__, id);
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"i2s_channels", &config->channels);
	if (ret < 0) {
		pr_err("%s: i2s[%d] get i2s_channels error.\n", __func__, id);
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				"i2s_rate", &config->rate);
	if (ret < 0) {
		pr_err("%s: i2s[%d] get i2s_rate error.\n", __func__, id);
		return ret;
	}

	config->clk = devm_clk_get(&pdev->dev, "jlq_i2s_clk");
	if (IS_ERR(config->clk)) {
		pr_err("%s: jlq-i2s-%d clk not available.\n", __func__, id);
		return -EINVAL;
	}

	mutex_init(&config->i2s_lock);

#ifdef I2S_CLK_DISABLE
    if (adsp_state_get() != ADSP_STAT_POWERUP
        && adsp_state_get() != ADSP_STAT_LOADED
        && adsp_state_get() != ADSP_STAT_RUNNING) {
        dev_dbg(&pdev->dev, "defering %s, adsp NOT dereset or running\n", __func__);
        return -EPROBE_DEFER;
    }

	pr_crit("%s: i2s[%d] clk will be disabled.\n", __func__, id);
	clk_prepare_enable(config->clk);
	clk_disable_unprepare(config->clk);
#endif

	return 0;
}

static int __exit jlq_i2s_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id jlq_i2s_table[] = {
	{ .compatible = "jlq,jlq-i2s", },
	{}
};
MODULE_DEVICE_TABLE(of, jlq_i2s_table);

static struct platform_driver jlq_i2s_driver = {
	.driver = {
		.name = "jlq-i2s",
		.owner = THIS_MODULE,
		.of_match_table = jlq_i2s_table,
	},
	.remove = __exit_p(jlq_i2s_remove),
};

static int __init jlq_i2s_init(void)
{
	return platform_driver_probe(&jlq_i2s_driver,
				jlq_i2s_probe);
}

static void __exit jlq_i2s_exit(void)
{
	platform_driver_unregister(&jlq_i2s_driver);
}

device_initcall(jlq_i2s_init);
module_exit(jlq_i2s_exit);

MODULE_DESCRIPTION("JlQ SOC I2S Driver");
MODULE_AUTHOR("jlqer <jlq@jlq.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jlq-i2s");

