/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018~2019 JLQ Technology Co., Ltd. or its affiliates.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */

#ifndef _VDSP_RUNTIME_TYPE_H
#define _VDSP_RUNTIME_TYPE_H

#define RUNTIME_NSID_INITIALIZER \
	{0x56, 0x44, 0x53, 0x50, 0x52, 0x55, 0x4e, 0x54, \
	0x49, 0x4d, 0x45, 0x5f, 0x4e, 0x53, 0x49, 0x44}

#define RUNTIME_NSID ((unsigned char [])RUNTIME_NSID_INITIALIZER)

enum vdsp_runtime_request_type {
	VDSP_RUNTIME_SET_LOG = 1,
	VDSP_RUNTIME_SET_STATS,
	VDSP_RUNTIME_GET_CPULOADING,
	VDSP_RUNTIME_SET_SYSDUMP,
	VDSP_RUNTIME_SET_FREQ,
	VDSP_RUNTIME_GET_FREQ,
	VDSP_RUNTIME_SET_CLOSE,
	VDSP_RUNTIME_TYPE_MAX = 0xFFFFFFFF,
};

enum log_output_mode {
	/*decide by dsp default settings or last settings*/
	OUTPUT_BY_DEFAULT = 0,
	OUTPUT_BY_UART,
	OUTPUT_BY_MEMORY,
	OUTPUT_BY_ALL,
	OUTPUT_BY_MAX = 0xFFFFFFFF,
};

enum log_output_modules {
	/*decide by dsp default settings or last settings*/
	OUTPUT_MODULE_INVALID = 0,
	OUTPUT_MODULE_DRIVER = 1 << 0,
	OUTPUT_MODULE_OS = 1 << 1,
	OUTPUT_MODULE_RUNTIME = 1 << 2,
	OUTPUT_MODULE_IPC = 1 << 3,
	OUTPUT_MODULE_APP = 1 << 4,
	OUTPUT_MODULE_ALL = 1 << 7,
	OUTPUT_MODULE_MAX = 1 << 8,
};

enum log_output_levels {
	OUTPUT_LEVEL_DISABLE = 0,/*default: dont output log to terminal*/
	OUTPUT_LEVEL_ERR,
	OUTPUT_LEVEL_WARN,
	OUTPUT_LEVEL_DEBUG,
	OUTPUT_LEVEL_MAX = 0xFFFFFFFF,
};

struct log_common_params_t {
	unsigned int fifo_width;
	unsigned int fifo_depth;
	unsigned int fifo_watermark;
};

struct vdsp_runtime_set_log_payload {
	unsigned int set_log_enable;
	enum log_output_mode output_mode;
	/*eg. output_modules = OUTPUT_MODULE_DRIVER | OUTPUT_MODULE_OS */
	unsigned int output_modules;
	enum log_output_levels output_levels;
	unsigned int set_common_params;
	struct log_common_params_t common_params;
};

enum stats_mode {
	STATS_MODE_DEFAULT = 0,/*decide by dsp default settings or last settings*/
	STATS_MODE_ONCE,/*Statistics only once*/
	STATS_MODE_LOOP,/*Continuous statistics*/
	STATS_MODE_MAX = 0xFFFFFFFF,
};

enum stats_methods {
	STATS_METHOD_DEFAULT = 0,/*decide by dsp default settings or last settings*/
	STATS_METHOD_SIMPLE,/*Only CPU usage is counted*/
	STATS_METHOD_DETAILS,/*Statistics CPU usage, thread status, thread stack usage*/
	STATS_METHOD_MAX = 0xFFFFFFFF,
};

struct vdsp_runtime_set_stats_payload {
	unsigned int set_stats_enable;
	enum stats_mode mode;
	enum stats_methods methods;
	/*If nonzero, then thread stats counters are reset after reading. This is useful if
	 *you want to track the stats so as to get a better idea of current system loading.
	 *E.g. calling this function once a second with 'reset' nonzero will provide CPU
	 *load information for the last second on each call
	 */
	unsigned int reset;
	unsigned int interval;/*Statistical interval*/
};

struct vdsp_runtime_get_cpuloading_payload {
	/*If nonzero, then thread stats counters are reset after reading. This is useful if
	 *you want to track the stats so as to get a better idea of current system loading.
	 *E.g. calling this function once a second with 'reset' nonzero will provide CPU
	 *load information for the last second on each call
	 */
	unsigned int reset;
};

struct vdsp_runtime_sysdump_payload {
	unsigned int reserved;
};

struct vdsp_runtime_set_freq_payload {
	unsigned int set_core_clk_freq;
	unsigned int core_clk_freq;
	unsigned int set_dma_clk_freq;
	unsigned int dma_clk_freq;
};

struct vdsp_runtime_get_freq_payload {
	unsigned int reserved;
};

struct vdsp_runtime_request_t {
	enum vdsp_runtime_request_type type;
	union{
		struct vdsp_runtime_set_log_payload log_payload;
		/*The statistical results are output to the terminal*/
		struct vdsp_runtime_set_stats_payload stats_payload;
		struct vdsp_runtime_get_cpuloading_payload cpuloading_payload;
		struct vdsp_runtime_sysdump_payload sysdump_payload;
		struct vdsp_runtime_set_freq_payload set_freq_payload;
		struct vdsp_runtime_get_freq_payload get_freq_payload;
	};
};

struct vdsp_runtime_response_t {
	enum vdsp_runtime_request_type type;
	int result;/*success: 0, Other values need to be confirmed with relevant developers*/
	union{
		unsigned int cpu_usage;/*eg. cpu use 80% ---> cpu_usage = 80 */
		uint64_t clkFreqHz;/*High 32 bits: dma_clk_freq, Low 32 bits: core_clk_freq*/
	};
};

#endif //_VDSP_RUNTIME_TYPE_H
