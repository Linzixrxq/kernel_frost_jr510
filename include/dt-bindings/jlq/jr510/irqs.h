/*
* include/dt-bindings/leadcore/lc1861/irqs.h
*
* Copyright (c) 2015-2017   LeadCoreTech Co.,Ltd
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
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*
*/

#ifndef __DT_BINDINGS_JLQ_IRQS_HH
#define __DT_BINDINGS_JLQ_IRQS_HH

/* Primary Interrupt Controller */
#define INT_GPU_JOB		0
#define INT_GPU_MMU		1
#define INT_GPU_GPU		2
#define INT_GPU_IRQEVENT	3
#define INT_VPU			4
#define INT_QTANG_ABORT_RST	5
#define INT_AI			6
#define INT_CETI_INTR		7
#define INT_CAMILLE		8
#define INT_DSI			9
#define INT_TOP_SYS_CTL		10
#define INT_TOP_LPM		11
#define INT_TOP_CRG		12
#define INT_MBOX_CM42AP		13
#define INT_PVT_INTR		14
#define INT_TIMER0		24
#define INT_TIMER1		25
#define INT_WDT0_BARK		26
#define INT_WDT0_BITE		27
#define INT_WDT1_BITE		29
#define INT_WDT2_BITE		30
#define INT_WDT3_BITE		31
#define INT_AP_DMAS		32
#define INT_TOP_DMAS		33
#define INT_DDR0_INTR		34
#define INT_DDR1_INTR		35
#define INT_USB0		36
#define INT_SDMMC0		37
#define INT_SDIO0		38
#define INT_SDIO1		39
#define INT_CIPHER		41
#define INT_TZC400_INTR0	42
#define INT_TZC400_INTR1	43
#define INT_UART0		44
#define INT_UART1		45
#define INT_UART2		46
#define INT_COM_UART		47
#define INT_I2C3		51
#define INT_I2C4		52
#define INT_I2C5		53
#define INT_I2C6		54
#define INT_SSI0		55
#define INT_SSI1		56
#define INT_SSI2		57
#define INT_GPIO		58
#define INT_I2S0		60
#define INT_I2S1		61
#define INT_NPL_RX_WAKE		62
#define INT_NPL_WSA_WAKE	63
#define INT_MAILBOX_APSEC	84
#define INT_EMMC_DLL		85
#define INT_SD0_WAKEUP		86
#define INT_SD1_WAKEUP		87
#define INT_SMMU_CMD_SYNC_NS	88
#define INT_SMMU_CMD_SYNC	89
#define INT_SMMU_EVENT_Q_NS	90
#define INT_SMMU_EVENT_Q	91
#define INT_SMMU_GERROR_NS	92
#define INT_SMMU_GERROR		93
#define INT_SMMU_PMU_IRQ	94
#define INT_PRI_Q_IRQ_NS	96
#define INT_TBU0_PMU_IRQ	97
#define INT_TBU1_PMU_IRQ	99
#define INT_TBU2_PMU_IRQ	101
#define INT_GIC_PMU_IRQ		103
#define INT_BUSMON		104
#define INT_DSU_PMU_IRQ		105
#define INT_BOLERO_WSA_SWR	106
#define INT_BOLERO_RX_SWR	107
#define INT_BOLERO_VA_SWR	108
#define INT_BOLERO_VA		109
#define INT_AUDIO_DMAS0		110
#define INT_AUDIO_DMAS1		111
#define INT_USB_WAKEUP		112
#define INT_MBOX_ADSP2AP	113
#define INT_MBOX_VDSP2AP	114
#define INT_UART1_RX_WAKEUP	115
#define INT_QT_CRASH		116
#define INT_CAM_TFE_0_CSID      130
#define INT_CAM_TFE_0           131
#define INT_CAM_TFE_1_CSID      132
#define INT_CAM_TFE_1           133
#define INT_CAM_TFE_2_CSID      134
#define INT_CAM_TFE_2           135
#define INT_CAM_CAMNOC          136
#define INT_CAM_TPG0            137
#define INT_CAM_TPG1            138
#define INT_CAM_CCI0            139
#define INT_CAM_CDM             140
#define INT_CAM_OPE_CDM         141
#define INT_CAM_OPE             142
#define INT_MIPI_CSI_3PHASE_0    149
#define INT_MIPI_CSI_3PHASE_1    150
#define INT_MIPI_CSI_3PHASE_2    151

#define INT_SLIMBUS		214
#define INT_CM4_TO_A55_IRQ0	215
#define INT_CM4_TO_A55_IRQ1	216
#define INT_CM4_TO_A55_IRQ2	217

/* the irq's source/destination CPU */
#define TO_A55          0
#define TO_CM4          1
#define TO_ADSP         2
#define TO_VDSP         3
#define TO_CM4_LPM      4

#endif /* DT_BINDINGS_JLQ_IRQS_H*/
