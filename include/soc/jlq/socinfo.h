/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 */

#ifndef __SOC_JLQ_SOCINFO_H__
#define __SOC_JLQ_SOCINFO_H__

#include <linux/types.h>

#if IS_ENABLED(CONFIG_JLQ_SOCINFO)
uint32_t socinfo_get_id(void);
uint32_t socinfo_get_serial_number(void);
const char *socinfo_get_id_string(void);
#else
static inline uint32_t socinfo_get_id(void)
{
	return 0;
}

static inline uint32_t socinfo_get_serial_number(void)
{
	return 0;
}

static inline const char *socinfo_get_id_string(void)
{
	return "N/A";
}
#endif /* CONFIG_JLQ_SOCINFO */

#endif /* __SOC_JLQ_SOCINFO_H__ */
