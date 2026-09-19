#VDSP
ccflags-y += -I$(srctree)/drivers/soc/jlq/vdsp

vdsp-objs += vdsp-hw-vp6.o vdsp-driver.o \
		vdsp-loader.o vdsp-event.o
obj-$(CONFIG_JLQ_VDSP)  += vdsp.o
obj-$(CONFIG_JLQ_VDSP)  += smmu/
obj-$(CONFIG_JLQ_VDSP)  += log/
obj-$(CONFIG_JLQ_VDSP)  += ssr/
obj-$(CONFIG_JLQ_VDSP)  += wdt/
