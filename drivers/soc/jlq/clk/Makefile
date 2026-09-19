# SPDX-License-Identifier: GPL-2.0
#
# Makefile for jlq clock driver
#
jlq-clk-objs		+= clk-jr510.o
jlq-smd-objs		+= smd.o clk-smd.o
jlq-pll-objs		+= pll-power.o
obj-y			+= jvcc/
obj-$(CONFIG_CLK_JLQ_JR510) += jlq-smd.o
obj-$(CONFIG_CLK_JLQ_JR510) += jlq-pll.o

ifeq ($(CONFIG_JGKI),y)
obj-y += jlq-clk.o
else
obj-m += jlq-clk.o
endif
