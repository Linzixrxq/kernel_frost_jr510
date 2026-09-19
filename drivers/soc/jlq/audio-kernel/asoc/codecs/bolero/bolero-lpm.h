/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#ifndef BOLERO_LPM_H
#define BOLERO_LPM_H

#include <sound/soc.h>
#include <linux/regmap.h>

//#define BOLERO_REGU

#if IS_ENABLED(CONFIG_SND_SOC_BOLERO)

int bolero_regu_get(struct platform_device *pdev);
void bolero_regu_put(struct platform_device *pdev);
int bolero_regu_enable(void);
int bolero_regu_disable(void);
int va_regu_get(struct platform_device *pdev);
void va_regu_put(struct platform_device *pdev);
int va_regu_enable(void);
int va_regu_disable(void);


#else
static inline int bolero_regu_get(struct platform_device *pdev);
{
	return 0;
}
static inline void bolero_regu_put(struct platform_device *pdev);
{
}

static inline int bolero_regu_enable(void);
{
	return 0;
}

static inline void bolero_regu_disable(void);
{
}

static inline int va_regu_get(struct platform_device *pdev);
{
	return 0;
}
static inline void va_regu_put(struct platform_device *pdev);
{
}

static inline int va_regu_enable(void);
{
	return 0;
}

static inline void va_regu_disable(void);
{
}


#endif /* CONFIG_SND_SOC_BOLERO */
#endif /* BOLERO_LPM_H */
