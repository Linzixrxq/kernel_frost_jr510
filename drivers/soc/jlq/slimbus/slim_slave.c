/*
 * Copyright (c)2019-2021   JLQ Technology Co.,Ltd
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
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include "slim_inc.h"

void wcd9306_enable(void)
{
    void __iomem *v;

    PR_INFO(LOGTAG"wcd9306_enable\n");

    /*  //GPIO13
        0x3450d040 =0x20002000
        0x3450d000 =0x20002000
    */
    v = ioremap_nocache(0x3450d000, 0x80);
    writel(0x20002000, v + 0x00);
    writel(0x20002000, v + 0x40);
    iounmap(v);
}

void wcd9326_enable(void)
{
    void __iomem *v;

    PR_INFO(LOGTAG"wcd9326_enable\n");

    /*  //GPIO14
        0x3450d040 =0x40004000
        0x3450d000 =0x40004000
    */
    v = ioremap_nocache(0x3450d000, 0x80);
    writel(0x40004000, v + 0x00);
    writel(0x40004000, v + 0x40);
    iounmap(v);
}

void wcn3950_enable(void)
{
    PR_INFO(LOGTAG"wcn3950_enable, now nothing.\n");
}

void wcx_enable(void)
{
#ifdef WCD9306
    wcd9306_enable();
#elif defined(WCD9326)
    wcd9326_enable();
#elif defined(WCN3950)
    wcn3950_enable();
#endif
}

MODULE_LICENSE("GPL");

