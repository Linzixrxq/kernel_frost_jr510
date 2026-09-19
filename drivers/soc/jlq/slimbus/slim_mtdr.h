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

#ifndef __SLIM_MTDR_H__
#define __SLIM_MTDR_H__

#include "slim_mtdr_if.h"


/* SLIMbus Message Fields */
#define MTDR_MESSAGE_ARBITRATION_TYPE_MASK                     0x0F
#define MTDR_MESSAGE_ARBITRATION_TYPE_SHIFT                    4
#define MTDR_MESSAGE_ARBITRATION_TYPE_OFFSET                   0
#define MTDR_MESSAGE_ARBITRATION_EXTENSION_MASK                0x01
#define MTDR_MESSAGE_ARBITRATION_EXTENSION_SHIFT               3
#define MTDR_MESSAGE_ARBITRATION_EXTENSION_OFFSET              0
#define MTDR_MESSAGE_ARBITRATION_PRIORITY_MASK                 0x07
#define MTDR_MESSAGE_ARBITRATION_PRIORITY_SHIFT                0
#define MTDR_MESSAGE_ARBITRATION_PRIORITY_OFFSET               0
#define MTDR_MESSAGE_SOURCE_ADDRESS_OFFSET                     1

#define MTDR_MESSAGE_MESSAGE_TYPE_MASK                         0x7
#define MTDR_MESSAGE_MESSAGE_TYPE_SHIFT                        5
#define MTDR_MESSAGE_MESSAGE_TYPE_OFFSET                       0
#define MTDR_MESSAGE_REMAINING_LENGTH_MASK                     0x1F
#define MTDR_MESSAGE_REMAINING_LENGTH_SHIFT                    0
#define MTDR_MESSAGE_REMAINING_LENGTH_OFFSET                   0
#define MTDR_MESSAGE_MESSAGE_CODE_MASK                         0x7F
#define MTDR_MESSAGE_MESSAGE_CODE_SHIFT                        0
#define MTDR_MESSAGE_MESSAGE_CODE_OFFSET                       1
#define MTDR_MESSAGE_MESSAGE_DESTINATION_TYPE_MASK             0x3
#define MTDR_MESSAGE_MESSAGE_DESTINATION_TYPE_SHIFT            4
#define MTDR_MESSAGE_MESSAGE_DESTINATION_TYPE_OFFSET           2
#define MTDR_MESSAGE_PRIMARY_INTEGRITY_MASK                    0xF
#define MTDR_MESSAGE_PRIMARY_INTEGRITY_SHIFT                   0
#define MTDR_MESSAGE_PRIMARY_INTEGRITY_OFFSET                  2
#define MTDR_MESSAGE_DESTINATION_ADDRESS_OFFSET                3
#define MTDR_MESSAGE_MESSAGE_INTEGRITY_OFFSET                  0
#define MTDR_MESSAGE_MESSAGE_INTEGRITY_MASK                    0xF
#define MTDR_MESSAGE_MESSAGE_INTEGRITY_SHIFT                   4
#define MTDR_MESSAGE_RESPONSE_CODE_OFFSET                      0
#define MTDR_MESSAGE_RESPONSE_CODE_MASK                        0xF
#define MTDR_MESSAGE_RESPONSE_CODE_SHIFT                       0

/* Other Message related defines */
#define MTDR_MESSAGE_ARBITRATION_LENGTH_LONG                   7
#define MTDR_MESSAGE_ARBITRATION_LENGTH_SHORT                  2
#define MTDR_MESSAGE_HEADER_LENGTH_BROADCAST                   3
#define MTDR_MESSAGE_HEADER_LENGTH_SHORT                       4
#define MTDR_MESSAGE_HEADER_LENGTH_LONG                        9

/* SLIMbus Messages Codes Defines */
/* Shared Message Channel Management Messages */
#define MTDR_MESSAGE_CODE_REPORT_PRESENT                       0x01
#define MTDR_MESSAGE_CODE_ASSIGN_LOGICAL_ADDRESS               0x02
#define MTDR_MESSAGE_CODE_RESET_DEVICE                         0x04
#define MTDR_MESSAGE_CODE_CHANGE_LOGICAL_ADDRESS               0x08
#define MTDR_MESSAGE_CODE_CHANGE_ARBITRATION_PRIORITY          0x09
#define MTDR_MESSAGE_CODE_REQUEST_SELF_ANNOUNCEMENT            0x0C
#define MTDR_MESSAGE_CODE_REPORT_ABSENT                        0x0F
/* Data Space Management Messages */
#define MTDR_MESSAGE_CODE_CONNECT_SOURCE                       0x10
#define MTDR_MESSAGE_CODE_CONNECT_SINK                         0x11
#define MTDR_MESSAGE_CODE_DISCONNECT_PORT                      0x14
#define MTDR_MESSAGE_CODE_CHANGE_CONTENT                       0x18
/* Information Messages */
#define MTDR_MESSAGE_CODE_REQUEST_INFORMATION                  0x20
#define MTDR_MESSAGE_CODE_REQUEST_CLEAR_INFORMATION            0x21
#define MTDR_MESSAGE_CODE_REPLY_INFORMATION                    0x24
#define MTDR_MESSAGE_CODE_CLEAR_INFORMATION                    0x28
#define MTDR_MESSAGE_CODE_REPORT_INFORMATION                   0x29
/* Value Element Messages */
#define MTDR_MESSAGE_CODE_REQUEST_VALUE                        0x60
#define MTDR_MESSAGE_CODE_REQUEST_CHANGE_VALUE                 0x61
#define MTDR_MESSAGE_CODE_REPLY_VALUE                          0x64
#define MTDR_MESSAGE_CODE_CHANGE_VALUE                         0x68
/* Synchronized Event Messages */
#define MTDR_MESSAGE_CODE_BEGIN_RECONFIGURATION                0x40
#define MTDR_MESSAGE_CODE_NEXT_ACTIVE_FRAMER                   0x44
#define MTDR_MESSAGE_CODE_NEXT_SUBFRAME_MODE                   0x45
#define MTDR_MESSAGE_CODE_NEXT_CLOCK_GEAR                      0x46
#define MTDR_MESSAGE_CODE_NEXT_ROOT_FREQUENCY                  0x47
#define MTDR_MESSAGE_CODE_NEXT_PAUSE_CLOCK                     0x4A
#define MTDR_MESSAGE_CODE_NEXT_RESET_BUS                       0x4B
#define MTDR_MESSAGE_CODE_NEXT_SHUTDOWN_BUS                    0x4C
#define MTDR_MESSAGE_CODE_NEXT_DEFINE_CHANNEL                  0x50
#define MTDR_MESSAGE_CODE_NEXT_DEFINE_CONTENT                  0x51
#define MTDR_MESSAGE_CODE_NEXT_ACTIVATE_CHANNEL                0x54
#define MTDR_MESSAGE_CODE_NEXT_DEACTIVATE_CHANNEL              0x55
#define MTDR_MESSAGE_CODE_NEXT_REMOVE_CHANNEL                  0x58
#define MTDR_MESSAGE_CODE_RECONFIGURE_NOW                      0x5F


/* SLIMbus Payload Fields */
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_BYTE_ADDRESS_SHIFT   4
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_BYTE_ADDRESS_MASK    0x0FFF
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_SLICE_SIZE_SHIFT     0
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_SLICE_SIZE_MASK      0x07
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_BIT_NUMBER_SHIFT     0
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_BIT_NUMBER_MASK      0x07
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_ACCESS_TYPE_SHIFT    3
#define MTDR_MESSAGE_PAYLOAD_ELEMENT_CODE_ACCESS_TYPE_MASK     0x01
#define MTDR_MESSAGE_PAYLOAD_DATA_CHANNEL_NUMBER_SHIFT         0
#define MTDR_MESSAGE_PAYLOAD_DATA_CHANNEL_NUMBER_MASK          0xFF
#define MTDR_MESSAGE_PAYLOAD_PRESENCE_RATE_SHIFT               0
#define MTDR_MESSAGE_PAYLOAD_PRESENCE_RATE_MASK                0x7F
#define MTDR_MESSAGE_PAYLOAD_FREQUENCY_LOCKED_BIT_SHIFT        7
#define MTDR_MESSAGE_PAYLOAD_FREQUENCY_LOCKED_BIT_MASK         0x01
#define MTDR_MESSAGE_PAYLOAD_DATA_TYPE_SHIFT                   0
#define MTDR_MESSAGE_PAYLOAD_DATA_TYPE_MASK                    0x0F
#define MTDR_MESSAGE_PAYLOAD_AUXILIARY_BIT_FORMAT_SHIFT        4
#define MTDR_MESSAGE_PAYLOAD_AUXILIARY_BIT_FORMAT_MASK         0x0F
#define MTDR_MESSAGE_PAYLOAD_DATA_LENGTH_SHIFT                 0
#define MTDR_MESSAGE_PAYLOAD_DATA_LENGTH_MASK                  0x1F
#define MTDR_MESSAGE_PAYLOAD_CHANNEL_LINK_SHIFT                5
#define MTDR_MESSAGE_PAYLOAD_CHANNEL_LINK_MASK                 0x01
#define MTDR_MESSAGE_PAYLOAD_SEGMENT_DISTRIBUTION_LOW_SHIFT    0
#define MTDR_MESSAGE_PAYLOAD_SEGMENT_DISTRIBUTION_LOW_MASK     0xFF
#define MTDR_MESSAGE_PAYLOAD_SEGMENT_DISTRIBUTION_HIGH_SHIFT   0
#define MTDR_MESSAGE_PAYLOAD_SEGMENT_DISTRIBUTION_HIGH_MASK    0x0F
#define MTDR_MESSAGE_PAYLOAD_TRANSPORT_PROTOCOL_SHIFT          4
#define MTDR_MESSAGE_PAYLOAD_TRANSPORT_PROTOCOL_MASK           0x0F
#define MTDR_MESSAGE_PAYLOAD_SEGMENT_LENGTH_SHIFT              0
#define MTDR_MESSAGE_PAYLOAD_SEGMENT_LENGTH_MASK               0x1F
#define MTDR_MESSAGE_PAYLOAD_PORT_NUMBER_SHIFT                 0
#define MTDR_MESSAGE_PAYLOAD_PORT_NUMBER_MASK                  0x3F
#define MTDR_MESSAGE_PAYLOAD_TRANSACTION_ID_SHIFT              0
#define MTDR_MESSAGE_PAYLOAD_TRANSACTION_ID_MASK               0xFF
#define MTDR_MESSAGE_PAYLOAD_LOGICAL_ADDRESS_SHIFT             0
#define MTDR_MESSAGE_PAYLOAD_LOGICAL_ADDRESS_MASK              0xFF
#define MTDR_MESSAGE_PAYLOAD_ARBITRATION_PRIORITY_SHIFT        0
#define MTDR_MESSAGE_PAYLOAD_ARBITRATION_PRIORITY_MASK         0x07
#define MTDR_MESSAGE_PAYLOAD_OUTGOING_FRAMER_CYCLES_LOW_SHIFT  0
#define MTDR_MESSAGE_PAYLOAD_OUTGOING_FRAMER_CYCLES_LOW_MASK   0xFF
#define MTDR_MESSAGE_PAYLOAD_OUTGOING_FRAMER_CYCLES_HIGH_SHIFT 0
#define MTDR_MESSAGE_PAYLOAD_OUTGOING_FRAMER_CYCLES_HIGH_MASK  0x0F
#define MTDR_MESSAGE_PAYLOAD_INCOMING_FRAMER_CYCLES_LOW_SHIFT  4
#define MTDR_MESSAGE_PAYLOAD_INCOMING_FRAMER_CYCLES_LOW_MASK   0x0F
#define MTDR_MESSAGE_PAYLOAD_INCOMING_FRAMER_CYCLES_HIGH_SHIFT 0
#define MTDR_MESSAGE_PAYLOAD_INCOMING_FRAMER_CYCLES_HIGH_MASK  0xFF
#define MTDR_MESSAGE_PAYLOAD_SUBFRAME_MODE_SHIFT               0
#define MTDR_MESSAGE_PAYLOAD_SUBFRAME_MODE_MASK                0x1F
#define MTDR_MESSAGE_PAYLOAD_CLOCK_GEAR_SHIFT                  0
#define MTDR_MESSAGE_PAYLOAD_CLOCK_GEAR_MASK                   0x0F
#define MTDR_MESSAGE_PAYLOAD_ROOT_FREQUENCY_SHIFT              0
#define MTDR_MESSAGE_PAYLOAD_ROOT_FREQUENCY_MASK               0x0F
#define MTDR_MESSAGE_PAYLOAD_RESTART_TIME_SHIFT                0
#define MTDR_MESSAGE_PAYLOAD_RESTART_TIME_MASK                 0xFF

#define MTDR_ELEMENT_CODE_BYTE_BASED_ACCESS                    1
#define MTDR_ELEMENT_CODE_ELEMENTAL_ACCESS                     0
#define MTDR_ELEMENT_CODE_LENGTH                               2
#define MTDR_TRANSACTION_ID_LENGTH                             1


/* SLIMbus Information Elements */
/* Interface Device Class-specific Information Elements */
#define MTDR_IE_INTERFACE_DATA_SLOT_OVERLAP_ADDRESS            0x400
#define MTDR_IE_INTERFACE_DATA_SLOT_OVERLAP_SHIFT              4
#define MTDR_IE_INTERFACE_DATA_SLOT_OVERLAP_MASK               0x1
#define MTDR_IE_INTERFACE_DATA_SLOT_OVERLAP_READONLY           0

#define MTDR_IE_INTERFACE_LOST_MS_ADDRESS                      0x400
#define MTDR_IE_INTERFACE_LOST_MS_SHIFT                        3
#define MTDR_IE_INTERFACE_LOST_MS_MASK                         0x1
#define MTDR_IE_INTERFACE_LOST_MS_READONLY                     0

#define MTDR_IE_INTERFACE_LOST_SFS_ADDRESS                     0x400
#define MTDR_IE_INTERFACE_LOST_SFS_SHIFT                       2
#define MTDR_IE_INTERFACE_LOST_SFS_MASK                        0x1
#define MTDR_IE_INTERFACE_LOST_SFS_READONLY                    0

#define MTDR_IE_INTERFACE_LOST_FS_ADDRESS                      0x400
#define MTDR_IE_INTERFACE_LOST_FS_SHIFT                        1
#define MTDR_IE_INTERFACE_LOST_FS_MASK                         0x1
#define MTDR_IE_INTERFACE_LOST_FS_READONLY                     0

#define MTDR_IE_INTERFACE_MC_TX_COL_ADDRESS                    0x400
#define MTDR_IE_INTERFACE_MC_TX_COL_SHIFT                      0
#define MTDR_IE_INTERFACE_MC_TX_COL_MASK                       0x1
#define MTDR_IE_INTERFACE_MC_TX_COL_READONLY                   0

/* Manager Class-specific Information Elements */
#define MTDR_IE_MANAGER_ACTIVE_MANAGER_ADDRESS                 0x400
#define MTDR_IE_MANAGER_ACTIVE_MANAGER_SHIFT                   0
#define MTDR_IE_MANAGER_ACTIVE_MANAGER_MASK                    0x1
#define MTDR_IE_MANAGER_ACTIVE_MANAGER_READONLY                1

/* Framer Class-specific Information Elements */
#define MTDR_IE_FRAMER_QUALITY_ADDRESS                         0x400
#define MTDR_IE_FRAMER_QUALITY_SHIFT                           6
#define MTDR_IE_FRAMER_QUALITY_MASK                            0x3
#define MTDR_IE_FRAMER_QUALITY_READONLY                        1

#define MTDR_IE_FRAMER_GC_TX_COL_ADDRESS                       0x400
#define MTDR_IE_FRAMER_GC_TX_COL_SHIFT                         3
#define MTDR_IE_FRAMER_GC_TX_COL_MASK                          0x1
#define MTDR_IE_FRAMER_GC_TX_COL_READONLY                      0

#define MTDR_IE_FRAMER_FI_TX_COL_ADDRESS                       0x400
#define MTDR_IE_FRAMER_FI_TX_COL_SHIFT                         2
#define MTDR_IE_FRAMER_FI_TX_COL_MASK                          0x1
#define MTDR_IE_FRAMER_FI_TX_COL_READONLY                      0

#define MTDR_IE_FRAMER_FS_TX_COL_ADDRESS                       0x400
#define MTDR_IE_FRAMER_FS_TX_COL_SHIFT                         1
#define MTDR_IE_FRAMER_FS_TX_COL_MASK                          0x1
#define MTDR_IE_FRAMER_FS_TX_COL_READONLY                      0

#define MTDR_IE_FRAMER_ACTIVE_FRAMER_ADDRESS                   0x400
#define MTDR_IE_FRAMER_ACTIVE_FRAMER_SHIFT                     0
#define MTDR_IE_FRAMER_ACTIVE_FRAMER_MASK                      0x1
#define MTDR_IE_FRAMER_ACTIVE_FRAMER_READONLY                  1

/* Core Information Elements */
#define MTDR_IE_CORE_DEVICE_CLASS_ADDRESS                      0x009
#define MTDR_IE_CORE_DEVICE_CLASS_SHIFT                        0
#define MTDR_IE_CORE_DEVICE_CLASS_MASK                         0xFF
#define MTDR_IE_CORE_DEVICE_CLASS_READONLY                     1

#define MTDR_IE_CORE_DEVICE_CLASS_VERSION_ADDRESS              0x008
#define MTDR_IE_CORE_DEVICE_CLASS_VERSION_SHIFT                0
#define MTDR_IE_CORE_DEVICE_CLASS_VERSION_MASK                 0xFF
#define MTDR_IE_CORE_DEVICE_CLASS_VERSION_READONLY             1

#define MTDR_IE_CORE_EX_ERROR_ADDRESS                          0x000
#define MTDR_IE_CORE_EX_ERROR_SHIFT                            3
#define MTDR_IE_CORE_EX_ERROR_MASK                             0x1
#define MTDR_IE_CORE_EX_ERROR_READONLY                         0

#define MTDR_IE_CORE_RECONFIG_OBJECTION_ADDRESS                0x000
#define MTDR_IE_CORE_RECONFIG_OBJECTION_SHIFT                  2
#define MTDR_IE_CORE_RECONFIG_OBJECTION_MASK                   0x1
#define MTDR_IE_CORE_RECONFIG_OBJECTION_READONLY               1

#define MTDR_IE_CORE_DATA_TX_COL_ADDRESS                       0x000
#define MTDR_IE_CORE_DATA_TX_COL_SHIFT                         1
#define MTDR_IE_CORE_DATA_TX_COL_MASK                          0x1
#define MTDR_IE_CORE_DATA_TX_COL_READONLY                      0

#define MTDR_IE_CORE_UNSPRTD_MSG_ADDRESS                       0x000
#define MTDR_IE_CORE_UNSPRTD_MSG_SHIFT                         0
#define MTDR_IE_CORE_UNSPRTD_MSG_MASK                          0x1
#define MTDR_IE_CORE_UNSPRTD_MSG_READONLY                      0

#define MTDR_IE_CLASS_SPECIFIC_OFFSET                          0x400
#define MTDR_IE_CORE_OFFSET                                    0x0

/* Transport Protocols */
#define MTDR_TP_ISOCHRONOUS_MASK                               1
#define MTDR_TP_ISOCHRONOUS_SHIFT                              0
#define MTDR_TP_PUSHED_MASK                                    1
#define MTDR_TP_PUSHED_SHIFT                                   1
#define MTDR_TP_PULLED_MASK                                    1
#define MTDR_TP_PULLED_SHIFT                                   2

/* SLIMbus Device Classes */
#define MTDR_DEVICE_CLASS_MANAGER                              0xFF
#define MTDR_DEVICE_CLASS_FRAMER                               0xFE
#define MTDR_DEVICE_CLASS_INTERFACE                            0xFD
#define MTDR_DEVICE_CLASS_GENERIC                              0x00

/* Misc defines */
#define MTDR_RX_FIFO_MSG_MAX_SIZE                              64
#define MTDR_TX_FIFO_MSG_MAX_SIZE                              64
#define MTDR_DATA_DIRECTION_SOURCE                             0
#define MTDR_DATA_DIRECTION_SINK                               1
#define MTDR_MESSAGE_SOURCE_ACTIVE_MANAGER                     0xFF

#define MTDR_RX_FIFO_FLAG_RX_OVERFLOW                          8
#define MTDR_RX_FIFO_FLAG_RX_MSG_LEN                           0
#define MTDR_RX_FIFO_FLAG_RX_MSG_MASK                          0x3F
#define MTDR_RX_FIFO_FLAG_OFFSET                               15


/* Get/Set Message fields Macros */
#define MTDR_GetMsgFieldMask(name)                             (MTDR_MESSAGE_ ## name ## _MASK)
#define MTDR_GetMsgFieldShift(name)                            (MTDR_MESSAGE_ ## name ## _SHIFT)
#define MTDR_GetMsgFieldOffset(name)                           (MTDR_MESSAGE_ ## name ## _OFFSET)
#define MTDR_CalcMsgFieldOffset(name, offset)                  (offset + (MTDR_GetMsgFieldOffset(name)))
#define MTDR_GetMsgField(source, offset, name)                 ((source[MTDR_CalcMsgFieldOffset(name, offset)] >> (MTDR_GetMsgFieldShift(name))) & (MTDR_GetMsgFieldMask(name)))
#define MTDR_SetMsgField(target, value, offset, name)          target[MTDR_CalcMsgFieldOffset(name, offset)] &= ~((MTDR_GetMsgFieldMask(name)) << (MTDR_GetMsgFieldShift(name))); \
                                                                target[MTDR_CalcMsgFieldOffset(name, offset)] |= ((value) & (MTDR_GetMsgFieldMask(name))) << (MTDR_GetMsgFieldShift(name))
#define MTDR_GetMsgPayloadFieldMask(name)                      (MTDR_MESSAGE_PAYLOAD_ ## name ## _MASK)
#define MTDR_GetMsgPayloadFieldShift(name)                     (MTDR_MESSAGE_PAYLOAD_ ## name ## _SHIFT)
#define MTDR_MsgPayloadField(name, value)                      (((value) & (MTDR_GetMsgPayloadFieldMask(name))) << (MTDR_GetMsgPayloadFieldShift(name)))
#define MTDR_GetMsgPayloadField(name, payload)                 (((payload) >> (MTDR_GetMsgPayloadFieldShift(name))) & (MTDR_GetMsgPayloadFieldMask(name)))
#define MTDR_ElementCodeByteBased(byteAddress, sliceSize)      (MTDR_MsgPayloadField(ELEMENT_CODE_BYTE_ADDRESS, byteAddress) | MTDR_MsgPayloadField(ELEMENT_CODE_SLICE_SIZE, sliceSize) | MTDR_MsgPayloadField(ELEMENT_CODE_ACCESS_TYPE, MTDR_ELEMENT_CODE_BYTE_BASED_ACCESS))
#define MTDR_GetIeFieldAddress(name)                           (MTDR_IE_ ## name ## _ADDRESS)
#define MTDR_GetIeFieldMask(name)                              (MTDR_IE_ ## name ## _MASK)
#define MTDR_GetIeFieldShift(name)                             (MTDR_IE_ ## name ## _SHIFT)
#define MTDR_GetIeFieldReadOnly(name)                          (MTDR_IE_ ## name ## _READONLY)
#define MTDR_GetIeMaskShifted(name)                            ((MTDR_GetIeFieldMask(name)) << (MTDR_GetIeFieldShift(name)))
#define MTDR_GetIeField(name, is)                              (((is) >> MTDR_GetIeFieldShift(name)) & MTDR_GetIeFieldMask(name))
#define MTDR_GetIeByteAccess(output, name, address, is)        if ((MTDR_GetIeFieldAddress(name)) == (address)) \
                                                                    output = MTDR_GetIeField(name, (is))
#define MTDR_GetIeElementalAccess(output, name, address, bitNumber, is) \
                                                                if (((MTDR_GetIeFieldAddress(name)) == (address)) && ((MTDR_GetIeFieldShift(name)) == (bitNumber))) \
                                                                    output = (is) & (MTDR_GetIeFieldMask(name))
#define MTDR_ClearIeByteAccess(name, address, is)              if ((MTDR_GetIeFieldReadOnly(name)) && ((MTDR_GetIeFieldAddress(name)) == (address))) \
                                                                    is &= (uint8_t)(~(MTDR_GetIeMaskShifted(name)))
#define MTDR_ClearIeElementalAccess(name, address, bitNumber, is) \
                                                                if ((MTDR_GetIeFieldReadOnly(name)) && ((MTDR_GetIeFieldAddress(name)) == (address)) && ((MTDR_GetIeFieldShift(name)) == (bitNumber))) \
                                                                    is &= (uint8_t)(~(MTDR_GetIeFieldMask(name)))
#define MTDR_GetTpFieldMask(name)                              (MTDR_TP_ ## name ## _MASK)
#define MTDR_GetTpFieldShift(name)                             (MTDR_TP_ ## name ## _SHIFT)
#define MTDR_TpField(name, value)                              (((value) & (MTDR_GetTpFieldMask(name))) << (MTDR_GetTpFieldShift(name)))
#define MTDR_GetTpField(name, reg)                             (((reg) >> (MTDR_GetTpFieldShift(name))) & (MTDR_GetTpFieldMask(name)))
#define MTDR_LowerByte(data)                                   ((data) & 0xFF)
#define MTDR_HigherByte(data)                                  (((data) >> 8) & 0xFF)
#define MTDR_HigherByteLowerNibble(data)                       (((data) >> 8) & 0x0F)
#define MTDR_GetField6B(source, offset) \
    (((uint64_t)(source)[(offset)] << 40) \
    | ((uint64_t)(source)[(offset) + 1] << 32) \
    | ((uint64_t)(source)[(offset) + 2] << 24) \
    | ((uint64_t)(source)[(offset) + 3] << 16) \
    | ((uint64_t)(source)[(offset) + 4] << 8) \
    | ((uint64_t)(source)[(offset) + 5]))

#define MTDR_SetField6B(target, value, offset) \
    (target)[offset] = (uint8_t)((uint64_t)(value) >> 40); \
    (target)[(offset) + 1] = (uint8_t)((uint64_t)(value) >> 32); \
    (target)[(offset) + 2] = (uint8_t)((uint64_t)(value) >> 24); \
    (target)[(offset) + 3] = (uint8_t)((uint64_t)(value) >> 16); \
    (target)[(offset) + 4] = (uint8_t)((uint64_t)(value) >> 8); \
    (target)[(offset) + 5] = (uint8_t)(value)

#define MTDR_GetField1B(source, offset)                        (source[offset])

#define MTDR_SetField1B(target, value, offset)                 target[offset] = (value) & 0xFF
#define MTDR_ReadReg(reg)                                      MTDR_UncachedRead32((uint32_t*) &(instance->registers->reg))
#define MTDR_WriteReg(reg, data)                               MTDR_UncachedWrite32((uint32_t*) &(instance->registers->reg), (data))
#define MTDR_ReadReg16b(reg)                                      MTDR_UncachedRead16((uint16_t*) &(instance->registers->reg))
#define MTDR_WriteReg16b(reg, data)                               MTDR_UncachedWrite16((uint16_t*) &(instance->registers->reg), (data))
#define MTDR_ReadReg8b(reg)                                      MTDR_UncachedRead8((uint16_t*) &(instance->registers->reg))
#define MTDR_WriteReg8b(reg, data)                               MTDR_UncachedWrite8((uint16_t*) &(instance->registers->reg), (data))



typedef struct MTDR_regs MTDR_Registers;

typedef struct {
    uintptr_t registerBase;
    MTDR_Registers *registers;
    bool disableHardwareCrcCalculation;
    bool enumerateDevices;
    MTDR_Callbacks basicCallbacks;
    MTDR_MessageCallbacks messageCallbacks;

} MTDR_Instance;

void *m_MTDR_pDrvIns_get(void);

uint32_t m_MTDR_Init(void);
uint32_t m_MTDR_SetFramerConfig(void);
uint32_t m_MTDR_SetGenericDeviceConfig(void);
uint32_t m_MTDR_GetGenericDeviceConfig(MTDR_GenericDeviceConfig *genericDeviceConfig);
uint32_t m_MTDR_GetDataPortStatus(uint8_t portNumber, MTDR_DataPortStatus *portStatus);
uint32_t m_MTDR_Start(void);
uint32_t m_MTDR_FifoReceive(uint8_t *rxFifoData, uint8_t rxFifoMaxSize);
uint32_t m_MTDR_DecodeMessage(uint8_t *receivedMessage, uint8_t messageLen, MTDR_Message *message);
uint32_t m_MTDR_MsgAssignLogicalAddress(uint64_t destinationEa, uint8_t newLa);
uint32_t m_MTDR_MsgRequestValue(            uint8_t deviceLa, uint8_t transactionId, uint16_t byteAddress, MTDR_SliceSize sliceSize);
uint32_t m_MTDR_MsgChangeValue(             uint8_t deviceLa, uint16_t byteAddress, MTDR_SliceSize sliceSize, uint8_t *valueUpdate, uint8_t valueUpdateSize);
uint32_t m_MTDR_MsgRequestInformation(             uint8_t deviceLa, uint8_t transactionId, uint16_t byteAddress, MTDR_SliceSize sliceSize);

uint32_t m_MTDR_SetDataPortInterrupts(uint8_t portNumber, uint8_t interruptMask);
uint32_t m_MTDR_GetDataPortInterrupts(uint8_t portNumber, uint8_t *interruptMask);
uint32_t m_MTDR_ClearDataPortFifo(uint8_t portNumber);
uint32_t m_MTDR_SetPresenceRateGeneration(uint8_t portNumber, bool enable);
uint32_t m_MTDR_GetPresenceRateGeneration(void *pD, uint8_t portNumber, bool *enable);


uint32_t m_MTDR_MsgConnectSource(uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber);
uint32_t m_MTDR_MsgConnectSink(uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber);
uint32_t m_MTDR_MsgDisconnectPort(uint8_t destinationLa, uint8_t portNumber);

uint32_t m_MTDR_MsgBeginReconfiguration(void);
uint32_t m_MTDR_MsgNextActiveFramer(uint8_t incomingFramerLa, uint16_t outgoingFramerClockCycles, uint16_t incomingFramerClockCycles);
uint32_t m_MTDR_MsgNextSubframeMode(MTDR_SubframeMode newSubframeMode);
uint32_t m_MTDR_MsgNextClockGear(MTDR_ClockGear newClockGear);
uint32_t m_MTDR_MsgNextRootFrequency(MTDR_RootFrequency newRootFrequency);
uint32_t m_MTDR_MsgNextPauseClock(MTDR_RestartTime newRestartTime);
uint32_t m_MTDR_MsgNextResetBus(void);
uint32_t m_MTDR_MsgNextShutdownBus(void);
uint32_t m_MTDR_MsgNextDefineChannel(uint8_t channelNumber, MTDR_TransportProtocol transportProtocol, uint16_t segmentDistribution, uint8_t segmentLength);
uint32_t m_MTDR_MsgNextDefineContent(uint8_t channelNumber, bool frequencyLockedBit, MTDR_PresenceRate presenceRate, MTDR_AuxFieldFormat auxiliaryBitFormat, MTDR_DataType dataType, bool channelLink, uint8_t dataLength);
uint32_t m_MTDR_MsgNextActivateChannel(uint8_t channelNumber);
uint32_t m_MTDR_MsgNextDeactivateChannel(uint8_t channelNumber);
uint32_t m_MTDR_MsgNextRemoveChannel(uint8_t channelNumber);
uint32_t m_MTDR_MsgReconfigureNow(void);

uint32_t m_MTDR_GetStatusSlimbus(MTDR_SubframeMode *subframeMode, MTDR_ClockGear *clockGear, MTDR_RootFrequency *rootFr);
uint32_t m_MTDR_GetStatusMessages(bool *sendingFinished);


u8 m_slim_slicesize_get(u32 slicecode);
u8 m_slim_slicecode_get(u32 slicesize);


#endif
