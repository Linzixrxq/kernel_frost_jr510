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

#ifndef SLIM_CHK_H
#define SLIM_CHK_H

#include "slim_mtdr_if.h"


uint32_t MTDR_ReadReg32(volatile uint32_t *address);
void MTDR_WriteReg32(volatile uint32_t *address, uint32_t value);
uint8_t MTDR_UncachedRead8(volatile uint8_t *address);
uint16_t MTDR_UncachedRead16(volatile uint16_t *address);
uint32_t MTDR_UncachedRead32(volatile uint32_t *address);
void MTDR_UncachedWrite8(volatile uint8_t *address, uint8_t value);
void MTDR_UncachedWrite16(volatile uint16_t *address, uint16_t value);
void MTDR_UncachedWrite32(volatile uint32_t *address, uint32_t value);
void MTDR_BufferCopy(volatile uint8_t *dst, volatile const uint8_t *src, uint32_t size);
void MTDR_CacheInvalidate(void *address, size_t size, uintptr_t devInfo);
void MTDR_CacheFlush(void *address, size_t size, uintptr_t devInfo);
void MTDR_DelayNs(uint32_t ns);
void MTDR_MemoryBarrier(void);


uint32_t MTDR_CallbacksSF(const MTDR_Callbacks *obj);
uint32_t MTDR_ConfigSF(const MTDR_Config *obj);
uint32_t MTDR_FramerConfigSF(const MTDR_FramerConfig *obj);
uint32_t MTDR_GenericDeviceConfigSF(const MTDR_GenericDeviceConfig *obj);
uint32_t MTDR_MessageCallbacksSF(const MTDR_MessageCallbacks *obj);
uint32_t MTDR_MessageSF(const MTDR_Message *obj);

uint32_t MTDR_ParasChk1(const MTDR_Config *config, const uint16_t *requiredMemory);
uint32_t MTDR_ParasChk2(const void *pD, const MTDR_Config *config, const MTDR_Callbacks *callbacks);
uint32_t MTDR_ParasChk3(const void *pD);
uint32_t MTDR_ParasChk8(const void *pD, const uint8_t *interruptMask);
uint32_t MTDR_ParasChk9(const void *pD, const uint8_t portNumber, const uint8_t interruptMask);
uint32_t MTDR_ParasChk10(const void *pD, const uint8_t portNumber, const uint8_t *interruptMask);
uint32_t MTDR_ParasChk11(const void *pD, const uint8_t portNumber);
uint32_t MTDR_ParasChk13(const void *pD, const uint8_t portNumber, const bool *enable);
uint32_t MTDR_ParasChk14(const void *pD, const MTDR_MessageCallbacks *msgCallbacks);
uint32_t MTDR_ParasChk15(const void *pD, const void *message, const uint8_t messageLength);
uint32_t MTDR_ParasChk16(const void *pD, const MTDR_Message *message);
uint32_t MTDR_ParasChk17(const void *pD, const uint32_t *regContent);
uint32_t MTDR_ParasChk19(const void *pD, const uint8_t mchLapse);
uint32_t MTDR_ParasChk21(const void *pD, const uint16_t *mchUsage);
uint32_t MTDR_ParasChk24(const void *pD, const bool *state);
uint32_t MTDR_ParasChk31(const void *pD, const MTDR_FramerConfig *framerConfig);
uint32_t MTDR_ParasChk32(const void *pD, const MTDR_FramerConfig *framerConfig);
uint32_t MTDR_ParasChk33(const void *pD, const MTDR_GenericDeviceConfig *genericDeviceConfig);
uint32_t MTDR_ParasChk34(const void *pD, const MTDR_GenericDeviceConfig *genericDeviceConfig);
uint32_t MTDR_ParasChk35(const void *pD, const uint8_t portNumber, const MTDR_DataPortStatus *portStatus);
uint32_t MTDR_ParasChk42(const void *pD, const uint8_t newLa);
uint32_t MTDR_ParasChk45(const void *pD, const MTDR_ArbitrationPriority newArbitrationPriority);
uint32_t MTDR_ParasChk50(const void *pD, const uint8_t channelNumber, const MTDR_PresenceRate presenceRate, const MTDR_AuxFieldFormat auxiliaryBitFormat, const MTDR_DataType dataType, const uint8_t dataLength);
uint32_t MTDR_ParasChk55(const void *pD, const uint8_t incomingFramerLa, const uint16_t outgoingFramerClockCycles, const uint16_t incomingFramerClockCycles);
uint32_t MTDR_ParasChk56(const void *pD, const MTDR_SubframeMode newSubframeMode);
uint32_t MTDR_ParasChk57(const void *pD, const MTDR_ClockGear newClockGear);
uint32_t MTDR_ParasChk58(const void *pD, const MTDR_RootFrequency newRootFrequency);
uint32_t MTDR_ParasChk59(const void *pD, const MTDR_RestartTime newRestartTime);
uint32_t MTDR_ParasChk62(const void *pD, const uint8_t channelNumber, const MTDR_TransportProtocol transportProtocol, const uint16_t segmentDistribution, const uint8_t segmentLength);
uint32_t MTDR_ParasChk69(const void *pD, const uint8_t valueUpdateSize);

#define	MTDR_ProbeSF MTDR_ParasChk1
#define	MTDR_InitSF MTDR_ParasChk2
#define	MTDR_IsrSF MTDR_ParasChk3
#define	MTDR_StartSF MTDR_ParasChk3
#define	MTDR_StopSF MTDR_ParasChk3
#define	MTDR_DestroySF MTDR_ParasChk3
#define	MTDR_SetInterruptsSF MTDR_ParasChk3
#define	MTDR_GetInterruptsSF MTDR_ParasChk8
#define	MTDR_SetDataPortInterruptsSF MTDR_ParasChk9
#define	MTDR_GetDataPortInterruptsSF MTDR_ParasChk10
#define	MTDR_ClearDataPortFifoSF MTDR_ParasChk11
#define	MTDR_SetPresenceRateGeneratiSF MTDR_ParasChk11
#define	MTDR_GetPresenceRateGeneratiSF MTDR_ParasChk13
#define	MTDR_AssignMessageCallbacksSF MTDR_ParasChk14
#define	MTDR_SendRawMessageSF MTDR_ParasChk15
#define	MTDR_SendMessageSF MTDR_ParasChk16
#define	MTDR_GetRegisterValueSF MTDR_ParasChk17
#define	MTDR_SetRegisterValueSF MTDR_ParasChk3
#define	MTDR_SetMessageChannelLapseSF MTDR_ParasChk19
#define	MTDR_GetMessageChannelLapseSF MTDR_ParasChk8
#define	MTDR_GetMessageChannelUsageSF MTDR_ParasChk21
#define	MTDR_GetMessageChannelCapaciSF MTDR_ParasChk21
#define	MTDR_SetSnifferModeSF MTDR_ParasChk3
#define	MTDR_GetSnifferModeSF MTDR_ParasChk24
#define	MTDR_SetFramerEnabledSF MTDR_ParasChk3
#define	MTDR_GetFramerEnabledSF MTDR_ParasChk24
#define	MTDR_SetDeviceEnabledSF MTDR_ParasChk3
#define	MTDR_GetDeviceEnabledSF MTDR_ParasChk24
#define	MTDR_SetGoAbsentSF MTDR_ParasChk3
#define	MTDR_GetGoAbsentSF MTDR_ParasChk24
#define	MTDR_SetFramerConfigSF MTDR_ParasChk31
#define	MTDR_GetFramerConfigSF MTDR_ParasChk32
#define	MTDR_SetGenericDeviceConfigSF MTDR_ParasChk33
#define	MTDR_GetGenericDeviceConfigSF MTDR_ParasChk34
#define	MTDR_GetDataPortStatusSF MTDR_ParasChk35
#define	MTDR_UnfreezeSF MTDR_ParasChk3
#define	MTDR_CancelConfigurationSF MTDR_ParasChk3
#define	MTDR_GetStatusSynchronizatioSF MTDR_ParasChk3
#define	MTDR_GetStatusDetachedSF MTDR_ParasChk24
#define	MTDR_GetStatusMessagesSF MTDR_ParasChk24
#define	MTDR_GetStatusSlimbusSF MTDR_ParasChk3
#define	MTDR_MsgAssignLogicalAddressSF MTDR_ParasChk42
#define	MTDR_MsgResetDeviceSF MTDR_ParasChk3
#define	MTDR_MsgChangeLogicalAddressSF MTDR_ParasChk42
#define	MTDR_MsgChangeArbitrationPriSF MTDR_ParasChk45
#define	MTDR_MsgRequestSelfAnnouncemSF MTDR_ParasChk3
#define	MTDR_MsgConnectSourceSF MTDR_ParasChk11
#define	MTDR_MsgConnectSinkSF MTDR_ParasChk11
#define	MTDR_MsgDisconnectPortSF MTDR_ParasChk11
#define	MTDR_MsgChangeContentSF MTDR_ParasChk50
#define	MTDR_MsgRequestInformationSF MTDR_ParasChk3
#define	MTDR_MsgRequestClearInformatSF MTDR_ParasChk3
#define	MTDR_MsgClearInformationSF MTDR_ParasChk3
#define	MTDR_MsgBeginReconfigurationSF MTDR_ParasChk3
#define	MTDR_MsgNextActiveFramerSF MTDR_ParasChk55
#define	MTDR_MsgNextSubframeModeSF MTDR_ParasChk56
#define	MTDR_MsgNextClockGearSF MTDR_ParasChk57
#define	MTDR_MsgNextRootFrequencySF MTDR_ParasChk58
#define	MTDR_MsgNextPauseClockSF MTDR_ParasChk59
#define	MTDR_MsgNextResetBusSF MTDR_ParasChk3
#define	MTDR_MsgNextShutdownBusSF MTDR_ParasChk3
#define	MTDR_MsgNextDefineChannelSF MTDR_ParasChk62
#define	MTDR_MsgNextDefineContentSF MTDR_ParasChk50
#define	MTDR_MsgNextActivateChannelSF MTDR_ParasChk3
#define	MTDR_MsgNextDeactivateChanneSF MTDR_ParasChk3
#define	MTDR_MsgNextRemoveChannelSF MTDR_ParasChk3
#define	MTDR_MsgReconfigureNowSF MTDR_ParasChk3
#define	MTDR_MsgRequestValueSF MTDR_ParasChk3
#define	MTDR_MsgRequestChangeValueSF MTDR_ParasChk69
#define	MTDR_MsgChangeValueSF MTDR_ParasChk69

#endif

