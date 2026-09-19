// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * 2021-6-30 author:taovis:The distinction will be implemented for subsequent better upgrades qualcomm's delivery.
 */
#include "ipa_vote.h"
#include <linux/clk.h>
//resource    resource class   resource ID     allowedkeys
//clk/ipa     ipa(0x617069)    0               KHz(0x007a484b)
#define CLOCK_IPA_RESOURCECLASS 0x617069       //clk/ipa resource class
#define CLKIPA_RESOURCEID 0                    //resource ID
#define KVP_KHZ 0x007a484b                     //allowedkeys

//resource    resource class   resource ID     allowedkeys
//qtang_cx    "rwcx"(0x78637772)    0          vlvl(0x6C766C76)
//qtang_mx    "rwmx"(0x786D777)     0          vlvl(0x6C766C76)
#define QTANG_VLCX_RESOURCECLASS 0x78637772    //qtang_cx class
#define QTANG_VLMX_RESOURCECLASS 0x786D777     //qtang_mx class
#define QTANG_VL_RESOURCEID 0                  //resource ID
#define KVP_VLVL 0x6C766C76                    //allowedkeys

static struct clk *ipa3_snoc_clk;
static struct clk *ipa3_cnoc_clk;

/**
* voltage mapping to bus index
* mapping to ipa3_get_bus_vote response value
*needed_voltage: need scaling voltage bus idx
* resource by QTANGQRM-602
*   mode          enum type                  int value
*  "MIN", -> RAIL_VOLTAGE_LEVEL_OFF             0
*  "SVS2", -> RAIL_VOLTAGE_LEVEL_SVS_MIN        1
*  "SVS", -> RAIL_VOLTAGE_LEVEL_SVS             2
*  "NOMINAL", -> RAIL_VOLTAGE_LEVEL_NOMINAL     3
*  "TURBO" -> RAIL_VOLTAGE_LEVEL_TURBO          4
**/
enum ipa_rail_voltage_level ipa3_voltage_mapping(int vote_idx)
{
	switch (vote_idx) {
	case 0:
		return RAIL_VOLTAGE_LEVEL_OFF;
	case 1:
		return RAIL_VOLTAGE_LEVEL_SVS_MIN;
	case 2:
		return RAIL_VOLTAGE_LEVEL_SVS;
	case 3:
		return RAIL_VOLTAGE_LEVEL_NOMINAL;
	case 4:
		return RAIL_VOLTAGE_LEVEL_TURBO;
	default:
		IPAERR("Not supported voltage,needed voltage idx(0-4) = %d\n", vote_idx);
		WARN_ON(1);
		return RAIL_VOLTAGE_LEVEL_NOMINAL;
	}
	return RAIL_VOLTAGE_LEVEL_NOMINAL;
}

int ipa3_get_busclks(struct device *dev)
{

	ipa3_snoc_clk = clk_get(dev, "qt_snoc");
	if (IS_ERR(ipa3_snoc_clk)) {
		if (ipa3_snoc_clk != ERR_PTR(-EPROBE_DEFER))
			IPAERR("fail to get ipa snoc clk\n");
		return PTR_ERR(ipa3_snoc_clk);
	}

	ipa3_cnoc_clk = clk_get(dev, "qt_cnoc");
	if (IS_ERR(ipa3_cnoc_clk)) {
		if (ipa3_cnoc_clk != ERR_PTR(-EPROBE_DEFER))
			IPAERR("fail to get ipa cnoc clk\n");
		return PTR_ERR(ipa3_cnoc_clk);
	}
	IPADBG("get ipa bus clk successful");
	return 0;
}

/**
 * ipa3_enable_busclks() - Enable qtang2 IPA snoc/cnoc clocks.
 */
void ipa3_enable_busclks(void)
{
	int ret;
	if (ipa3_snoc_clk && ipa3_cnoc_clk) {
		IPADBG_LOW("enabling ipa snoc and ipa cnoc clk\n");

		ret = clk_prepare_enable(ipa3_snoc_clk);
		if(ret) {
			IPAERR("enabling ipa snoc clk failure, ret = %d", ret);
		}

		ret |= clk_prepare_enable(ipa3_cnoc_clk);
		if(ret) {
			IPAERR("enabling ipa cnoc clk failure, ret = %d", ret);
		}

		ipa3_ctx->bus_enable = ret == 0 ? true : false;
		return;
	}
	IPAERR("ipa snoc/cnoc clk is null");
}

/**
 * ipa_disable_busclks() - Disable IPA IPA snoc/cnoc clocks.
 */
void ipa3_disable_busclks(void)
{
	if (ipa3_snoc_clk && ipa3_cnoc_clk && ipa3_ctx->bus_enable) {
		ipa3_ctx->bus_enable = false;
		clk_disable_unprepare(ipa3_snoc_clk);
		clk_disable_unprepare(ipa3_cnoc_clk);
		return;
	}
	IPAERR("ipa snoc clk = %llx ipa cnoc clk = %llx, bus_enable = %s\n",
			ipa3_snoc_clk,
			ipa3_cnoc_clk,
			ipa3_ctx->bus_enable == true ? "true" : "false");
}
