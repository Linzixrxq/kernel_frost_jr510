/*
 * Copyright (c)2019-2021   JLQ Technology Co.,Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#include <asm/io.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>

#include "slim_inc.h"
#include "slim_mtdr_if.h"
#include "slim_chk.h"
#include "slim_regs.h"
#include "slim_mtdr.h"

/**
 * Read a (32-bit) word
 */
uint32_t MTDR_ReadReg32(volatile uint32_t *address)
{
    return readl(address);
}

/**
 * Write a (32-bit) word to memory
 */
void MTDR_WriteReg32(volatile uint32_t *address, uint32_t value)
{

    writel(value, address);
}

/**
 * Read a byte, bypassing the cache
 */
uint8_t MTDR_UncachedRead8(volatile uint8_t *address)
{

    return readb(address);
}

/**
 * Read a short, bypassing the cache
 */
uint16_t MTDR_UncachedRead16(volatile uint16_t *address)
{

    return readw(address);
}

/**
 * Read a (32-bit) word, bypassing the cache
 */
uint32_t MTDR_UncachedRead32(volatile uint32_t *address)
{

    return readl(address);
}

/**
 * Write a byte to memory, bypassing the cache
 */
void MTDR_UncachedWrite8(volatile uint8_t *address, uint8_t value)
{

    writeb (value, address);
}

/**
 * Write a short to memory, bypassing the cache
 */
void MTDR_UncachedWrite16(volatile uint16_t *address, uint16_t value)
{

    writew (value, address);
}

/**
 * Write a (32-bit) word to memory, bypassing the cache
 */
void MTDR_UncachedWrite32(volatile uint32_t *address, uint32_t value)
{

    writel (value, address);
}

/**
 * Hardware specific memcpy.
 */
void MTDR_BufferCopy(volatile uint8_t *dst, volatile const uint8_t *src, uint32_t size)
{

    memcpy((void *)dst, (void *)src, size);
}

/**
* Invalidate the cache for the specified memory region.
*/
void MTDR_CacheInvalidate(void *address, size_t size, uintptr_t devInfo)
{
    uintptr_t phys_address = __pa(address);
    struct device *dev = (struct device *) devInfo;
    dma_sync_single_for_cpu(dev, phys_address, size, DMA_BIDIRECTIONAL);
    return;
}

/**
* Flush the cache for the specified memory region
*/
void MTDR_CacheFlush(void *address, size_t size, uintptr_t devInfo)
{
    uintptr_t phys_address = __pa(address);
    struct device *dev = (struct device *) devInfo;
    dma_sync_single_for_device(dev, phys_address, size, DMA_BIDIRECTIONAL);
    return;
}

/**
 * Delay software execution by a number of nanoseconds
 */
void MTDR_DelayNs(uint32_t ns)
{

    return;
}

/**
 * Memory barrier
 * Waits until previous data accesses are finished
 */
void MTDR_MemoryBarrier(void)
{
    return;
}


/**
 * Function to validate struct Message
 */
uint32_t MTDR_MessageSF(const MTDR_Message *obj)
{
    uint32_t ret = 0;

    if (obj == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {
        if (
            (obj->arbitrationType != MTDR_AT_NONE) &&
            (obj->arbitrationType != MTDR_AT_LONG) &&
            (obj->arbitrationType != MTDR_AT_SHORT)
        ) {
            ret = MTDR_RET_EINVAL;
        }
        if (
            (obj->arbitrationPriority != MTDR_AP_LOW) &&
            (obj->arbitrationPriority != MTDR_AP_DEFAULT) &&
            (obj->arbitrationPriority != MTDR_AP_HIGH) &&
            (obj->arbitrationPriority != MTDR_AP_MANAGER_1) &&
            (obj->arbitrationPriority != MTDR_AP_MANAGER_2) &&
            (obj->arbitrationPriority != MTDR_AP_MANAGER_3) &&
            (obj->arbitrationPriority != MTDR_AP_MAXIMUM)
        ) {
            ret = MTDR_RET_EINVAL;
        }
        if (
            (obj->messageType != MTDR_MT_CORE) &&
            (obj->messageType != MTDR_MT_DESTINATION_REFERRED_CLASS_SPECIFIC_MESSAGE) &&
            (obj->messageType != MTDR_MT_DESTINATION_REFERRED_USER_MESSAGE) &&
            (obj->messageType != MTDR_MT_SOURCE_REFERRED_CLASS_SPECIFIC_MESSAGE) &&
            (obj->messageType != MTDR_MT_SOURCE_REFERRED_USER_MESSAGE)
        ) {
            ret = MTDR_RET_EINVAL;
        }
        if (
            (obj->destinationType != MTDR_DT_LOGICAL_ADDRESS) &&
            (obj->destinationType != MTDR_DT_ENUMERATION_ADDRESS) &&
            (obj->destinationType != MTDR_DT_BROADCAST)
        ) {
            ret = MTDR_RET_EINVAL;
        }
        if (
            (obj->response != MTDR_MR_POSITIVE_ACK) &&
            (obj->response != MTDR_MR_NEGATIVE_ACK) &&
            (obj->response != MTDR_MR_NO_RESPONSE)
        ) {
            ret = MTDR_RET_EINVAL;
        }
    }

    return ret;
}


/**
 * Function to validate struct FramerConfig
 */
uint32_t MTDR_FramerConfigSF(const MTDR_FramerConfig *obj)
{
    uint32_t ret = 0;

    if (obj == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {
        if (
            (obj->quality != MTDR_FQ_PUNCTURED) &&
            (obj->quality != MTDR_FQ_IRREGULAR) &&
            (obj->quality != MTDR_FQ_REGULAR) &&
            (obj->quality != MTDR_FQ_LOW_JITTER)
        ) {
            ret = MTDR_RET_EINVAL;
        }
    }

    return ret;
}


/**
 * Function to validate struct GenericDeviceConfig
 */
uint32_t MTDR_GenericDeviceConfigSF(const MTDR_GenericDeviceConfig *obj)
{
    uint32_t ret = 0;

    if (obj == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {
        if (obj->presenceRatesSupported > (16777215)) {
            ret = MTDR_RET_EINVAL;
        }
        if (obj->dataPortClockPrescaler > (15)) {
            ret = MTDR_RET_EINVAL;
        }
        if (obj->cportClockDivider > (7)) {
            ret = MTDR_RET_EINVAL;
        }
        if (
            (obj->referenceClockSelector != MTDR_RC_CLOCK_GEAR_6) &&
            (obj->referenceClockSelector != MTDR_RC_CLOCK_GEAR_7) &&
            (obj->referenceClockSelector != MTDR_RC_CLOCK_GEAR_8) &&
            (obj->referenceClockSelector != MTDR_RC_CLOCK_GEAR_9)
        ) {
            ret = MTDR_RET_EINVAL;
        }
    }

    return ret;
}


/**
 * Function to validate struct Config
 */
uint32_t MTDR_ConfigSF(const MTDR_Config *obj)
{
    uint32_t ret = 0;

    if (obj == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {
        if (obj->retryLimit > (15)) {
            ret = MTDR_RET_EINVAL;
        }
    }

    return ret;
}


/**
 * Function to validate struct Callbacks
 */
uint32_t MTDR_CallbacksSF(const MTDR_Callbacks *obj)
{
    uint32_t ret = 0;

    if (obj == NULL) {
        ret = MTDR_RET_EINVAL;
    }

    return ret;
}


/**
 * Function to validate struct MessageCallbacks
 */
uint32_t MTDR_MessageCallbacksSF(const MTDR_MessageCallbacks *obj)
{
    uint32_t ret = 0;

    if (obj == NULL) {
        ret = MTDR_RET_EINVAL;
    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk1(const MTDR_Config *config, const uint16_t *requiredMemory)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (requiredMemory == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (MTDR_ConfigSF(config) == MTDR_RET_EINVAL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk2(const void *pD, const MTDR_Config *config, const MTDR_Callbacks *callbacks)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (MTDR_ConfigSF(config) == MTDR_RET_EINVAL) {
        ret = MTDR_RET_EINVAL;
    } else if (MTDR_CallbacksSF(callbacks) == MTDR_RET_EINVAL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk3(const void *pD)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    }


    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk8(const void *pD, const uint8_t *interruptMask)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (interruptMask == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk9(const void *pD, const uint8_t portNumber, const uint8_t interruptMask)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (portNumber > (63)) {
        ret = MTDR_RET_EINVAL;
    } else if (interruptMask > (63)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk10(const void *pD, const uint8_t portNumber, const uint8_t *interruptMask)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (interruptMask == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (portNumber > (63)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk11(const void *pD, const uint8_t portNumber)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (portNumber > (63)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk13(const void *pD, const uint8_t portNumber, const bool *enable)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (enable == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (portNumber > (63)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk14(const void *pD, const MTDR_MessageCallbacks *msgCallbacks)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (MTDR_MessageCallbacksSF(msgCallbacks) == MTDR_RET_EINVAL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk15(const void *pD, const void *message, const uint8_t messageLength)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (message == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if ((messageLength < (MTDR_MESSAGE_MIN_LENGTH)) || (messageLength > (MTDR_MESSAGE_MAX_LENGTH))) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk16(const void *pD, const MTDR_Message *message)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (MTDR_MessageSF(message) == MTDR_RET_EINVAL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk17(const void *pD, const uint32_t *regContent)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (regContent == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk19(const void *pD, const uint8_t mchLapse)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (mchLapse > (15)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk21(const void *pD, const uint16_t *mchUsage)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (mchUsage == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk24(const void *pD, const bool *state)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (state == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk31(const void *pD, const MTDR_FramerConfig *framerConfig)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (MTDR_FramerConfigSF(framerConfig) == MTDR_RET_EINVAL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk32(const void *pD, const MTDR_FramerConfig *framerConfig)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (framerConfig == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk33(const void *pD, const MTDR_GenericDeviceConfig *genericDeviceConfig)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (MTDR_GenericDeviceConfigSF(genericDeviceConfig) == MTDR_RET_EINVAL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk34(const void *pD, const MTDR_GenericDeviceConfig *genericDeviceConfig)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (genericDeviceConfig == NULL) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk35(const void *pD, const uint8_t portNumber, const MTDR_DataPortStatus *portStatus)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (portStatus == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (portNumber > (63)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk42(const void *pD, const uint8_t newLa)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (newLa > (239)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk45(const void *pD, const MTDR_ArbitrationPriority newArbitrationPriority)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (newArbitrationPriority != MTDR_AP_LOW) &&
        (newArbitrationPriority != MTDR_AP_DEFAULT) &&
        (newArbitrationPriority != MTDR_AP_HIGH) &&
        (newArbitrationPriority != MTDR_AP_MANAGER_1) &&
        (newArbitrationPriority != MTDR_AP_MANAGER_2) &&
        (newArbitrationPriority != MTDR_AP_MANAGER_3) &&
        (newArbitrationPriority != MTDR_AP_MAXIMUM)
    ) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk50(const void *pD, const uint8_t channelNumber, const MTDR_PresenceRate presenceRate, const MTDR_AuxFieldFormat auxiliaryBitFormat, const MTDR_DataType dataType, const uint8_t dataLength)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (presenceRate != MTDR_PR_12K) &&
        (presenceRate != MTDR_PR_24K) &&
        (presenceRate != MTDR_PR_48K) &&
        (presenceRate != MTDR_PR_96K) &&
        (presenceRate != MTDR_PR_192K) &&
        (presenceRate != MTDR_PR_384K) &&
        (presenceRate != MTDR_PR_768K) &&
        (presenceRate != MTDR_PR_11025) &&
        (presenceRate != MTDR_PR_22050) &&
        (presenceRate != MTDR_PR_44100) &&
        (presenceRate != MTDR_PR_88200) &&
        (presenceRate != MTDR_PR_176400) &&
        (presenceRate != MTDR_PR_352800) &&
        (presenceRate != MTDR_PR_705600) &&
        (presenceRate != MTDR_PR_4K) &&
        (presenceRate != MTDR_PR_8K) &&
        (presenceRate != MTDR_PR_16K) &&
        (presenceRate != MTDR_PR_32K) &&
        (presenceRate != MTDR_PR_64K) &&
        (presenceRate != MTDR_PR_128K) &&
        (presenceRate != MTDR_PR_256K) &&
        (presenceRate != MTDR_PR_512K)
    ) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (auxiliaryBitFormat != MTDR_AF_NOT_APPLICABLE) &&
        (auxiliaryBitFormat != MTDR_AF_ZCUV) &&
        (auxiliaryBitFormat != MTDR_AF_USER_DEFINED)
    ) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (dataType != MTDR_DF_NOT_INDICATED) &&
        (dataType != MTDR_DF_LPCM) &&
        (dataType != MTDR_DF_IEC61937) &&
        (dataType != MTDR_DF_PACKED_PDM_AUDIO) &&
        (dataType != MTDR_DF_USER_DEFINED_1) &&
        (dataType != MTDR_DF_USER_DEFINED_2)
    ) {
        ret = MTDR_RET_EINVAL;
    } else if (dataLength > (31)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk55(const void *pD, const uint8_t incomingFramerLa, const uint16_t outgoingFramerClockCycles, const uint16_t incomingFramerClockCycles)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (incomingFramerLa > (239)) {
        ret = MTDR_RET_EINVAL;
    } else if (outgoingFramerClockCycles > (4095)) {
        ret = MTDR_RET_EINVAL;
    } else if (incomingFramerClockCycles > (4095)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk56(const void *pD, const MTDR_SubframeMode newSubframeMode)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (newSubframeMode != MTDR_SM_24_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_16_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_16_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_12_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_12_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_8_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_8_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_6_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_6_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_6_CSW_8_SL) &&
        (newSubframeMode != MTDR_SM_4_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_4_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_4_CSW_8_SL) &&
        (newSubframeMode != MTDR_SM_4_CSW_6_SL) &&
        (newSubframeMode != MTDR_SM_3_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_3_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_3_CSW_8_SL) &&
        (newSubframeMode != MTDR_SM_3_CSW_6_SL) &&
        (newSubframeMode != MTDR_SM_2_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_2_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_2_CSW_8_SL) &&
        (newSubframeMode != MTDR_SM_2_CSW_6_SL) &&
        (newSubframeMode != MTDR_SM_1_CSW_32_SL) &&
        (newSubframeMode != MTDR_SM_1_CSW_24_SL) &&
        (newSubframeMode != MTDR_SM_1_CSW_8_SL) &&
        (newSubframeMode != MTDR_SM_1_CSW_6_SL) &&
        (newSubframeMode != MTDR_SM_8_CSW_8_SL)
    ) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk57(const void *pD, const MTDR_ClockGear newClockGear)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (newClockGear != MTDR_CG_0) &&
        (newClockGear != MTDR_CG_1) &&
        (newClockGear != MTDR_CG_2) &&
        (newClockGear != MTDR_CG_3) &&
        (newClockGear != MTDR_CG_4) &&
        (newClockGear != MTDR_CG_5) &&
        (newClockGear != MTDR_CG_6) &&
        (newClockGear != MTDR_CG_7) &&
        (newClockGear != MTDR_CG_8) &&
        (newClockGear != MTDR_CG_9) &&
        (newClockGear != MTDR_CG_10)
    ) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk58(const void *pD, const MTDR_RootFrequency newRootFrequency)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (newRootFrequency != MTDR_RF_0) &&
        (newRootFrequency != MTDR_RF_1) &&
        (newRootFrequency != MTDR_RF_2) &&
        (newRootFrequency != MTDR_RF_3) &&
        (newRootFrequency != MTDR_RF_4) &&
        (newRootFrequency != MTDR_RF_5) &&
        (newRootFrequency != MTDR_RF_6) &&
        (newRootFrequency != MTDR_RF_7) &&
        (newRootFrequency != MTDR_RF_8) &&
        (newRootFrequency != MTDR_RF_9)
    ) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk59(const void *pD, const MTDR_RestartTime newRestartTime)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (newRestartTime != MTDR_RT_FAST_RECOVERY) &&
        (newRestartTime != MTDR_RT_CONSTANT_PHASE_RECOVERY) &&
        (newRestartTime != MTDR_RT_UNSPECIFIED_DELAY)
    ) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk62(const void *pD, const uint8_t channelNumber, const MTDR_TransportProtocol transportProtocol, const uint16_t segmentDistribution, const uint8_t segmentLength)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (
        (transportProtocol != MTDR_TP_ISOCHRONOUS) &&
        (transportProtocol != MTDR_TP_PUSHED) &&
        (transportProtocol != MTDR_TP_PULLED) &&
        (transportProtocol != MTDR_TP_LOCKED) &&
        (transportProtocol != MTDR_TP_ASYNC_SIMPLEX) &&
        (transportProtocol != MTDR_TP_ASYNC_HALF_DUPLEX) &&
        (transportProtocol != MTDR_TP_EXT_ASYNC_SIMPLEX) &&
        (transportProtocol != MTDR_TP_EXT_ASYNC_HALF_DUPLEX) &&
        (transportProtocol != MTDR_TP_USER_DEFINED_1) &&
        (transportProtocol != MTDR_TP_USER_DEFINED_2)
    ) {
        ret = MTDR_RET_EINVAL;
    } else if (segmentDistribution > (4095)) {
        ret = MTDR_RET_EINVAL;
    } else if (segmentLength > (31)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}


/**
 * A common function to check the validity of API functions with
 */
uint32_t MTDR_ParasChk69(const void *pD, const uint8_t valueUpdateSize)
{
    /* Declaring return variable */
    uint32_t ret = 0;

    if (pD == NULL) {
        ret = MTDR_RET_EINVAL;
    } else if (valueUpdateSize > (16)) {
        ret = MTDR_RET_EINVAL;
    } else {

    }

    return ret;
}

MODULE_LICENSE("GPL");

