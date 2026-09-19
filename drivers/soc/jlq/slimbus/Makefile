# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the jlq slim bus
#

EXTRA_CFLAGS := -I$(src)/ -I$(srctree)/drivers/soc/jlq/audio-kernel/adspload

EXTRA_CFLAGS += -DADSP_SUB_RESET
#EXTRA_CFLAGS += -DFRM_CLK_DISABLE

##that is $(CONFIG_JLQ_SLIMBUS)

#obj-y += slimbus.o
#obj-y += slim_chk.o
#obj-y += slim_mtdr.o
#obj-y += slim_ctrl.o
#obj-y += slim_master.o
#obj-y += slim_slave.o
#obj-y += slim_devs.o
#obj-n += slim_ve_ut.o
#obj-n += slim_btfm_api.o
#obj-n += slim_dai_api.o

jlq-slimbus-objs	:=	slimbus.o of_slimbus.o

jlq-slimctrl-objs	:=	slim_ctrl.o slim_chk.o slim_mtdr.o slim_master.o slim_slave.o slim_devs.o

obj-$(CONFIG_JLQ_SLIMBUS)	+= jlq-slimbus.o
obj-${CONFIG_JLQ_SLIMBUS}	+= jlq-slimctrl.o