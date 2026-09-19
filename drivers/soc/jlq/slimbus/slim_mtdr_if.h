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

#ifndef SLIM_MTDR_IF_H
#define SLIM_MTDR_IF_H

/** Maximum message payload length */
#define	MTDR_MESSAGE_PAYLOAD_MAX_LENGTH 28U

/** Maximum message length */
#define	MTDR_MESSAGE_MAX_LENGTH 39U

/** Minimum message length */
#define	MTDR_MESSAGE_MIN_LENGTH 6U

typedef struct MTDR_Message_s MTDR_Message;
typedef struct MTDR_FramerConfig_s MTDR_FramerConfig;
typedef struct MTDR_GenericDeviceConfig_s MTDR_GenericDeviceConfig;
typedef struct MTDR_InformationElements_s MTDR_InformationElements;
typedef struct MTDR_DataPortStatus_s MTDR_DataPortStatus;
typedef struct MTDR_Config_s MTDR_Config;
typedef struct MTDR_Callbacks_s MTDR_Callbacks;
typedef struct MTDR_MessageCallbacks_s MTDR_MessageCallbacks;

/** Arbitration Type Arbitration Field. */
typedef enum {
    /** No Arbitration */
    MTDR_AT_NONE = 0U,
    /** Long Arbitration */
    MTDR_AT_LONG = 5U,
    /** Short Arbitration */
    MTDR_AT_SHORT = 15U
} MTDR_ArbitrationType;

/** Arbitration Priority Arbitration Field. */
typedef enum {
    /** Low Priority Messages */
    MTDR_AP_LOW = 1U,
    /** Default Messages */
    MTDR_AP_DEFAULT = 2U,
    /** High Priority Messages */
    MTDR_AP_HIGH = 3U,
    /** Manager assigned only */
    MTDR_AP_MANAGER_1 = 4U,
    /** Manager assigned only */
    MTDR_AP_MANAGER_2 = 5U,
    /** Manager assigned only */
    MTDR_AP_MANAGER_3 = 6U,
    /** Maximum Priority, for test and debug only */
    MTDR_AP_MAXIMUM = 7U
} MTDR_ArbitrationPriority;

/** Message Types Header Field. */
typedef enum {
    /** Core Message */
    MTDR_MT_CORE = 0U,
    /** Destination-referred Class-specific Message */
    MTDR_MT_DESTINATION_REFERRED_CLASS_SPECIFIC_MESSAGE = 1U,
    /** Destination-referred User Message */
    MTDR_MT_DESTINATION_REFERRED_USER_MESSAGE = 2U,
    /** Source-referred Class-specific Message */
    MTDR_MT_SOURCE_REFERRED_CLASS_SPECIFIC_MESSAGE = 5U,
    /** Source-referred User Message */
    MTDR_MT_SOURCE_REFERRED_USER_MESSAGE = 6U
} MTDR_MessageType;

/** Destination Type Header Field. */
typedef enum {
    /** Destination is a Logical Address */
    MTDR_DT_LOGICAL_ADDRESS = 0U,
    /** Destination is an Enumeration Address */
    MTDR_DT_ENUMERATION_ADDRESS = 1U,
    /** All Devices are Destinations, no Destination Address included in Header */
    MTDR_DT_BROADCAST = 3U
} MTDR_DestinationType;

/** Message Response Field. */
typedef enum {
    /** Positive Acknowledge */
    MTDR_MR_POSITIVE_ACK = 10U,
    /** Negative Acknowledge */
    MTDR_MR_NEGATIVE_ACK = 15U,
    /** No Response */
    MTDR_MR_NO_RESPONSE = 0U
} MTDR_MessageResponse;

/** SLIMbus Transport Protocols. */
typedef enum {
    /** Isochronous Protocol (Multicast). */
    MTDR_TP_ISOCHRONOUS = 0U,
    /** Pushed Protocol (Multicast). */
    MTDR_TP_PUSHED = 1U,
    /** Pulled Protocol (Unicast). */
    MTDR_TP_PULLED = 2U,
    /** Locked Protocol (Multicast). */
    MTDR_TP_LOCKED = 3U,
    /** Asynchronous Protocol - Simplex (Unicast). */
    MTDR_TP_ASYNC_SIMPLEX = 4U,
    /** Asynchronous Protocol - Half-duplex (Unicast). */
    MTDR_TP_ASYNC_HALF_DUPLEX = 5U,
    /** Extended Asynchronous Protocol - Simplex (Unicast). */
    MTDR_TP_EXT_ASYNC_SIMPLEX = 6U,
    /** Extended Asynchronous Protocol - Half-duplex (Unicast). */
    MTDR_TP_EXT_ASYNC_HALF_DUPLEX = 7U,
    /** User Defined 1. */
    MTDR_TP_USER_DEFINED_1 = 14U,
    /** User Defined 2. */
    MTDR_TP_USER_DEFINED_2 = 15U
} MTDR_TransportProtocol;

/** Presence Rates. */
typedef enum {
    MTDR_PR_12K = 1U,
    MTDR_PR_24K = 2U,
    MTDR_PR_48K = 3U,
    MTDR_PR_96K = 4U,
    MTDR_PR_192K = 5U,
    MTDR_PR_384K = 6U,
    MTDR_PR_768K = 7U,
    MTDR_PR_11025 = 9U,
    MTDR_PR_22050 = 10U,
    MTDR_PR_44100 = 11U,
    MTDR_PR_88200 = 12U,
    MTDR_PR_176400 = 13U,
    MTDR_PR_352800 = 14U,
    MTDR_PR_705600 = 15U,
    MTDR_PR_4K = 16U,
    MTDR_PR_8K = 17U,
    MTDR_PR_16K = 18U,
    MTDR_PR_32K = 19U,
    MTDR_PR_64K = 20U,
    MTDR_PR_128K = 21U,
    MTDR_PR_256K = 22U,
    MTDR_PR_512K = 23U
} MTDR_PresenceRate;

/** Data types. */
typedef enum {
    /** Not indicated. */
    MTDR_DF_NOT_INDICATED = 0U,
    /** LPCM audio. */
    MTDR_DF_LPCM = 1U,
    /** IEC61937 Compressed audio. */
    MTDR_DF_IEC61937 = 2U,
    /** Packed PDM audio. */
    MTDR_DF_PACKED_PDM_AUDIO = 3U,
    /** User Defined 1. */
    MTDR_DF_USER_DEFINED_1 = 14U,
    /** User Defined 2. */
    MTDR_DF_USER_DEFINED_2 = 15U
} MTDR_DataType;

/** Auxiliary Field formats formats. */
typedef enum {
    /** Not applicable. */
    MTDR_AF_NOT_APPLICABLE = 0U,
    /** ZCUV for tunneling IEC60958. */
    MTDR_AF_ZCUV = 1U,
    /** User defined. */
    MTDR_AF_USER_DEFINED = 11U
} MTDR_AuxFieldFormat;

/** SLIMbus interrupts. */
typedef enum {
    MTDR_INT_EN = 1U,
    MTDR_INT_RX = 2U,
    MTDR_INT_TX = 4U,
    MTDR_INT_TX_ERR = 8U,
    MTDR_INT_SYNC_LOST = 16U,
    MTDR_INT_RCFG = 32U,
    MTDR_INT_MCH = 64U
} MTDR_Interrupt;

/** Data Port interrupts. */
typedef enum {
    /** Channel activation. */
    MTDR_DP_INT_ACT = 1U,
    /** Channel content definition. */
    MTDR_DP_INT_CON = 2U,
    /** Channel definition. */
    MTDR_DP_INT_CHAN = 4U,
    /** Data Port DMA request. */
    MTDR_DP_INT_DMA = 8U,
    /** Data Port FIFO overflow. */
    MTDR_DP_INT_OVF = 16U,
    /** Data Port FIFO underrun. */
    MTDR_DP_INT_UND = 32U
} MTDR_DataPortInterrupt;

/** Quality of the CLK signal that is generated by the Framer. */
typedef enum {
    MTDR_FQ_PUNCTURED = 0U,
    MTDR_FQ_IRREGULAR = 1U,
    MTDR_FQ_REGULAR = 2U,
    MTDR_FQ_LOW_JITTER = 3U
} MTDR_FramerQuality;

/** User Value / Information Elements Slice sizes. */
typedef enum {
    /** Slice size is 1 byte. */
    MTDR_SS_1_BYTE = 0U,
    /** Slice size is 2 bytes. */
    MTDR_SS_2_BYTES = 1U,
    /** Slice size is 3 bytes. */
    MTDR_SS_3_BYTES = 2U,
    /** Slice size is 4 bytes. */
    MTDR_SS_4_BYTES = 3U,
    /** Slice size is 6 bytes. */
    MTDR_SS_6_BYTES = 4U,
    /** Slice size is 8 bytes. */
    MTDR_SS_8_BYTES = 5U,
    /** Slice size is 12 bytes. */
    MTDR_SS_12_BYTES = 6U,
    /** Slice size is 16 bytes. */
    MTDR_SS_16_BYTES = 7U
} MTDR_SliceSize;

/** SLIMbus device classes */
typedef enum {
    /** Manager Class Device */
    MTDR_DC_MANAGER = 255U,
    /** Framer Class Device */
    MTDR_DC_FRAMER = 254U,
    /** Interface Class Device */
    MTDR_DC_INTERFACE = 253U,
    /** Generic Class Device */
    MTDR_DC_GENERIC = 0U
} MTDR_DeviceClass;

/** Arbitration Type Arbitration Field. */
typedef enum {
    /** Clock Gear 6 */
    MTDR_RC_CLOCK_GEAR_6 = 0U,
    /** Clock Gear 7 */
    MTDR_RC_CLOCK_GEAR_7 = 1U,
    /** Clock Gear 8 */
    MTDR_RC_CLOCK_GEAR_8 = 2U,
    /** Clock Gear 9 */
    MTDR_RC_CLOCK_GEAR_9 = 3U
} MTDR_ReferenceClock;

/** Subframe Mode Codings. CSW - Control Space Width (Slots), SL - Subframe Length (Slots) */
typedef enum {
    MTDR_SM_24_CSW_32_SL = 31U,
    MTDR_SM_16_CSW_32_SL = 29U,
    MTDR_SM_16_CSW_24_SL = 28U,
    MTDR_SM_12_CSW_32_SL = 27U,
    MTDR_SM_12_CSW_24_SL = 26U,
    MTDR_SM_8_CSW_32_SL = 25U,
    MTDR_SM_8_CSW_24_SL = 24U,
    MTDR_SM_6_CSW_32_SL = 23U,
    MTDR_SM_6_CSW_24_SL = 22U,
    MTDR_SM_6_CSW_8_SL = 21U,
    MTDR_SM_4_CSW_32_SL = 19U,
    MTDR_SM_4_CSW_24_SL = 18U,
    MTDR_SM_4_CSW_8_SL = 17U,
    MTDR_SM_4_CSW_6_SL = 16U,
    MTDR_SM_3_CSW_32_SL = 15U,
    MTDR_SM_3_CSW_24_SL = 14U,
    MTDR_SM_3_CSW_8_SL = 13U,
    MTDR_SM_3_CSW_6_SL = 12U,
    MTDR_SM_2_CSW_32_SL = 11U,
    MTDR_SM_2_CSW_24_SL = 10U,
    MTDR_SM_2_CSW_8_SL = 9U,
    MTDR_SM_2_CSW_6_SL = 8U,
    MTDR_SM_1_CSW_32_SL = 7U,
    MTDR_SM_1_CSW_24_SL = 6U,
    MTDR_SM_1_CSW_8_SL = 5U,
    MTDR_SM_1_CSW_6_SL = 4U,
    /** 100% Control Space, 0% Data Space */
    MTDR_SM_8_CSW_8_SL = 0U
} MTDR_SubframeMode;

/** SLIMbus Frequencies and Clock Gear Codings. */
typedef enum {
    /** Not Indicated, Min: 0 MHz, Max: 28.8 MHz */
    MTDR_CG_0 = 0U,
    /** Min: 0.025 MHz, Max: 0.05625 MHz */
    MTDR_CG_1 = 1U,
    /** Min: 0.05 MHz, Max: 0.1125 MHz */
    MTDR_CG_2 = 2U,
    /** Min: 0.1 MHz, Max: 0.225 MHz */
    MTDR_CG_3 = 3U,
    /** Min: 0.2 MHz, Max: 0.45 MHz */
    MTDR_CG_4 = 4U,
    /** Min: 0.4 MHz, Max: 0.9 MHz */
    MTDR_CG_5 = 5U,
    /** Min: 0.8 MHz, Max: 1.8 MHz */
    MTDR_CG_6 = 6U,
    /** Min: 1.6 MHz, Max: 3.6 MHz */
    MTDR_CG_7 = 7U,
    /** Min: 3.2 MHz, Max: 7.2 MHz */
    MTDR_CG_8 = 8U,
    /** Min: 6.4 MHz, Max: 14.4 MHz */
    MTDR_CG_9 = 9U,
    /** Min: 12.8 MHz, Max: 28.8 MHz */
    MTDR_CG_10 = 10U
} MTDR_ClockGear;

/** SLIMbus Root Frequency (RF) and Phase Modulus (PM) Codings. */
typedef enum {
    /** RF: Not Indicated, PM: 160 */
    MTDR_RF_0 = 0U,
    /** RF: 24.576 MHz, PM: 160 */
    MTDR_RF_1 = 1U,
    /** RF: 22.5792 MHz, PM: 147 */
    MTDR_RF_2 = 2U,
    /** RF: 15.36 MHz, PM: 100 */
    MTDR_RF_3 = 3U,
    /** RF: 16.8 MHz, PM: 875 */
    MTDR_RF_4 = 4U,
    /** RF: 19.2 MHz, PM: 125 */
    MTDR_RF_5 = 5U,
    /** RF: 24 MHz, PM: 625 */
    MTDR_RF_6 = 6U,
    /** RF: 25 MHz, PM: 15625 */
    MTDR_RF_7 = 7U,
    /** RF: 26 MHz, PM: 8125 */
    MTDR_RF_8 = 8U,
    /** RF: 27 MHz, PM: 5625 */
    MTDR_RF_9 = 9U
} MTDR_RootFrequency;

/** Restart Time Values. */
typedef enum {
    MTDR_RT_FAST_RECOVERY = 0U,
    MTDR_RT_CONSTANT_PHASE_RECOVERY = 1U,
    MTDR_RT_UNSPECIFIED_DELAY = 2U
} MTDR_RestartTime;


/** Requests Logical Address for SLIMbus Device. Called function should return proper [0x00 - 0xEF] and unique Logical Address. */
typedef uint8_t (*MTDR_AssignLogicalAddressHandler)(uint64_t enumerationAddress, MTDR_DeviceClass class);

/** Requests Device Class for assigned Logical Address. Called function should return Device Class. Mandatory. */
typedef MTDR_DeviceClass (*MTDR_DeviceClassHandler)(uint8_t logicalAddress);

/** Callback for manager interrupts. NOTE: If Message Channel Lapse was set to 0, Hardware may incorrectly set MCH_USAGE interrupt. To avoid this issue, disable MCH_USAGE interrupt before changing Subframe Mode. Because MCH_LAPSE = 0 is a rare setting, no software workaround has been implemented in order to avoid driver performance impact. */
typedef void (*MTDR_ManagerInterruptsHandler)(void *pD, MTDR_Interrupt interrupt);

/** Callback for data ports interrupts */
typedef void (*MTDR_DataPortInterruptsHandler)(void *pD, uint8_t dataPortNumber, MTDR_DataPortInterrupt dataPortInterrupt);

/** Callback for receiving SLIMbus Raw Message (before decoding). */
typedef void (*MTDR_ReceivedRawMessageHandler)(void *pD, void *message, uint8_t messageLength);

/** Callback for receiving SLIMbus Message (after decoding). */
typedef void (*MTDR_ReceivedMessageHandler)(void *pD, MTDR_Message *message);

/** Callback for sending SLIMbus Raw Message (after encoding). */
typedef void (*MTDR_SendingRawMessageHandler)(void *pD, void *message, uint8_t messageLength);

/** Callback for sending SLIMbus Message (before encoding). */
typedef void (*MTDR_SendingMessageHandler)(void *pD, MTDR_Message *message);

/** Callback for SLIMbus Reports of Information Elements. */
typedef void (*MTDR_InformationElementsHandler)(void *pD, uint8_t sourceLa, MTDR_InformationElements *informationElements);

/** Called when all messages were sent. */
typedef void (*MTDR_SendingMessagesFinishedHandler)(void *pD);

/** Called when sending message failed. */
typedef void (*MTDR_SendingMessageFailedHandler)(void *pD);

/** Callback for SLIMbus REPORT_PRESENT Message, which is sent by an unenumerated Device to announce its presence on the bus. */
typedef void (*MTDR_MsgReportPresentHandler)(void *pD, uint64_t sourceEa, MTDR_DeviceClass deviceClass, uint8_t deviceClassVersion);

/** Callback for SLIMbus REPORT_ABSENT Message, which shall be sent from a Device to announce that it is about to leave the bus. */
typedef void (*MTDR_MsgReportAbsentHandler)(void *pD, uint8_t sourceLa);

/** Callback for SLIMbus REPLY_INFORMATION Message, which is sent by a Device in response to a REQUEST_INFORMATION or REQUEST_CLEAR_INFORMATION Message. */
typedef void (*MTDR_MsgReplyInformationHandler)(void *pD, uint8_t sourceLa, uint8_t transactionId, uint8_t *informationSlice, uint8_t informationSliceLength);

/** Callback for SLIMbus REPORT_INFORMATION Message, which used by a Device to inform another Device about a change in an Information Slice. */
typedef void (*MTDR_MsgReportInformationHandler)(void *pD, uint8_t sourceLa, uint16_t elementCode, uint8_t *informationSlice, uint8_t informationSliceLength);

/** Callback for SLIMbus REPLY_VALUE Message, which is sent by a Device in response to a REQUEST_VALUE or REQUEST_CHANGE_VALUE Message. */
typedef void (*MTDR_MsgReplyValueHandler)(void *pD, uint8_t sourceLa, uint8_t transactionId, uint8_t *valueSlice, uint8_t valueSliceLength);

/*struct_if*/
/** SLIMbus Message structure definition. */
struct MTDR_Message_s {
    /** Arbitration Type */
    MTDR_ArbitrationType arbitrationType;
    /** Source Address */
    uint64_t sourceAddress;
    /** Arbitration Priority */
    MTDR_ArbitrationPriority arbitrationPriority;
    /** Message Type */
    MTDR_MessageType messageType;
    /** Message Codes are specific to a Message Type. */
    uint8_t messageCode;
    /** Destination Type */
    MTDR_DestinationType destinationType;
    /** Destination Address*/
    uint64_t destinationAddress;
    /** Message Payload */
    uint8_t payload[28];
    /** Message Payload Length */
    uint8_t payloadLength;
    /** Message Response */
    MTDR_MessageResponse response;
};

/** Structure describing configuration of the Framer device implemented in SLIMbus Manager. */
struct MTDR_FramerConfig_s {
    uint16_t rootFrequenciesSupported;
    MTDR_FramerQuality quality;
    bool pauseAtRootFrequencyChange;
};

/** Structure describing configuration of the Generic device and its Data Ports implemented in SLIMbus Manager. */
struct MTDR_GenericDeviceConfig_s {
    uint32_t presenceRatesSupported;
    bool transportProtocolIsochronous;
    bool transportProtocolPushed;
    bool transportProtocolPulled;
    uint8_t sinkStartLevel;
    uint8_t dataPortClockPrescaler;
    uint8_t cportClockDivider;
    MTDR_ReferenceClock referenceClockSelector;
    uint16_t dmaTresholdSource;
    uint16_t dmaTresholdSink;
};

/** State of Information Elements */
struct MTDR_InformationElements_s {
    bool coreExError;
    bool coreReconfigObjection;
    bool coreDataTxCol;
    bool coreUnsprtdMsg;
    bool interfaceDataSlotOverlap;
    bool interfaceLostMs;
    bool interfaceLostSfs;
    bool interfaceLostFs;
    bool interfaceMcTxCol;
    bool managerActiveManager;
    MTDR_FramerQuality framerQuality;
    bool framerGcTxCol;
    bool framerFiTxCol;
    bool framerFsTxCol;
    bool framerActiveFramer;
};

/** Structure describing status of Generic Data Port implemented in the Manager component. */
struct MTDR_DataPortStatus_s {
    bool active;
    bool contentDefined;
    bool channelDefined;
    bool sink;
    bool overflow;
    bool underrun;
    bool dportReady;
    uint16_t segmentInterval;
    MTDR_TransportProtocol transportProtocol;
    MTDR_PresenceRate presenceRate;
    bool frequencyLock;
    MTDR_DataType dataType;
    uint8_t dataLength;
    uint8_t portLinked;
    bool channelLink;
};

/** Configuration parameters passed to probe & init functions. */
struct MTDR_Config_s {
    uintptr_t regBase;
    bool snifferMode;
    bool enableFramer;
    bool enableDevice;
    uint8_t retryLimit;
    bool reportAtEvent;
    bool disableHardwareCrcCalculation;
    bool limitReports;
    uint16_t eaProductId;
    uint8_t eaInstanceValue;
    uint8_t eaInterfaceId;
    uint8_t eaGenericId;
    uint8_t eaFramerId;
    bool enumerateDevices;
};

/** Structure containing pointers to functions defined by user that will be called when specific event occurs. */
struct MTDR_Callbacks_s {
    /** Request for Logical Address. Required if enumerateDevices in MTDR_Config is enabled. */
    MTDR_AssignLogicalAddressHandler onAssignLogicalAddress;
    /** Request for Device Class for Logical Address. Mandatory. */
    MTDR_DeviceClassHandler onDeviceClassRequest;
    /** Callback for SLIMbus Reports of Information Elements. Optional, unused if NULL was assigned. */
    MTDR_InformationElementsHandler onInformationElementReported;
    /** Callback for Manager Interrupts. Optional, unused if NULL was assigned. */
    MTDR_ManagerInterruptsHandler onManagerInterrupt;
    /** Callback for Data Ports Interrupts. Optional, unused if NULL was assigned. */
    MTDR_DataPortInterruptsHandler onDataPortInterrupt;
    /** Callback for Receiving SLIMbus Raw Message (before decoding). Optional, unused if NULL was assigned. */
    MTDR_ReceivedRawMessageHandler onRawMessageReceived;
    /** Callback for receiving SLIMbus Message (after decoding). Optional, unused if NULL was assigned. */
    MTDR_ReceivedMessageHandler onMessageReceived;
    /** Callback for sending SLIMbus Raw Message (after encoding). Optional, unused if NULL was assigned. */
    MTDR_SendingRawMessageHandler onRawMessageSending;
    /** Callback for sending SLIMbus Message (before encoding). Optional, unused if NULL was assigned. */
    MTDR_SendingMessageHandler onMessageSending;
    /** Callback for Messages Sending Finished. Optional, unused if NULL was assigned. */
    MTDR_SendingMessagesFinishedHandler onMessagesSendingFinished;
    /** Callback for Message Sending Failed. Optional, unused if NULL was assigned. */
    MTDR_SendingMessageFailedHandler onMessageSendingFailed;
};

/** Structure containing pointers to functions defined by user that will be called when specific SLIMbus message has been received. */
struct MTDR_MessageCallbacks_s {
    /** Callback for REPORT_PRESENT Message. Unused if NULL was assigned. */
    MTDR_MsgReportPresentHandler onMsgReportPresent;
    /** Callback for REPORT_ABSENT Message. Unused if NULL was assigned. */
    MTDR_MsgReportAbsentHandler onMsgReportAbsent;
    /** Callback for REPLY_INFORMATION Message. Unused if NULL was assigned. */
    MTDR_MsgReplyInformationHandler onMsgReplyInformation;
    /** Callback for REPORT_INFORMATION Message. Unused if NULL was assigned. */
    MTDR_MsgReportInformationHandler onMsgReportInformation;
    /** Callback for REPLY_VALUE Message. Unused if NULL was assigned. */
    MTDR_MsgReplyValueHandler onMsgReplyValue;
};


/**
 * The client may call this to disable the hardware (disabling its IRQ
 * at the source and disconnecting it if applicable).
 */
uint32_t MTDR_Stop(void *pD);

/**
 * This performs an automatic stop and then de-initializes the driver.
 */
uint32_t MTDR_Destroy(void *pD);

/**
 * Sets (enable or disable) SLIMbus Manager interrupt mask.
 */
uint32_t MTDR_SetInterrupts(void *pD, uint8_t interruptMask);

/**
 * Obtains information about SLIMbus Manager enabled interrupts.
 */
uint32_t MTDR_GetInterrupts(void *pD, uint8_t *interruptMask);

/**
 * Sets (enable or disable) Data Port interrupt mask.
 */
uint32_t MTDR_SetDataPortInterrupts(void *pD, uint8_t portNumber, uint8_t interruptMask);

/**
 * Obtains information about Data Port enabled interrupts.
 */
uint32_t MTDR_GetDataPortInterrupts(void *pD, uint8_t portNumber, uint8_t *interruptMask);

/**
 * Clears Data Port FIFO. Used when Data Port FIFO is accessed through
 * AHB.
 */
uint32_t MTDR_ClearDataPortFifo(void *pD, uint8_t portNumber);

/**
 * Sets (enable or disable) Presence Rate Generation.
 */
uint32_t MTDR_SetPresenceRateGeneration(void *pD, uint8_t portNumber, bool enable);

/**
 * Obtains information about Presence Rate Generation.
 */
uint32_t MTDR_GetPresenceRateGeneration(void *pD, uint8_t portNumber, bool *enable);

/**
 * Assigns callbacks for receiving SLIMbus messages
 */
uint32_t MTDR_AssignMessageCallbacks(void *pD, MTDR_MessageCallbacks *msgCallbacks);

/**
 * Send Raw SLIMbus Message (bytes)
 */
uint32_t MTDR_SendRawMessage(void *pD, void *message, uint8_t messageLength);

/**
 * Send SLIMbus message.
 */
uint32_t MTDR_SendMessage(void *pD, MTDR_Message *message);

/**
 * Read contents of SLIMbus Manager's register. Register Address must
 * be aligned to 32-bits.
 */
uint32_t MTDR_GetRegisterValue(void *pD, uint16_t regAddress, uint32_t *regContent);

/**
 * Write contents to SLIMbus Manager's register. Register Address must
 * be aligned to 32-bits.
 */
uint32_t MTDR_SetRegisterValue(void *pD, uint16_t regAddress, uint32_t regContent);

/**
 * Sets message channel Lapse.
 */
uint32_t MTDR_SetMessageChannelLapse(void *pD, uint8_t mchLapse);

/**
 * Returns current value of message channel Lapse.
 */
uint32_t MTDR_GetMessageChannelLapse(void *pD, uint8_t *mchLapse);

/**
 * Returns current value of message channel usage.
 */
uint32_t MTDR_GetMessageChannelUsage(void *pD, uint16_t *mchUsage);

/**
 * Returns current value of message channel capacity.
 */
uint32_t MTDR_GetMessageChannelCapacity(void *pD, uint16_t *mchCapacity);

/**
 * Enable or Disable Sniffer Mode. If 1, the Sniffer functionality is
 * enabled.
 */
uint32_t MTDR_SetSnifferMode(void *pD, bool state);

/**
 * Returns state of Sniffer Mode feature.
 */
uint32_t MTDR_GetSnifferMode(void *pD, bool *state);

/**
 * Enable or Disable internal Framer. If 1, the internal Framer device
 * is enabled. If set 0, the internal Framer device is disabled.
 */
uint32_t MTDR_SetFramerEnabled(void *pD, bool state);

/**
 * Returns state of internal Framer.
 */
uint32_t MTDR_GetFramerEnabled(void *pD, bool *state);

/**
 * Enable or Disable internal Generic Device. If 1, the internal
 * Generic device is enabled. If set 0, the internal Generic device is
 * disabled.
 */
uint32_t MTDR_SetDeviceEnabled(void *pD, bool state);

/**
 * Returns state of internal Generic Device.
 */
uint32_t MTDR_GetDeviceEnabled(void *pD, bool *state);

/**
 * If set to 1, then whole SLIMbus Manager will be detached from
 * SLIMbus.
 */
uint32_t MTDR_SetGoAbsent(void *pD, bool state);

/**
 * Informs if SLIMbus Manager Component was set to Go Absent.
 */
uint32_t MTDR_GetGoAbsent(void *pD, bool *state);

/**
 * Sets Framer configuration.
 */
uint32_t MTDR_SetFramerConfig(void *pD, MTDR_FramerConfig *framerConfig);

/**
 * Obtains Framer configuration.
 */
uint32_t MTDR_GetFramerConfig(void *pD, MTDR_FramerConfig *framerConfig);

/**
 * Sets Generic Device configuration.
 */
uint32_t MTDR_SetGenericDeviceConfig(void *pD, MTDR_GenericDeviceConfig *genericDeviceConfig);

/**
 * Obtains Generic Device configuration.
 */
uint32_t MTDR_GetGenericDeviceConfig(void *pD, MTDR_GenericDeviceConfig *genericDeviceConfig);

/**
 * Obtains status of Data port implemented in the Generic Device in
 * Manager component.
 */
uint32_t MTDR_GetDataPortStatus(void *pD, uint8_t portNumber, MTDR_DataPortStatus *portStatus);

/**
 * When the IP is in PAUSE state, and the Framer function is enabled,
 * then calling this function forces to generate a toggle at SLIMbus
 * data line, what in turn wakes-up SLIMbus from PAUSE state.
 */
uint32_t MTDR_Unfreeze(void *pD);

/**
 * Cancels pending configuration (clears CFG_STROBE).
 */
uint32_t MTDR_CancelConfiguration(void *pD);

/**
 * Returns status of Manager Synchronization with SLIMbus
 */
uint32_t MTDR_GetStatusSynchronization(void *pD, bool *fSync, bool *sfSync, bool *mSync, bool *sfbSync, bool *phSync);

/**
 * Informs if SLIMbus Manager is detached from the bus after
 * successful transmission of REPORT_ABSENT message.
 */
uint32_t MTDR_GetStatusDetached(void *pD, bool *detached);

/**
 * Informs if all Messages have been send and TX_FIFO is empty.
 */
uint32_t MTDR_GetStatusMessages(void *pD, bool *sendingFinished);

/**
 * Returns information about SLIMbus operation properties
 */
uint32_t MTDR_GetStatusSlimbus(void *pD, MTDR_SubframeMode *subframeMode, MTDR_ClockGear *clockGear, MTDR_RootFrequency *rootFr);

/**
 * Sends ASSIGN_LOGICAL_ADDRESS Message, which assigns a Logical
 * Address to a Device.
 */
uint32_t MTDR_MsgAssignLogicalAddress(void *pD, uint64_t destinationEa, uint8_t newLa);

/**
 * Sends RESET_DEVICE Message, which informs a Device to perform its
 * reset procedure.
 */
uint32_t MTDR_MsgResetDevice(void *pD, uint8_t destinationLa);

/**
 * Sends CHANGE_LOGICAL_ADDRESS Message, which changes the value of
 * the Logical Address of the destination Device.
 */
uint32_t MTDR_MsgChangeLogicalAddress(void *pD, uint8_t destinationLa, uint8_t newLa);

/**
 * Sends CHANGE_ARBITRATION_PRIORITY Message, which is sent to one or
 * more Devices to change the value of their Arbitration Priority.
 */
uint32_t MTDR_MsgChangeArbitrationPriority(void *pD, bool broadcast, uint8_t destinationLa, MTDR_ArbitrationPriority newArbitrationPriority);

/**
 * Sends REQUEST_SELF_ANNOUNCEMENT Message, which requests an
 * unenumerated Device retransmit a REPORT_PRESENT Message.
 */
uint32_t MTDR_MsgRequestSelfAnnouncement(void *pD);

/**
 * Sends CONNECT_SOURCE Message, which informs the Device to connect
 * the specified Port, to the specified Data Channel. The Port shall
 * act as the data source for the Data Channel.
 */
uint32_t MTDR_MsgConnectSource(void *pD, uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber);

/**
 * Sends CONNECT_SINK Message, which informs the Device to connect the
 * specified Port, to the specified Data Channel. The Port shall act
 * as the data sink for the Data Channel.
 */
uint32_t MTDR_MsgConnectSink(void *pD, uint8_t destinationLa, uint8_t portNumber, uint8_t channelNumber);

/**
 * Sends DISCONNECT_PORT Message, which informs the Device to
 * disconnect the Port specified by Port Number.
 */
uint32_t MTDR_MsgDisconnectPort(void *pD, uint8_t destinationLa, uint8_t portNumber);

/**
 * Sends CHANGE_CONTENT Message, which broadcasts detailed information
 * about the structure of the Data Channel contents.
 */
uint32_t MTDR_MsgChangeContent(void *pD, uint8_t channelNumber, bool frequencyLockedBit, MTDR_PresenceRate presenceRate, MTDR_AuxFieldFormat auxiliaryBitFormat, MTDR_DataType dataType, bool channelLink, uint8_t dataLength);

/**
 * Sends REQUEST_INFORMATION Message, which instructs a Device to send
 * the indicated Information Slice.
 */
uint32_t MTDR_MsgRequestInformation(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode);

/**
 * Sends REQUEST_CLEAR_INFORMATION Message, which instructs a Device
 * to send the indicated Information Slice and to clear all, or parts,
 * of that Information Slice.
 */
uint32_t MTDR_MsgRequestClearInformation(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode, uint8_t *clearMask, uint8_t clearMaskSize);

/**
 * Sends CLEAR_INFORMATION Message, which instructs a Device to
 * selectively clear all, or parts, of the Information Slice.
 */
uint32_t MTDR_MsgClearInformation(void *pD, bool broadcast, uint8_t destinationLa, uint16_t elementCode, uint8_t *clearMask, uint8_t clearMaskSize);

/**
 * Sends BEGIN_RECONFIGURATION Message, which informs all Devices of
 * the start of a Reconfiguration Sequence.
 */
uint32_t MTDR_MsgBeginReconfiguration(void *pD);

/**
 * Sends Message NEXT_ACTIVE_FRAMER, which informs a Device that the
 * active Framer is going to hand over the role of Framer to a
 * specified inactive Framer with the Logical Address.
 */
uint32_t MTDR_MsgNextActiveFramer(void *pD, uint8_t incomingFramerLa, uint16_t outgoingFramerClockCycles, uint16_t incomingFramerClockCycles);

/**
 * Sends Message NEXT_SUBFRAME_MODE, which informs all destinations
 * that the active Manager intends to change the Subframe Mode.
 */
uint32_t MTDR_MsgNextSubframeMode(void *pD, MTDR_SubframeMode newSubframeMode);

/**
 * Sends NEXT_CLOCK_GEAR Message, which informs all destinations that
 * the active Manager intends to change the Clock Gear.
 */
uint32_t MTDR_MsgNextClockGear(void *pD, MTDR_ClockGear newClockGear);

/**
 * Sends NEXT_ROOT_FREQUENCY Message, which informs all destinations
 * that the active Manager intends to change the Root Frequency.
 */
uint32_t MTDR_MsgNextRootFrequency(void *pD, MTDR_RootFrequency newRootFrequency);

/**
 * Sends NEXT_PAUSE_CLOCK Message, which informs all destinations that
 * the active Manager intends to have the active Framer pause the bus.
 */
uint32_t MTDR_MsgNextPauseClock(void *pD, MTDR_RestartTime newRestartTime);

/**
 * Sends NEXT_RESET_BUS Message, which informs all destinations that
 * the active Manager intends to have the active Framer reset the bus.
 */
uint32_t MTDR_MsgNextResetBus(void *pD);

/**
 * Sends NEXT_SHUTDOWN_BUS Message, which informs all destinations
 * that the active Manager intends to shutdown the bus.
 */
uint32_t MTDR_MsgNextShutdownBus(void *pD);

/**
 * Sends NEXT_DEFINE_CHANNEL Message, which informs a Device that the
 * active Manager intends to define the parameters of a Data Channel.
 */
uint32_t MTDR_MsgNextDefineChannel(void *pD, uint8_t channelNumber, MTDR_TransportProtocol transportProtocol, uint16_t segmentDistribution, uint8_t segmentLength);

/**
 * Sends NEXT_DEFINE_CONTENT Message, which informs a Device how the
 * Data Channel CN shall be used.
 */
uint32_t MTDR_MsgNextDefineContent(void *pD, uint8_t channelNumber, bool frequencyLockedBit, MTDR_PresenceRate presenceRate, MTDR_AuxFieldFormat auxiliaryBitFormat, MTDR_DataType dataType, bool channelLink, uint8_t dataLength);

/**
 * Sends NEXT_ACTIVATE_CHANNEL Message, which is used to switch on the
 * specified Data Channel.
 */
uint32_t MTDR_MsgNextActivateChannel(void *pD, uint8_t channelNumber);

/**
 * Sends NEXT_DEACTIVATE_CHANNEL Message, which is used to switch off
 * the specified Data Channel.
 */
uint32_t MTDR_MsgNextDeactivateChannel(void *pD, uint8_t channelNumber);

/**
 * Sends NEXT_REMOVE_CHANNEL Message, which is used to switch off and
 * disconnect the specified Data Channel.
 */
uint32_t MTDR_MsgNextRemoveChannel(void *pD, uint8_t channelNumber);

/**
 * Sends RECONFIGURE_NOW Message, which is used to change the bus
 * configuration.
 */
uint32_t MTDR_MsgReconfigureNow(void *pD);

/**
 * Sends REQUEST_VALUE Message, which instructs a Device to send the
 * indicated Value Slice.
 */
uint32_t MTDR_MsgRequestValue(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode);

/**
 * Sends REQUEST_CHANGE_VALUE Message, which instructs a Device to
 * send the specified Value Slice and to update that Value Slice.
 */
uint32_t MTDR_MsgRequestChangeValue(void *pD, bool broadcast, uint8_t destinationLa, uint8_t transactionId, uint16_t elementCode, uint8_t *valueUpdate, uint8_t valueUpdateSize);

/**
 * Sends CHANGE_VALUE Message, which instructs a Device to update the
 * indicated Value Slice.
 */
uint32_t MTDR_MsgChangeValue(void *pD, bool broadcast, uint8_t destinationLa, uint16_t elementCode, uint8_t *valueUpdate, uint8_t valueUpdateSize);

uint32_t MTDR_Dport_rx(void *pD, uint8_t portNumber, uint16_t *pdata);
uint32_t MTDR_Dport_tx(void *pD, uint8_t portNumber, uint16_t data);

#endif	/* SLIM_MTDR_IF_H */
