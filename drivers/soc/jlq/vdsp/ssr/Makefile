# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I $(srctree)/drivers/soc/jlq/vdsp
ccflags-y += -I $(srctree)/drivers/soc/jlq/vdsp/wdt

vdsp_ssr-objs += vdsp_subsystem_restart.o vdsp_ramdump.o vdsp-ssr.o
obj-${CONFIG_JLQ_VDSP}  += vdsp_ssr.o
