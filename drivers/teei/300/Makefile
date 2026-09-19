# Copyright (c) 2015-2020, MICROTRUST Incorporated
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# version 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

$(info "CONFIG_MICROTRUST_TEE_SUPPORT=$(CONFIG_MICROTRUST_TEE_SUPPORT)")
$(info "CONFIG_MICROTRUST_TZ_DRIVER=$(CONFIG_MICROTRUST_TZ_DRIVER)")
$(info "CONFIG_MICROTRUST_VFS_DRIVER=$(CONFIG_MICROTRUST_VFS_DRIVER)")
$(info "CONFIG_MICROTRUST_FP_DRIVER=$(CONFIG_MICROTRUST_FP_DRIVER)")
$(info "CONFIG_MICROTRUST_DEBUG=$(CONFIG_MICROTRUST_DEBUG)")
$(info "CONFIG_MICROTRUST_TEST_DRIVERS=$(CONFIG_MICROTRUST_TEST_DRIVERS)")

isrc=$(srctree)/$(src)

ccflags-y  += -I$(isrc)/common/include

ccflags-y  += \
	-I$(isrc)/tz_dcih \
	-I$(isrc)/public \
	-I$(isrc)/tz_driver/include

ccflags-y += -I$(srctree)/include/linux/

ccflags-y += -DDEBUG

#obj-$(CONFIG_MICROTRUST_TZ_DRIVER) += isee.o
obj-$(CONFIG_MICROTRUST_TEE_SUPPORT) += isee.o

isee-y :=		tee/tee_core.o \
			tee/capi_proxy.o \
			tee/tee_client_api.o \
			tee/soter/core.o \
			tee/soter/call.o \
			tee/tee_shm.o \
			tee/tee_shm_pool.o \
			ut_keymaster/ut_keymaster.o \
			tz_driver/teei_fp.o \
			tz_driver/teei_client_transfer_data.o \
			tz_driver/teei_smc_call.o \
			tz_driver/sysfs.o \
			tz_driver/teei_cancel_cmd.o \
			tz_driver/fdrv.o \
			tz_driver/notify_queue.o \
			tz_driver/switch_queue.o \
			tz_driver/teei_keymaster.o \
			tz_driver/teei_task_link.o \
			tz_driver/tz_log.o \
			tz_driver/irq_register.o \
			tz_driver/teei_client_main.o \
			tz_driver/backward_driver.o \
			tz_vfs/fp_vendor.o \
			tz_vfs/vfsFun.o \
			tz_dcih/tz_dcih_test.o \
			tz_dcih/tz_dcih.o

isee-$(CONFIG_MICROTRUST_FP_DRIVER) +=		teei_fp/fp_func.o

isee-$(CONFIG_MICROTRUST_TEST_DRIVERS) +=		tests/main.o \
			tests/xtest_isee_1000.o \
			tests/xtest_isee_3000.o \
			tests/xtest_main.o

isee-$(CONFIG_MICROTRUST_UNITTEST_SUPPORT) +=		ut_tester/ut_tester.o

isee-$(CONFIG_MICROTRUST_VERIFIER_DRIVER) +=		tz_verifier/tz_verifier.o
