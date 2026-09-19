# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the touchscreen drivers.
#

# Each configuration option enables a list of files.
gsl_ts-objs	+= gsl_point_id.o gsl_ts_driver.o
obj-$(CONFIG_TOUCHSCREEN_GSL)	+= gsl_ts.o gen_vkeys.o
$(obj)/gsl_point_id.o: $(srctree)/$(obj)/gsl_point_id
	cp $(srctree)/$(obj)/gsl_point_id $(obj)/gsl_point_id.o
