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
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb/phy.h>
#include <linux/usb/otg.h>
#include <linux/usb/hcd.h>
#include <linux/usb/gadget.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/power_supply.h>
#include "core.h"
#include <linux/extcon-provider.h>
#include <linux/usb/typec.h>

#define DRIVER_NAME "jlq_dwc2_otg_phy"

#define PHYCTL_NON_DRIV		(0x1 << 4)
#define PHYCTL_SUSPENDM_DIS	(0x1 << 3)
#define PHYCTL_VBUSVLDEXT       (0x1 << 1)
#define PHYCTL_VBUSVLDEXTSEL    (0x1 << 0)

#ifdef CONFIG_ARCH_JA301
#define USB_CTL_RSTCTL		(0x100)
#define USB_CTL_PHYCTL		(0x104)
#define USB_CTL_PHYPARAM	(0x108)
#define USB_CTL_HCLKEN		(0x000)
#define CTL_HCLKEN_EN		(0x1 << 16)
#else
#define USB_CTL_RSTCTL		(0x000)
#ifdef CONFIG_ARCH_JA310
#define USB_CTL_PHYCTL		(0x004)
#define USB_CTL_PHYPARAM	(0x008)
#else
#define USB_CTL_PHYCTL		(0x000)
#define USB_CTL_PHYPARAM	(0x004)
#endif
#define USB_CTL_HCLKEN		(0x00C)
#define CTL_HCLKEN_EN		(0x1 << 0)
#endif

#define PHYCTL_CHGDET(x)	((x & 0x400) >> 10)
#define PHYCTL_CHRGSEL_DM	(0x1 << 9)
#define PHYCTL_CHRGSEL_DP	(~(0x1 << 9))
#define PHYCTL_SRC_EN		(0x1 << 8)
#define PHYCTL_DET_EN		(0x1 << 7)

#if defined(CONFIG_ARCH_JA301) || defined(CONFIG_ARCH_JA310)
#define RSTCTL_PORT_SET		(0x1 << 3)
#define RSTCTL_POR_SET		(0x1 << 2)
#define RSTCTL_POR_CLR		(0x0 << 2)
#define RSTCTL_HRSTN_SET	(0x0 << 1)
#define RSTCTL_HRSTN_CLR	(0x1 << 1)
#define RSTCTL_PRSTN_SET	(0x0 << 0)
#define RSTCTL_PRSTN_CLR	(0x1 << 0)
#else
#define WRITE_ENABLE(x)		(0x1 << (16 + x))
#define RSTCTL_POR_SET		(WRITE_ENABLE(2) | (0x1 << 2))
#define RSTCTL_POR_CLR		(WRITE_ENABLE(2) | (0x0 << 2))
#define RSTCTL_HRSTN_SET	(WRITE_ENABLE(1) | (0x0 << 1))
#define RSTCTL_HRSTN_CLR	(WRITE_ENABLE(1) | (0x1 << 1))
#define RSTCTL_PRSTN_SET	(WRITE_ENABLE(0) | (0x0 << 0))
#define RSTCTL_PRSTN_CLR	(WRITE_ENABLE(0) | (0x1 << 0))
#endif

#define USB_RSTCTL	(0x0 << 10) | (0x1 << 1)

#define USB2PHY_VDD_VOL_MIN	800000
#define USB2PHY_VDD_VOL_MAX	800000

#define USB2PHY_1P8_VOL_MIN           1800000 /* uV */
#define USB2PHY_1P8_VOL_MAX           1800000 /* uV */
#define USB2PHY_1P8_HPM_LOAD          30000   /* uA */

#define USB2PHY_3P3_VOL_MIN		3300000 /* uV */
#define USB2PHY_3P3_VOL_MAX		3300000 /* uV */
#define USB2PHY_3P3_HPM_LOAD		30000	/* uA */

enum jlq_usb_state {
	USB_STATE_UNDEFINED = 0,

	USB_STATE_IDLE,
	USB_STATE_PERIPHERAL,
	USB_STATE_HOST,
};

struct jlq_otg {
	struct usb_phy phy;
	struct platform_device *pdev;
	struct jlq_dwc2_otg_platform_data *pdata;

	void __iomem *regs;
	void __iomem *pwr_usb_rst;
	void __iomem *usb_rst;
	struct clk *clk;
	struct clk *hclk;
	int parm;

	struct regulator *vdd;
	struct regulator *vdda33;
	struct regulator *vdda18;

	bool vbus_active;
	bool id_active;
	enum jlq_usb_state state;
	struct workqueue_struct *sm_usb_wq;
	struct delayed_work sm_work;

	int cs_gpio;
	int reset_gpio;

	unsigned int init_flag:1;
	unsigned int vol_en_flag:1;
	unsigned int no_plug_notify_flag:1;
	unsigned int init_sync:1;
	unsigned int set_host_flag:1;

	struct wakeup_source *wakesrc;
	struct pm_qos_request pm_qos;
	struct mutex set_lock;

	struct mutex typec_lock;
	struct typec_port *typec_port;
	struct typec_capability typec_caps;
	struct typec_partner *typec_partner;
	struct typec_partner_desc typec_partner_desc;
};

static inline u32 phy_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void phy_writel(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}

static void jlq_dwc2_wake_lock(struct wakeup_source *wakesrc)
{
	if (!wakesrc->active) {
		pr_info("%s lock\n", wakesrc->name);
		__pm_stay_awake(wakesrc);
	}
}

static void jlq_dwc2_wake_unlock(struct wakeup_source *wakesrc)
{
	if (wakesrc->active) {
		pr_info("%s unlock\n", wakesrc->name);
		__pm_relax(wakesrc);
	}
}
void jlq_dwc2_otg_vbus_indicator(struct jlq_otg *jlq_otg, int en)
{
	u32 value = 0;
	void __iomem *reg = jlq_otg->regs;

	if (en == 1)
		value = readl(reg + USB_CTL_PHYCTL) | PHYCTL_VBUSVLDEXT;
	else
		value = readl(reg + USB_CTL_PHYCTL) & (~PHYCTL_VBUSVLDEXT);

	phy_writel(reg, USB_CTL_PHYCTL, value);
}

void jlq_dwc2_otg_set_non_driving(struct jlq_otg *jlq_otg, int en)
{
	u32 value = 0;
	void __iomem *reg = jlq_otg->regs;

	if (en == 1)
		value = readl(reg + USB_CTL_PHYCTL) | PHYCTL_NON_DRIV;
	else
		value = readl(reg + USB_CTL_PHYCTL) & (~PHYCTL_NON_DRIV);

	phy_writel(reg, USB_CTL_PHYCTL, value);
}

void jlq_dwc2_otg_set_suspend(struct jlq_otg *jlq_otg, int en)
{
	u32 value = 0;
	void __iomem *reg = jlq_otg->regs;

	if (en == 0)
		value = readl(reg + USB_CTL_PHYCTL) | PHYCTL_SUSPENDM_DIS;
	else
		value = readl(reg + USB_CTL_PHYCTL) & (~PHYCTL_SUSPENDM_DIS);

	phy_writel(reg, USB_CTL_PHYCTL, value);
}

static void jlq_dwc2_otg_start_host(struct jlq_otg *jlq_otg, int on)
{
	struct usb_otg *otg = jlq_otg->phy.otg;
	struct usb_hcd *hcd;
	struct dwc2_hsotg *hsotg;

	mutex_lock(&jlq_otg->set_lock);
	if (!otg->host) {
		mutex_unlock(&jlq_otg->set_lock);
		dev_info(jlq_otg->phy.dev, "host is null\n");
		return;
	}

	dev_info(&jlq_otg->pdev->dev, "%s host\n", on ? "start" : "stop");

	hcd = bus_to_hcd(otg->host);
	hsotg = dwc2_hcd_to_hsotg(hcd);
	if (!hsotg) {
		mutex_unlock(&jlq_otg->set_lock);
		return;
	}

	if (on) {
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
		device_wakeup_enable(hcd->self.controller);
		dwc2_force_mode(hsotg, true);
	} else {
		usb_remove_hcd(hcd);
		dwc2_disable_global_interrupts(hsotg);
		dwc2_force_mode(hsotg, false);
	}
	mutex_unlock(&jlq_otg->set_lock);
}

static void jlq_dwc2_otg_start_periphrals(struct jlq_otg *jlq_otg, int on)
{
	struct usb_otg *otg = jlq_otg->phy.otg;
	struct dwc2_hsotg *hsotg = NULL;

	mutex_lock(&jlq_otg->set_lock);
	if (!otg->gadget) {
		mutex_unlock(&jlq_otg->set_lock);
		dev_info(jlq_otg->phy.dev, "gadget is null\n");
		return;
	}
	hsotg = container_of(otg->gadget, struct dwc2_hsotg, gadget);

	dev_info(jlq_otg->phy.dev, "gadget %s\n", on ? "on" : "off");

	if (on) {
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		usb_gadget_vbus_connect(otg->gadget);
	} else {
		dwc2_disable_global_interrupts(hsotg);
		usb_gadget_vbus_disconnect(otg->gadget);
	}
	mutex_unlock(&jlq_otg->set_lock);
}

static int jlq_dwc2_otg_phy_enable_power(struct jlq_otg *jlq_otg, bool on)
{
	int ret = 0;

	dev_dbg(jlq_otg->phy.dev, "%s turn %s regulators\n",
			__func__, on ? "on" : "off");
	if (!on)
		goto disable_vdda33;

	ret = regulator_set_voltage(jlq_otg->vdd, USB2PHY_VDD_VOL_MIN,
						USB2PHY_VDD_VOL_MAX);
	if (ret) {
		dev_err(jlq_otg->phy.dev, "Unable to config VDD:%d\n", ret);
		goto err_vdd;
	}

	ret = regulator_enable(jlq_otg->vdd);
	if (ret) {
		dev_err(jlq_otg->phy.dev, "Unable to enable VDD\n");
		goto unconfig_vdd;
	}

	ret = regulator_set_voltage(jlq_otg->vdda18, USB2PHY_1P8_VOL_MIN,
						USB2PHY_1P8_VOL_MAX);
	if (ret) {
		dev_err(jlq_otg->phy.dev,
				"Unable to set voltage for vdda18:%d\n", ret);
		goto disable_vdd;
	}

	ret = regulator_enable(jlq_otg->vdda18);
	if (ret) {
		dev_err(jlq_otg->phy.dev, "Unable to enable vdda18:%d\n", ret);
		goto unset_vdda18;
	}

	ret = regulator_set_voltage(jlq_otg->vdda33, USB2PHY_3P3_VOL_MIN,
						USB2PHY_3P3_VOL_MAX);

	if (ret) {
		dev_err(jlq_otg->phy.dev,
				"Unable to set voltage for vdda33:%d\n", ret);
		goto disable_vdda18;
	}

	ret = regulator_enable(jlq_otg->vdda33);
	if (ret) {
		dev_err(jlq_otg->phy.dev, "Unable to enable vdda33:%d\n", ret);
		goto unset_vdd33;
	}

	jlq_otg->vol_en_flag = 1;
	dev_dbg(jlq_otg->phy.dev, "USB PHY's regulators are turned ON.\n");
	return ret;

disable_vdda33:
	ret = regulator_disable(jlq_otg->vdda33);
	if (ret)
		dev_err(jlq_otg->phy.dev, "U:wnable to disable vdda33:%d\n", ret);

unset_vdd33:
	ret = regulator_set_voltage(jlq_otg->vdda33, 0, USB2PHY_3P3_VOL_MAX);
	if (ret)
		dev_err(jlq_otg->phy.dev,
			"Unable to set (0) voltage for vdda33:%d\n", ret);

disable_vdda18:
	ret = regulator_disable(jlq_otg->vdda18);
	if (ret)
		dev_err(jlq_otg->phy.dev, "Unable to disable vdda18:%d\n", ret);

unset_vdda18:
	ret = regulator_set_voltage(jlq_otg->vdda18, 0, USB2PHY_1P8_VOL_MAX);
	if (ret)
		dev_err(jlq_otg->phy.dev,
			"Unable to set (0) voltage for vdda18:%d\n", ret);

disable_vdd:
	ret = regulator_disable(jlq_otg->vdd);
	if (ret)
		dev_err(jlq_otg->phy.dev, "Unable to disable vdd:%d\n", ret);

unconfig_vdd:
err_vdd:
	jlq_otg->vol_en_flag = 0;
	dev_dbg(jlq_otg->phy.dev, "USB PHY's regulators are turned OFF.\n");

	return ret;
}

static int jlq_dwc2_otg_typec_partner_register(struct jlq_otg *jlq_otg, bool host_en)
{
	int ret = 0;

	mutex_lock(&jlq_otg->typec_lock);

	if (!jlq_otg->typec_port)
		goto unlock;

	if (!jlq_otg->typec_partner) {
		jlq_otg->typec_partner_desc.accessory = TYPEC_ACCESSORY_NONE;

		jlq_otg->typec_partner = typec_register_partner(jlq_otg->typec_port,
			&jlq_otg->typec_partner_desc);
		if (IS_ERR(jlq_otg->typec_partner)) {
			ret = PTR_ERR(jlq_otg->typec_partner);
			dev_err(jlq_otg->phy.dev, "register typec_partner failed ret=%d\n", ret);
			goto unlock;
		}
		dev_dbg(jlq_otg->phy.dev, "register typec_partner success\n");
	}

	if (host_en) {
		typec_set_data_role(jlq_otg->typec_port, TYPEC_HOST);
		typec_set_pwr_role(jlq_otg->typec_port, TYPEC_SOURCE);
	} else {
		typec_set_data_role(jlq_otg->typec_port, TYPEC_DEVICE);
		typec_set_pwr_role(jlq_otg->typec_port, TYPEC_SINK);
	}
unlock:
	mutex_unlock(&jlq_otg->typec_lock);
	return ret;
}

static int jlq_dwc2_otg_phy_init(struct usb_phy *phy)
{
	struct jlq_otg *jlq_otg;
	void __iomem *reg;
	u32 value = 0;
	int ret = 0;

	jlq_otg = container_of(phy, struct jlq_otg, phy);
	if (!jlq_otg) {
		pr_err("%s invalid jlq_otg\n", __func__);
		return -EINVAL;
	}
	reg = jlq_otg->regs;

	if (jlq_otg->cs_gpio > 0)
		gpio_direction_output(jlq_otg->cs_gpio, 1);

	if (jlq_otg->reset_gpio > 0)
		gpio_direction_output(jlq_otg->reset_gpio, 1);

	if (jlq_otg->pwr_usb_rst > 0) {
		writel(USB_RSTCTL, jlq_otg->pwr_usb_rst);
		while ((readl(jlq_otg->pwr_usb_rst) & 0x2) != 0)
			;
	}

	/* Disable Suspend */
	jlq_dwc2_otg_set_suspend(jlq_otg, 0);

	/* use ext vbus cmp*/
	value = phy_readl(reg, USB_CTL_PHYCTL) | PHYCTL_VBUSVLDEXTSEL;
	phy_writel(reg, USB_CTL_PHYCTL, value);

	/* Set POR/hrstn/prstn*/
	if (jlq_otg->usb_rst > 0)
		writel(RSTCTL_POR_SET | RSTCTL_HRSTN_SET | RSTCTL_PRSTN_SET,
			jlq_otg->usb_rst);
	else
		phy_writel(reg, USB_CTL_RSTCTL,
			RSTCTL_POR_SET | RSTCTL_HRSTN_SET | RSTCTL_PRSTN_SET);
	udelay(10);

	if (!IS_ERR(jlq_otg->clk))
		clk_prepare_enable(jlq_otg->clk);

	if (jlq_otg->parm)
		phy_writel(reg, USB_CTL_PHYPARAM, jlq_otg->parm);

	/* Clear POR*/
	if (jlq_otg->usb_rst > 0)
		writel(RSTCTL_POR_CLR, jlq_otg->usb_rst);
	else {
		value = phy_readl(reg, USB_CTL_RSTCTL) & (~RSTCTL_POR_SET);
		phy_writel(reg, USB_CTL_RSTCTL, value);
	}
	udelay(45);

	/*enable hclk for core*/
	if (IS_ERR(jlq_otg->hclk)) {
		value = phy_readl(reg, USB_CTL_HCLKEN) | CTL_HCLKEN_EN;
		phy_writel(reg, USB_CTL_HCLKEN, value);
	} else
		clk_prepare_enable(jlq_otg->hclk);

	if (jlq_otg->usb_rst > 0) {
		/*Clear prstn*/
		writel(RSTCTL_PRSTN_CLR, jlq_otg->usb_rst);
		/*Clear hrstn*/
		writel(RSTCTL_HRSTN_CLR, jlq_otg->usb_rst);
		value = readl(jlq_otg->usb_rst);
	} else {
		/*Clear prstn*/
		value = phy_readl(reg, USB_CTL_RSTCTL) | RSTCTL_PRSTN_CLR;
		phy_writel(reg, USB_CTL_RSTCTL, value);
		/*Clear hrstn*/
		value = phy_readl(reg, USB_CTL_RSTCTL) | RSTCTL_HRSTN_CLR;
		phy_writel(reg, USB_CTL_RSTCTL, value);
		value = phy_readl(reg, USB_CTL_RSTCTL);
	}
	udelay(10);
	jlq_otg->init_flag = 1;

	if (jlq_otg->init_sync) {
		if (jlq_otg->vbus_active || jlq_otg->id_active) {
			if (!jlq_otg->no_plug_notify_flag)
				jlq_dwc2_wake_lock(jlq_otg->wakesrc);
			if (jlq_otg->vbus_active)
				jlq_dwc2_otg_typec_partner_register(jlq_otg, false);
			if (jlq_otg->id_active)
				jlq_dwc2_otg_typec_partner_register(jlq_otg, true);
			jlq_otg->state = USB_STATE_IDLE;
		}
		queue_delayed_work(jlq_otg->sm_usb_wq, &jlq_otg->sm_work, 1000);
		jlq_otg->init_sync = 0;
	} else
		jlq_dwc2_otg_set_suspend(jlq_otg, 1);

	pr_info("%s USB_CTL_RSTCTL 0x%x USB_CTL_PHYCTL 0x%x\n",
			__func__, value, phy_readl(reg, USB_CTL_PHYCTL));
	return ret;
}

static void jlq_dwc2_otg_phy_shutdown(struct usb_phy *phy)
{
	struct jlq_otg *jlq_otg;
	void __iomem *reg;
	u32 value = 0;

	jlq_otg = container_of(phy, struct jlq_otg, phy);
	if (!jlq_otg) {
		pr_err("%s invalid jlq_otg\n", __func__);
		return;
	}
	reg = jlq_otg->regs;
	/* enable Suspend */
	jlq_dwc2_otg_set_suspend(jlq_otg, 1);

	/*disable hclk for core*/
	if (IS_ERR(jlq_otg->hclk)) {
		value = phy_readl(reg, USB_CTL_HCLKEN) & (~CTL_HCLKEN_EN);
		phy_writel(reg, USB_CTL_HCLKEN, value);
	} else
		clk_disable_unprepare(jlq_otg->hclk);
	udelay(1);

	/* Set POR/hrstn/prstn*/
	if (jlq_otg->usb_rst > 0)
		writel(RSTCTL_POR_SET | RSTCTL_HRSTN_SET | RSTCTL_PRSTN_SET,
			jlq_otg->usb_rst);
	else
		phy_writel(reg, USB_CTL_RSTCTL,
			RSTCTL_POR_SET | RSTCTL_HRSTN_SET | RSTCTL_PRSTN_SET);

	if (!IS_ERR(jlq_otg->clk))
		clk_disable_unprepare(jlq_otg->clk);

	if (jlq_otg->reset_gpio > 0)
		gpio_set_value(jlq_otg->reset_gpio, 0);
	if (jlq_otg->cs_gpio > 0)
		gpio_set_value(jlq_otg->cs_gpio, 0);

	jlq_otg->init_flag = 0;

}

int jlq_dwc2_otg_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct jlq_otg *jlq_otg = container_of(otg->usb_phy, struct jlq_otg, phy);

	pr_info("%s : %lx\n", __func__, (long)host);

	mutex_lock(&jlq_otg->set_lock);
	otg->host = host;
	jlq_otg->set_host_flag = 1;
	mutex_unlock(&jlq_otg->set_lock);

	return 0;
}

int jlq_dwc2_otg_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	struct jlq_otg *jlq_otg = container_of(otg->usb_phy, struct jlq_otg, phy);

	pr_info("%s : %lx\n", __func__, (long)gadget);

	mutex_lock(&jlq_otg->set_lock);
	otg->gadget = gadget;
	mutex_unlock(&jlq_otg->set_lock);

	return 0;
}

int jlq_dwc2_otg_phy_set_vbus(struct usb_phy *x, int on)
{
	return 0;
}
static void jlq_dwc2_otg_phy_sm_work(struct work_struct *w)
{
	struct jlq_otg *jlq_otg = container_of(w, struct jlq_otg, sm_work.work);
	bool work = 0;
	unsigned long delay = 0;

	if (!jlq_otg) {
		pr_err("jlq_otg is NULL.\n");
		return;
	}

	switch (jlq_otg->state) {
	case USB_STATE_UNDEFINED:
		dev_dbg(jlq_otg->phy.dev, "USB_STATE_UNDEFINED: vbus active: %x id active: %x\n",
			jlq_otg->vbus_active, jlq_otg->id_active);
		if (jlq_otg->init_flag && jlq_otg->set_host_flag) {
			if (jlq_otg->vol_en_flag)
				jlq_dwc2_otg_phy_enable_power(jlq_otg, false);
			jlq_otg->state = USB_STATE_IDLE;
			jlq_dwc2_otg_set_non_driving(jlq_otg, 1);
			jlq_dwc2_otg_set_suspend(jlq_otg, 1);
			jlq_dwc2_wake_unlock(jlq_otg->wakesrc);
		} else {
			dev_dbg(jlq_otg->phy.dev, "try again init: %x set_host: %x vol: %x\n",
				jlq_otg->init_flag, jlq_otg->set_host_flag, jlq_otg->vol_en_flag);
			delay = 100;
			work = 1;
		}
		if (jlq_otg->vbus_active || jlq_otg->id_active) {
			jlq_otg->state = USB_STATE_IDLE;
			jlq_dwc2_wake_lock(jlq_otg->wakesrc);
			work = 1;
		}
		break;
	case USB_STATE_IDLE:
		dev_dbg(jlq_otg->phy.dev, "USB_STATE_IDLE: vbus active: %x id active: %x\n",
			jlq_otg->vbus_active, jlq_otg->id_active);
		if (jlq_otg->vbus_active) {
			jlq_dwc2_otg_set_suspend(jlq_otg, 0);
			jlq_dwc2_otg_set_non_driving(jlq_otg, 0);
			jlq_otg->state = USB_STATE_PERIPHERAL;
			if (!jlq_otg->vol_en_flag)
				jlq_dwc2_otg_phy_enable_power(jlq_otg, true);
			jlq_dwc2_otg_vbus_indicator(jlq_otg, 1);
			jlq_dwc2_otg_start_periphrals(jlq_otg, 1);
			pm_qos_add_request(&jlq_otg->pm_qos, PM_QOS_CPU_DMA_LATENCY, 0);
		}
		if (jlq_otg->id_active) {
			jlq_dwc2_otg_set_suspend(jlq_otg, 0);
			jlq_dwc2_otg_set_non_driving(jlq_otg, 0);
			jlq_dwc2_otg_vbus_indicator(jlq_otg, 0);
			jlq_otg->state = USB_STATE_HOST;
			if (!jlq_otg->vol_en_flag)
				jlq_dwc2_otg_phy_enable_power(jlq_otg, true);
			jlq_dwc2_otg_start_host(jlq_otg, 1);
			pm_qos_add_request(&jlq_otg->pm_qos, PM_QOS_CPU_DMA_LATENCY, 0);
		}
		break;
	case USB_STATE_PERIPHERAL:
		dev_dbg(jlq_otg->phy.dev, "USB_STATE_PERIPHERAL: vbus active: %x\n",
			jlq_otg->vbus_active);
		if (!jlq_otg->vbus_active) {
			jlq_dwc2_otg_vbus_indicator(jlq_otg, 0);
			jlq_dwc2_otg_start_periphrals(jlq_otg, 0);
			pm_qos_remove_request(&jlq_otg->pm_qos);
			jlq_otg->state = USB_STATE_UNDEFINED;
			work = 1;
		}
		break;
	case USB_STATE_HOST:
		dev_dbg(jlq_otg->phy.dev, "USB_STATE_HOST: id active: %x\n", jlq_otg->id_active);
		if (!jlq_otg->id_active) {
			jlq_dwc2_otg_start_host(jlq_otg, 0);
			pm_qos_remove_request(&jlq_otg->pm_qos);
			jlq_otg->state = USB_STATE_UNDEFINED;
			work = 1;
		}
		break;
	default:
		dev_err(jlq_otg->phy.dev, "%s: invalid state %x\n", __func__, jlq_otg->state);
		break;
	}
	dev_info(jlq_otg->phy.dev, "%s: state: %x vbus active: %x id active: %x\n",
		 __func__, jlq_otg->state, jlq_otg->vbus_active, jlq_otg->id_active);

	if (work)
		queue_delayed_work(jlq_otg->sm_usb_wq, &jlq_otg->sm_work, delay);
}

static void jlq_dwc2_otg_typec_partner_unregister(struct jlq_otg *jlq_otg)
{
	mutex_lock(&jlq_otg->typec_lock);

	if (!jlq_otg->typec_port)
		goto unlock;

	if (jlq_otg->typec_partner) {
		dev_dbg(jlq_otg->phy.dev, "unregister typec_partner\n");
		typec_unregister_partner(jlq_otg->typec_partner);
		jlq_otg->typec_partner = NULL;
	}

unlock:
	mutex_unlock(&jlq_otg->typec_lock);
}

static int jlq_dwc2_otg_vbus_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct usb_phy *phy = container_of(nb, struct usb_phy, vbus_nb);
	struct jlq_otg *jlq_otg = container_of(phy, struct jlq_otg, phy);
	bool new_state = !!event;

	if (!jlq_otg || !data) {
		pr_err("Failed to get jlq_otg for vbus_notifier\n");
		return NOTIFY_DONE;
	}

	dev_info(jlq_otg->phy.dev, "Got VBUS notification: %x, last %x\n",
		new_state, jlq_otg->vbus_active);
	if (jlq_otg->vbus_active != new_state) {
		jlq_otg->vbus_active = new_state;
		if (jlq_otg->vbus_active) {
			jlq_dwc2_wake_lock(jlq_otg->wakesrc);
			jlq_dwc2_otg_typec_partner_register(jlq_otg, false);
		} else {
			jlq_dwc2_otg_typec_partner_unregister(jlq_otg);
			if (jlq_otg->state == USB_STATE_IDLE)
				jlq_otg->state = USB_STATE_UNDEFINED;
		}
		queue_delayed_work(jlq_otg->sm_usb_wq, &jlq_otg->sm_work, 0);
	}

	return NOTIFY_DONE;
}

static int jlq_dwc2_otg_id_notifier(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct usb_phy *phy = container_of(nb, struct usb_phy, id_nb);
	struct jlq_otg *jlq_otg = container_of(phy, struct jlq_otg, phy);
	bool new_state = !!event;

	if (!jlq_otg || !data) {
		pr_err("Failed to get jlq_otg for vbus_notifier\n");
		return NOTIFY_DONE;
	}

	dev_info(jlq_otg->phy.dev, "Got id notification: %x, last %x\n",
		new_state, jlq_otg->id_active);
	if (jlq_otg->id_active != new_state) {
		jlq_otg->id_active = new_state;
		if (jlq_otg->id_active) {
			jlq_dwc2_wake_lock(jlq_otg->wakesrc);
			jlq_dwc2_otg_typec_partner_register(jlq_otg, true);
		} else {
			jlq_dwc2_otg_typec_partner_unregister(jlq_otg);
			if (jlq_otg->state == USB_STATE_IDLE)
				jlq_otg->state = USB_STATE_UNDEFINED;
		}
		queue_delayed_work(jlq_otg->sm_usb_wq, &jlq_otg->sm_work, 0);
	}

	return NOTIFY_DONE;
}

static int jlq_dwc2_otg_init_typec_class(struct jlq_otg *jlq_otg)
{
	int ret = 0;

	mutex_init(&jlq_otg->typec_lock);

	jlq_otg->typec_caps.type = TYPEC_PORT_DRP;
	jlq_otg->typec_caps.data = TYPEC_PORT_DRD;
	jlq_otg->typec_partner_desc.usb_pd = false;
	jlq_otg->typec_partner_desc.accessory = TYPEC_ACCESSORY_NONE;
	jlq_otg->typec_caps.revision = 0x0100;

	jlq_otg->typec_port = typec_register_port(jlq_otg->phy.dev, &jlq_otg->typec_caps);
	if (IS_ERR(jlq_otg->typec_port)) {
		ret = PTR_ERR(jlq_otg->typec_port);
		pr_err("failed to register typec_port rc=%d\n", ret);
	}

	return ret;
}

static struct extcon_dev *dwc2_otg_extcon_scan (
	struct device *dev,
	const char *prop_name
)
{
	int ret;
	int i;
	struct extcon_dev *edev;

	const char *extcon_names[4];
	ret = of_property_read_string_array(dev->of_node, prop_name,
			(const char**)extcon_names, 4);
	if (ret < 0)
		return ERR_PTR(ret);

	for (i = 0; i < ret; i++){
		edev = extcon_get_extcon_dev(extcon_names[i]);
		if (!IS_ERR_OR_NULL(edev))
			return edev;
	}
	return NULL;
}

static bool jlq_dwc2_otg_check_extcon_ready(struct device *dev, const char *prop_name)
{
	int ret = 0;
	int i;
	struct extcon_dev *edev;
	const char *extcon_names[4];

	if (of_property_read_bool(dev->of_node, "extcon")) {
		edev = extcon_get_edev_by_phandle(dev, 0);
		if (IS_ERR(edev))
			return false;
		else
			return true;
	} else {
		ret = of_property_read_string_array(dev->of_node, prop_name,
				(const char **)extcon_names, 4);
		if (ret > 0) {
			for (i = 0; i < ret; i++) {
				edev = extcon_get_extcon_dev(extcon_names[i]);
				if (!IS_ERR_OR_NULL(edev))
					return true;
			}
			return false;
		}
	}

	return true;
}

static int jlq_dwc2_otg_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct jlq_otg *jlq_otg;
	struct usb_phy *phy;
	int state = 0;

	if (!jlq_dwc2_otg_check_extcon_ready(&pdev->dev, "usb-dev-extcons")) {
		dev_err(&pdev->dev, "extcon no ready\n");
		return -EPROBE_DEFER;
	}

	jlq_otg = devm_kzalloc(&pdev->dev, sizeof(struct jlq_otg), GFP_KERNEL);
	if (!jlq_otg)
		return -ENOMEM;

	jlq_otg->phy.otg = devm_kzalloc(&pdev->dev, sizeof(struct usb_otg),
				     GFP_KERNEL);
	if (!jlq_otg->phy.otg) {
		ret = -ENOMEM;
		goto err_otg_alloc;
	}

	jlq_otg->pdev = pdev;
	phy = &jlq_otg->phy;
	phy->dev = &pdev->dev;

	jlq_otg->clk = devm_clk_get(&pdev->dev, "usb_clk");
	if (IS_ERR(jlq_otg->clk)) {
		dev_err(&pdev->dev, "failed to get usb_clk\n");
		ret = PTR_ERR(jlq_otg->clk);
		goto err_clk_get;
	}

	jlq_otg->hclk = devm_clk_get(&pdev->dev, "usb_hclk");
	if (IS_ERR(jlq_otg->hclk))
		dev_info(&pdev->dev, "failed to get usb_hclk\n");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pwr_usbrst");
	if (!res) {
		dev_info(&pdev->dev, "failed to get pwr_usbrst regs\n");
		jlq_otg->pwr_usb_rst = 0;
	} else {
		jlq_otg->pwr_usb_rst = devm_ioremap_resource(&pdev->dev, res);
		if (!jlq_otg->pwr_usb_rst) {
			dev_err(&pdev->dev, "pwr_usbrst regs ioremap error\n");
			ret = -ENOMEM;
			goto err_clk_get;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "usbrst");
	if (!res) {
		dev_info(&pdev->dev, "failed to get usbrst regs\n");
		jlq_otg->usb_rst = 0;
	} else {
		jlq_otg->usb_rst = devm_ioremap_resource(&pdev->dev, res);
		if (!jlq_otg->usb_rst) {
			dev_err(&pdev->dev, "usbrst regs ioremap error\n");
			ret = -ENOMEM;
			goto err_clk_get;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "usbphy");
	if (!res) {
		dev_err(&pdev->dev, "failed to get usbphy regs\n");
		ret = -EINVAL;
		goto err_clk_get;
	}

	jlq_otg->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!jlq_otg->regs) {
		dev_err(&pdev->dev, "usbphy regs ioremap error\n");
		ret = -ENOMEM;
		goto err_clk_get;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "eyediagram-parm", &jlq_otg->parm);
	if (ret)
		dev_info(&pdev->dev, "no eyediagram-parm\n");

	jlq_otg->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(jlq_otg->vdd)) {
		dev_err(&pdev->dev, "unable to get vdd supply\n");
		ret = PTR_ERR(jlq_otg->vdd);
		goto err_clk_get;
	}

	jlq_otg->vdda33 = devm_regulator_get(&pdev->dev, "vdda33");
	if (IS_ERR(jlq_otg->vdda33)) {
		dev_err(&pdev->dev, "unable to get vdda33 supply\n");
		ret = PTR_ERR(jlq_otg->vdda33);
		goto err_v33_get;
	}

	jlq_otg->vdda18 = devm_regulator_get(&pdev->dev, "vdda18");
	if (IS_ERR(jlq_otg->vdda18)) {
		dev_err(&pdev->dev, "unable to get vdda18 supply\n");
		ret = PTR_ERR(jlq_otg->vdda18);
		goto err_v18_get;
	}

	ret = jlq_dwc2_otg_phy_enable_power(jlq_otg, true);
	if (ret)
		goto err_enable_power;

	jlq_otg->cs_gpio = of_get_named_gpio(pdev->dev.of_node, "cs-gpio", 0);
	if (jlq_otg->cs_gpio < 0) {
		dev_info(&pdev->dev, "no cs-gpio\n");
	} else {
		gpio_request(jlq_otg->cs_gpio, "usb cs gpio");
		gpio_direction_output(jlq_otg->cs_gpio, 0);
	}
	jlq_otg->reset_gpio = of_get_named_gpio(pdev->dev.of_node,
						"reset-gpio", 0);
	if (jlq_otg->reset_gpio < 0) {
		dev_info(&pdev->dev, "no reset-gpio\n");
	} else {
		gpio_request(jlq_otg->reset_gpio, "usb reset gpio");
		gpio_direction_output(jlq_otg->reset_gpio, 0);
	}

	jlq_otg->wakesrc = wakeup_source_register(&pdev->dev, "usb_phy_wakesrc");
	if (!jlq_otg->wakesrc) {
		dev_err(&pdev->dev, "wake source register failed\n");
		ret = -EINVAL;
		goto err_register_wake;
	}

	jlq_otg->state = USB_STATE_UNDEFINED;
	jlq_otg->set_host_flag = 0;
	jlq_otg->vbus_active = false;
	jlq_otg->id_active = false;
	INIT_DELAYED_WORK(&jlq_otg->sm_work, jlq_dwc2_otg_phy_sm_work);
	jlq_otg->sm_usb_wq = alloc_ordered_workqueue("k_sm_usb", WQ_FREEZABLE);
	if (!jlq_otg->sm_usb_wq) {
		dev_err(&pdev->dev, "alloc workqueue failed\n");
		ret = -ENOMEM;
		goto err_wk_alloc;
	}

	ret = jlq_dwc2_otg_init_typec_class(jlq_otg);
	if (ret)
		goto err_typec_init;

	phy->type = USB_PHY_TYPE_USB2;
	phy->dev = &pdev->dev;
	phy->label = DRIVER_NAME;
	phy->init = jlq_dwc2_otg_phy_init;
	phy->shutdown = jlq_dwc2_otg_phy_shutdown;
	phy->set_vbus = jlq_dwc2_otg_phy_set_vbus;
	phy->vbus_nb.notifier_call = jlq_dwc2_otg_vbus_notifier;
	phy->id_nb.notifier_call = jlq_dwc2_otg_id_notifier;

	phy->otg->usb_phy = &jlq_otg->phy;
	phy->otg->set_host = jlq_dwc2_otg_set_host;
	phy->otg->set_peripheral = jlq_dwc2_otg_set_peripheral;

	mutex_init(&jlq_otg->set_lock);
	ret = usb_add_phy_dev(&jlq_otg->phy);
	if (ret) {
		dev_err(&pdev->dev, "usb_add_phy failed\n");
		goto err_typec_init;
	}

	platform_set_drvdata(pdev, jlq_otg);

	if (jlq_otg->phy.edev == NULL) {
		jlq_otg->phy.edev = 
			dwc2_otg_extcon_scan(&pdev->dev, "usb-dev-extcons");
		if (IS_ERR_OR_NULL(jlq_otg->phy.edev)) {
			jlq_otg->phy.edev = NULL;
			dev_err(&pdev->dev, "Can't found usb-dev-extcon\n ");
		}
		else
		{
			ret  = extcon_register_notifier(jlq_otg->phy.edev, EXTCON_USB, &phy->vbus_nb);
			if(ret<0)
			{
				dev_err(&pdev->dev, "Cannot register notifier EXTCON_USB for %s \n",
					extcon_get_edev_name(jlq_otg->phy.edev) );
			}
			else
			{
				dev_err(&pdev->dev, " Register notifier EXTCON_USB for %s  success!\n",
					extcon_get_edev_name(jlq_otg->phy.edev) );
			}
		}
	}
	
	if (jlq_otg->phy.id_edev == NULL) {
		jlq_otg->phy.id_edev = 
			dwc2_otg_extcon_scan(&pdev->dev, "usb-id-extcons");
		if (IS_ERR_OR_NULL(jlq_otg->phy.id_edev)) {
			jlq_otg->phy.id_edev = NULL;
			dev_err(&pdev->dev, "Can't found usb-id-extcon\n ");
		}
		else
		{
			ret  = extcon_register_notifier(jlq_otg->phy.id_edev, EXTCON_USB_HOST, &phy->id_nb);
			if(ret<0)
			{
				dev_err(&pdev->dev, "Cannot register notifier EXTCON_USB_HOST for %s \n",
					extcon_get_edev_name(jlq_otg->phy.id_edev) );
			}
			else
			{
				dev_err(&pdev->dev, " Register notifier EXTCON_USB_HOST for %s  success!\n",
					extcon_get_edev_name(jlq_otg->phy.id_edev) );
			}
		}
	}

	if (jlq_otg->phy.edev != NULL) {
		state = extcon_get_state(jlq_otg->phy.edev, EXTCON_USB);
		if (state < 0)
			dev_info(&pdev->dev, "get EXTCON_USB err: %x\n", state);
		else
			jlq_otg->vbus_active = !!state;
	}
	if (jlq_otg->phy.id_edev != NULL) {
		state = extcon_get_state(jlq_otg->phy.id_edev, EXTCON_USB_HOST);
		if (state < 0)
			dev_info(&pdev->dev, "get EXTCON_USB_HOST err: %x\n", state);
		else
			jlq_otg->id_active = !!state;
	}
	if (of_get_property(pdev->dev.of_node, "no-plug-notify", NULL)) {
		jlq_otg->no_plug_notify_flag = 1;
		jlq_otg->vbus_active = true;
	} else
		jlq_otg->no_plug_notify_flag = 0;

	jlq_otg->init_sync = 1;

	return ret;

err_typec_init:
err_wk_alloc:
	wakeup_source_unregister(jlq_otg->wakesrc);
err_register_wake:
	jlq_dwc2_otg_phy_enable_power(jlq_otg, false);
err_enable_power:
	regulator_put(jlq_otg->vdda18);
err_v18_get:
	regulator_put(jlq_otg->vdda33);
err_v33_get:
	regulator_put(jlq_otg->vdd);
err_clk_get:
	devm_kfree(&pdev->dev, jlq_otg->phy.otg);
err_otg_alloc:
	devm_kfree(&pdev->dev, jlq_otg);
	return ret;
}

static int jlq_dwc2_otg_remove(struct platform_device *pdev)
{
	struct jlq_otg *jlq_otg;

	jlq_otg = platform_get_drvdata(pdev);

	if (jlq_otg->typec_port)
		typec_unregister_port(jlq_otg->typec_port);
	wakeup_source_unregister(jlq_otg->wakesrc);
	regulator_put(jlq_otg->vdd);
	regulator_put(jlq_otg->vdda33);
	regulator_put(jlq_otg->vdda18);

	usb_remove_phy(&jlq_otg->phy);

	return 0;
}

static int __maybe_unused jlq_dwc2_otg_prepare(struct device *dev)
{
	struct jlq_otg *jlq_otg = dev_get_drvdata(dev);

	if (jlq_otg->vbus_active || jlq_otg->id_active)
		return -EPERM;
	else
		return 0;
}

static const struct of_device_id jlq_dwc2_otg_dt_match[] = {
	{ .compatible = "jlq,jlq-usb-phy", },
	{}
};

static const struct dev_pm_ops jlq_dwc2_otg_pm_ops = {
	.prepare = jlq_dwc2_otg_prepare,
};

static struct platform_driver jlq_dwc2_otg_driver = {
	.probe = jlq_dwc2_otg_probe,
	.remove = jlq_dwc2_otg_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = jlq_dwc2_otg_dt_match,
		.pm = &jlq_dwc2_otg_pm_ops,
	},
};

module_platform_driver(jlq_dwc2_otg_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("JLQ DWC2 USB transceiver driver");
MODULE_SOFTDEP("pre: sd7601 sgm4154x_charger extcon-jlq-gpio type-c-pericom");
MODULE_SOFTDEP("pre: qcom-qpnp-qg qpnp-smb5-main");
