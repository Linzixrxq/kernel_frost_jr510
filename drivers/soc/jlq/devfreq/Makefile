# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/drivers/opp
ccflags-y += -I$(srctree)/drivers/devfreq/

obj-$(CONFIG_DEVFREQ_GOV_CPUFREQ)       += governor_cpufreq.o

# DEVFREQ Drivers
obj-$(CONFIG_DEVFREQ_SIMPLE_DEV)        += devfreq_simple_dev.o

#
# Makefile for ddr devfreq
#
obj-$(CONFIG_DEVFREQ_DDR) += ddr_dfs.o
 ddr_dfs-objs := devfreq_ddr.o devfreq_ddr_bridge.o

#
# Makefile for ddr busmon
#
obj-$(CONFIG_BW_BUSMON)	+= ddr_bw_busmon.o
# devfreq_ddr-objs := governor_busmon-bw.o devfreq_icc_port.o
