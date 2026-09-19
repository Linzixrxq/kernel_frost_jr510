
EXTRA_CFLAGS := -I$(src)/

EXTRA_CFLAGS += -DLOAD_ADSP_BY_AP
##EXTRA_CFLAGS += -DLOAD_ADSP_BY_AP_DIRECT
##EXTRA_CFLAGS += -DLOAD_ADSP_BY_HAPS
EXTRA_CFLAGS += -DADSP_SSR
EXTRA_CFLAGS += -DADSP_SSR_UEVENT_NOTIFY
EXTRA_CFLAGS += -DADSP_LPM
EXTRA_CFLAGS += -DADSP_REGU
EXTRA_CFLAGS += -DADSP_OPP
EXTRA_CFLAGS += -DADSP_MDM_NOTIFI
EXTRA_CFLAGS += -DADSP_SUB_RESET
EXTRA_CFLAGS += -DADSP_PANIC_NOTIFI
##EXTRA_CFLAGS += -DADSP_PANIC_DUMP
EXTRA_CFLAGS += -DADSP_WDT
EXTRA_CFLAGS += -DADSP_WDT_CLK_BY_ADSP
##EXTRA_CFLAGS += -DADSP_WDT_CTL
##EXTRA_CFLAGS += -DADSP_WDT_DEBUG
#EXTRA_CFLAGS += -DADSP_IMG_AUTH
EXTRA_CFLAGS += -DADSP_DUMP_DEV
EXTRA_CFLAGS += -DMDM_SSR_DUMP_DEV


#obj-$(CONFIG_JLQ_ADSPLOAD) := adsp_load.o
#obj-$(CONFIG_JLQ_ADSPLOAD) += adsp_test.o
#obj-$(CONFIG_JLQ_ADSPLOAD) += adsp_lpm.o
#obj-$(CONFIG_JLQ_ADSPLOAD) += adsp_wdt.o
#obj-$(CONFIG_JLQ_ADSPLOAD) += adsp_opp.o
#obj-$(CONFIG_JLQ_ADSPLOAD) += adsp_noti.o

jlq-adspwdt-objs       :=      adsp_wdt2.o
jlq-adspload-objs      :=      adsp_load.o adsp_lpm.o adsp_opp.o adsp_noti.o adsp_dump.o adsp_wdt.o
jlq-adsptest-objs      :=      adsp_test.o

obj-$(CONFIG_JLQ_ADSPLOAD)     += jlq-adspwdt.o
obj-${CONFIG_JLQ_ADSPLOAD}     += jlq-adspload.o
obj-${CONFIG_JLQ_ADSPLOAD}     += jlq-adsptest.o