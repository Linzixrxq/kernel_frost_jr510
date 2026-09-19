/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef SGM4154X_H_
#define SGM4154X_H_

#define TRIGGER_CHARGE_TYPE_DETECTION 1

#define DEFAULT_ILIMIT	3000
#define DEFAULT_VLIMIT		4600
#define DEFAULT_DCP_VLIMIT      4500
#define DEFAULT_VLIMIT_OFFSET		500
#define DEFAULT_CC			2000
#define DEFAULT_CV			4400
#define DEFAULT_IPRECHG		540
#define DEFAULT_ITERM		180
#define DEFAULT_VSYS_MIN	3500
#define DEFAULT_RECHG		100
#define DEFAULT_V_WAKEUP	2900
#define DEFAULT_IR_MHOM		40
#define DEFAULT_COMP_MAX	32

#define SGM4154X_REG_NUM 16

#define SGM4154X_R00		0x00
#define SGM4154X_R01		0x01
#define SGM4154X_R02		0x02
#define SGM4154X_R03		0x03
#define SGM4154X_R04		0x04
#define SGM4154X_R05		0x05
#define SGM4154X_R06		0x06
#define SGM4154X_R07		0x07
#define SGM4154X_R08		0x08
#define SGM4154X_R09		0x09
#define SGM4154X_R0A		0x0A
#define SGM4154X_R0B		0x0B
#define SGM4154X_R0C		0x0C
#define SGM4154X_R0D		0x0D
#define SGM4154X_R0E		0x0E
#define SGM4154X_R0F		0x0F
#define SGM4154X_R10		0x10

#define SGM4154X_COMPINENT_ID		0x0100
#define SGM4154XD_COMPINENT_ID		0x01A3
#define SGM4154XAD_COMPINENT_ID		0x0100
#define SGM4154XAD1_COMPINENT_ID		0x02A1

/* CON0 */
#define CON0_EN_HIZ_MASK	0x1
#define CON0_EN_HIZ_SHIFT	7

#define CON0_EN_ICHG_MON_MASK	0x3
#define CON0_EN_ICHG_MON_SHIFT	5

#define CON0_IINLIM_MASK	0x1F
#define CON0_IINLIM_SHIFT	0

#define INPUT_CURRT_STEP	((uint32_t)100)
#define INPUT_CURRT_MAX		((uint32_t)3200)
#define INPUT_CURRT_MIN		((uint32_t)100)

/* CON1 */
#define CON1_WD_MASK		0x1
#define CON1_WD_SHIFT		6

#define CON1_OTG_CONFIG_MASK		0x1
#define CON1_OTG_CONFIG_SHIFT		5

#define CON1_CHG_CONFIG_MASK		0x1
#define CON1_CHG_CONFIG_SHIFT		4

#define CON1_SYS_V_LIMIT_MASK		0x7
#define CON1_SYS_V_LIMIT_SHIFT		1
#define CON1_SYS_V_LIMIT_MIN		((uint32_t)3000)
#define CON1_SYS_V_LIMIT_MAX		((uint32_t)3700)
#define CON1_SYS_V_LIMIT_STEP		((uint32_t)100)

#define CON1_MIN_VBAT_SEL_MASK		0x1
#define CON1_MIN_VBAT_SEL_SHIFT		0

#define SYS_VOL_STEP		((uint32_t)100)
#define SYS_VOL_MIN		((uint32_t)3000)
#define SYS_VOL_MAX		((uint32_t)3700)

/* CON2 */
#define CON2_BOOST_ILIM_MASK		0x1
#define CON2_BOOST_ILIM_SHIFT		7

#define CON2_ICHG_MASK		0x3F
#define CON2_ICHG_SHIFT		0
#define ICHG_CURR_STEP		((uint32_t)60)
#define ICHG_CURR_MIN		((uint32_t)0)
#define ICHG_CURR_MAX		((uint32_t)3000)

/* CON3 */
#define CON3_IPRECHG_MASK	0xF
#define CON3_IPRECHG_SHIFT	4

#define CON3_ITERM_MASK		0xF
#define CON3_ITERM_SHIFT	0

#define	EOC_CURRT_STEP		((uint32_t)60)
#define	EOC_CURRT_MAX		((uint32_t)780)
#define	EOC_CURRT_MIN		((uint32_t)60)

#define IPRECHG_CURRT_STEP	((uint32_t)60)
#define IPRECHG_CURRT_MAX	((uint32_t)780)
#define IPRECHG_CURRT_MIN	((uint32_t)60)

/* CON4 */
#define CON4_VREG_MASK		0x1F
#define CON4_VREG_SHIFT		3

#define CON4_TOPOFF_TIME_MASK		0x3
#define CON4_TOPOFF_TIME_SHIFT		1

#define CON4_VRECHG_MASK	0x1
#define CON4_VRECHG_SHIFT	0

//cv
#define VREG_VOL_STEP		((uint32_t)32)
#define VREG_VOL_MIN		((uint32_t)3856)
#define VREG_VOL_MAX		((uint32_t)4624)

//rechg vol
#define VRCHG_VOL_STEP		((uint32_t)100)
#define VRCHG_VOL_MIN		((uint32_t)100)
#define VRCHG_VOL_MAX		((uint32_t)200)

/* CON5 */
#define CON5_EN_TERM_CHG_MASK	0x1
#define CON5_EN_TERM_CHG_SHIFT	7

#define CON5_WTG_TIM_SET_MASK		0x3
#define CON5_WTG_TIM_SET_SHIFT	4

#define CON5_EN_TIMER_MASK		0x1
#define CON5_EN_TIMER_SHIFT		3

#define CON5_SET_CHG_TIM_MASK	0x1
#define CON5_SET_CHG_TIM_SHIFT	2

#define CON5_TREG_MASK		0x1
#define CON5_TREG_SHIFT	1

#define CON5_JEITA_ISET_MASK		0x1
#define CON5_JEITA_ISET_SHIFT		0

/* CON6 */
#define CON6_OVP_MASK		0x3
#define CON6_OVP_SHIFT		6
#define OVP_VOL_MIN		((uint32_t)5500)
#define OVP_VOL_MAX		((uint32_t)14000)

#define CON6_BOOST_VLIM_MASK		0x3
#define CON6_BOOST_VLIM_SHIFT		4
#define BOOST_VOL_STEP		((uint32_t)150)
#define BOOST_VOL_MIN		((uint32_t)4850)
#define BOOST_VOL_MAX		((uint32_t)5300)

#define CON6_VINDPM_MASK	0xF
#define CON6_VINDPM_SHIFT	0
#define VINDPM_VOL_STEP		((uint32_t)100)
#define VINDPM_VOL_MIN		((uint32_t)3900)
#define VINDPM_VOL_MAX		((uint32_t)5400)

/* CON7 */
#define FORCE_IINDET_MASK	0x1
#define FORCE_IINDET_SHIFT	7

#define TMR2X_MASK		0x1
#define TMR2X_SHIFT		6

#define BATFET_DIS_MASK	0x1
#define BATFET_DIS_SHIFT	5

#define JEITA_VSET_MASK	0x1
#define JEITA_VSET_SHIFT	 4

#define BATFET_DLY_MASK		0x1
#define BATFET_DLY_SHIFT	3

#define BATFET_RST_EN_MASK	0x1
#define BATFET_RST_EN_SHIFT	2

#define VDPM_BAT_TRACK_MASK	0x1
#define VDPM_BAT_TRACK_SHIFT	0

/* CON8 */
#define CON8_VBUS_STAT_MASK	0x7
#define CON8_VBUS_STAT_SHIFT	5

#define CON8_CHRG_STAT_MASK	0x3
#define CON8_CHRG_STAT_SHIFT	3

#define CON8_PG_STAT_MASK	0x1
#define CON8_PG_STAT_SHIFT	2

#define CON8_THM_STAT_MASK	0x1
#define CON8_THM_STAT_SHIFT	1

#define CON8_VSYS_STAT_MASK	0x1
#define CON8_VSYS_STAT_SHIFT	 0

/* CON9 */
#define CON9_WATG_STAT_MASK	0x1
#define CON9_WATG_STAT_SHIFT	7

#define CON9_BOOST_STAT_MASK	0x1
#define CON9_BOOST_STAT_SHIFT	6

#define CON9_CHRG_FAULT_MASK	0x3
#define CON9_CHRG_FAULT_SHIFT	4

#define CON9_BAT_STAT_MASK	0x1
#define CON9_BAT_STAT_SHIFT	3

#define CON9_NTC_STAT_MASK	0x7
#define CON9_NTC_STAT_SHIFT	0

/* CONA */
#define CONA_VBUS_GD_MASK	0x1
#define CONA_VBUS_GD_SHIFT	7

#define CONA_VINDPM_STAT_MASK	0x1
#define CONA_VINDPM_STAT_SHIFT	6

#define CONA_IDPM_STAT_MASK	0x1
#define CONA_IDPM_STAT_SHIFT	5

#define CONA_TOPOFF_ACTIVE_MASK	0x1
#define CONA_TOPOFF_ACTIVE_SHIFT	3

#define CONA_ACOV_STAT_MASK	0x1
#define CONA_ACOV_STAT_SHIFT	2

#define	CONA_VINDPM_INT_MASK		0x1
#define	CONA_VINDPM_INT_SHIFT	1

#define	CONA_IINDPM_INT_MASK		0x1
#define	CONA_IINDPM_INT_SHIFT		0

/*CONB*/
#define CONB_REG_RST_MASK	0x1
#define CONB_REG_RST_SHIFT	7

#define CONB_PN_MASK				0x0F
#define CONB_PN_SHIFT			3

#define CONB_DEV_REV_MASK		0x03
#define CONB_DEV_REV_SHIFT		0

/*CONC*/
#define CONC_EN_HVDCP_MASK	0x1
#define CONC_EN_HVDCP_SHIFT	7

#define CONC_DP_VOLT_MASK	0x3
#define CONC_DP_VOLT_SHIFT	2

#define CONC_DM_VOLT_MASK	0x3
#define CONC_DM_VOLT_SHIFT	0

#define CONC_DP_DM_MASK	0xF
#define CONC_DP_DM_SHIFT	0

#define CONC_DP_DM_VOL_HIZ	0
#define CONC_DP_DM_VOL_0P0	1
#define CONC_DP_DM_VOL_0P6	2
#define CONC_DP_DM_VOL_3P3	3

/*COND*/
#define COND_CV_SP_MASK	0x1
#define COND_CV_SPP_SHIFT	6

#define COND_BAT_COMP_MASK	0x7
#define COND_BAT_COMP_SHIFT	3

#define COND_BAT_COMP_MAX		140
#define COND_BAT_COMP_STEP		20

#define COND_COMP_MAX_MASK	0x7
#define COND_COMP_MAX_SHIFT	0

#define COND_COMP_MAX_MAX	224
#define COND_COMP_MAX_STEP	32

/*CON10*/
#define CON10_HV_VINDPM_MASK	0x1
#define CON10_HV_VINDPM_SHIFT	7

#define CON10_VLIMIT_MASK	0x7F
#define CON10_VLIMIT_SHIFT	0
#define VILIMIT_VOL_STEP	((uint32_t)100)
#define VILIMIT_VOL_MAX	((uint32_t)12000)
#define VILIMIT_VOL_MIN		((uint32_t)3900)

//for SGM4154XAD only
/*CON11*/
#define SGM4154X_R11		      0x11
#define SGM4154X_R12		      0x12
#define SGM4154X_R13		      0x13
#define SGM4154X_R14		      0x14
#define SGM4154X_R15		      0x15
#define SGM4154X_R16		      0x16
#define SGM4154X_R17		      0x17

#define CON11_START_ADC_MASK  0x01
#define CON11_START_ADC_SHIFT 7
#define CON11_CONV_RATE_MASK  0x01
#define CON11_CONV_RATE_SHIFT 6

/*define register*/
#define SGM4154x_CHRG_CTRL_0	0x00
#define SGM4154x_CHRG_CTRL_1	0x01
#define SGM4154x_CHRG_CTRL_2	0x02
#define SGM4154x_CHRG_CTRL_3	0x03
#define SGM4154x_CHRG_CTRL_4	0x04
#define SGM4154x_CHRG_CTRL_5	0x05
#define SGM4154x_CHRG_CTRL_6	0x06
#define SGM4154x_CHRG_CTRL_7	0x07
#define SGM4154x_CHRG_STAT	    0x08
#define SGM4154x_CHRG_FAULT	    0x09
#define SGM4154x_CHRG_CTRL_a	0x0a
#define SGM4154x_CHRG_CTRL_b	0x0b
#define SGM4154x_CHRG_CTRL_c	0x0c
#define SGM4154x_CHRG_CTRL_d	0x0d
#define SGM4154x_INPUT_DET   	0x0e
#define SGM4154x_CHRG_CTRL_f	0x0f

/* charge status flags  */
#define SGM4154x_CHRG_EN		BIT(4)
#define SGM4154x_HIZ_EN		    BIT(7)
#define SGM4154x_TERM_EN		BIT(7)
#define SGM4154x_VAC_OVP_MASK	GENMASK(7, 6)
#define SGM4154x_DPDM_ONGOING   BIT(7)
#define SGM4154x_VBUS_GOOD      BIT(7)

#define SGM4154x_BOOSTV 		GENMASK(5, 4)
#define SGM4154x_BOOST_LIM 		BIT(7)
#define SGM4154x_OTG_EN		    BIT(5)

/* Part ID  */
#define SGM4154x_PN_MASK	    GENMASK(6, 3)
#define SGM4154x_PN_41541_ID    (BIT(6)| BIT(5))
#define SGM4154x_PN_41516_ID    (BIT(6)| BIT(5))
#define SGM4154x_PN_41542_ID    (BIT(6)| BIT(5)| BIT(3))
#define SGM4154x_PN_41516D_ID   (BIT(6)| BIT(5)| BIT(3))

/* WDT TIMER SET  */
#define SGM4154x_WDT_TIMER_MASK        GENMASK(5, 4)
#define SGM4154x_WDT_TIMER_DISABLE     0
#define SGM4154x_WDT_TIMER_40S         BIT(4)
#define SGM4154x_WDT_TIMER_80S         BIT(5)
#define SGM4154x_WDT_TIMER_160S        (BIT(4)| BIT(5))

#define SGM4154x_WDT_RST_MASK          BIT(6)

/* SAFETY TIMER SET  */
#define SGM4154x_SAFETY_TIMER_MASK     GENMASK(3, 3)
#define SGM4154x_SAFETY_TIMER_DISABLE     0
#define SGM4154x_SAFETY_TIMER_EN       BIT(3)
#define SGM4154x_SAFETY_TIMER_5H         0
#define SGM4154x_SAFETY_TIMER_10H      BIT(2)

/* recharge voltage  */
#define SGM4154x_VRECHARGE              BIT(0)
#define SGM4154x_VRECHRG_STEP_mV		100
#define SGM4154x_VRECHRG_OFFSET_mV		100

/* charge status  */
#define SGM4154x_VSYS_STAT		BIT(0)
#define SGM4154x_THERM_STAT		BIT(1)
#define SGM4154x_PG_STAT		BIT(2)
#define SGM4154x_CHG_STAT_MASK	GENMASK(4, 3)
#define SGM4154x_PRECHRG		BIT(3)
#define SGM4154x_FAST_CHRG	    BIT(4)
#define SGM4154x_TERM_CHRG	    (BIT(3)| BIT(4))

/* charge type  */
#define SGM4154x_VBUS_STAT_MASK	GENMASK(7, 5)
#define SGM4154x_NOT_CHRGING	0
#define SGM4154x_USB_SDP		BIT(5)
#define SGM4154x_USB_CDP		BIT(6)
#define SGM4154x_USB_DCP		(BIT(5) | BIT(6))
#define SGM4154x_UNKNOWN	    (BIT(7) | BIT(5))
#define SGM4154x_NON_STANDARD	(BIT(7) | BIT(6))
#define SGM4154x_OTG_MODE	    (BIT(7) | BIT(6) | BIT(5))

/* TEMP Status  */
#define SGM4154x_TEMP_MASK	    GENMASK(2, 0)
#define SGM4154x_TEMP_NORMAL	BIT(0)
#define SGM4154x_TEMP_WARM	    BIT(1)
#define SGM4154x_TEMP_COOL	    (BIT(0) | BIT(1))
#define SGM4154x_TEMP_COLD	    (BIT(0) | BIT(3))
#define SGM4154x_TEMP_HOT	    (BIT(2) | BIT(3))

/* precharge current  */
#define SGM4154x_PRECHRG_CUR_MASK		GENMASK(7, 4)
#define SGM4154x_PRECHRG_CURRENT_STEP_uA		60000
#define SGM4154x_PRECHRG_I_MIN_uA		60000
#define SGM4154x_PRECHRG_I_MAX_uA		780000
#define SGM4154x_PRECHRG_I_DEF_uA		180000

/* termination current  */
#define SGM4154x_TERMCHRG_CUR_MASK		GENMASK(3, 0)
#define SGM4154x_TERMCHRG_CURRENT_STEP_uA	60000
#define SGM4154x_TERMCHRG_I_MIN_uA		60000
#define SGM4154x_TERMCHRG_I_MAX_uA		960000
#define SGM4154x_TERMCHRG_I_DEF_uA		180000

/* charge current  */
#define SGM4154x_ICHRG_CUR_MASK		GENMASK(5, 0)
#define SGM4154x_ICHRG_CURRENT_STEP_uA		60000
#define SGM4154x_ICHRG_I_MIN_uA			0
#define SGM4154x_ICHRG_I_MAX_uA			3780000
#define SGM4154x_ICHRG_I_DEF_uA			2040000

/* charge voltage  */
#define SGM4154x_VREG_V_MASK		GENMASK(7, 3)
#define SGM4154x_VREG_V_MAX_uV	    4624000
#define SGM4154x_VREG_V_MIN_uV	    3856000
#define SGM4154x_VREG_V_DEF_uV	    4208000
#define SGM4154x_VREG_V_STEP_uV	    32000

/* VREG Fine Tuning  */
#define SGM4154x_VREG_FT_MASK	     GENMASK(7, 6)
#define SGM4154x_VREG_FT_UP_8mV	     BIT(6)
#define SGM4154x_VREG_FT_DN_8mV	     BIT(7)
#define SGM4154x_VREG_FT_DN_16mV	 (BIT(7) | BIT(6))

/* iindpm current  */
#define SGM4154x_IINDPM_I_MASK		GENMASK(4, 0)
#define SGM4154x_IINDPM_I_MIN_uA	100000
#define SGM4154x_IINDPM_I_MAX_uA	3800000
#define SGM4154x_IINDPM_STEP_uA	    100000
#define SGM4154x_IINDPM_DEF_uA	    2400000

/* vindpm voltage  */
#define SGM4154x_VINDPM_V_MASK      GENMASK(3, 0)
#define SGM4154x_VINDPM_V_MIN_uV    3900000
#define SGM4154x_VINDPM_V_MAX_uV    12000000
#define SGM4154x_VINDPM_STEP_uV     100000
#define SGM4154x_VINDPM_DEF_uV	    4500000
#define SGM4154x_VINDPM_OS_MASK     GENMASK(1, 0)

/* DP DM SEL  */
#define SGM4154x_DP_VSEL_MASK       GENMASK(4, 3)
#define SGM4154x_DM_VSEL_MASK       GENMASK(2, 1)

/* PUMPX SET  */
#define SGM4154x_EN_PUMPX           BIT(7)
#define SGM4154x_PUMPX_UP           BIT(6)
#define SGM4154x_PUMPX_DN           BIT(5)

/* customer define jeita paramter */
#define JEITA_TEMP_ABOVE_T4_CV	0
#define JEITA_TEMP_T3_TO_T4_CV	4100000
#define JEITA_TEMP_T2_TO_T3_CV	4350000
#define JEITA_TEMP_T1_TO_T2_CV	4350000
#define JEITA_TEMP_T0_TO_T1_CV	0
#define JEITA_TEMP_BELOW_T0_CV	0

#define JEITA_TEMP_ABOVE_T4_CC_CURRENT	0
#define JEITA_TEMP_T3_TO_T4_CC_CURRENT	1000000
#define JEITA_TEMP_T2_TO_T3_CC_CURRENT	2400000
#define JEITA_TEMP_T1_TO_T2_CC_CURRENT	2000000
#define JEITA_TEMP_T0_TO_T1_CC_CURRENT	0
#define JEITA_TEMP_BELOW_T0_CC_CURRENT	0

#define TEMP_T4_THRES  50
#define TEMP_T4_THRES_MINUS_X_DEGREE 47
#define TEMP_T3_THRES  45
#define TEMP_T3_THRES_MINUS_X_DEGREE 39
#define TEMP_T2_THRES  20
#define TEMP_T2_THRES_PLUS_X_DEGREE 16
#define TEMP_T1_THRES  0
#define TEMP_T1_THRES_PLUS_X_DEGREE 6
#define TEMP_T0_THRES  0
#define TEMP_T0_THRES_PLUS_X_DEGREE  0
#define TEMP_NEG_10_THRES 0


#define SGM4154X_ADC_CHANNEL_START SGM4154X_R12
enum sgm4154x_adc_channel {
	error_channel = -1,
	vbat_channel =  0,
	vsys_channel =  1,
	tbat_channel =  2,
	vbus_channel =  3,
	ibat_channel =  4,
	ibus_channel =  5,
	max_channel,
};

#endif // _sgm4154x_SW_H_

