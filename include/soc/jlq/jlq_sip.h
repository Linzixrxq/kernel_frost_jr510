/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __JLQ_SIP_H_
#define __JLQ_SIP_H_

#include <uapi/linux/psci.h>
#include <linux/arm-smccc.h>

enum EFUSE_ID {
	EFUSE_CHIP_ID = 1,
	EFUSE_PRODUCT_ID,
	EFUSE_OTP_FUSE_ID,
	EFUSE_FOUNDRY_ECO_ID,
	EFUSE_COMMON_ID,        /* read efuse by offset */
	EFUSE_INVALID_ID,
};

#define JLQ_SIP_CALL_FN_ID	(0xC2000008)
#define JLQ_SIP_TZ_SVCW		(0xC2000009)
#define JLQ_SIP_TZ_SVCR		(0xC200000A)
#define JLQ_SIP_TZ_DBG		(0xC200000B)
#define JLQ_SIP_SECCAM_TZ_DBG	(0xC200000C)
#define DBG_OP_SECCAM_TZ_INIT				0
#define DBG_OP_SECCAM_TZ_PROTECT_PHY		1
#define DBG_OP_SECCAM_TZ_PROTECT_PHY_LANES	2

static inline void qcom_seccam_dbg_init(void)
{
	struct arm_smccc_res res;

	arm_smccc_smc(JLQ_SIP_SECCAM_TZ_DBG, DBG_OP_SECCAM_TZ_INIT,
				0, 0, 0, 0, 0, 0, &res);
}

static inline unsigned long qcom_seccam_dbg_protect_phy(bool protect,
							uint32_t phy_id)
{
	struct arm_smccc_res res;

	arm_smccc_smc(JLQ_SIP_SECCAM_TZ_DBG, DBG_OP_SECCAM_TZ_PROTECT_PHY,
				protect, phy_id, 0, 0, 0, 0, &res);

	return res.a0;
}

static inline unsigned long qcom_seccam_dbg_protect_phy_lanes(bool protect,
							uint32_t phy_id_mask)
{
	struct arm_smccc_res res;

	arm_smccc_smc(JLQ_SIP_SECCAM_TZ_DBG, DBG_OP_SECCAM_TZ_PROTECT_PHY_LANES,
				protect, phy_id_mask, 0, 0, 0, 0, &res);

	return res.a0;
}


static inline unsigned long jlq_sip_call(unsigned long arg0,
		unsigned long arg1, unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_smc(JLQ_SIP_CALL_FN_ID, arg0, arg1, arg2, 0, 0, 0, 0, &res);

	return res.a0;
}

static inline unsigned long jlq_sip_tz_svcw(unsigned long base0,unsigned long data0,
		unsigned long base1,unsigned long data1)
{
	struct arm_smccc_res res;

	//printk(KERN_DEBUG "jlq_sip_tz_svcw1: ret:%d write num:%d read[%x]0x%x(w:0x%x), [%x]0x%x(w:0x%x)\n", res.a0, res.a1,
	//					base0, res.a2, data0, base1, res.a3, data1);

	arm_smccc_smc(JLQ_SIP_TZ_SVCW, base0, data0, base1, data1, 0, 0, 0, &res);

	//printk(KERN_DEBUG "jlq_sip_tz_svcw2: ret:%d write num:%d read[%x]0x%x(w:0x%x), [%x]0x%x(w:0x%x)\n", res.a0, res.a1,
	//					base0, res.a2, data0, base1, res.a3, data1);

	return res.a0;
}

static inline unsigned long jlq_sip_tz_svcr(unsigned long base0, unsigned int *data0,
		unsigned long base1, unsigned int *data1)
{
	struct arm_smccc_res res;

	//printk(KERN_DEBUG "jlq_sip_tz_svcr1: ret:%d read num:%d [%x]0x%x, [%x]0x%x\n", res.a0, res.a1,
	//					base0, res.a2, base1, res.a3);

	arm_smccc_smc(JLQ_SIP_TZ_SVCR, base0, base1, 0, 0, 0, 0, 0, &res);

	//printk(KERN_DEBUG "jlq_sip_tz_svcr2: ret:%d read num:%d [%x]0x%x, [%x]0x%x\n", res.a0, res.a1,
	//					base0, res.a2, base1, res.a3);

	if (!res.a0) {
		if (data0)
			*data0 = (unsigned int)res.a2;
		if (data1)
			*data1 = (unsigned int)res.a3;
	}

	return res.a0;
}

// op: 0 for fde dump
static inline unsigned long jlq_sip_tz_dbg(unsigned long op,unsigned long arg0,
		unsigned long arg1,unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_smc(JLQ_SIP_TZ_DBG, op, arg0, arg1, arg2, 0, 0, 0, &res);

	return res.a0;
}



#endif
