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

#ifndef __ADSP_SUBSYS_H__
#define __ADSP_SUBSYS_H__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/notifier.h>


/*PLL Frequency*/
/*
ADSP_CORECLK_SRC_MX_CTL,
001: select pll_comm_2d
010: select pll_opt_4d
100: select pll_i2s
*/

/*ADSP CORE CLK SRC*/
#define ADSP_CORECLK_PLL_COMMON     (1)
#define ADSP_CORECLK_PLL_OPT        (2)
#define ADSP_CORECLK_PLL_I2S        (4)

/*ADSP CORE CLK RATE*/
#define PLL_COMM_HZ       (400000000)
#define PLL_OPT_HZ        (519321533)  //519321600
#define PLL_I2S_HZ        (589824000)  /*0.8*/

/*ADSP BUS CLK*/
/*for the soc limited, 133M and 100M can't be used now, all map to 200M*/
//#define BCLK_FREQ_133330K_HZ   (133330000)  /*0.8/0.7, PLL_COMM*/
#define BCLK_FREQ_133330K_HZ   (200000000)  /*0.8/0.7, PLL_COMM*/
#define BCLK_FREQ_147456K_HZ   (147456000)  /*0.8/0.7, PLL_I2S*/
#define BCLK_FREQ_196608K_HZ   (196608000)  /*0.8, PLL_I2S*/
#define BCLK_FREQ_200000K_HZ   (200000000)  /*0.8, PLL_COMM*/

/*adsp state*/
typedef enum {
    ADSP_STAT_SHUTDOWN	= 0,
    ADSP_STAT_POWERUP	= 1,
    ADSP_STAT_LOADED    = 2,
    ADSP_STAT_RUNNING   = 3,
    ADSP_STAT_RERESET   = 4,  /*only for noti, don't record this state*/
    ADSP_STAT_UNINIT    = 0xff
} ADSP_LOAD_STATE;

typedef void (*bolero_pow_updw_CB)(void);

/*api for getting the state of adsp load*/
int adsp_state_get(void);

/*api for requesting adsp to power on or down*/
int adsp_regu_enable(void);
int adsp_regu_disable(void);

/*api for adsp dvfs*/
int adsp_devfreq_set(unsigned long will_freq);
unsigned long adsp_devfreq_get(void);


/*adsp core clk to 400M*/
int adsp_devfreq_down(void);
/*adsp core clk to 589M*/
int adsp_devfreq_up(void);


/*adsp subsys reset*/
void adsp_subsys_reset(void);

/*adsp submod notify, strlen(submod_name) shall be less than 64*/
void *adsp_submod_notif_register(const char *submod_name, struct notifier_block *nb);
int adsp_submod_notif_unregister(const char *submod_name, struct notifier_block *nb);
int adsp_submod_notify(unsigned long val, void *v);

void adsp_register_boleno_updw(bolero_pow_updw_CB upcb, bolero_pow_updw_CB dwcb);
bolero_pow_updw_CB bolero_pow_up_cb_get(void);
bolero_pow_updw_CB bolero_pow_dw_cb_get(void);


#endif
