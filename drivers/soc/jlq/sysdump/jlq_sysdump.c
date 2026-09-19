// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/freezer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <asm/irq_regs.h>
#include <asm/cacheflush.h>
#include <asm-generic/cacheflush.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <jlq_wdt.h>
#include <linux/limits.h>
#include <linux/of_reserved_mem.h>
#include <linux/memblock.h>
#include <asm/io.h>
#include <linux/smp.h>
#if IS_ENABLED(CONFIG_JLQ_ISR_MONITOR)
#include <soc/jlq/jr510/isr_monitor.h>
#endif

#define TOP_CHIP_RSTN_CTL0	0x0
#define TOP_CHIP_RSTN_CTL2	0x8
#define START_REG0	0x14
#define START_REG0_USER_MSK	GEN_MASK(31, 7)

/* bits of TOP_CHIP_RSTN_CTL2 */
#define START_REG_RAMDUMP_DEBUG_EN       (1 << 19)
#define RAM_DUMP_SAVE_REBOOT_LOG         (1 << 18)
#define ABNORMAL_RST_PWR2SYS_MSK_BIT    BIT(8)
#define OVERHEAT_RST_PWR2SYS_MSK_BIT    BIT(7)
#define PPU_RESET_MSK_BIT               BIT(4)
#define CM4_WDTRST_PWR2SYS_MSK_BIT      BIT(3)
#define NONE_SEC_WDTRST_PWR2SYS_MSK_BIT BIT(2)
#define SEC_WDRST_PWR2SYS_MSK_BIT       BIT(1)
#define SFRST_PWR2SYS_MSK_BIT           BIT(0)

struct minidump_region {
	uint32_t base;
	uint32_t size;
	void __iomem *ioaddr;
};

struct jlq_sysdump_dev {
	struct device *dev;
	void __iomem *base;
	void __iomem *cm4_mem_start;
	void *cm4_dump_mem;

	u32	cm4_dump_size;
	u32	sysdump_enable;
	u16	force_dump_gpio;
	u8	force_dump_enable;
	u32	force_dump_irq;
	u32	abnormal_rst_irq;
	u32	cm4_wdog_bite_irq;

	u32 *minidump_addr;	/* memory to save minidump */
	u32 minidump_size;	/* size of minidump memory region */
	u32	minidump_region_num; /* number of regions to save */
	struct minidump_region *minidump_region;
};

struct jlq_sysdump_dev *sdev;
static void jlq_request_dump(void)
{
	local_irq_disable();
	jlq_trigger_wdog_bite();
#if IS_ENABLED(CONFIG_JGKI)
	flush_cache_all();
#endif
}

void cm4_dump(struct jlq_sysdump_dev *s)
{
	memcpy_fromio(s->cm4_dump_mem, s->cm4_mem_start, s->cm4_dump_size);
}

void sysdump_collect_minidump(struct jlq_sysdump_dev *s)
{
	int i, j;
	struct minidump_region *region = s->minidump_region;
	u32 *p = (u32 *)s->minidump_addr;
	u32 io_size = 0;

	/* Emmc driver will disable apb clk which register is not accesible
	 * after enter runtime suspend and ramdump flow will be interrupted
	 */
	return;
	for (i = 0; i < s->minidump_region_num; i++) {
		if (!region->size || region->base >= U32_MAX)
			continue;

		if (region->size + io_size > s->minidump_size)
			break;
		*p++ = region->base;
		*p++ = region->size;

		for (j = 0; j < region->size; j += 4)
			*p++ = readl(region->ioaddr + j);
		region++;
	}
}
static int sysdump_panic_notifier(struct notifier_block *nb,
	unsigned long action, void *p)
{
	sysdump_collect_minidump(sdev);
#if IS_ENABLED(CONFIG_JLQ_ISR_MONITOR)
	show_isr_monitor();
#endif
	if (sdev->sysdump_enable && (1 == jlq_sysdump_enabled())) {
		cm4_dump(sdev);
		jlq_request_dump();
	}
	return NOTIFY_DONE;
}

static struct notifier_block force_dump_nb = {
	.notifier_call = sysdump_panic_notifier,
	.priority = INT_MIN,
};

static int reboot_edl_notifier(struct notifier_block *nb,
	unsigned long action, void *p)
{
	char *cmd = (char *)p;
	uint32_t reg = readl(sdev->base + START_REG0);

	if (!cmd)
		return NOTIFY_DONE;

	if (action == SYS_RESTART) {
		if (!strcmp(cmd, "edl")) {
			pr_info("emergency download\n");
			writel(0xa0000000, sdev->base + START_REG0);
		} else if (strcmp(cmd, "bootloader")) {
			/* for none reboot bootloader mode, reboot to
			 * sdi to save pstore log
			 */
			reg |= RAM_DUMP_SAVE_REBOOT_LOG;
			writel(reg, sdev->base + START_REG0);
		}
	} else
		/* clear ramdump cooike */
		writel(0x0, sdev->base + START_REG0);

	return NOTIFY_DONE;
}

static struct notifier_block reboot_edl_nb = {
	.notifier_call = reboot_edl_notifier,
	.priority = INT_MIN,
};

static irqreturn_t jlq_force_dump_interrupt(int irq, void *dev_id)
{
	panic("force dump trigger\n");
}

static irqreturn_t abnormal_interrupt(int irq, void *dev_id)
{
	panic("PMIC abnormal rest\n");
}
static irqreturn_t cm4_wdog_bite_interrupt(int irq, void *dev_id)
{
	panic("cm4 watchdog bite\n");
}

static int jlq_sysdump_parse_dt(struct platform_device *pdev,
	struct jlq_sysdump_dev *dev)
{
	int ret = 0, i;
	char *key;
	struct resource *res;
	uint32_t *buf;
	struct reserved_mem *rmem;
	struct device_node *node;

	key = "shutdown_reg";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!res) {
		dev_err(&pdev->dev, "get %s failed\n", key);
		return -ENOMEM;
	}
	dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	key = "cm4_ram";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key) ;
	if (!res) {
		dev_err(&pdev->dev, "get %s memory failed\n", key);
		return -ENOMEM;
	}
	dev->cm4_mem_start = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->cm4_mem_start)) {
		dev_err(&pdev->dev, "ioremap %s memory failed\n", key);
		return -ENOMEM;
	}

	key = "cm4_dump";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key) ;
	if (!res) {
		dev_err(&pdev->dev, "get %s memory failed\n", key);
		return -ENOMEM;
	}

	/* reserve ddr to dump cm4 sram */
	node = of_parse_phandle(pdev->dev.of_node, "memory-region", 1);
	if (!node) {
		dev_info(&pdev->dev, "no memory region found\n");
	} else {
		rmem = of_reserved_mem_lookup(node);
		if (!rmem) {
			dev_warn(&pdev->dev, "fail get cm4 reservememory \n");
			return -ENOMEM;
		}
		of_node_put(node);
	}

	dev->cm4_dump_mem = phys_to_virt(rmem->base);
	dev->cm4_dump_size = rmem->size;

	key = "mini_dump_size";
	ret = of_property_read_u32(pdev->dev.of_node, key, &dev->minidump_size);
	if (ret) {
		dev_info(&pdev->dev, "no mini_dump_size specified, using default\n");
		dev->minidump_size = SZ_64K;
	}

	key = "mini_dump_region";
	dev->minidump_region_num =
		of_property_count_u32_elems(pdev->dev.of_node, key);

	if (dev->minidump_region_num < 0)
		return dev->minidump_region_num;

	dev->minidump_region_num /= 2;
	dev->minidump_region = devm_kzalloc(
		&pdev->dev,
		sizeof(struct minidump_region) * dev->minidump_region_num,
		GFP_KERNEL);

	if (IS_ERR_OR_NULL(dev->minidump_region))
		return -ENOMEM;

	buf = devm_kzalloc(&pdev->dev,
		8 * dev->minidump_region_num, GFP_KERNEL);
	ret = of_property_read_u32_array(pdev->dev.of_node,
		"mini_dump_region",
		buf,
		2 * dev->minidump_region_num);

	for (i = 0; i < dev->minidump_region_num; i++) {
		struct minidump_region *region = &dev->minidump_region[i];
		region->base = buf[i * 2];
		region->size = buf[i * 2 + 1];
		region->ioaddr = ioremap(region->base, region->size);
	}
	dev->minidump_addr = devm_kzalloc(&pdev->dev, dev->minidump_size,
		GFP_KERNEL);
	if (IS_ERR_OR_NULL(dev->minidump_addr)) {
		dev_err(&pdev->dev, "alloc minidupm failed\n");
		return -ENOMEM;
	}

	key = "enable_sysdump";
	dev->sysdump_enable = of_property_read_bool(pdev->dev.of_node, key);
	if (!dev->sysdump_enable) {
		dev_info(&pdev->dev, "sydump not enabled, return\n");
		ret = -ENODEV;
		goto error;
	}

	key = "force-dump-enable";
	dev->force_dump_enable = of_property_read_bool(pdev->dev.of_node, key);
	if (!dev->force_dump_enable)
		return 0;

	dev->force_dump_gpio = of_get_named_gpio_flags(pdev->dev.of_node,
				"force-dump-gpio", 0, NULL);
	if (dev->force_dump_gpio < 0) {
		dev_info(&pdev->dev, "no force-dump gpio specifiled\n");
		ret = -ENODEV;
		goto error;
	}

	if (!gpio_is_valid(dev->force_dump_gpio)) {
		dev_err(&pdev->dev, "dump gpio invalid\n");
		dev->force_dump_enable = 0;
		goto error;

	}
	ret = devm_gpio_request(&pdev->dev, dev->force_dump_gpio, "force-dump");
	if (ret < 0) {
		dev_err(&pdev->dev, "err%d request gpio\n", ret);
		goto error;
	}
	dev->force_dump_irq = gpio_to_irq(dev->force_dump_gpio);
	return 0;
error:
	return ret;
}

int jlq_sysdump_enabled(void)
{
	uint32_t val;

	if (!sdev)
		return 0;

	val = readl(sdev->base + START_REG0);
	return !!(val & START_REG_RAMDUMP_DEBUG_EN);
}
EXPORT_SYMBOL_GPL(jlq_sysdump_enabled);

/*
 * find memory-region which points to reserve memory and free the memory
 */
int jlq_free_reserve_memory(struct platform_device *pdev, int index)
{
	struct device_node *node;
	unsigned long start, end, pfn;
	struct reserved_mem *rmem;
	int ret;
	int no_map;

	node = of_parse_phandle(pdev->dev.of_node, "memory-region", index);
	if (!node) {
		dev_info(&pdev->dev, "no memory region found\n");
		return -ENOMEM;
	} else {
		rmem = of_reserved_mem_lookup(node);
		if (!rmem) {
			dev_warn(&pdev->dev, "failed to get memory region\n");
			return -ENOMEM;
		}
		no_map = of_property_read_bool(node, "no-map");
		of_node_put(node);
	}

	start = rmem->base >> PAGE_SHIFT;
	end = (rmem->base + rmem->size) >> PAGE_SHIFT;

	if (no_map) {
		dev_warn(&pdev->dev, "memory had no-map prop, failed\n");
		return -ENOMEM;
	} else {
		ret = memblock_free(rmem->base, rmem->size);
		if (ret) {
			dev_err(&pdev->dev, "free memblock failed\n");
			return -ENOMEM;
		}
		dev_info(&pdev->dev, "free memblock\n");
	}

	for (pfn = start; pfn < end; pfn++)
		free_reserved_page(pfn_to_page(pfn));

	dev_info(&pdev->dev, "free base 0x%lx size 0x%lx\n",
		rmem->base, rmem->size);

	return 0;
}
EXPORT_SYMBOL_GPL(jlq_free_reserve_memory);

static int jlq_sysdump_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct jlq_sysdump_dev *dev;

	dev = devm_kzalloc(&pdev->dev, sizeof(struct jlq_sysdump_dev), GFP_KERNEL);
	if (IS_ERR_OR_NULL(dev)) {
		dev_err(&pdev->dev, "alloc mem failed\n");
		return -ENOMEM;
	}

	ret = jlq_sysdump_parse_dt(pdev, dev);
	if (ret) {
		dev_err(&pdev->dev, "parse dt failed\n");
		return -ENODEV;
	}

	if (dev->force_dump_enable) {
		ret = gpio_direction_input(dev->force_dump_gpio);
		if (ret < 0) {
			dev_err(&pdev->dev, "error set gpio input\n");
			goto error;
		}
		ret = devm_request_irq(&pdev->dev,
				dev->force_dump_irq,
				jlq_force_dump_interrupt,
				IRQF_TRIGGER_HIGH,
				"gpio-dump",
				(void *)dev);
		if (ret) {
			dev_err(&pdev->dev, "error force-dump irq request\n");
			goto error;
		}
		irq_set_irq_wake(dev->force_dump_irq, 1);
	}

	dev_info(&pdev->dev, "request abnormal irq\n");
	dev->abnormal_rst_irq = platform_get_irq_byname(pdev, "abnormal_irq");
	if (dev->abnormal_rst_irq < 0) {
		dev_err(&pdev->dev, "get abnormal rst failed\n");
		ret =  -ENODEV;
		goto error;
	}

	if (devm_request_irq(&pdev->dev, dev->abnormal_rst_irq,
		abnormal_interrupt, IRQF_TRIGGER_HIGH, "abnormal", NULL)) {
		ret = -EIO;
		dev_err(&pdev->dev, "request abnormal irq failed\n");
		goto error;
	}

	irq_set_irq_wake(dev->abnormal_rst_irq, 1);

	dev->cm4_wdog_bite_irq = platform_get_irq_byname(pdev, "cm4_wdog_bite");
	if (dev->cm4_wdog_bite_irq < 0) {
		dev_err(&pdev->dev, "get cm4 watchdog bite irq failed\n");
		ret =  -ENODEV;
		goto error;
	}
	irq_set_irq_wake(dev->cm4_wdog_bite_irq, 1);

	if (devm_request_irq(&pdev->dev, dev->cm4_wdog_bite_irq,
		cm4_wdog_bite_interrupt, IRQF_TRIGGER_HIGH,
		"cm4_wdog_bite", NULL)) {
		ret = -EIO;
		dev_err(&pdev->dev, "request cm4 watchdog bite irq failed\n");
		goto error;
	}
	dev->dev = &pdev->dev;
	sdev = dev;
	atomic_notifier_chain_register(&panic_notifier_list, &force_dump_nb);

	/* free sdi_mem when SDI is disabled
	 * currenly, we needs SDI to do pstore save
	 */
#if 0
	if (0 == jlq_sysdump_enabled()) {
		jlq_free_reserve_memory(pdev, 0);
		jlq_free_reserve_memory(pdev, 1);
	}
#endif

	pr_info("Success to register sysdump driver\n");
	return 0;
error:
	gpio_free(dev->force_dump_gpio);
	return ret;
}

static int jlq_sysdump_remove(struct platform_device *pdev)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &force_dump_nb);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id jlq_sysdump_of_match[] = {
	{ .compatible  = "jlq,sysdump", },
	{ /*  sentinel */ }
};
MODULE_DEVICE_TABLE(of, jlq_sysdump_of_match);
#endif

static struct platform_driver jlq_sysdump_driver = {
	.probe = jlq_sysdump_probe,
	.remove = jlq_sysdump_remove,
	.driver = {
		.name = "jlq,sysdump",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(jlq_sysdump_of_match),
	},
};

static int __init jlq_sysdump_init(void)
{
	return platform_driver_register(&jlq_sysdump_driver);
}

static void __exit jlq_sysdump_exit(void)
{
       platform_driver_unregister(&jlq_sysdump_driver);
}

module_init(jlq_sysdump_init);
module_exit(jlq_sysdump_exit);

MODULE_DESCRIPTION("JLQ sysdump driver");
MODULE_LICENSE("GPL");
