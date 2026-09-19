# SPDX-License-Identifier: GPL-2.0
#
# Makefile for jlq pmctrl driver
#
ccflags-y += -I$(srctree)/drivers/opp
pm-ctrl-objs		+= pmctrl.o
pm-ctrl-opp-objs		+= pmctrl-opp.o

obj-$(CONFIG_PM_CTRL) += pm-ctrl.o
obj-$(CONFIG_PM_CTRL) += pm-ctrl-opp.o

