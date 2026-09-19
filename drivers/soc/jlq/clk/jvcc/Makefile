# SPDX-License-Identifier: GPL-2.0
#
# Makefile for jvcc clock driver
#

bengals-objs	+= common.o vdd-class.o clk-regmap.o \
		clk-alpha-pll.o clk-opp.o clk-pll.o \
		clk-rcg.o clk-rcg2.o clk-branch.o \
		clk-regmap-divider.o clk-regmap-mux.o \
		clk-regmap-mux-div.o clk-hfpll.o reset.o \
		clk-debug.o gdsc.o

obj-${CONFIG_CLK_JLQ_JR510} += gcc-bengal.o bengals.o
obj-${CONFIG_CLK_JLQ_JR510} += clk-smd-rpm.o
obj-${CONFIG_CLK_JLQ_JR510} += gdsc-regulator.o
obj-${CONFIG_CLK_JLQ_JR510} += jlq-gdsc-regulator.o
obj-${CONFIG_CLK_JLQ_JR510} += clk-dummy.o
