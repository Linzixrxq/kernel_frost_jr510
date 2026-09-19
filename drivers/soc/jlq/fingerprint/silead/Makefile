#
# Makefile for kernel Silead Fingerprint drivers.
#

#ifneq
ifneq (,$(filter $(CONFIG_MICROTRUST_TEE_SUPPORT),y m))
        ccflags-y       += -I$(srctree)/drivers/teei/300/common/include
        ccflags-y       += -I$(srctree)/drivers/teei/300/tz_driver/include
        ccflags-y       += -DCONFIG_MICROTRUST_TEE_SUPPORT
endif

ifneq (,$(filter $(CONFIG_ARCH_JLQ),y m))
        ccflags-y       += -DBSP_SIL_PLAT_JLQ
endif
#endif

ccflags-y += -DCONFIG_SILEAD_FP_PLATFORM

obj-$(CONFIG_SILEAD_FP)     += silead_fp.o


