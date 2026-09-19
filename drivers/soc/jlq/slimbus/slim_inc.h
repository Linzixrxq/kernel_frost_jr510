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

#ifndef __SLIM_INC_H__
#define __SLIM_INC_H__


//#define LOGTAG "[slimbus][JR510]"
#define LOGTAG "[slim]"

/*PR INFO*/
//#define ADSP_PR_INFO
//#define ADSP_PR_DEBUG

#ifdef ADSP_PR_INFO
#define PR_INFO(fmt, args...) pr_info(fmt, ##args)
#else
#define PR_INFO(fmt, args...)
#endif

#ifdef ADSP_PR_DEBUG
#define PR_DEBUG(fmt, args...) pr_debug(fmt, ##args)
#else
#define PR_DEBUG(fmt, args...)
#endif



#define DEBUG_CHECK_INFO        1
#define DEBUG_CHECK_DATA        0
#define DEBUG_ONLY              0

/*switch for version*/
#define MASTER_SLAVE_NORMAL_VERSION             1   /*normal JR510 version, must be 1, others are 0*/
#define MASTER_SLAVE_NORMAL_TEST                0   /*for unit test, OK*/
#define MASTER_SLIM_DAI_SND                     1   /*normal JR510 version, must be 1, with sound dai*/
#define MASTER_SLIM_DAI_SND_UT                  0   /*sound dai api ut*/
#define MASTER_SLIM_DAI_SND_UT_WITH_LOOPBACK    0   /*sound dai api ut with loopback*/
#define SLAVE_SLIM_BTFM_UT                      0   /*sound dai api ut*/


/*switch for master loopback only, while test this, set all 0 in the 'switch for version'*/
#define MASTER_LOOPBACK_TEST                    0   /*master dport loopback, should with CHANNEL_DEFINE_CONTENT_BY_MASTER or MASTER_LOOPBACK_TEST_WITH_CH_CONTENT*/
#define MASTER_LOOPBACK_TEST_WITH_CH_CONTENT    0   /*master dport loopback, CH DEFINE and CONTENT by ctrl*/
#define MASTER_SLAVE_LOOPBACK_TEST              0   /*no verify, maybe later*/
/*CHANNEL DEFINE and CONTENT by master or slave.
    if dont define this, CHANNEL DEFINE and CONTENT will be done in the ctrl.
    normal JR510 version, DONT define this, maybe should do CHANNEL DEFINE/CONTENT by slave/ctrl.*/
//#define CHANNEL_DEFINE_CONTENT_BY_MASTER


/*switch for slave soc*/
#define WCX_ENABLE
//#define WCD9306
//#define WCD9326
#define WCN3950



/*MTDR ret value*/
#define MTDR_RET_EOK             0U      /* no error */
#define MTDR_RET_EPERM           1U      /* Operation not permitted */
#define MTDR_RET_ENOENT          2U      /* No such file or directory */
#define MTDR_RET_EIO             5U      /* I/O error */
#define MTDR_RET_ENOEXEC         8U      /* Exec format error */
#define MTDR_RET_EAGAIN          11U      /* Try again */
#define MTDR_RET_ENOMEM          12U      /* Out of memory */
#define MTDR_RET_EFAULT          14U      /* Bad address */
#define MTDR_RET_EBUSY           16U      /* Device or resource busy */
#define MTDR_RET_EINVAL          22U      /* Invalid argument */
#define MTDR_RET_ENOSPC          28U      /* No space left on device */
#define MTDR_RET_EBADSLT         57U      /* Invalid slot */
#define MTDR_RET_EPROTO          71U      /* Protocol error */
#define MTDR_RET_EOVERFLOW       75U      /* Value too large for defined data type */
#define MTDR_RET_EOPNOTSUPP      95U      /* Operation not supported */
#define MTDR_RET_ETIMEDOUT       110U     /* Connection timed out */
#define MTDR_RET_EINPROGRESS     115U     /* Operation now in progress */
#define MTDR_RET_EDQUOT          122U     /* Quota exceeded */
#define MTDR_RET_ENOTSUP         MTDR_RET_EOPNOTSUPP
#define MTDR_RET_ECANCELED       126U      /* Cancelled */

#endif
