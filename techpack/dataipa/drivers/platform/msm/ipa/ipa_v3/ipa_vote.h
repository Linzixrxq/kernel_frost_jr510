// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2021, The Linux Foundation. All rights reserved.
 */
//resource    resource class   resource ID     allowedkeys
//clk/ipa     ipa(0x617069)    0               KHz(0x007a484b)
#ifndef _IPA_VOTE_H_
#define _IPA_VOTE_H_
#include <soc/qcom/rpm-smd.h>
#include <linux/msm_ipa.h>
#include "ipa_i.h"

/**
* enum ipa_rail_voltage_level - IPA RAIL Voltage levels
 */
enum ipa_rail_voltage_level {
	RAIL_VOLTAGE_LEVEL_OFF = 0,
	RAIL_VOLTAGE_LEVEL_RETENTION = 0x010,
	RAIL_VOLTAGE_LEVEL_RETENTION_HIGH = 0x020,
	RAIL_VOLTAGE_LEVEL_SVS_MIN = 0x030,
	RAIL_VOLTAGE_LEVEL_SVS_LOW = 0x040,
	RAIL_VOLTAGE_LEVEL_SVS = 0x080,
	RAIL_VOLTAGE_LEVEL_SVS_HIGH = 0x0C0,
	RAIL_VOLTAGE_LEVEL_NOMINAL = 0x100,
	RAIL_VOLTAGE_LEVEL_NOMINAL_L1 = 0x140,
	RAIL_VOLTAGE_LEVEL_TURBO = 0x180,
	RAIL_VOLTAGE_LEVEL_TURBO_L1 = 0x1A0,
};

int ipa3_get_busclks(struct device *dev);
void ipa3_enable_busclks(void);
void ipa3_disable_busclks(void);

#endif /* _IPA_VOTE_H_ */

