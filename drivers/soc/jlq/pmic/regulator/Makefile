ccflags-y += -I$(srctree)/drivers/opp
ccflags-y += -I$(srctree)/drivers/regulator

obj-$(CONFIG_REGULATOR_STUB)		+= stub-regulator.o
obj-$(CONFIG_JLQ_REGULATOR)		+= jlq-regulator.o
obj-$(CONFIG_REGULATOR_PROXY_CONSUMER) += proxy-consumer.o
obj-$(CONFIG_REGULATOR_QCOM_PM8008) += qcom_pm8008-regulator.o
obj-$(CONFIG_REGULATOR_RPM_SMD)		+= rpm-smd-regulator.o
obj-$(CONFIG_REGULATOR_QPNP_LCDB) += qpnp-lcdb-regulator.o
obj-$(CONFIG_REGULATOR_JLQ_DVFS) += jlq-dvfs.o
obj-y += jlq-sysfs.o
obj-$(CONFIG_JLQ_REGULATOR) += wl2866d.o
obj-$(CONFIG_JLQ_REGULATOR) += wl2866d-regulator.o
