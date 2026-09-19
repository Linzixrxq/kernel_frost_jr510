# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/drivers/opp

bus-jr510-objs                 := jr510_bus.o icc-bus.o
icc-test-objs                   := debugfs-test.o

obj-$(CONFIG_INTERCONNECT_JR510) += bus-jr510.o
obj-$(CONFIG_INTERCONNECT_TEST)  += icc-test.o
