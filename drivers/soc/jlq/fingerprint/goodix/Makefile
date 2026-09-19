#
# Makefile for kernel Goodix Fingerprint drivers.
#


ccflags-$(CONFIG_MICROTRUST_TEE_SUPPORT)	+= -I$(srctree)/drivers/teei/300/common/include
ccflags-$(CONFIG_MICROTRUST_TEE_SUPPORT)	+= -I$(srctree)/drivers/teei/300/tz_driver/include
ccflags-$(CONFIG_MICROTRUST_TEE_SUPPORT)	+= -DCONFIG_MICROTRUST_TEE_SUPPORT
ccflags-$(CONFIG_ARCH_JLQ)	+= -DBSP_SIL_PLAT_JLQ
#ifneq
ifneq (,$(filter $(CONFIG_MICROTRUST_TEE_SUPPORT),y m))
	ccflags-y	+= -I$(srctree)/drivers/teei/300/common/include
	ccflags-y	+= -I$(srctree)/drivers/teei/300/tz_driver/include
	ccflags-y	+= -DCONFIG_MICROTRUST_TEE_SUPPORT
endif

ifneq (,$(filter $(CONFIG_ARCH_JLQ),y m))
	ccflags-y	+= -DBSP_SIL_PLAT_JLQ
endif
#endif
GOODIX_MODULE_NAME = goodix_fp
obj-$(CONFIG_GOODIX_FINGERPRINT)	+= $(GOODIX_MODULE_NAME).o
$(GOODIX_MODULE_NAME)-objs := gf_spi.o platform.o netlink.o
