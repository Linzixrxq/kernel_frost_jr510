#
# Makefile for kernel FPC Fingerprint drivers.
#

ifneq (,$(filter $(CONFIG_MICROTRUST_TEE_SUPPORT),y m))
	ccflags-y	+= -I$(srctree)/drivers/teei/300/common/include
	ccflags-y	+= -I$(srctree)/drivers/teei/300/tz_driver/include
	ccflags-y	+= -DCONFIG_MICROTRUST_TEE_SUPPORT
endif

ifneq (,$(filter $(CONFIG_ARCH_JLQ),y m))
	ccflags-y	+= -DCONFIG_JLQ_SOLUTION
endif

FPC_MODULE_NAME = fpc_1020
obj-$(CONFIG_FPC_FINGERPRINT)	+= $(FPC_MODULE_NAME).o
$(FPC_MODULE_NAME)-objs := fpc1020.o fpc1020_ree.o
