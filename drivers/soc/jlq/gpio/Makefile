ccflags-y += -I$(srctree)/drivers/pinctrl

jlq-gpio-objs	+= jlq-ap-gpio.o pinctrl-data-jr510.o pinctrl-data-ja310.o
qtang_gpio-objs	+= jlq-qtang-gpio.o pinctrl-data-qtang.o
obj-$(CONFIG_GPIO_JLQ) += jlq-gpio.o
obj-$(CONFIG_GPIO_JLQ_QTANG) += qtang_gpio.o
