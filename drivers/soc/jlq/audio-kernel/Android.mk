# Android makefile for audio kernel modules
MY_LOCAL_PATH := $(call my-dir)

UAPI_OUT := $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/include

$(shell mkdir -p $(UAPI_OUT)/linux;)
$(shell mkdir -p $(UAPI_OUT)/sound;)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/ipc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/dsp/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/dsp/codecs/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/soc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/asoc/Module.symvers)
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/asoc/codecs/Module.symvers)

include $(MY_LOCAL_PATH)/include/uapi/Android.mk
include $(MY_LOCAL_PATH)/ipc/Android.mk
include $(MY_LOCAL_PATH)/dsp/Android.mk
include $(MY_LOCAL_PATH)/soc/Android.mk
include $(MY_LOCAL_PATH)/asoc/Android.mk
include $(MY_LOCAL_PATH)/asoc/codecs/Android.mk

$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/asoc/codecs/bolero/Module.symvers)
include $(MY_LOCAL_PATH)/asoc/codecs/bolero/Android.mk
$(shell rm -rf $(PRODUCT_OUT)/obj/vendor/jlq/opensource/audio-kernel/asoc/codecs/wcd937x/Module.symvers)
include $(MY_LOCAL_PATH)/asoc/codecs/wcd937x/Android.mk

