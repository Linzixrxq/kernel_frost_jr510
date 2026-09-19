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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "slim_inc.h"
#include "slim_mtdr_if.h"
#include "slim_chk.h"
#include "slim_regs.h"
#include "slim_mtdr.h"
#include "slim_master.h"


/*mtdi driver private data*/
static void *pDrvIns = NULL;


uint32_t MTDR_MsgAssignLogicalAddress(void *pD, uint64_t destinationEa, uint8_t newLa);
uint32_t MTDR_MsgClearInformation(void *pD, bool broadcast, uint8_t destinationLa, uint16_t elementCode, uint8_t *clearMask, uint8_t clearMaskSize);

/**
 * Wait until previous configuration is set (CFG_STROBE bit is cleared)
 */
static uint32_t MTDR_CfgStrobeCheck(MTDR_Instance *instance)
{
    uint32_t reg = 0;
    uint32_t timeout = 0;
    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    /* If Manager component is disabled do not poll CFG_STROBE bit */
    if ((CONFIGURATION__CONFIG_MODE__ENABLE__READ(reg)) == 0)
        return MTDR_RET_EOK;

    do {
        reg = MTDR_ReadReg(COMMAND_STATUS.COMMAND);
        if (++timeout == 0)
            return MTDR_RET_EIO;
    } while (COMMAND_STATUS__COMMAND__CFG_STROBE__READ(reg));

    return MTDR_RET_EOK;
}

/**
 * Set CFG_STROBE bit to apply changes in CONFIG Registers
 */
static void MTDR_CfgStrobeSet(MTDR_Instance *instance, bool force)
{
    uint32_t reg = 0;
    if (!force) {
        reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
        if ((CONFIGURATION__CONFIG_MODE__ENABLE__READ(reg)) == 0)
            return;
    }

    reg = MTDR_ReadReg(COMMAND_STATUS.COMMAND);
    COMMAND_STATUS__COMMAND__CFG_STROBE__SET(reg);
    MTDR_WriteReg(COMMAND_STATUS.COMMAND, reg);
}

/**
 * Masks or unmasks all interrupts
 */
static void MTDR_SetInterruptsEnabled(MTDR_Instance *instance, bool enabled)
{
    uint32_t reg = 0;

    reg = MTDR_ReadReg(INTERRUPTS.INT);
    if ((reg != 0) && (enabled == 0))
        return;

    reg = MTDR_ReadReg(INTERRUPTS.INT_EN);
    INTERRUPTS__INT_EN__INT_EN__MODIFY(reg, enabled);
    MTDR_WriteReg(INTERRUPTS.INT_EN, reg);
}

/**
 * Convert MTDR_SliceSize enum to size [bytes]
 */
static inline uint8_t MTDR_SliceSizeToBytes(MTDR_SliceSize sliceSize)
{
#if 0 // XXX DK: Replace with?:
    return ((unsigned)sliceSize < 8)
    ? (uint8_t const []) {
        1, 2, 3, 4, 6, 8, 12, 16
    }[(unsigned)sliceSize];
    : 0;
#endif

    switch (sliceSize) {
    case MTDR_SS_1_BYTE:
        return 1;
    case MTDR_SS_2_BYTES:
        return 2;
    case MTDR_SS_3_BYTES:
        return 3;
    case MTDR_SS_4_BYTES:
        return 4;
    case MTDR_SS_6_BYTES:
        return 6;
    case MTDR_SS_8_BYTES:
        return 8;
    case MTDR_SS_12_BYTES:
        return 12;
    case MTDR_SS_16_BYTES:
        return 16;
    default:
        return 0;
    }
}

/**
 * Fills memory area with zeroes
 */
static void MTDR_ClearMemory(void *memPtr, uint16_t size)
{
    uint16_t i;
    uint8_t *memory = (uint8_t *) memPtr;
    for (i = 0; i < size; i++, memory++)
        *memory = 0x00;
}

/**
 * Calculate Primary Integrity (CRC4)
 */
static uint32_t MTDR_CrcPrimaryIntegrity(uint8_t *data, uint8_t offset, uint8_t dataLen, uint8_t initVal)
{
    uint8_t crc = initVal;
    uint8_t tmp0 = 0;
    uint8_t tmp1 = 0;
    uint8_t i, bit;

    for (i = offset; i < offset + dataLen; i++) {
        for (bit = 0; bit < 8; bit++) {
            if ((i == dataLen - 1) && (bit == 4)) // Only lower four bits from last byte
                break;
            tmp0 = (((crc & 8) >> 3) ^ (((data[i] << bit) & 128) >> 7)) & 1;
            tmp1 = (((crc & 1) ^ tmp0) << 1) & 2;
            crc = ((crc << 1) & 12) | tmp1 | tmp0;
        }
    }
    return crc;
}

/**
 * Calculate Message Integrity (CRC4)
 */
static uint32_t MTDR_CrcMessageIntegrity(uint8_t *data, uint8_t offset, uint8_t dataLen, uint8_t initVal)
{
    uint8_t crc = initVal;
    uint8_t tmp0 = 0;
    uint8_t tmp1 = 0;
    uint8_t i, bit;

    for (i = offset; i < offset + dataLen; i++) {
        // Only higher four bits from first byte
        for (bit = (i == offset) ? 4 : 0; bit < 8; bit++) {
            tmp0 = (((crc & 8) >> 3 ) ^ (((data[i] << bit) & 128) >> 7)) & 1;
            tmp1 = (((crc & 1) ^ tmp0) << 1) & 2;
            crc = (((crc << 1) & 12) | tmp1 | tmp0) & 0xFF;
        }
    }
    return crc;
}

/**
 * Decode message - convert array of bytes to structure
 */
static uint32_t MTDR_DecodeMessage(MTDR_Instance *instance, uint8_t *receivedMessage, uint8_t messageLen, MTDR_Message *message)
{
    uint8_t i;
    uint8_t remainingLength = 0;
    uint8_t lengthArbitrationField = 0;
    uint8_t lengthHeaderField = 0;
    uint8_t offset = 0;

    /**
     *  Arbitration Field: 2 or 7 bytes
     *  BYTE  0:    Arbitration Type, Arbitration Extension, Arbitration Priority
     *  BYTE  1:    Source Logical Address / Enumeration Address [7:0]
     *  BYTES 2-6:  NA / Source Enumeration Address [47:8]
     */
    message->arbitrationType = MTDR_GetMsgField(receivedMessage, offset, ARBITRATION_TYPE);
    switch (message->arbitrationType) {
    case MTDR_AT_LONG:
        message->sourceAddress = MTDR_GetField6B(receivedMessage, MTDR_GetMsgFieldOffset(SOURCE_ADDRESS));
        lengthArbitrationField = MTDR_MESSAGE_ARBITRATION_LENGTH_LONG;
        break;
    case MTDR_AT_SHORT:
        message->sourceAddress = MTDR_GetField1B(receivedMessage, MTDR_GetMsgFieldOffset(SOURCE_ADDRESS));
        lengthArbitrationField = MTDR_MESSAGE_ARBITRATION_LENGTH_SHORT;
        break;
    default:
        return MTDR_RET_EINVAL;
    }
    message->arbitrationPriority = MTDR_GetMsgField(receivedMessage, offset, ARBITRATION_PRIORITY);

    /*
     *  Header Field : 3, 4 or 9 bytes
     *  BYTE  0:    Message Type, Remaining Length
     *  BYTE  1:    Message Code
     *  BYTE  2:    Destination Type, Primary Integrity
     *  BYTE  3:    Destination Logical Address / Enumeration Address [7:0]
     *  BYTES 4-8:  NA / Destination Enumeration Address [47:8]
     */
    offset = lengthArbitrationField;

    message->messageType = MTDR_GetMsgField(receivedMessage, offset, MESSAGE_TYPE);
    remainingLength = MTDR_GetMsgField(receivedMessage, offset, REMAINING_LENGTH);
    message->messageCode = MTDR_GetMsgField(receivedMessage, offset, MESSAGE_CODE);
    message->destinationType = MTDR_GetMsgField(receivedMessage, offset, MESSAGE_DESTINATION_TYPE);

    switch (message->destinationType) {
    case MTDR_DT_BROADCAST:
        lengthHeaderField = MTDR_MESSAGE_HEADER_LENGTH_BROADCAST;
        message->destinationAddress = 0x0;
        break;

    case MTDR_DT_LOGICAL_ADDRESS:
        lengthHeaderField = MTDR_MESSAGE_HEADER_LENGTH_SHORT;
        message->destinationAddress = MTDR_GetField1B(receivedMessage, offset + MTDR_GetMsgFieldOffset(DESTINATION_ADDRESS));
        break;

    case MTDR_DT_ENUMERATION_ADDRESS:
        lengthHeaderField = MTDR_MESSAGE_HEADER_LENGTH_LONG;
        message->destinationAddress = MTDR_GetField6B(receivedMessage, offset + MTDR_GetMsgFieldOffset(DESTINATION_ADDRESS));
        break;

    default:
        return MTDR_RET_EINVAL;
    }

    /* Payload length */
    offset = lengthArbitrationField + lengthHeaderField;

    if (remainingLength - lengthHeaderField > MTDR_MESSAGE_PAYLOAD_MAX_LENGTH)
        return MTDR_RET_EINVAL;
    message->payloadLength = remainingLength - lengthHeaderField;

    /*
     * Message Integrity and Response Field
     * BYTE 0:      Message Integrity, Message Response
     */
    message->response = MTDR_GetMsgField(receivedMessage, offset + message->payloadLength, RESPONSE_CODE);

    /* Payload data */
    for (i = 0; i < message->payloadLength; i++)
        message->payload[i] = MTDR_GetField1B(receivedMessage, offset + i);

    return MTDR_RET_EOK;
}

uint32_t m_MTDR_DecodeMessage(uint8_t *receivedMessage, uint8_t messageLen, MTDR_Message *message)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_DecodeMessage(instance, receivedMessage, messageLen, message);
}


/**
 * Encode message - convert structure to array of bytes
 */
static uint32_t MTDR_EncodeMessage(MTDR_Instance *instance, uint8_t *memoryToFill, MTDR_Message *message)
{
    uint8_t i;
    uint8_t remainingLength = 0;
    uint8_t primaryIntegrity = 0;
    uint8_t messageIntegrity = 0;
    uint8_t offset = 0;
    uint8_t lengthArbitrationField = 0;
    uint8_t lengthHeaderField = 0;
    uint8_t primaryIntegrityBytes = 0;

    /* Arbitration Field: 2 or 7 bytes */
    MTDR_SetMsgField(memoryToFill, message->arbitrationType, offset, ARBITRATION_TYPE);

    switch (message->arbitrationType) {
    case MTDR_AT_LONG:
        MTDR_SetField6B(memoryToFill, message->sourceAddress, MTDR_GetMsgFieldOffset(SOURCE_ADDRESS));
        lengthArbitrationField = MTDR_MESSAGE_ARBITRATION_LENGTH_LONG;
        break;
    case MTDR_AT_SHORT:
        MTDR_SetField1B(memoryToFill, message->sourceAddress, MTDR_GetMsgFieldOffset(SOURCE_ADDRESS));
        lengthArbitrationField = MTDR_MESSAGE_ARBITRATION_LENGTH_SHORT;
        break;
    default:
        return 0;
    }
    MTDR_SetMsgField(memoryToFill, message->arbitrationPriority, offset, ARBITRATION_PRIORITY);
    MTDR_SetMsgField(memoryToFill, 0, offset, ARBITRATION_EXTENSION);

    /* Header Field: 3, 4 or 9 bytes */
    offset = lengthArbitrationField;

    MTDR_SetMsgField(memoryToFill, message->messageType, offset, MESSAGE_TYPE);
    MTDR_SetMsgField(memoryToFill, message->messageCode, offset, MESSAGE_CODE);
    MTDR_SetMsgField(memoryToFill, message->destinationType, offset, MESSAGE_DESTINATION_TYPE);

    switch (message->destinationType) {
    case MTDR_DT_BROADCAST:
        lengthHeaderField = MTDR_MESSAGE_HEADER_LENGTH_BROADCAST;
        break;

    case MTDR_DT_LOGICAL_ADDRESS:
        lengthHeaderField = MTDR_MESSAGE_HEADER_LENGTH_SHORT;
        MTDR_SetField1B(memoryToFill, message->destinationAddress, offset + MTDR_GetMsgFieldOffset(DESTINATION_ADDRESS));
        break;

    case MTDR_DT_ENUMERATION_ADDRESS:
        lengthHeaderField = MTDR_MESSAGE_HEADER_LENGTH_LONG;
        MTDR_SetField6B(memoryToFill, message->destinationAddress, offset + MTDR_GetMsgFieldOffset(DESTINATION_ADDRESS));
        break;

    default:
        return 0;
    }

    remainingLength = message->payloadLength + lengthHeaderField;
    MTDR_SetMsgField(memoryToFill, remainingLength, offset, REMAINING_LENGTH);

    /* Primary Integrity */
    if (instance->disableHardwareCrcCalculation) {
        primaryIntegrityBytes = offset + MTDR_GetMsgFieldOffset(DESTINATION_ADDRESS);
        primaryIntegrity = MTDR_CrcPrimaryIntegrity(memoryToFill, 0, primaryIntegrityBytes, 0);
        MTDR_SetMsgField(memoryToFill, primaryIntegrity, offset, PRIMARY_INTEGRITY);
    }

    /* Payload data */
    offset = lengthArbitrationField + lengthHeaderField;
    for (i = 0; i < message->payloadLength; i++)
        MTDR_SetField1B(memoryToFill, message->payload[i], offset + i);

    /* Message integrity */
    if (instance->disableHardwareCrcCalculation) {
        messageIntegrity = MTDR_CrcMessageIntegrity(memoryToFill, primaryIntegrityBytes - 1, remainingLength - 3 + 1, primaryIntegrity);
        MTDR_SetMsgField(memoryToFill, messageIntegrity, offset + message->payloadLength, MESSAGE_INTEGRITY);
    }
    return lengthArbitrationField + lengthHeaderField + message->payloadLength + 1;
}

/**
 * Transmit data to TX_FIFO
 */
static uint32_t MTDR_FifoTransmit(MTDR_Instance *instance, uint8_t *txFifoData, uint8_t txFifoDataSize)
{
    uint8_t i;
    uint8_t regsToWrite = 0;
    uint32_t *txFifoData32b = (uint32_t *) txFifoData;
    uint32_t reg;
    uint32_t timeout = 0;

#if DEBUG_CHECK_INFO
    //PR_INFO(LOGTAG"[1]MTDR_FifoTransmit->COMMAND_STATUS.STATE: %x\n", readl(MTDR_REGS_BASE + SB_STATE));
#endif

    do { //If TX_FIFO is full or command is being sent, wait
        reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
        if (++timeout == 0)
            return MTDR_RET_EIO;
    } while (((COMMAND_STATUS__STATE__TX_FULL__READ(reg))) || (COMMAND_STATUS__STATE__TX_PUSH__READ(reg)));

    timeout = 0;
    MTDR_SetInterruptsEnabled(instance, 0);

    regsToWrite = (txFifoDataSize >> 2) + ((txFifoDataSize & 0x3) != 0); //regsToWrite = txFifoDataSize / 4 + 1 if txFifoDataSize % 4 > 0

#if DEBUG_CHECK_INFO
    //PR_INFO(LOGTAG"[2]MTDR_FifoTransmit->COMMAND_STATUS.STATE: %x\n", readl(MTDR_REGS_BASE + SB_STATE));
#endif

    /*regsToWrite < 16, maybe check this, deman*/

    for (i = 0; i < regsToWrite; i++, txFifoData32b++) {
        MTDR_WriteReg(MESSAGE_FIFOS.MC_FIFO[i], *txFifoData32b);
    }

    reg = MTDR_ReadReg(COMMAND_STATUS.COMMAND);
    COMMAND_STATUS__COMMAND__TX_PUSH__SET(reg);
    MTDR_WriteReg(COMMAND_STATUS.COMMAND, reg);

#if DEBUG_CHECK_INFO
    //PR_INFO(LOGTAG"[3]MTDR_FifoTransmit->COMMAND_STATUS.STATE: %x\n", readl(MTDR_REGS_BASE + SB_STATE));
#endif

    do { //If TX_PUSH command is still in progress, wait until it's finished
        reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
        if (++timeout == 0) {
            MTDR_SetInterruptsEnabled(instance, 1);
            return MTDR_RET_EIO;
        }
    } while (COMMAND_STATUS__STATE__TX_PUSH__READ(reg));

#if DEBUG_CHECK_INFO
    //PR_INFO(LOGTAG"[4]MTDR_FifoTransmit->COMMAND_STATUS.STATE: %x\n", readl(MTDR_REGS_BASE + SB_STATE));
#endif

    MTDR_SetInterruptsEnabled(instance, 1);
    return MTDR_RET_EOK;
}

/**
 * Receive data from RX_FIFO
 */
static uint32_t MTDR_FifoReceive(MTDR_Instance *instance, uint8_t *rxFifoData, uint8_t rxFifoMaxSize)
{
    uint32_t rxFifoFlag = 0;
    uint8_t rxFifoMsgSize = 0;
    uint8_t i;
    uint8_t regsToRead = 0;
    uint8_t result = 0;
    uint32_t *rxFifoData32b = (uint32_t *) rxFifoData;
    uint32_t reg;
    rxFifoFlag = MTDR_ReadReg(MESSAGE_FIFOS.MC_FIFO[MTDR_RX_FIFO_FLAG_OFFSET]);

    if (rxFifoFlag & (1 << MTDR_RX_FIFO_FLAG_RX_OVERFLOW)) { //Message Overflow
        result = 0;
        goto rxFifoEnd;
    }

    rxFifoMsgSize = rxFifoFlag & MTDR_RX_FIFO_FLAG_RX_MSG_MASK;
    if (rxFifoMsgSize > rxFifoMaxSize) {
        result = 0;
        goto rxFifoEnd;
    }
    regsToRead = (rxFifoMsgSize >> 2) + ((rxFifoMsgSize & 0x3) != 0); //regsToRead = rxFifoMsgSize / 4 + 1 if rxFifoSize % 4 > 0

    /*regsToRead < (16-1), one byte(0x7c) used for RX_FIFO_FLAG, maybe check this, deman*/

    for (i = 0; i < regsToRead; i++, rxFifoData32b++) {
        *rxFifoData32b = MTDR_ReadReg(MESSAGE_FIFOS.MC_FIFO[i]);
    }
    result = rxFifoMsgSize;

rxFifoEnd:
    reg = MTDR_ReadReg(COMMAND_STATUS.COMMAND);
    COMMAND_STATUS__COMMAND__RX_PULL__SET(reg);
    MTDR_WriteReg(COMMAND_STATUS.COMMAND, reg);
    return result;
}

uint32_t m_MTDR_FifoReceive(uint8_t *rxFifoData, uint8_t rxFifoMaxSize)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_FifoReceive(instance, rxFifoData, rxFifoMaxSize);
}


/**
 * Transmit SLIMbus message
 */
static uint32_t MTDR_TransmitMessage(MTDR_Instance *instance, MTDR_Message *message)
{
    uint8_t txFifoData[MTDR_TX_FIFO_MSG_MAX_SIZE];
    uint8_t fifoSize = 0;

    MTDR_ClearMemory((void *) txFifoData, MTDR_TX_FIFO_MSG_MAX_SIZE);

    if (instance->basicCallbacks.onMessageSending != NULL)
        instance->basicCallbacks.onMessageSending((void *) instance, message);

    fifoSize = MTDR_EncodeMessage(instance, txFifoData, message);
    if (fifoSize == 0)
        return MTDR_RET_EINVAL;

    if (instance->basicCallbacks.onRawMessageSending != NULL)
        instance->basicCallbacks.onRawMessageSending((void *) instance, (void *) txFifoData, fifoSize);


#if DEBUG_CHECK_DATA
    {
        int i = 0;
        uint8_t *p = txFifoData;
        PR_INFO(LOGTAG"[TX DATA](%d)BEGIN: [", fifoSize);
        for(i = 0; i < fifoSize; i++)
            PR_INFO("%x ", *p++);
        PR_INFO("]END\n");
    }
#endif


#if DEBUG_CHECK_DATA
    {
        int i = 0;
        uint8_t *p = message->payload;

        PR_INFO(LOGTAG"TX_FIFO message BEGIN: {\n");
        if(message->arbitrationType == MTDR_AT_SHORT)
            PR_INFO("srcAddr: %x\n", (uint8_t)(message->sourceAddress & 0xff));
        else if(message->arbitrationType == MTDR_AT_LONG)
            PR_INFO("srcAddr: %4x %8x\n", (uint16_t)((message->sourceAddress >> 32) & 0xffff), (uint32_t)((message->sourceAddress) & 0xffffffff));

        if(message->destinationType == MTDR_DT_LOGICAL_ADDRESS)
            PR_INFO("dstAddr: %x\n", (uint8_t)(message->destinationAddress & 0xff));
        else if(message->destinationType == MTDR_DT_ENUMERATION_ADDRESS)
            PR_INFO("dstAddr: %4x %8x\n", (uint16_t)((message->destinationAddress >> 32) & 0xffff), (uint32_t)((message->destinationAddress) & 0xffffffff));

        PR_INFO("MT: %x\n", message->messageType);
        PR_INFO("MC: %x\n", message->messageCode);

        PR_INFO("PD: ");
        for(i = 0; i < message->payloadLength; i++) {
            PR_INFO("%x ", *p++);
        }
        PR_INFO("}END\n");
    }
#endif


    if (MTDR_FifoTransmit(instance, txFifoData, fifoSize) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    return MTDR_RET_EOK;
}



/**
 * Assign logical address to a discovered device
 */
static inline uint32_t MTDR_AssignLogicalAddress(MTDR_Instance *instance, MTDR_Message *message)
{
    uint8_t logicalAddress = 0x0;
    if (instance->basicCallbacks.onAssignLogicalAddress == NULL)
        return MTDR_RET_EINVAL;
    logicalAddress = instance->basicCallbacks.onAssignLogicalAddress(message->sourceAddress, message->payload[0]);
    if (logicalAddress > 0xEF) //Reserved value was used
        return MTDR_RET_EINVAL;
    return MTDR_MsgAssignLogicalAddress((void *) instance, message->sourceAddress, logicalAddress);
}

/**
 * Check Received Information Element for errors and possible callbacks
 * Also clears received information elements.
 */
static inline uint32_t MTDR_CheckInformationElement(MTDR_Instance *instance, MTDR_Message *message)
{
    uint16_t elementCode;
    uint16_t ecByteAddress;
    bool ecAccessByteBased;         // 1 - Byte-based Access, 0 - Elemental Access
    MTDR_SliceSize ecSliceSize;     // For Byte-based Access
    uint8_t ecBitNumber;            // For Elemental Access
    MTDR_DeviceClass deviceClass;
    MTDR_InformationElements ie;
    uint8_t i;
    uint8_t *informationSlice;
    uint8_t informationSliceLen;

    if (message->messageCode != MTDR_MESSAGE_CODE_REPORT_INFORMATION)
        return MTDR_RET_EINVAL;

    /*
     * Payload:
     * 0    EC       [7:0]
     * 1    EC      [15:8]
     * 2    IS       [7:0]
     * N+1  IS [8N-1:8N-8]
     */
    elementCode = message->payload[0] | (message->payload[1] << 8);
    ecAccessByteBased = MTDR_GetMsgPayloadField(ELEMENT_CODE_ACCESS_TYPE, elementCode);
    ecByteAddress = MTDR_GetMsgPayloadField(ELEMENT_CODE_BYTE_ADDRESS, elementCode);
    if (ecAccessByteBased) {
        ecSliceSize = MTDR_GetMsgPayloadField(ELEMENT_CODE_SLICE_SIZE, elementCode);
        if (MTDR_SliceSizeToBytes(ecSliceSize) != message->payloadLength - 2)
            return MTDR_RET_EINVAL; //Received Slice Size is different than payload
    } else {
        ecBitNumber = MTDR_GetMsgPayloadField(ELEMENT_CODE_BIT_NUMBER, elementCode);
    }

    informationSlice = &(message->payload[MTDR_ELEMENT_CODE_LENGTH]);
    informationSliceLen = message->payloadLength - MTDR_ELEMENT_CODE_LENGTH;

    if (informationSliceLen == 0)
        return MTDR_RET_EINVAL;

    MTDR_ClearMemory((void *) &ie, sizeof(MTDR_InformationElements));

    if (ecByteAddress >= MTDR_IE_CLASS_SPECIFIC_OFFSET) {
        deviceClass = instance->basicCallbacks.onDeviceClassRequest(message->sourceAddress);
        switch (deviceClass) {
        case MTDR_DC_MANAGER:
            if (ecAccessByteBased) {
                for (i = 0; i < informationSliceLen; i++) {
                    MTDR_GetIeByteAccess(ie.managerActiveManager, MANAGER_ACTIVE_MANAGER, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(MANAGER_ACTIVE_MANAGER, ecByteAddress + i, informationSlice[i]);
                }
            } else {
                MTDR_GetIeElementalAccess(ie.managerActiveManager, MANAGER_ACTIVE_MANAGER, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(MANAGER_ACTIVE_MANAGER, ecByteAddress, ecBitNumber, informationSlice[0]);
            }
            break;

        case MTDR_DC_FRAMER:
            if (ecAccessByteBased) {
                for (i = 0; i < informationSliceLen; i++) {
                    MTDR_GetIeByteAccess(ie.framerGcTxCol, FRAMER_GC_TX_COL, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(FRAMER_GC_TX_COL, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.framerFiTxCol, FRAMER_FI_TX_COL, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(FRAMER_FI_TX_COL, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.framerFsTxCol, FRAMER_FS_TX_COL, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(FRAMER_FS_TX_COL, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.framerActiveFramer, FRAMER_ACTIVE_FRAMER, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(FRAMER_ACTIVE_FRAMER, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.framerQuality, FRAMER_QUALITY, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(FRAMER_QUALITY, ecByteAddress + i, informationSlice[i]);
                }
            } else {
                MTDR_GetIeElementalAccess(ie.framerGcTxCol, FRAMER_GC_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(FRAMER_GC_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.framerFiTxCol, FRAMER_FI_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(FRAMER_FI_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.framerFsTxCol, FRAMER_FS_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(FRAMER_FS_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.framerActiveFramer, FRAMER_ACTIVE_FRAMER, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(FRAMER_ACTIVE_FRAMER, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.framerQuality, FRAMER_QUALITY, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(FRAMER_QUALITY, ecByteAddress, ecBitNumber, informationSlice[0]);
            }
            break;

        case MTDR_DC_INTERFACE:
            if (ecAccessByteBased) {
                for (i = 0; i < informationSliceLen; i++) {
                    MTDR_GetIeByteAccess(ie.interfaceDataSlotOverlap, INTERFACE_DATA_SLOT_OVERLAP, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(INTERFACE_DATA_SLOT_OVERLAP, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.interfaceLostMs, INTERFACE_LOST_MS, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(INTERFACE_LOST_MS, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.interfaceLostSfs, INTERFACE_LOST_SFS, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(INTERFACE_LOST_SFS, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.interfaceLostFs, INTERFACE_LOST_FS, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(INTERFACE_LOST_FS, ecByteAddress + i, informationSlice[i]);

                    MTDR_GetIeByteAccess(ie.interfaceMcTxCol, INTERFACE_MC_TX_COL, ecByteAddress + i, informationSlice[i]);
                    MTDR_ClearIeByteAccess(INTERFACE_MC_TX_COL, ecByteAddress + i, informationSlice[i]);
                }
            } else {
                MTDR_GetIeElementalAccess(ie.interfaceDataSlotOverlap, INTERFACE_DATA_SLOT_OVERLAP, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(INTERFACE_DATA_SLOT_OVERLAP, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.interfaceLostMs, INTERFACE_LOST_MS, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(INTERFACE_LOST_MS, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.interfaceLostSfs, INTERFACE_LOST_SFS, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(INTERFACE_LOST_SFS, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.interfaceLostFs, INTERFACE_LOST_FS, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(INTERFACE_LOST_FS, ecByteAddress, ecBitNumber, informationSlice[0]);

                MTDR_GetIeElementalAccess(ie.interfaceMcTxCol, INTERFACE_MC_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);
                MTDR_ClearIeElementalAccess(INTERFACE_MC_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);
            }
            break;

        case MTDR_DC_GENERIC:
            break;

        default:
            return MTDR_RET_EINVAL;
        }
    } else {
        if (ecAccessByteBased) {
            for (i = 0; i < informationSliceLen; i++) { //Information Slice is bytes 2 and up of the Payload, loop through all received bytes
                MTDR_GetIeByteAccess(ie.coreExError, CORE_EX_ERROR, ecByteAddress + i, informationSlice[i]);
                MTDR_ClearIeByteAccess(CORE_EX_ERROR, ecByteAddress + i, informationSlice[i]);

                MTDR_GetIeByteAccess(ie.coreReconfigObjection, CORE_RECONFIG_OBJECTION, ecByteAddress + i, informationSlice[i]);
                MTDR_ClearIeByteAccess(CORE_RECONFIG_OBJECTION, ecByteAddress + i, informationSlice[i]);

                MTDR_GetIeByteAccess(ie.coreDataTxCol, CORE_DATA_TX_COL, ecByteAddress + i, informationSlice[i]);
                MTDR_ClearIeByteAccess(CORE_DATA_TX_COL, ecByteAddress + i, informationSlice[i]);

                MTDR_GetIeByteAccess(ie.coreUnsprtdMsg, CORE_UNSPRTD_MSG, ecByteAddress + i, informationSlice[i]);
                MTDR_ClearIeByteAccess(CORE_UNSPRTD_MSG, ecByteAddress + i, informationSlice[i]);

                MTDR_ClearIeByteAccess(CORE_DEVICE_CLASS, ecByteAddress + i, informationSlice[i]);
                MTDR_ClearIeByteAccess(CORE_DEVICE_CLASS_VERSION, ecByteAddress + i, informationSlice[i]);
            }
        } else {    //Information Slice is 1 Information Element
            MTDR_GetIeElementalAccess(ie.coreExError, CORE_EX_ERROR, ecByteAddress, ecBitNumber, informationSlice[0]);
            MTDR_ClearIeElementalAccess(CORE_EX_ERROR, ecByteAddress, ecBitNumber, informationSlice[0]);

            MTDR_GetIeElementalAccess(ie.coreReconfigObjection, CORE_RECONFIG_OBJECTION, ecByteAddress, ecBitNumber, informationSlice[0]);
            MTDR_ClearIeElementalAccess(CORE_RECONFIG_OBJECTION, ecByteAddress, ecBitNumber, informationSlice[0]);

            MTDR_GetIeElementalAccess(ie.coreDataTxCol, CORE_DATA_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);
            MTDR_ClearIeElementalAccess(CORE_DATA_TX_COL, ecByteAddress, ecBitNumber, informationSlice[0]);

            MTDR_GetIeElementalAccess(ie.coreUnsprtdMsg, CORE_UNSPRTD_MSG, ecByteAddress, ecBitNumber, informationSlice[0]);
            MTDR_ClearIeElementalAccess(CORE_UNSPRTD_MSG, ecByteAddress, ecBitNumber, informationSlice[0]);

            MTDR_ClearIeElementalAccess(CORE_DEVICE_CLASS, ecByteAddress, ecBitNumber, informationSlice[0]);
            MTDR_ClearIeElementalAccess(CORE_DEVICE_CLASS_VERSION, ecByteAddress, ecBitNumber, informationSlice[0]);
        }
    }

    if (instance->basicCallbacks.onInformationElementReported != NULL)
        instance->basicCallbacks.onInformationElementReported((void *) instance, message->sourceAddress, &ie);

    //Clear received Information Element
    MTDR_MsgClearInformation((void *) instance, 0, message->sourceAddress, elementCode, informationSlice, informationSliceLen);

    return MTDR_RET_EOK;
}

/**
 * Process received message.
 * Calls callbacks (if assigned)
 * Calls decoding and clear of reported information element
 */
static inline uint32_t MTDR_ProcessReceivedMessage(MTDR_Instance *instance, MTDR_Message *message)
{

    switch (message->messageCode) {
    case MTDR_MESSAGE_CODE_REPORT_PRESENT:         //Payload: 0 - Device Class code, 1 - Device Class Version
        if (instance->messageCallbacks.onMsgReportPresent != NULL)
            instance->messageCallbacks.onMsgReportPresent((void *) instance, message->sourceAddress, message->payload[0], message->payload[1]);

        if (instance->enumerateDevices)
            return MTDR_AssignLogicalAddress(instance, message);
        break;

    case MTDR_MESSAGE_CODE_REPORT_ABSENT:          //Payload: None
        if (instance->messageCallbacks.onMsgReportAbsent != NULL)
            instance->messageCallbacks.onMsgReportAbsent((void *) instance, message->sourceAddress);
        break;

    case MTDR_MESSAGE_CODE_REPLY_INFORMATION:      //Payload: 0 - Transaction ID, 1 => 16 - Information Slice
        if (instance->messageCallbacks.onMsgReplyInformation != NULL)
            instance->messageCallbacks.onMsgReplyInformation((void *) instance, message->sourceAddress, message->payload[0], &(message->payload[MTDR_TRANSACTION_ID_LENGTH]), message->payloadLength - MTDR_TRANSACTION_ID_LENGTH);
        break;

    case MTDR_MESSAGE_CODE_REPORT_INFORMATION:     //Payload: 0 - Element Code [7:0], 1 - Element Code [15:8] 2 => 17 - Information Slice
        if (instance->messageCallbacks.onMsgReportInformation != NULL)
            instance->messageCallbacks.onMsgReportInformation((void *) instance, message->sourceAddress, message->payload[0] | (message->payload[1] << 8), &(message->payload[MTDR_ELEMENT_CODE_LENGTH]), message->payloadLength - MTDR_ELEMENT_CODE_LENGTH);
        MTDR_CheckInformationElement(instance, message);
        break;

    case MTDR_MESSAGE_CODE_REPLY_VALUE:            //Payload: 0 - Transaction ID, 1 => 16 - Value Slice
        if (instance->messageCallbacks.onMsgReplyValue != NULL)
            instance->messageCallbacks.onMsgReplyValue((void *) instance, message->sourceAddress, message->payload[0], &(message->payload[MTDR_TRANSACTION_ID_LENGTH]), message->payloadLength - MTDR_TRANSACTION_ID_LENGTH);
        break;

    default:
        return MTDR_RET_EINVAL;

    }
    return MTDR_RET_EOK;
}


/**
 * Receive all SLIMbus messages from RX_FIFO
 */
static inline uint32_t MTDR_ReceiveMessages(MTDR_Instance *instance)
{
    uint8_t rxFifoData[MTDR_RX_FIFO_MSG_MAX_SIZE];
    uint8_t fifoSize = 0;
    uint32_t reg = 0;
    uint8_t errors = 0;
    MTDR_Message rxMsg;

    for (;;) {
        reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
        if (COMMAND_STATUS__STATE__RX_PULL__READ(reg))
            continue;

        if (COMMAND_STATUS__STATE__RX_NOTEMPTY__READ(reg) == 0)
            return MTDR_RET_EOK;

        fifoSize = MTDR_FifoReceive(instance, rxFifoData, MTDR_RX_FIFO_MSG_MAX_SIZE);
        if ((fifoSize > 0) && (fifoSize <= MTDR_MESSAGE_MAX_LENGTH)) {
            if (instance->basicCallbacks.onRawMessageReceived != NULL)
                instance->basicCallbacks.onRawMessageReceived((void *) instance, (void *) rxFifoData, fifoSize);

#if DEBUG_CHECK_DATA
            {
                int i = 0;
                uint8_t *p = rxFifoData;
                PR_INFO(LOGTAG"[RX DATA](%d)BEGIN: [", fifoSize);
                for(i = 0; i < fifoSize; i++)
                    PR_INFO("%x ", *p++);
                PR_INFO("]END\n");
            }
#endif

            if (MTDR_DecodeMessage(instance, rxFifoData, fifoSize, &rxMsg) != MTDR_RET_EOK)
                errors++;

#if DEBUG_CHECK_DATA
            {
                int i = 0;
                uint8_t *p = rxMsg.payload;

                PR_INFO(LOGTAG"RX_FIFO message BEGIN: {\n");
                if(rxMsg.arbitrationType == MTDR_AT_SHORT)
                    PR_INFO("srcAddr: %x\n", (uint8_t)(rxMsg.sourceAddress & 0xff));
                else if(rxMsg.arbitrationType == MTDR_AT_LONG)
                    PR_INFO("srcAddr: %4x %8x\n", (uint16_t)((rxMsg.sourceAddress >> 32) & 0xffff), (uint32_t)((rxMsg.sourceAddress) & 0xffffffff));

                if(rxMsg.destinationType == MTDR_DT_LOGICAL_ADDRESS)
                    PR_INFO("dstAddr: %x\n", (uint8_t)(rxMsg.destinationAddress & 0xff));
                else if(rxMsg.destinationType == MTDR_DT_ENUMERATION_ADDRESS)
                    PR_INFO("dstAddr: %4x %8x\n", (uint16_t)((rxMsg.destinationAddress >> 32) & 0xffff), (uint32_t)((rxMsg.destinationAddress) & 0xffffffff));

                PR_INFO("MT: %x\n", rxMsg.messageType);
                PR_INFO("MC: %x\n", rxMsg.messageCode);

                PR_INFO("PD: ");
                for(i = 0; i < rxMsg.payloadLength; i++) {
                    PR_INFO("%x ", *p++);
                }
                PR_INFO("}END\n");
            }
#endif

            if (instance->basicCallbacks.onMessageReceived != NULL)
                instance->basicCallbacks.onMessageReceived((void *) instance, &rxMsg);

            errors += (MTDR_ProcessReceivedMessage(instance, &rxMsg) != MTDR_RET_EOK);
        }
    }
    return errors ? MTDR_RET_EIO : MTDR_RET_EOK;
}

/**
 * Receive all SLIMbus messages from RX_FIFO
 */
uint32_t m_MTDR_Init(void)
{
    MTDR_Config *config = &mtdiConfig;
    MTDR_Instance *instance;
    uint32_t reg;

    pDrvIns = kzalloc(sizeof(MTDR_Instance), GFP_KERNEL);
    if (!pDrvIns)
        return MTDR_RET_ENOMEM;

    if (config->eaInterfaceId == config->eaGenericId)
        return MTDR_RET_EINVAL;
    if (config->eaInterfaceId == config->eaFramerId)
        return MTDR_RET_EINVAL;
    if (config->eaGenericId == config->eaFramerId)
        return MTDR_RET_EINVAL;

    instance = (MTDR_Instance *) pDrvIns;
    instance->registerBase = config->regBase;
    instance->registers = (MTDR_Registers *) config->regBase;

    /* Exit if Manager component is already enabled */
    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    if (CONFIGURATION__CONFIG_MODE__ENABLE__READ(reg))
        return MTDR_RET_EINPROGRESS;

    /* CONFIG_MODE - Configuration Mode */
    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);

    CONFIGURATION__CONFIG_MODE__SNIFFER_MODE__MODIFY(reg, config->snifferMode);
    CONFIGURATION__CONFIG_MODE__MANAGER_MODE__SET(reg);

    CONFIGURATION__CONFIG_MODE__FR_EN__MODIFY(reg, config->enableFramer);
    CONFIGURATION__CONFIG_MODE__DEV_EN__MODIFY(reg, config->enableDevice);

    CONFIGURATION__CONFIG_MODE__RETRY_LMT__MODIFY(reg, config->retryLimit);
    CONFIGURATION__CONFIG_MODE__REPORT_AT_EVENT__MODIFY(reg, config->reportAtEvent);

    instance->disableHardwareCrcCalculation = config->disableHardwareCrcCalculation;
    CONFIGURATION__CONFIG_MODE__CRC_CALC_DISABLE__MODIFY(reg, config->disableHardwareCrcCalculation);

    CONFIGURATION__CONFIG_MODE__LMTD_REPORT__MODIFY(reg, config->limitReports);
    CONFIGURATION__CONFIG_MODE__RECONF_TX_DIS__CLR(reg);

    MTDR_WriteReg(CONFIGURATION.CONFIG_MODE, reg);

    /* CONFIG_EA - Enumeration Address Part 1 */
    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_EA);
    CONFIGURATION__CONFIG_EA__PRODUCT_ID__MODIFY(reg, config->eaProductId);
    CONFIGURATION__CONFIG_EA__INSTANCE_VAL__MODIFY(reg, config->eaInstanceValue);
    MTDR_WriteReg(CONFIGURATION.CONFIG_EA, reg);

    /* CONFIG_EA2 - Enumeration Address Part 2 */
    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_EA2);
    CONFIGURATION__CONFIG_EA2__DEVICE_ID_1__MODIFY(reg, config->eaInterfaceId);
    CONFIGURATION__CONFIG_EA2__DEVICE_ID_2__MODIFY(reg, config->eaGenericId);
    CONFIGURATION__CONFIG_EA2__DEVICE_ID_3__MODIFY(reg, config->eaFramerId);
    MTDR_WriteReg(CONFIGURATION.CONFIG_EA2, reg);


    /* INT_EN  - Interrupts Enable */
    reg = MTDR_ReadReg(INTERRUPTS.INT_EN);
    INTERRUPTS__INT_EN__RX_INT_EN__SET(reg);        //Enable interrupt for receiving messages
    //if (callbacks->onMessageSendingFailed != NULL)
    INTERRUPTS__INT_EN__TX_ERR_EN__SET(reg);        //Enable interrupt for sending messages error
    //else
    //    INTERRUPTS__INT_EN__TX_ERR_EN__CLR(reg);

    //if (callbacks->onMessagesSendingFinished != NULL)
    INTERRUPTS__INT_EN__TX_INT_EN__SET(reg);        //Enable interrupt for sending messages
    //else
    //    INTERRUPTS__INT_EN__TX_INT_EN__CLR(reg);

    /*enable rcfg int for rcfg done, */
    //INTERRUPTS__INT_EN__RCFG_INT_EN__SET(reg);

    MTDR_WriteReg(INTERRUPTS.INT_EN, reg);

    instance->enumerateDevices = config->enumerateDevices;

    MTDR_ClearMemory((void *) & (instance->basicCallbacks), sizeof(MTDR_Callbacks));
    MTDR_ClearMemory((void *) & (instance->messageCallbacks), sizeof(MTDR_MessageCallbacks));

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_Start(void)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t timeout = 0;

    instance = (MTDR_Instance *) pDrvIns;

    PR_INFO(LOGTAG"m_MTDR_Start\n");

    /* Exit if Manager component is already enabled */
    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    if (CONFIGURATION__CONFIG_MODE__ENABLE__READ(reg))
        return MTDR_RET_EINPROGRESS;

    PR_INFO(LOGTAG"Enable component\n");

    /* Enable component */
    CONFIGURATION__CONFIG_MODE__ENABLE__SET(reg);
    MTDR_WriteReg(CONFIGURATION.CONFIG_MODE, reg);

    /* Enable all interrupts */
    reg = MTDR_ReadReg(INTERRUPTS.INT_EN);
    INTERRUPTS__INT_EN__INT_EN__SET(reg);
    MTDR_WriteReg(INTERRUPTS.INT_EN, reg);
    PR_INFO(LOGTAG"Enable interrupts, INT_EN: %x\n", readl(SBRegBase + SB_INT_EN));

    MTDR_CfgStrobeSet(instance, 1);
    //MTDR_CfgStrobeCheck(instance);

    PR_INFO(LOGTAG"Wait for synchronization with SLIMbus\n");
    PR_INFO(LOGTAG"CONFIG_MODE: %x\n", readl(SBRegBase + SB_CONFIG_MODE));

    /* Wait for synchronization with SLIMbus */
    do {
        reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
        if (++timeout == 0)
            return MTDR_RET_EIO;

#if DEBUG_CHECK_INFO
        PR_INFO(LOGTAG"COMMAND_STATUS.STATE: %x\n\n", readl(SBRegBase + SB_STATE));
#endif
/*
#if DEBUG_ONLY
        //break;
#endif
*/
        mdelay(1);

    } while ( (COMMAND_STATUS__STATE__F_SYNC__READ(reg) == 0)
              || (COMMAND_STATUS__STATE__SF_SYNC__READ(reg) == 0)
              || (COMMAND_STATUS__STATE__M_SYNC__READ(reg) == 0)) ;

    pr_info(LOGTAG"sync ok!\n");
    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_Stop(void *pD)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_StopSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    if (MTDR_CfgStrobeCheck(instance) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    /* Disable all interrupts */
    reg = MTDR_ReadReg(INTERRUPTS.INT_EN);
    INTERRUPTS__INT_EN__INT_EN__CLR(reg);
    MTDR_WriteReg(INTERRUPTS.INT_EN, reg);

    /* Disable component */
    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    CONFIGURATION__CONFIG_MODE__ENABLE__CLR(reg);
    MTDR_WriteReg(CONFIGURATION.CONFIG_MODE, reg);

    MTDR_CfgStrobeSet(instance, 1);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_Destroy(void *pD)
{
    uint32_t result = MTDR_DestroySF(pD);
    if (result)
        return result;
    return MTDR_Stop(pD);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetInterrupts(void *pD, uint8_t interruptMask)
{
    MTDR_Instance *instance;
    uint32_t reg = 0;
    uint32_t result = MTDR_SetInterruptsSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = interruptMask;

    /* Make sure that mandatory interrupts are enabled */
    INTERRUPTS__INT_EN__INT_EN__SET(reg);       //Enable interrupts
    INTERRUPTS__INT_EN__RX_INT_EN__SET(reg);    //Interrupt for receiving messages
    if (instance->basicCallbacks.onMessageSendingFailed != NULL)
        INTERRUPTS__INT_EN__TX_ERR_EN__SET(reg);    //Interrupt for sending message error
    if (instance->basicCallbacks.onMessagesSendingFinished != NULL)
        INTERRUPTS__INT_EN__TX_INT_EN__SET(reg);    //Interrupt for sending messages finished

    MTDR_WriteReg(INTERRUPTS.INT_EN, reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetInterrupts(void *pD, uint8_t *interruptMask)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetInterruptsSF(pD, interruptMask);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(INTERRUPTS.INT_EN);
    *interruptMask = reg;

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetDataPortInterrupts(void *pD, uint8_t portNumber, uint8_t interruptMask)
{
    MTDR_Instance *instance;
    uint8_t portAddress;
    uint32_t reg;
    uint32_t result = MTDR_SetDataPortInterruptsSF(pD, portNumber, interruptMask);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    portAddress = portNumber / 4;
    //There are 4 Port registers per 1 32 bit P_INT_EN register

    reg = MTDR_ReadReg(PORT_INTERRUPTS.P_INT_EN[portAddress]);

    switch (portNumber % 4) {
    case 0:
        PORT_INTERRUPTS__P_INT_EN__P0_ACT_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_ACT) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P0_CON_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CON) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P0_CHAN_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CHAN) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P0_DMA_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_DMA) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P0_OVF_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_OVF) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P0_UND_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_UND) != 0 );
        break;
    case 1:
        PORT_INTERRUPTS__P_INT_EN__P1_ACT_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_ACT) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P1_CON_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CON) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P1_CHAN_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CHAN) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P1_DMA_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_DMA) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P1_OVF_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_OVF) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P1_UND_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_UND) != 0 );
        break;
    case 2:
        PORT_INTERRUPTS__P_INT_EN__P2_ACT_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_ACT) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P2_CON_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CON) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P2_CHAN_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CHAN) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P2_DMA_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_DMA) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P2_OVF_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_OVF) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P2_UND_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_UND) != 0 );
        break;
    case 3:
        PORT_INTERRUPTS__P_INT_EN__P3_ACT_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_ACT) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P3_CON_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CON) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P3_CHAN_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_CHAN) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P3_DMA_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_DMA) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P3_OVF_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_OVF) != 0 );
        PORT_INTERRUPTS__P_INT_EN__P3_UND_INT_EN__MODIFY(reg, (interruptMask & MTDR_DP_INT_UND) != 0 );
        break;
    }
    MTDR_WriteReg(PORT_INTERRUPTS.P_INT_EN[portAddress], reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_SetDataPortInterrupts(uint8_t portNumber, uint8_t interruptMask)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_SetDataPortInterrupts(instance, portNumber, interruptMask);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetDataPortInterrupts(void *pD, uint8_t portNumber, uint8_t *interruptMask)
{
    MTDR_Instance *instance;
    uint8_t portAddress;
    uint32_t reg;
    MTDR_DataPortInterrupt output = 0;
    uint32_t result = MTDR_GetDataPortInterruptsSF(pD, portNumber, interruptMask);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    portAddress = portNumber / 4;
    //There are 4 Port registers per 1 32 bit P_INT_EN register
    reg = MTDR_ReadReg(PORT_INTERRUPTS.P_INT_EN[portAddress]);

    switch (portNumber % 4) {
    case 0:
        output |=  (PORT_INTERRUPTS__P_INT_EN__P0_ACT_INT_EN__READ(reg)) ? MTDR_DP_INT_ACT : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P0_CON_INT_EN__READ(reg)) ? MTDR_DP_INT_CON : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P0_CHAN_INT_EN__READ(reg)) ? MTDR_DP_INT_CHAN : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P0_DMA_INT_EN__READ(reg)) ? MTDR_DP_INT_DMA : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P0_OVF_INT_EN__READ(reg)) ? MTDR_DP_INT_OVF : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P0_UND_INT_EN__READ(reg)) ? MTDR_DP_INT_UND : 0;
        break;
    case 1:
        output |=  (PORT_INTERRUPTS__P_INT_EN__P1_ACT_INT_EN__READ(reg)) ? MTDR_DP_INT_ACT : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P1_CON_INT_EN__READ(reg)) ? MTDR_DP_INT_CON : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P1_CHAN_INT_EN__READ(reg)) ? MTDR_DP_INT_CHAN : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P1_DMA_INT_EN__READ(reg)) ? MTDR_DP_INT_DMA : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P1_OVF_INT_EN__READ(reg)) ? MTDR_DP_INT_OVF : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P1_UND_INT_EN__READ(reg)) ? MTDR_DP_INT_UND : 0;
        break;
    case 2:
        output |=  (PORT_INTERRUPTS__P_INT_EN__P2_ACT_INT_EN__READ(reg)) ? MTDR_DP_INT_ACT : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P2_CON_INT_EN__READ(reg)) ? MTDR_DP_INT_CON : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P2_CHAN_INT_EN__READ(reg)) ? MTDR_DP_INT_CHAN : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P2_DMA_INT_EN__READ(reg)) ? MTDR_DP_INT_DMA : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P2_OVF_INT_EN__READ(reg)) ? MTDR_DP_INT_OVF : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P2_UND_INT_EN__READ(reg)) ? MTDR_DP_INT_UND : 0;
        break;
    case 3:
        output |=  (PORT_INTERRUPTS__P_INT_EN__P3_ACT_INT_EN__READ(reg)) ? MTDR_DP_INT_ACT : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P3_CON_INT_EN__READ(reg)) ? MTDR_DP_INT_CON : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P3_CHAN_INT_EN__READ(reg)) ? MTDR_DP_INT_CHAN : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P3_DMA_INT_EN__READ(reg)) ? MTDR_DP_INT_DMA : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P3_OVF_INT_EN__READ(reg)) ? MTDR_DP_INT_OVF : 0;
        output |=  (PORT_INTERRUPTS__P_INT_EN__P3_UND_INT_EN__READ(reg)) ? MTDR_DP_INT_UND : 0;
        break;
    }
    *interruptMask = output;

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_GetDataPortInterrupts(uint8_t portNumber, uint8_t *interruptMask)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_GetDataPortInterrupts(instance, portNumber, interruptMask);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_ClearDataPortFifo(void *pD, uint8_t portNumber)
{
    MTDR_Instance *instance;
    uint8_t portAddress;
    uint32_t reg;
    uint32_t result = MTDR_ClearDataPortFifoSF(pD, portNumber);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    portAddress = portNumber / 4;
    //There are 4 Port registers per 1 32 bit P_INT_EN register

    reg = MTDR_ReadReg(PORT_INTERRUPTS.P_INT_EN[portAddress]);

    switch (portNumber % 4) {
    case 0:
        PORT_INTERRUPTS__P_INT_EN__P0_FIFO_CLR__SET(reg);
        break;
    case 1:
        PORT_INTERRUPTS__P_INT_EN__P1_FIFO_CLR__SET(reg);
        break;
    case 2:
        PORT_INTERRUPTS__P_INT_EN__P2_FIFO_CLR__SET(reg);
        break;
    case 3:
        PORT_INTERRUPTS__P_INT_EN__P3_FIFO_CLR__SET(reg);
        break;
    }
    MTDR_WriteReg(PORT_INTERRUPTS.P_INT_EN[portAddress], reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_ClearDataPortFifo(uint8_t portNumber)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_ClearDataPortFifo(instance, portNumber);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetPresenceRateGeneration(void *pD, uint8_t portNumber, bool enable)
{
    MTDR_Instance *instance;
    uint8_t portAddress;
    uint32_t reg;
    uint32_t result = MTDR_SetPresenceRateGeneratiSF(pD, portNumber);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    portAddress = portNumber / 4;
    //There are 4 Port registers per 1 32 bit P_INT_EN register

    reg = MTDR_ReadReg(PORT_INTERRUPTS.P_INT_EN[portAddress]);

    switch (portNumber % 4) {
    case 0:
        PORT_INTERRUPTS__P_INT_EN__P0_PR_GEN_EN__MODIFY(reg, enable);
        break;
    case 1:
        PORT_INTERRUPTS__P_INT_EN__P1_PR_GEN_EN__MODIFY(reg, enable);
        break;
    case 2:
        PORT_INTERRUPTS__P_INT_EN__P2_PR_GEN_EN__MODIFY(reg, enable);
        break;
    case 3:
        PORT_INTERRUPTS__P_INT_EN__P3_PR_GEN_EN__MODIFY(reg, enable);
        break;
    }
    MTDR_WriteReg(PORT_INTERRUPTS.P_INT_EN[portAddress], reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_SetPresenceRateGeneration(uint8_t portNumber, bool enable)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_SetPresenceRateGeneration(instance, portNumber, enable);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetPresenceRateGeneration(void *pD, uint8_t portNumber, bool *enable)
{
    MTDR_Instance *instance;
    uint8_t portAddress;
    uint32_t reg;
    uint32_t result = MTDR_GetPresenceRateGeneratiSF(pD, portNumber, enable);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    portAddress = portNumber / 4;
    reg = MTDR_ReadReg(PORT_INTERRUPTS.P_INT_EN[portAddress]);

    switch (portNumber % 4) {
    case 0:
        *enable = PORT_INTERRUPTS__P_INT_EN__P0_PR_GEN_EN__READ(reg);
        break;
    case 1:
        *enable = PORT_INTERRUPTS__P_INT_EN__P1_PR_GEN_EN__READ(reg);
        break;
    case 2:
        *enable = PORT_INTERRUPTS__P_INT_EN__P2_PR_GEN_EN__READ(reg);
        break;
    case 3:
        *enable = PORT_INTERRUPTS__P_INT_EN__P3_PR_GEN_EN__READ(reg);
        break;
    }

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_GetPresenceRateGeneration(void *pD, uint8_t portNumber, bool *enable)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_GetPresenceRateGeneration(instance, portNumber, enable);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_AssignMessageCallbacks(void *pD, MTDR_MessageCallbacks *msgCallbacks)
{
    MTDR_Instance *instance;
    uint32_t result = MTDR_AssignMessageCallbacksSF(pD, msgCallbacks);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    MTDR_BufferCopy((uint8_t *) & (instance->messageCallbacks), (uint8_t *) msgCallbacks, sizeof(MTDR_MessageCallbacks));

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SendRawMessage(void *pD, void *message, uint8_t messageLength)
{
    MTDR_Instance *instance;
    uint32_t result = MTDR_SendRawMessageSF(pD, message, messageLength);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    return MTDR_FifoTransmit(instance, (uint8_t *) message, messageLength);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SendMessage(void *pD, MTDR_Message *message)
{
    MTDR_Instance *instance;
    uint32_t result = MTDR_SendMessageSF(pD, message);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    return MTDR_TransmitMessage(instance, message);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetRegisterValue(void *pD, uint16_t regAddress, uint32_t *regContent)
{
    MTDR_Instance *instance;
    uint32_t result = MTDR_GetRegisterValueSF(pD, regContent);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    if (regAddress & 0x3) //Register address must be aligned to 32-bits
        return MTDR_RET_EINVAL;

    *regContent = MTDR_UncachedRead32((uint32_t *) instance->registerBase + (regAddress >> 2) );

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetRegisterValue(void *pD, uint16_t regAddress, uint32_t regContent)
{
    MTDR_Instance *instance;
    uint32_t result = MTDR_SetRegisterValueSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    if (regAddress & 0x3) //Register address must be aligned to 32-bits
        return MTDR_RET_EINVAL;

    MTDR_UncachedWrite32((uint32_t *) instance->registerBase + (regAddress >> 2), regContent);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetMessageChannelLapse(void *pD, uint8_t mchLapse)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_SetMessageChannelLapseSF(pD, mchLapse);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.MCH_USAGE);
    COMMAND_STATUS__MCH_USAGE__MCH_LAPSE__MODIFY(reg, mchLapse);
    MTDR_WriteReg(COMMAND_STATUS.MCH_USAGE, reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetMessageChannelLapse(void *pD, uint8_t *mchLapse)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetMessageChannelLapseSF(pD, mchLapse);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.MCH_USAGE);
    *mchLapse = COMMAND_STATUS__MCH_USAGE__MCH_LAPSE__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetMessageChannelUsage(void *pD, uint16_t *mchUsage)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetMessageChannelUsageSF(pD, mchUsage);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.MCH_USAGE);
    *mchUsage = COMMAND_STATUS__MCH_USAGE__MCH_USAGE__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetMessageChannelCapacity(void *pD, uint16_t *mchCapacity)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetMessageChannelCapaciSF(pD, mchCapacity);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.MCH_USAGE);
    *mchCapacity = COMMAND_STATUS__MCH_USAGE__MCH_CAPACITY__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetSnifferMode(void *pD, bool state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_SetSnifferModeSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    if (MTDR_CfgStrobeCheck(instance) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    CONFIGURATION__CONFIG_MODE__SNIFFER_MODE__MODIFY(reg, state);
    MTDR_WriteReg(CONFIGURATION.CONFIG_MODE, reg);

    MTDR_CfgStrobeSet(instance, 0);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetSnifferMode(void *pD, bool *state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetSnifferModeSF(pD, state);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    *state = CONFIGURATION__CONFIG_MODE__SNIFFER_MODE__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetFramerEnabled(void *pD, bool state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_SetFramerEnabledSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    if (MTDR_CfgStrobeCheck(instance) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    CONFIGURATION__CONFIG_MODE__FR_EN__MODIFY(reg, state);
    MTDR_WriteReg(CONFIGURATION.CONFIG_MODE, reg);

    MTDR_CfgStrobeSet(instance, 0);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetFramerEnabled(void *pD, bool *state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetFramerEnabledSF(pD, state);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    *state = CONFIGURATION__CONFIG_MODE__FR_EN__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetDeviceEnabled(void *pD, bool state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_SetDeviceEnabledSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    if (MTDR_CfgStrobeCheck(instance) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    CONFIGURATION__CONFIG_MODE__DEV_EN__MODIFY(reg, state);
    MTDR_WriteReg(CONFIGURATION.CONFIG_MODE, reg);

    MTDR_CfgStrobeSet(instance, 0);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetDeviceEnabled(void *pD, bool *state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetDeviceEnabledSF(pD, state);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    *state = CONFIGURATION__CONFIG_MODE__DEV_EN__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetGoAbsent(void *pD, bool state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_SetGoAbsentSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    if (MTDR_CfgStrobeCheck(instance) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    CONFIGURATION__CONFIG_MODE__GO_ABSENT__MODIFY(reg, state);
    MTDR_WriteReg(CONFIGURATION.CONFIG_MODE, reg);

    MTDR_CfgStrobeSet(instance, 0);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetGoAbsent(void *pD, bool *state)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetGoAbsentSF(pD, state);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_MODE);
    *state = CONFIGURATION__CONFIG_MODE__GO_ABSENT__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetFramerConfig(void *pD, MTDR_FramerConfig *framerConfig)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_SetFramerConfigSF(pD, framerConfig);
    if (result)
        return result;

    if ((framerConfig->quality != MTDR_FQ_IRREGULAR) && (framerConfig->quality != MTDR_FQ_LOW_JITTER) && (framerConfig->quality != MTDR_FQ_PUNCTURED) && (framerConfig->quality != MTDR_FQ_REGULAR))
        return MTDR_RET_EINVAL;
    instance = (MTDR_Instance *) pD;

    if (MTDR_CfgStrobeCheck(instance) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_FR);
    CONFIGURATION__CONFIG_FR__PAUSE_AT_RFCHNG__MODIFY(reg, framerConfig->pauseAtRootFrequencyChange);
    CONFIGURATION__CONFIG_FR__QUALITY__MODIFY(reg, framerConfig->quality);
    CONFIGURATION__CONFIG_FR__RF_SUPP__MODIFY(reg, framerConfig->rootFrequenciesSupported);
    MTDR_WriteReg(CONFIGURATION.CONFIG_FR, reg);

    MTDR_CfgStrobeSet(instance, 0);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_SetFramerConfig(void)
{
    MTDR_FramerConfig *framerConfig = &mtdiFramerConfig;
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;

    return MTDR_SetFramerConfig(instance, framerConfig);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetFramerConfig(void *pD, MTDR_FramerConfig *framerConfig)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetFramerConfigSF(pD, framerConfig);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_FR);
    framerConfig->pauseAtRootFrequencyChange = CONFIGURATION__CONFIG_FR__PAUSE_AT_RFCHNG__READ(reg);
    framerConfig->quality = CONFIGURATION__CONFIG_FR__QUALITY__READ(reg);
    framerConfig->rootFrequenciesSupported = CONFIGURATION__CONFIG_FR__RF_SUPP__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_SetGenericDeviceConfig(void *pD, MTDR_GenericDeviceConfig *genericDeviceConfig)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint8_t temp;
    uint32_t result = MTDR_SetGenericDeviceConfigSF(pD, genericDeviceConfig);
    if (result)
        return result;

    /* Checking input structure parameters */
    /* Limit maximum values, based on register field length */
    if (genericDeviceConfig->presenceRatesSupported > 0xFFFFFF)
        return MTDR_RET_EINVAL;
    if (genericDeviceConfig->dataPortClockPrescaler > 0xF)
        return MTDR_RET_EINVAL;
    if (genericDeviceConfig->cportClockDivider > 0x7)
        return MTDR_RET_EINVAL;
    if ((genericDeviceConfig->referenceClockSelector != MTDR_RC_CLOCK_GEAR_6)
        && (genericDeviceConfig->referenceClockSelector != MTDR_RC_CLOCK_GEAR_7)
        && (genericDeviceConfig->referenceClockSelector != MTDR_RC_CLOCK_GEAR_8)
        && (genericDeviceConfig->referenceClockSelector != MTDR_RC_CLOCK_GEAR_9))
        return MTDR_RET_EINVAL;

    instance = (MTDR_Instance *) pD;

    if (MTDR_CfgStrobeCheck(instance) != MTDR_RET_EOK)
        return MTDR_RET_EIO;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_PR_TP);
    CONFIGURATION__CONFIG_PR_TP__PR_SUPP__MODIFY(reg, genericDeviceConfig->presenceRatesSupported);
    temp  = MTDR_TpField(ISOCHRONOUS, genericDeviceConfig->transportProtocolIsochronous);
    temp |= MTDR_TpField(PUSHED, genericDeviceConfig->transportProtocolPushed);
    temp |= MTDR_TpField(PULLED, genericDeviceConfig->transportProtocolPulled);
    CONFIGURATION__CONFIG_PR_TP__TP_SUPP__MODIFY(reg, temp);
    MTDR_WriteReg(CONFIGURATION.CONFIG_PR_TP, reg);

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_CPORT);
    CONFIGURATION__CONFIG_CPORT__CPORT_CLK_DIV__MODIFY(reg, genericDeviceConfig->cportClockDivider);
    MTDR_WriteReg(CONFIGURATION.CONFIG_CPORT, reg);

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_DPORT);
    CONFIGURATION__CONFIG_DPORT__SINK_START_LVL__MODIFY(reg, genericDeviceConfig->sinkStartLevel);
    CONFIGURATION__CONFIG_DPORT__DPORT_CLK_PRESC__MODIFY(reg, genericDeviceConfig->dataPortClockPrescaler);
    CONFIGURATION__CONFIG_DPORT__REFCLK_SEL__MODIFY(reg, genericDeviceConfig->referenceClockSelector);
    MTDR_WriteReg(CONFIGURATION.CONFIG_DPORT, reg);

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_THR);
    CONFIGURATION__CONFIG_THR__SRC_THR__MODIFY(reg, genericDeviceConfig->dmaTresholdSource);
    CONFIGURATION__CONFIG_THR__SINK_THR__MODIFY(reg, genericDeviceConfig->dmaTresholdSink);
    MTDR_WriteReg(CONFIGURATION.CONFIG_THR, reg);

    MTDR_CfgStrobeSet(instance, 0);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_SetGenericDeviceConfig(void)
{
    MTDR_GenericDeviceConfig *genericDeviceConfig = &mtdiDeviceConfig;
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;

    return MTDR_SetGenericDeviceConfig(instance, genericDeviceConfig);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetGenericDeviceConfig(void *pD, MTDR_GenericDeviceConfig *genericDeviceConfig)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint8_t temp;
    uint32_t result = MTDR_GetGenericDeviceConfigSF(pD, genericDeviceConfig);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_PR_TP);
    genericDeviceConfig->presenceRatesSupported = CONFIGURATION__CONFIG_PR_TP__PR_SUPP__READ(reg);
    temp = CONFIGURATION__CONFIG_PR_TP__TP_SUPP__READ(reg);
    genericDeviceConfig->transportProtocolIsochronous = MTDR_GetTpField(ISOCHRONOUS, temp);
    genericDeviceConfig->transportProtocolPushed = MTDR_GetTpField(PUSHED, temp);
    genericDeviceConfig->transportProtocolPulled = MTDR_GetTpField(PULLED, temp);

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_CPORT);
    genericDeviceConfig->cportClockDivider = CONFIGURATION__CONFIG_CPORT__CPORT_CLK_DIV__READ(reg);

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_DPORT);
    genericDeviceConfig->sinkStartLevel = CONFIGURATION__CONFIG_DPORT__SINK_START_LVL__READ(reg);
    genericDeviceConfig->dataPortClockPrescaler = CONFIGURATION__CONFIG_DPORT__DPORT_CLK_PRESC__READ(reg);
    genericDeviceConfig->referenceClockSelector = CONFIGURATION__CONFIG_DPORT__REFCLK_SEL__READ(reg);

    reg = MTDR_ReadReg(CONFIGURATION.CONFIG_THR);
    genericDeviceConfig->dmaTresholdSource = CONFIGURATION__CONFIG_THR__SRC_THR__READ(reg);
    genericDeviceConfig->dmaTresholdSink = CONFIGURATION__CONFIG_THR__SINK_THR__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_GetGenericDeviceConfig(MTDR_GenericDeviceConfig *genericDeviceConfig)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_GetGenericDeviceConfig(instance, genericDeviceConfig);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetDataPortStatus(void *pD, uint8_t portNumber, MTDR_DataPortStatus *portStatus)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetDataPortStatusSF(pD, portNumber, portStatus);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(PORT_STATE[portNumber].P_STATE_0);
    portStatus->active = PORT_STATE__P_STATE_0__ACTIVE__READ(reg);
    portStatus->contentDefined = PORT_STATE__P_STATE_0__CONTENT_DEFINED__READ(reg);
    portStatus->channelDefined = PORT_STATE__P_STATE_0__CHANNEL_DEFINED__READ(reg);
    portStatus->sink = PORT_STATE__P_STATE_0__SINK__READ(reg);
    portStatus->overflow = PORT_STATE__P_STATE_0__OVF__READ(reg);
    portStatus->underrun = PORT_STATE__P_STATE_0__UND__READ(reg);
    portStatus->dportReady = PORT_STATE__P_STATE_0__DPORT_READY__READ(reg);
    portStatus->segmentInterval = PORT_STATE__P_STATE_0__S_INTERVAL__READ(reg);
    portStatus->transportProtocol = PORT_STATE__P_STATE_0__TR_PROTOCOL__READ(reg);

    reg = MTDR_ReadReg(PORT_STATE[portNumber].P_STATE_1);
    portStatus->presenceRate = PORT_STATE__P_STATE_1__P_RATE__READ(reg);
    portStatus->frequencyLock = PORT_STATE__P_STATE_1__FR_LOCK__READ(reg);
    portStatus->dataType = PORT_STATE__P_STATE_1__DATA_TYPE__READ(reg);
    portStatus->dataLength = PORT_STATE__P_STATE_1__DATA_LENGTH__READ(reg);
    portStatus->portLinked = PORT_STATE__P_STATE_1__PORT_LINKED__READ(reg);
    portStatus->channelLink = PORT_STATE__P_STATE_1__CH_LINK__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_GetDataPortStatus(uint8_t portNumber, MTDR_DataPortStatus *portStatus)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_GetDataPortStatus(instance, portNumber, portStatus);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_Unfreeze(void *pD)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_UnfreezeSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;
    reg = MTDR_ReadReg(COMMAND_STATUS.COMMAND);
    COMMAND_STATUS__COMMAND__UNFREEZE__SET(reg);
    MTDR_WriteReg(COMMAND_STATUS.COMMAND, reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_CancelConfiguration(void *pD)
{
    MTDR_Instance *instance;
    uint32_t reg = 0;
    uint32_t result = MTDR_CancelConfigurationSF(pD);
    if (result)
        return result;

    instance = (MTDR_Instance *) pD;
    reg = MTDR_ReadReg(COMMAND_STATUS.COMMAND);
    COMMAND_STATUS__COMMAND__CFG_STROBE__CLR(reg);
    COMMAND_STATUS__COMMAND__CFG_STROBE_CLR__SET(reg);
    MTDR_WriteReg(COMMAND_STATUS.COMMAND, reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetStatusSynchronization(void *pD, bool *fSync, bool *sfSync, bool *mSync, bool *sfbSync, bool *phSync)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetStatusSynchronizatioSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
    if (fSync != NULL)
        *fSync = COMMAND_STATUS__STATE__F_SYNC__READ(reg);
    if (sfSync != NULL)
        *sfSync = COMMAND_STATUS__STATE__SF_SYNC__READ(reg);
    if (mSync != NULL)
        *mSync = COMMAND_STATUS__STATE__M_SYNC__READ(reg);
    if (sfbSync != NULL)
        *sfbSync = COMMAND_STATUS__STATE__SFB_SYNC__READ(reg);
    if (phSync != NULL)
        *phSync = COMMAND_STATUS__STATE__PH_SYNC__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetStatusDetached(void *pD, bool *detached)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetStatusDetachedSF(pD, detached);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
    *detached = COMMAND_STATUS__STATE__DETACHED__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetStatusSlimbus(void *pD, MTDR_SubframeMode *subframeMode, MTDR_ClockGear *clockGear, MTDR_RootFrequency *rootFr)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetStatusSlimbusSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
    if (subframeMode != NULL)
        *subframeMode = COMMAND_STATUS__STATE__SUBFRAME_MODE__READ(reg);
    if (clockGear != NULL)
        *clockGear = COMMAND_STATUS__STATE__CLOCK_GEAR__READ(reg);
    if (rootFr != NULL)
        *rootFr = COMMAND_STATUS__STATE__ROOT_FR__READ(reg);

    return MTDR_RET_EOK;
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_GetStatusSlimbus(MTDR_SubframeMode *subframeMode, MTDR_ClockGear *clockGear, MTDR_RootFrequency *rootFr)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_GetStatusSlimbus(pD, subframeMode, clockGear, rootFr);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_GetStatusMessages(void *pD, bool *sendingFinished)
{
    MTDR_Instance *instance;
    uint32_t reg;
    uint32_t result = MTDR_GetStatusMessagesSF(pD, sendingFinished);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    reg = MTDR_ReadReg(COMMAND_STATUS.STATE);
    if ((COMMAND_STATUS__STATE__TX_PUSH__READ(reg)) || (COMMAND_STATUS__STATE__TX_NOTEMPTY__READ(reg)))
        *sendingFinished = 0;
    else
        *sendingFinished = 1;

    return MTDR_RET_EOK;

}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_GetStatusMessages(bool *sendingFinished)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_GetStatusMessages(pD, sendingFinished);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgAssignLogicalAddress(void *pD, uint64_t destinationEa, uint8_t newLa)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgAssignLogicalAddressSF(pD, newLa);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_ASSIGN_LOGICAL_ADDRESS;
    txMsg.destinationType = MTDR_DT_ENUMERATION_ADDRESS;
    txMsg.destinationAddress = destinationEa;

    /*
     * Payload:
     * 0    LA [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(LOGICAL_ADDRESS, newLa);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgAssignLogicalAddress(uint64_t destinationEa, uint8_t newLa)
{
    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgAssignLogicalAddress(instance, destinationEa, newLa);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgResetDevice(void *pD, uint8_t destinationLa)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgResetDeviceSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_RESET_DEVICE;
    txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
    txMsg.destinationAddress = destinationLa;

    /*
     * Payload:
     * None
     */
    txMsg.payloadLength = 0;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgChangeLogicalAddress(void *pD, uint8_t destinationLa, uint8_t newLa)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgChangeLogicalAddressSF(pD, newLa);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_CHANGE_LOGICAL_ADDRESS;
    txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
    txMsg.destinationAddress = destinationLa;

    /*
     * Payload:
     * 0    LA [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(LOGICAL_ADDRESS, newLa);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgChangeArbitrationPriority(void *pD, bool broadcast, uint8_t destinationLa, MTDR_ArbitrationPriority newArbitrationPriority)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgChangeArbitrationPriSF(pD, newArbitrationPriority);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_CHANGE_ARBITRATION_PRIORITY;
    if (broadcast) {
        txMsg.destinationType = MTDR_DT_BROADCAST;
        txMsg.destinationAddress = 0x00;
    } else {
        txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
        txMsg.destinationAddress = destinationLa;
    }

    /*
     * Payload:
     * 0    AP [2:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(ARBITRATION_PRIORITY, newArbitrationPriority);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgRequestSelfAnnouncement(void *pD)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgRequestSelfAnnouncemSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_REQUEST_SELF_ANNOUNCEMENT;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * None
     */
    txMsg.payloadLength = 0;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgConnectSource(void *pD, uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgConnectSourceSF(pD, portNumber);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_CONNECT_SOURCE;
    txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
    txMsg.destinationAddress = destinationLa;

    /*
     * Payload:
     * 0    PN [5:0]
     * 1    CN [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(PORT_NUMBER, portNumber);
    txMsg.payload[1] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payloadLength = 2;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgConnectSource(uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    pr_info(LOGTAG"m_MTDR_MsgConnectSource(la: %x port: %d chn: %d)\n", destinationLa, portNumber, channelNumber);
    return MTDR_MsgConnectSource(pD, destinationLa, portNumber, channelNumber);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgConnectSink(void *pD, uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgConnectSinkSF(pD, portNumber);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_CONNECT_SINK;
    txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
    txMsg.destinationAddress = destinationLa;

    /*
     * Payload:
     * 0    PN [5:0]
     * 1    CN [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(PORT_NUMBER, portNumber);
    txMsg.payload[1] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payloadLength = 2;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgConnectSink(uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    pr_info(LOGTAG"m_MTDR_MsgConnectSink(la: %x port: %d chn: %d)\n", destinationLa, portNumber, channelNumber);
    return MTDR_MsgConnectSink(pD, destinationLa, portNumber, channelNumber);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgDisconnectPort(void *pD, uint8_t destinationLa, uint8_t portNumber)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgDisconnectPortSF(pD, portNumber);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_DISCONNECT_PORT;
    txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
    txMsg.destinationAddress = destinationLa;

    /*
     * Payload:
     * 0    PN [5:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(PORT_NUMBER, portNumber);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgDisconnectPort(uint8_t destinationLa, uint8_t portNumber)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    PR_INFO(LOGTAG"m_MTDR_MsgDisconnectPort(la: %x port: %d)\n", destinationLa, portNumber);
    return MTDR_MsgDisconnectPort(pD, destinationLa, portNumber);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgChangeContent(void *pD, uint8_t channelNumber, bool frequencyLockedBit, MTDR_PresenceRate presenceRate, MTDR_AuxFieldFormat auxiliaryBitFormat, MTDR_DataType dataType, bool channelLink, uint8_t dataLength)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgChangeContentSF(pD, channelNumber, presenceRate, auxiliaryBitFormat, dataType, dataLength);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_CHANGE_CONTENT;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    CN [7:0]
     * 1    FL [7:7]    PR [6:0]
     * 2    AF [7:4]    DT [3:0]
     * 3    CL [5:5]    DL [4:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payload[1] = MTDR_MsgPayloadField(PRESENCE_RATE, presenceRate) | MTDR_MsgPayloadField(FREQUENCY_LOCKED_BIT, frequencyLockedBit);
    txMsg.payload[2] = MTDR_MsgPayloadField(DATA_TYPE, dataType) | MTDR_MsgPayloadField(AUXILIARY_BIT_FORMAT, auxiliaryBitFormat);
    txMsg.payload[3] = MTDR_MsgPayloadField(DATA_LENGTH, dataLength) | MTDR_MsgPayloadField(CHANNEL_LINK, channelLink);
    txMsg.payloadLength = 4;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgRequestInformation(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgRequestInformationSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_REQUEST_INFORMATION;
    if (broadcast) {
        txMsg.destinationType = MTDR_DT_BROADCAST;
        txMsg.destinationAddress = 0x00;
    } else {
        txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
        txMsg.destinationAddress = destinationLa;
    }

    /*
     * Payload:
     * 0    TID [7:0]
     * 1    EC [ 7:0]
     * 2    EC [15:8]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(TRANSACTION_ID, transactionId);
    txMsg.payload[1] = MTDR_LowerByte(elementCode);
    txMsg.payload[2] = MTDR_HigherByte(elementCode);
    txMsg.payloadLength = 3;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgRequestInformation(             uint8_t deviceLa, uint8_t transactionId, uint16_t byteAddress, MTDR_SliceSize sliceSize)
{
    uint32_t ret;
    uint16_t elementCode;

    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;

    /*
     * Element Code
     * 16 bit;
     * [15: 4] element address
     * [ 3: 3] access type (1 - byte based, 0 - elemental)
     * [ 2: 0] value slice size
     */
    elementCode = (byteAddress << 4) | (1 << 3) | (sliceSize);  /*here is byte based*/

    ret = MTDR_MsgRequestInformation(instance, 0, deviceLa, transactionId, elementCode);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"requestInformation(%d) fail\n", byteAddress);
    }
    return ret;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgRequestClearInformation(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode, uint8_t *clearMask, uint8_t clearMaskSize)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint8_t i;
    uint32_t result = MTDR_MsgRequestClearInformatSF(pD);
    if (result)
        return result;

    if ((clearMask == NULL) && (clearMaskSize > 0))
        return MTDR_RET_EINVAL;

    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_REQUEST_CLEAR_INFORMATION;
    if (broadcast) {
        txMsg.destinationType = MTDR_DT_BROADCAST;
        txMsg.destinationAddress = 0x00;
    } else {
        txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
        txMsg.destinationAddress = destinationLa;
    }

    /*
     * Payload:
     * 0    TID [7:0]
     * 1    EC [ 7:0]
     * 2    EC [15:8]
     * 3    CM [ 7:0]
     * N+2  CM [8N-1: 8N-8]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(TRANSACTION_ID, transactionId);
    txMsg.payload[1] = MTDR_LowerByte(elementCode);
    txMsg.payload[2] = MTDR_HigherByte(elementCode);
    for (i = 0; i < clearMaskSize; i++)
        txMsg.payload[i + 3] = clearMask[i];
    txMsg.payloadLength = 3 + clearMaskSize;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgClearInformation(void *pD, bool broadcast, uint8_t destinationLa, uint16_t elementCode, uint8_t *clearMask, uint8_t clearMaskSize)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint8_t i;
    uint32_t result = MTDR_MsgClearInformationSF(pD);
    if (result)
        return result;

    if ((clearMask == NULL) && (clearMaskSize > 0))
        return MTDR_RET_EINVAL;

    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_CLEAR_INFORMATION;
    if (broadcast) {
        txMsg.destinationType = MTDR_DT_BROADCAST;
        txMsg.destinationAddress = 0x00;
    } else {
        txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
        txMsg.destinationAddress = destinationLa;
    }

    /*
     * Payload:
     * 0    EC [ 7:0]
     * 1    EC [15:8]
     * 2    CM [ 7:0]
     * N+1  CM [8N-1: 8N-8]
     */
    txMsg.payload[0] = MTDR_LowerByte(elementCode);
    txMsg.payload[1] = MTDR_HigherByte(elementCode);
    for (i = 0; i < clearMaskSize; i++)
        txMsg.payload[i + 2] = clearMask[i];
    txMsg.payloadLength = 2 + clearMaskSize;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgBeginReconfiguration(void *pD)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgBeginReconfigurationSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_BEGIN_RECONFIGURATION;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * None
     */
    txMsg.payloadLength = 0;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgBeginReconfiguration(void)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    PR_INFO(LOGTAG"m_MTDR_MsgBeginReconfiguration\n");
    return MTDR_MsgBeginReconfiguration(pD);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextActiveFramer(void *pD, uint8_t incomingFramerLa, uint16_t outgoingFramerClockCycles, uint16_t incomingFramerClockCycles)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextActiveFramerSF(pD, incomingFramerLa, outgoingFramerClockCycles, incomingFramerClockCycles);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_ACTIVE_FRAMER;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    LAIF [7:0]
     * 1    NCo  [7:0]
     * 2    NCi  [3:0]  NCo [11:8]
     * 3    NCo [11:4]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(LOGICAL_ADDRESS, incomingFramerLa);
    txMsg.payload[1] = MTDR_MsgPayloadField(OUTGOING_FRAMER_CYCLES_LOW, MTDR_LowerByte(outgoingFramerClockCycles));
    txMsg.payload[2] = MTDR_MsgPayloadField(OUTGOING_FRAMER_CYCLES_HIGH, MTDR_HigherByte(outgoingFramerClockCycles))
                       | MTDR_MsgPayloadField(INCOMING_FRAMER_CYCLES_LOW, MTDR_LowerByte(incomingFramerClockCycles));
    txMsg.payload[3] = MTDR_MsgPayloadField(INCOMING_FRAMER_CYCLES_HIGH, MTDR_HigherByte(incomingFramerClockCycles));
    txMsg.payloadLength = 4;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextActiveFramer(uint8_t incomingFramerLa, uint16_t outgoingFramerClockCycles, uint16_t incomingFramerClockCycles)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgNextActiveFramer(pD, incomingFramerLa, outgoingFramerClockCycles, incomingFramerClockCycles);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextSubframeMode(void *pD, MTDR_SubframeMode newSubframeMode)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextSubframeModeSF(pD, newSubframeMode);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_SUBFRAME_MODE;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    SM [4:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(SUBFRAME_MODE, newSubframeMode);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextSubframeMode(MTDR_SubframeMode newSubframeMode)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgNextSubframeMode(pD, newSubframeMode);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextClockGear(void *pD, MTDR_ClockGear newClockGear)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextClockGearSF(pD, newClockGear);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_CLOCK_GEAR;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    CG [3:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(CLOCK_GEAR, newClockGear);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextClockGear(MTDR_ClockGear newClockGear)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgNextClockGear(pD, newClockGear);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextRootFrequency(void *pD, MTDR_RootFrequency newRootFrequency)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextRootFrequencySF(pD, newRootFrequency);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_ROOT_FREQUENCY;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    RF [3:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(ROOT_FREQUENCY, newRootFrequency);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextRootFrequency(MTDR_RootFrequency newRootFrequency)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgNextRootFrequency(pD, newRootFrequency);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextPauseClock(void *pD, MTDR_RestartTime newRestartTime)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextPauseClockSF(pD, newRestartTime);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_PAUSE_CLOCK;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    RT [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(RESTART_TIME, newRestartTime);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextPauseClock(MTDR_RestartTime newRestartTime)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgNextPauseClock(pD, newRestartTime);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextResetBus(void *pD)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextResetBusSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_RESET_BUS;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * None
     */
    txMsg.payloadLength = 0;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextResetBus(void)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgNextResetBus(pD);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextShutdownBus(void *pD)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextShutdownBusSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_SHUTDOWN_BUS;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * None
     */
    txMsg.payloadLength = 0;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextShutdownBus(void)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    return MTDR_MsgNextShutdownBus(pD);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextDefineChannel(void *pD, uint8_t channelNumber, MTDR_TransportProtocol transportProtocol, uint16_t segmentDistribution, uint8_t segmentLength)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextDefineChannelSF(pD, channelNumber, transportProtocol, segmentDistribution, segmentLength);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_DEFINE_CHANNEL;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    CN [7:0]
     * 1    SD [7:0]
     * 2    TP [7:4]    SD [11:8]
     * 3                SL [4:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payload[1] = MTDR_MsgPayloadField(SEGMENT_DISTRIBUTION_LOW, MTDR_LowerByte(segmentDistribution));
    txMsg.payload[2] = MTDR_MsgPayloadField(SEGMENT_DISTRIBUTION_HIGH, MTDR_HigherByte(segmentDistribution))
                       | MTDR_MsgPayloadField(TRANSPORT_PROTOCOL, transportProtocol);
    txMsg.payload[3] = MTDR_MsgPayloadField(SEGMENT_LENGTH, segmentLength);
    txMsg.payloadLength = 4;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextDefineChannel(uint8_t channelNumber, MTDR_TransportProtocol transportProtocol, uint16_t segmentDistribution, uint8_t segmentLength)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    PR_INFO(LOGTAG"m_MTDR_MsgNextDefineChannel(CN: %d, TP: %d, SD: 0x%x, SL: %d)\n", channelNumber, transportProtocol, segmentDistribution, segmentLength);
    return MTDR_MsgNextDefineChannel(pD, channelNumber, transportProtocol, segmentDistribution, segmentLength);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextDefineContent(void *pD, uint8_t channelNumber, bool frequencyLockedBit, MTDR_PresenceRate presenceRate, MTDR_AuxFieldFormat auxiliaryBitFormat, MTDR_DataType dataType, bool channelLink, uint8_t dataLength)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextDefineContentSF(pD, channelNumber, presenceRate, auxiliaryBitFormat, dataType, dataLength);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_DEFINE_CONTENT;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    CN [7:0]
     * 1    FL [7:7]    PR [6:0]
     * 2    AF [7:4]    DT [3:0]
     * 3    CL [5:5]    DL [4:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payload[1] = MTDR_MsgPayloadField(FREQUENCY_LOCKED_BIT, frequencyLockedBit)
                       | MTDR_MsgPayloadField(PRESENCE_RATE, presenceRate);
    txMsg.payload[2] = MTDR_MsgPayloadField(AUXILIARY_BIT_FORMAT, auxiliaryBitFormat)
                       | MTDR_MsgPayloadField(DATA_TYPE, dataType);
    txMsg.payload[3] = MTDR_MsgPayloadField(CHANNEL_LINK, channelLink)
                       | MTDR_MsgPayloadField(DATA_LENGTH, dataLength);
    txMsg.payloadLength = 4;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextDefineContent(uint8_t channelNumber, bool frequencyLockedBit, MTDR_PresenceRate presenceRate, MTDR_AuxFieldFormat auxiliaryBitFormat, MTDR_DataType dataType, bool channelLink, uint8_t dataLength)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    PR_INFO(LOGTAG"m_MTDR_MsgNextDefineContent(CN: %d, FL: %d, PR: %d, AF: %d, DT: %d, CL: %d, DL: %d)\n", channelNumber, frequencyLockedBit, presenceRate, auxiliaryBitFormat, dataType, channelLink, dataLength);
    return MTDR_MsgNextDefineContent(pD, channelNumber, frequencyLockedBit, presenceRate, auxiliaryBitFormat, dataType, channelLink, dataLength);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextActivateChannel(void *pD, uint8_t channelNumber)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextActivateChannelSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_ACTIVATE_CHANNEL;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    CN [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextActivateChannel(uint8_t channelNumber)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    pr_info(LOGTAG"m_MTDR_MsgNextActivateChannel(chn: %d)\n", channelNumber);
    return MTDR_MsgNextActivateChannel(pD, channelNumber);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextDeactivateChannel(void *pD, uint8_t channelNumber)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextDeactivateChanneSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_DEACTIVATE_CHANNEL;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    CN [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextDeactivateChannel(uint8_t channelNumber)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    pr_info(LOGTAG"m_MTDR_MsgNextDeactivateChannel(chn: %d)\n", channelNumber);
    return MTDR_MsgNextDeactivateChannel(pD, channelNumber);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgNextRemoveChannel(void *pD, uint8_t channelNumber)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgNextRemoveChannelSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_NEXT_REMOVE_CHANNEL;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * 0    CN [7:0]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(DATA_CHANNEL_NUMBER, channelNumber);
    txMsg.payloadLength = 1;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgNextRemoveChannel(uint8_t channelNumber)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    PR_INFO(LOGTAG"m_MTDR_MsgNextRemoveChannel(chn: %d)\n", channelNumber);
    return MTDR_MsgNextRemoveChannel(pD, channelNumber);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgReconfigureNow(void *pD)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgReconfigureNowSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_RECONFIGURE_NOW;
    txMsg.destinationType = MTDR_DT_BROADCAST;
    txMsg.destinationAddress = 0x00;

    /*
     * Payload:
     * None
     */
    txMsg.payloadLength = 0;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgReconfigureNow(void)
{
    MTDR_Instance *pD = (MTDR_Instance *) pDrvIns;
    PR_INFO(LOGTAG"m_MTDR_MsgReconfigureNow\n");
    return MTDR_MsgReconfigureNow(pD);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgRequestValue(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint32_t result = MTDR_MsgRequestValueSF(pD);
    if (result)
        return result;
    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_REQUEST_VALUE;
    if (broadcast) {
        txMsg.destinationType = MTDR_DT_BROADCAST;
        txMsg.destinationAddress = 0x00;
    } else {
        txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
        txMsg.destinationAddress = destinationLa;
    }

    /*
     * Payload:
     * 0    TID [7:0]
     * 1    EC [ 7:0]
     * 2    EC [15:8]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(TRANSACTION_ID, transactionId);
    txMsg.payload[1] = MTDR_LowerByte(elementCode);
    txMsg.payload[2] = MTDR_HigherByte(elementCode);
    txMsg.payloadLength = 3;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgRequestValue(            uint8_t deviceLa, uint8_t transactionId, uint16_t byteAddress, MTDR_SliceSize sliceSize)
{
    uint32_t ret;
    uint16_t elementCode;

    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;

    /*
     * Element Code
     * 16 bit;
     * [15: 4] element address
     * [ 3: 3] access type (1 - byte based, 0 - elemental)
     * [ 2: 0] value slice size
     */
    elementCode = (byteAddress << 4) | (1 << 3) | (sliceSize);  /*here is byte based*/

    ret = MTDR_MsgRequestValue(instance, 0, deviceLa, transactionId, elementCode);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"msgRequestValue(%d) fail\n", byteAddress);
    }
    return ret;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgRequestChangeValue(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode, uint8_t *valueUpdate, uint8_t valueUpdateSize)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint8_t i;
    uint32_t result = MTDR_MsgRequestChangeValueSF(pD, valueUpdateSize);
    if (result)
        return result;

    if ((valueUpdate == NULL) && (valueUpdateSize > 0))
        return MTDR_RET_EINVAL;

    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_REQUEST_CHANGE_VALUE;
    if (broadcast) {
        txMsg.destinationType = MTDR_DT_BROADCAST;
        txMsg.destinationAddress = 0x00;
    } else {
        txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
        txMsg.destinationAddress = destinationLa;
    }

    /*
     * Payload:
     * 0    TID [7:0]
     * 1    EC [ 7:0]
     * 2    EC [15:8]
     * 3    VU [ 7:0]
     * N+2  VU [8N-1: 8N-8]
     */
    txMsg.payload[0] = MTDR_MsgPayloadField(TRANSACTION_ID, transactionId);
    txMsg.payload[1] = MTDR_LowerByte(elementCode);
    txMsg.payload[2] = MTDR_HigherByte(elementCode);
    for (i = 0; i < valueUpdateSize; i++)
        txMsg.payload[i + MTDR_ELEMENT_CODE_LENGTH + MTDR_TRANSACTION_ID_LENGTH] = valueUpdate[i];
    txMsg.payloadLength = MTDR_ELEMENT_CODE_LENGTH + MTDR_TRANSACTION_ID_LENGTH + valueUpdateSize;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_MsgChangeValue(void *pD, bool broadcast, uint8_t destinationLa, uint16_t elementCode, uint8_t *valueUpdate, uint8_t valueUpdateSize)
{
    MTDR_Instance *instance;
    MTDR_Message txMsg;
    uint8_t i;
    uint32_t result = MTDR_MsgChangeValueSF(pD, valueUpdateSize);
    if (result)
        return result;

    if ((valueUpdate == NULL) && (valueUpdateSize > 0))
        return MTDR_RET_EINVAL;

    instance = (MTDR_Instance *) pD;

    txMsg.arbitrationType = MTDR_AT_SHORT;
    txMsg.sourceAddress = MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER;
    txMsg.arbitrationPriority = MTDR_AP_DEFAULT;
    txMsg.messageType = MTDR_MT_CORE;
    txMsg.messageCode = MTDR_MESSAGE_CODE_CHANGE_VALUE;
    if (broadcast) {
        txMsg.destinationType = MTDR_DT_BROADCAST;
        txMsg.destinationAddress = 0x00;
    } else {
        txMsg.destinationType = MTDR_DT_LOGICAL_ADDRESS;
        txMsg.destinationAddress = destinationLa;
    }

    /*
     * Payload:
     * 0    EC [ 7:0]
     * 1    EC [15:8]
     * 2    VU [ 7:0]
     * N+1  VU [8N-1: 8N-8]
     */
    txMsg.payload[0] = MTDR_LowerByte(elementCode);
    txMsg.payload[1] = MTDR_HigherByte(elementCode);
    for (i = 0; i < valueUpdateSize; i++)
        txMsg.payload[i + MTDR_ELEMENT_CODE_LENGTH] = valueUpdate[i];
    txMsg.payloadLength = MTDR_ELEMENT_CODE_LENGTH + valueUpdateSize;

    return MTDR_TransmitMessage(instance, &txMsg);
}

/**
 * For MTDR internal api
 */
uint32_t m_MTDR_MsgChangeValue(             uint8_t deviceLa, uint16_t byteAddress, MTDR_SliceSize sliceSize, uint8_t *valueUpdate, uint8_t valueUpdateSize)
{
    uint32_t ret;
    uint16_t elementCode;

    MTDR_Instance *instance = (MTDR_Instance *) pDrvIns;

    /*
     * Element Code
     * 16 bit;
     * [15: 4] element address
     * [ 3: 3] access type (1 - byte based, 0 - elemental)
     * [ 2: 0] value slice size
     */
    elementCode = (byteAddress << 4) | (1 << 3) | (sliceSize);  /*here is byte based*/

    ret = MTDR_MsgChangeValue(instance, 0, deviceLa, elementCode, valueUpdate, valueUpdateSize);
    if (ret != MTDR_RET_EOK) {
        PR_INFO(LOGTAG"msgChangeValue(%d) fail\n", byteAddress);
    }
    return ret;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_Dport_rx(void *pD, uint8_t portNumber, uint16_t *pdata)
{
    MTDR_Instance *instance;
    instance = (MTDR_Instance *) pD;

    if(!pD || !pdata)
        return -1;

    *pdata = MTDR_ReadReg16b(PORT_FIFO_SPACE[portNumber].PORT_FIFO[0]);

    PR_INFO(LOGTAG"MTDR_Dport_rx(port: %d, data: %x)\n", portNumber, *pdata);

    return 0;
}

/**
 * For MTDR internal api
 */
uint32_t MTDR_Dport_tx(void *pD, uint8_t portNumber, uint16_t data)
{
    MTDR_Instance *instance;
    instance = (MTDR_Instance *) pD;

    if(!pD)
        return -1;

    PR_INFO(LOGTAG"MTDR_Dport_tx(port: %d, data: %x)\n", portNumber, data);

    MTDR_WriteReg16b(PORT_FIFO_SPACE[portNumber].PORT_FIFO[0], data);

    return 0;
}

/**
 * For MTDR internal api
 */
void *m_MTDR_pDrvIns_get(void)
{
    return pDrvIns;
}

/**
 * For MTDR internal api
 */
u8 m_slim_slicesize_get(u32 slicecode)
{
    u8 codetosize[8] = {1, 2, 3, 4, 6, 8, 12, 16};

    if (slicecode >= 8)
        return 0;
    else
        return codetosize[slicecode];
}

/**
 * For MTDR internal api
 */
u8 m_slim_slicecode_get(u32 slicesize)
{
    u8 sizetocode[16] = {0, 1, 2, 3, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7};

    if (slicesize == 0)
        slicesize = 1;
    if (slicesize > 16)
        slicesize = 16;
    return sizetocode[slicesize - 1];
}

MODULE_LICENSE("GPL");

