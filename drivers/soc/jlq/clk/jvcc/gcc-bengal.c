// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "clk: %s: " fmt, __func__

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-bengal.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "reset.h"
#include "vdd-level-bengal.h"
#include "clk-debug.h"

#ifdef JVCC_HAPS
#define JVCC_REGISTER_TBD	0x0
#endif

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_NUM, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_cx_ao, VDD_NUM, 1, vdd_corner);
static DEFINE_VDD_REGULATORS(vdd_mx, VDD_NUM, 1, vdd_corner);

enum {
	P_BI_TCXO,
	P_CORE_BI_PLL_TEST_SE,
	P_GPLL0_OUT_EARLY,
	P_GPLL0_OUT_AUX,
	P_GPLL0_OUT_AUX2,
	P_GPLL0_OUT_MAIN,
	P_GPLL1_OUT_EARLY,
	P_GPLL1_OUT_AUX,
	P_GPLL1_OUT_AUX2,
	P_GPLL1_OUT_MAIN,
	P_GPLL2_OUT_EARLY,
	P_GPLL2_OUT_AUX,
	P_GPLL2_OUT_AUX2,
	P_GPLL2_OUT_MAIN,
	P_GPLL3_OUT_EARLY,
	P_GPLL3_OUT_AUX,
	P_GPLL3_OUT_AUX2,
	P_GPLL3_OUT_MAIN,
	P_GPLL4_OUT_EARLY,
	P_GPLL4_OUT_AUX,
	P_GPLL4_OUT_AUX2,
	P_GPLL4_OUT_MAIN,
	P_GPLL5_OUT_EARLY,
	P_GPLL5_OUT_AUX,
	P_GPLL5_OUT_AUX2,
	P_GPLL5_OUT_MAIN,
	P_GPLL6_OUT_EARLY,
	P_GPLL6_OUT_AUX,
	P_GPLL6_OUT_AUX2,
	P_GPLL6_OUT_MAIN,
};

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_0[] = {
	"bi_tcxo",
	"gpll0",
	"gpll0_out_aux2",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL6_OUT_MAIN, 4 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_1[] = {
	"bi_tcxo",
	"gpll0",
	"gpll0_out_aux2",
	"gpll6_out_main",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL1_OUT_EARLY, 2 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL1_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_3[] = {
	"bi_tcxo",
	"gpll0",
	"gpll1",
	"gpll5_out_main",
	"gpll1_out_main",
	"gpll3_out_main",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL3_OUT_MAIN, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_4[] = {
	"bi_tcxo",
	"gpll0",
	"gpll0_out_aux2",
	//"gpll4_out_main",
	"gpll3_out_main",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_5[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL2_OUT_EARLY, 2 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL2_OUT_MAIN, 4 },
	{ P_GPLL1_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_5[] = {
	"bi_tcxo",
	"gpll0",
	"gpll2",
	"gpll5_out_main",
	"gpll2_out_main",
	"gpll1_out_main",
	"gpll3_out_main",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_6[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL2_OUT_EARLY, 2 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL6_OUT_EARLY, 4 },
	{ P_GPLL1_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_6[] = {
	"bi_tcxo",
	"gpll0",
	"gpll2",
	"gpll5_out_main",
	"gpll6",
	"gpll1_out_main",
	"gpll3",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_7[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL3_OUT_EARLY, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_7[] = {
	"bi_tcxo",
	"gpll0",
	"gpll0_out_aux2",
	"gpll5_out_main",
	"gpll3",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_8[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL2_OUT_EARLY, 2 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL2_OUT_MAIN, 4 },
	{ P_GPLL1_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_8[] = {
	"bi_tcxo",
	"gpll0",
	"gpll2",
	"gpll5_out_main",
	"gpll2_out_main",
	"gpll1_out_main",
	"gpll3",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_9[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL2_OUT_MAIN, 4 },
	{ P_GPLL1_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_EARLY, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_9[] = {
	"bi_tcxo",
	"gpll0",
	"gpll0_out_aux2",
	"gpll5_out_main",
	"gpll2_out_main",
	"gpll1_out_main",
	"gpll3",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_10[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL4_OUT_MAIN, 2 },
	{ P_GPLL5_OUT_MAIN, 3 },
	{ P_GPLL6_OUT_EARLY, 4 },
	{ P_GPLL1_OUT_MAIN, 5 },
	{ P_GPLL3_OUT_MAIN, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_10[] = {
	"bi_tcxo",
	"gpll0",
	"gpll4_out_main",
	"gpll5_out_main",
	"gpll6",
	"gpll1_out_main",
	"gpll3_out_main",
	"core_bi_pll_test_se",
};

static const struct parent_map gcc_parent_map_11[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPLL0_OUT_EARLY, 1 },
	{ P_GPLL0_OUT_AUX2, 2 },
	{ P_GPLL2_OUT_MAIN, 4 },
	{ P_GPLL3_OUT_EARLY, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gcc_parent_names_11[] = {
	"bi_tcxo",
	"gpll0",
	"gpll0_out_aux2",
	"gpll2_out_main",
	"gpll3",
	"core_bi_pll_test_se",
};

//static struct pll_vco brammo_vco[] = {
//	{ 500000000, 1250000000, 0 },
//};

static struct pll_vco default_vco[] = {
	{ 500000000, 1000000000, 2 },
};

static struct pll_vco alpha_vco[] = {
	{ 750000000, 1500000000, 1 },
};

static const u8 clk_alpha_pll_regs_offset[][PLL_OFF_MAX_REGS] = {
	[CLK_ALPHA_PLL_TYPE_DEFAULT] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL_U] = 0x0c,
		[PLL_OFF_TEST_CTL] = 0x10,
		[PLL_OFF_TEST_CTL_U] = 0x14,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_USER_CTL_U] = 0x1C,
		[PLL_OFF_CONFIG_CTL] = 0x20,
		[PLL_OFF_STATUS] = 0x24,
	},
	[CLK_ALPHA_PLL_TYPE_BRAMMO] =  {
		[PLL_OFF_L_VAL] = 0x04,
		[PLL_OFF_ALPHA_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL_U] = 0x0c,
		[PLL_OFF_TEST_CTL] = 0x10,
		[PLL_OFF_TEST_CTL_U] = 0x14,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_CONFIG_CTL] = 0x1C,
		[PLL_OFF_STATUS] = 0x20,
	},
};

/* 600MHz configuration */
/* EARLY=600M, AUX2=300M */
static const struct alpha_pll_config gpll0_config = {
	.l = 0x1f,
	.alpha = 0x0,
	.alpha_hi = 0x40,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = GENMASK(21, 20),
	.aux2_output_mask = BIT(2),
	.early_output_mask = BIT(3),
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(11, 8),
	//.config_ctl_val = 0x4001055b,//don't use in jvcc
	//.test_ctl_hi1_val = 0x1,
	//.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll gpll0 = {
	.offset = 0x0,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		//.enable_reg = 0x79000,
		//.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpll0",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 1000000000,
				[VDD_NOMINAL] = 2000000000
			},
		},
	},
};

static const struct clk_div_table post_div_table_gpll0_out_aux2[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_aux2 = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll0_out_aux2,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_aux2),
	.width = 4,
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_aux2",
		.parent_names = (const char *[]){ "gpll0" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

static const struct clk_div_table post_div_table_gpll0_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll0_out_main = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll0_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll0_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll0_out_main",
		.parent_names = (const char *[]){ "gpll0" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 1152MHz configuration */
/* EARLY=1152M */
static const struct alpha_pll_config gpll1_config = {
	.l = 0x3C,
	.alpha = 0x0,
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(9, 8),
	.early_output_mask = BIT(3),
	//.config_ctl_val = 0x00004289,//don't use in jvcc
	//.test_ctl_mask = GENMASK(31, 0),
	//.test_ctl_val = 0x08000000,
};

static struct clk_alpha_pll gpll1 = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x01000,
#endif
	//.vco_table = brammo_vco,
	//.num_vco = ARRAY_SIZE(brammo_vco),
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_BRAMMO],
	.clkr = {
		//.enable_reg = 0x79000,
		//.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "gpll1",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 1250000000,
				[VDD_LOW] = 1250000000,
				[VDD_NOMINAL] = 1250000000
			},
		},
	},
};

/* 800MHz configuration */
static const struct alpha_pll_config gpll2_config = {
	.l = 0x29,
	.alpha = 0xAAAAAAAA,
	.alpha_hi = 0xAA,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(11, 8),
	//.config_ctl_val = 0x4001055b,
	//.test_ctl_hi1_val = 0x1,
	//.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll gpll2 = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x02000,
#endif
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		//.enable_reg = 0x79000,
		//.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "gpll2",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 800000000,
				[VDD_NOMINAL] = 1000000000
			},
		},
	},
};

static const struct clk_div_table post_div_table_gpll2_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll2_out_main = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x02000,
#endif
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll2_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll2_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll2_out_main",
		.parent_names = (const char *[]){ "gpll2" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

/* 1066MHz configuration */
static const struct alpha_pll_config gpll3_config = {
	.l = 0x37,
	.alpha = 0x55555555,
	.alpha_hi = 0x85,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x6 << 20,
	.vco_mask = GENMASK(22, 20),
	.early_output_mask = BIT(3),
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(11, 8),
	//.config_ctl_val = 0x4001055b,//don't use in jvcc
	//.test_ctl_hi1_val = 0x1,
	//.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll gpll3 = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x03000,
#endif
	//.vco_table = default_vco,
	//.num_vco = ARRAY_SIZE(default_vco),
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		//.enable_reg = 0x79000,
		//.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gpll3",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 2000000000
			},
		},
	},
};

/* 680.4MHz configuration */
static const struct alpha_pll_config gpll4_config = {
	.l = 0x23,
	.alpha = 0xAAAAAAAA,
	.alpha_hi = 0x6A,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
	.post_div_val = 0x0 << 8,
	.post_div_mask = GENMASK(11, 8),
	//.config_ctl_val = 0x4001055b,//don't use in jvcc
	//.test_ctl_hi1_val = 0x1,
	//.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll gpll4 = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x04000,
#endif
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		//.enable_reg = 0x79000,
		//.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gpll4",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 1000000000,
				[VDD_NOMINAL] = 2000000000
			},
		},
	},
};

static const struct clk_div_table post_div_table_gpll4_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll4_out_main = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x04000,
#endif
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll4_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll4_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll4_out_main",
		.parent_names = (const char *[]){ "gpll4" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 1152MHz configuration */
static const struct alpha_pll_config gpll5_config = {
	.l = 0x3c,
	.alpha = 0x0,
	.vco_val = 0x1 << 20,
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
	.post_div_val = 0x0 << 8,
	.post_div_mask = GENMASK(11, 8),
	//.config_ctl_val = 0x4001055b,//don't use in jvcc
	//.test_ctl_hi1_val = 0x1,
	//.test_ctl_hi_mask = 0x1,
};

static struct clk_alpha_pll gpll5 = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x05000,
#endif
	//.vco_table = default_vco,
	//.num_vco = ARRAY_SIZE(default_vco),
	.vco_table = alpha_vco,
	.num_vco = ARRAY_SIZE(alpha_vco),
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		//.enable_reg = 0x79000,
		//.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "gpll5",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_mx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 1000000000,
				[VDD_LOW] = 2000000000
			},
		},
	},
};

static const struct clk_div_table post_div_table_gpll5_out_main[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpll5_out_main = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x05000,
#endif
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpll5_out_main,
	.num_post_div = ARRAY_SIZE(post_div_table_gpll5_out_main),
	.width = 4,
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpll5_out_main",
		.parent_names = (const char *[]){ "gpll5" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_ro_ops,
	},
};

/* 768MHz configuration */
static const struct alpha_pll_config gpll6_config = {
	.l = 0x28,
	.alpha = 0x0,
	.vco_val = 0x2 << 20,
	.vco_mask = GENMASK(21, 20),
	.early_output_mask = BIT(3),
	.post_div_val = 0x1 << 8,
	.post_div_mask = GENMASK(11, 8),
	//.config_ctl_val = 0x4001055b,//don't use in jvcc
	//.test_ctl_hi1_val = 0x1,
	//.test_ctl_hi_mask = 0x1,
};


static struct clk_alpha_pll gpll6 = {
#ifdef JVCC_HAPS
	.offset = JVCC_REGISTER_TBD,
#else
	.offset = 0x06000,
#endif
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.regs = clk_alpha_pll_regs_offset[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		//.enable_reg = 0x79000,
		//.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "gpll6",
			.parent_names = (const char *[]){ "bi_tcxo" },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_LOWER] = 1000000000,
				[VDD_NOMINAL] = 2000000000
			},
		},
	},
};

static const struct freq_tbl ftbl_gcc_camss_axi_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_AUX2, 1.5, 0, 0),
	F(300000000, P_GPLL0_OUT_AUX2, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_axi_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x1e02c,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_camss_axi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_axi_clk_src",
		.parent_names = gcc_parent_names_7,
		.num_parents = 6,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 150000000,
			[VDD_LOW_L1] = 200000000,
			[VDD_NOMINAL] = 300000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_cci_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(37500000, P_GPLL0_OUT_AUX2, 8, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_cci_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x1c000,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_9,
	.freq_tbl = ftbl_gcc_camss_cci_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_cci_clk_src",
		.parent_names = gcc_parent_names_9,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 37500000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_csi0phytimer_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(100000000, P_GPLL0_OUT_EARLY, 6, 0, 0),
	F(200000000, P_GPLL0_OUT_EARLY, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_csi0phytimer_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x17000,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_csi0phytimer_clk_src",
		.parent_names = gcc_parent_names_4,
		.num_parents = 5,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_NOMINAL] = 240000000},
	},
};

static struct clk_rcg2 gcc_camss_csi1phytimer_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x1701c,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_csi1phytimer_clk_src",
		.parent_names = gcc_parent_names_4,
		.num_parents = 5,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_NOMINAL] = 240000000},
	},
};

static struct clk_rcg2 gcc_camss_csi2phytimer_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x17038,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_4,
	.freq_tbl = ftbl_gcc_camss_csi0phytimer_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_csi2phytimer_clk_src",
		.parent_names = gcc_parent_names_4,
		.num_parents = 5,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_NOMINAL] = 240000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_mclk0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(24000000, P_GPLL1_OUT_EARLY, 2, 1, 24),
	F(64000000, P_GPLL1_OUT_EARLY, 9, 1, 2),
	{ }
};

static struct clk_rcg2 gcc_camss_mclk0_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x18000,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk0_clk_src",
		.parent_names = gcc_parent_names_3,
		.num_parents = 7,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static struct clk_rcg2 gcc_camss_mclk1_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x1801c,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk1_clk_src",
		.parent_names = gcc_parent_names_3,
		.num_parents = 7,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static struct clk_rcg2 gcc_camss_mclk2_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x18038,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk2_clk_src",
		.parent_names = gcc_parent_names_3,
		.num_parents = 7,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static struct clk_rcg2 gcc_camss_mclk3_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x18054,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_3,
	.freq_tbl = ftbl_gcc_camss_mclk0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_mclk3_clk_src",
		.parent_names = gcc_parent_names_3,
		.num_parents = 7,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 64000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_ope_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(171428571, P_GPLL0_OUT_EARLY, 3.5, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_ope_ahb_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x1b024,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_ope_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_ope_ahb_clk_src",
		.parent_names = gcc_parent_names_8,
		.num_parents = 8,
		//.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 171428571,
			[VDD_NOMINAL] = 240000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_ope_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F_SLEW(200000000, P_GPLL2_OUT_MAIN, 2, 0, 0, 800000000),
	F_SLEW(266600000, P_GPLL2_OUT_MAIN, 1, 0, 0, 533200000),
	F_SLEW(465000000, P_GPLL2_OUT_MAIN, 1, 0, 0, 930000000),
	F_SLEW(580000000, P_GPLL2_OUT_EARLY, 1, 0, 0, 580000000),
	{ }
};

static struct clk_rcg2 gcc_camss_ope_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x1b004,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_8,
	.freq_tbl = ftbl_gcc_camss_ope_clk_src,
	.enable_safe_config = true,
	.flags = RCG_UPDATE_BEFORE_PLL,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_ope_clk_src",
		.parent_names = gcc_parent_names_8,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 200000000,
			[VDD_LOW_L1] = 266600000,
			[VDD_NOMINAL] = 465000000,
			[VDD_HIGH] = 580000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_0_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(128000000, P_GPLL5_OUT_MAIN, 9, 0, 0),
	F(192000000, P_GPLL5_OUT_MAIN, 6, 0, 0),
	F(256000000, P_GPLL5_OUT_MAIN, 4.5, 0, 0),
	F(288000000, P_GPLL5_OUT_MAIN, 4, 0, 0),
	F(384000000, P_GPLL5_OUT_MAIN, 3, 0, 0),
	F(460800000, P_GPLL5_OUT_MAIN, 2.5, 0, 0),
	F(576000000, P_GPLL5_OUT_MAIN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_0_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x19004,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_0_clk_src",
		.parent_names = gcc_parent_names_5,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 256000000,
			[VDD_LOW_L1] = 460800000,
			[VDD_NOMINAL] = 576000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_0_csid_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(120000000, P_GPLL0_OUT_EARLY, 5, 0, 0),
	F(192000000, P_GPLL6_OUT_EARLY, 4, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	F(256000000, P_GPLL6_OUT_EARLY, 3, 0, 0),
	F(384000000, P_GPLL6_OUT_EARLY, 2, 0, 0),
	F(426400000, P_GPLL3_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_0_csid_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x19094,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_0_csid_clk_src",
		.parent_names = gcc_parent_names_6,
		.num_parents = 8,
		//.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 256000000,
			[VDD_LOW_L1] = 384000000,
			[VDD_HIGH] = 426400000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x19024,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_1_clk_src",
		.parent_names = gcc_parent_names_5,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 256000000,
			[VDD_LOW_L1] = 460800000,
			[VDD_NOMINAL] = 576000000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_1_csid_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x190b4,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_1_csid_clk_src",
		.parent_names = gcc_parent_names_6,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 256000000,
			[VDD_LOW_L1] = 384000000,
			[VDD_HIGH] = 426400000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_2_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x19044,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_5,
	.freq_tbl = ftbl_gcc_camss_tfe_0_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_2_clk_src",
		.parent_names = gcc_parent_names_5,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 256000000,
			[VDD_LOW_L1] = 460800000,
			[VDD_NOMINAL] = 576000000},
	},
};

static struct clk_rcg2 gcc_camss_tfe_2_csid_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x190d4,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_6,
	.freq_tbl = ftbl_gcc_camss_tfe_0_csid_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_2_csid_clk_src",
		.parent_names = gcc_parent_names_6,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 256000000,
			[VDD_LOW_L1] = 384000000,
			[VDD_HIGH] = 426400000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_tfe_cphy_rx_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(256000000, P_GPLL6_OUT_EARLY, 3, 0, 0),
	F(340000000, P_GPLL4_OUT_MAIN, 2, 0, 0),
	F(384000000, P_GPLL6_OUT_EARLY, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_tfe_cphy_rx_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x19064,
#endif
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = gcc_parent_map_10,
	.freq_tbl = ftbl_gcc_camss_tfe_cphy_rx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_tfe_cphy_rx_clk_src",
		.parent_names = gcc_parent_names_10,
		.num_parents = 8,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 256000000,
			[VDD_LOW_L1] = 340000000,
			[VDD_NOMINAL] = 384000000},
	},
};

static const struct freq_tbl ftbl_gcc_camss_top_ahb_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(40000000, P_GPLL0_OUT_AUX2, 7.5, 0, 0),
	F(80000000, P_GPLL0_OUT_EARLY, 7.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_camss_top_ahb_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x1e010,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_7,
	.freq_tbl = ftbl_gcc_camss_top_ahb_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_camss_top_ahb_clk_src",
		.parent_names = gcc_parent_names_7,
		.num_parents = 6,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000,
			[VDD_LOW] = 80000000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_axi_clk_src[] = {
	F(25000000, P_GPLL0_OUT_AUX2, 12, 0, 0),
	F(50000000, P_GPLL0_OUT_AUX2, 6, 0, 0),
	F(100000000, P_GPLL0_OUT_AUX2, 3, 0, 0),
	F(200000000, P_GPLL0_OUT_EARLY, 3, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_axi_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x16020,
#endif
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_axi_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_axi_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 50000000,
			[VDD_LOW] = 100000000,
			[VDD_NOMINAL] = 200000000,
			[VDD_HIGH] = 240000000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_phy_aux_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x16060,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_phy_aux_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_phy_aux_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 19200000},
	},
};

static const struct freq_tbl ftbl_gcc_ufs_phy_unipro_core_clk_src[] = {
	F(37500000, P_GPLL0_OUT_AUX2, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_ufs_phy_unipro_core_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x16044,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_ufs_phy_unipro_core_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_ufs_phy_unipro_core_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 37500000,
			[VDD_LOW] = 75000000,
			[VDD_NOMINAL] = 150000000},
	},
};

static struct clk_branch gcc_camera_ahb_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = JVCC_REGISTER_TBD,
	.hwcg_bit = 1,
#else
	.halt_reg = 0x0d004,
	.halt_check = BRANCH_HALT_DELAY,
	.hwcg_reg = 0x0d004,
	.hwcg_bit = 1,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x0d004,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camera_xo_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x0d008,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x0d008,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camera_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_axi_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1e044,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1e044,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_axi_clk",
			.parent_names = (const char *[]){
				"gcc_camss_axi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cci_0_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1c018,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1c018,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cci_0_clk",
			.parent_names = (const char *[]){
				"gcc_camss_cci_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cphy_0_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x19088,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x19088,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cphy_0_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cphy_1_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1908c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1908c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cphy_1_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_cphy_2_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x19090,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x19090,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_cphy_2_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi0phytimer_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x17018,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x17018,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi0phytimer_clk",
			.parent_names = (const char *[]){
				"gcc_camss_csi0phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi1phytimer_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x17034,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x17034,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi1phytimer_clk",
			.parent_names = (const char *[]){
				"gcc_camss_csi1phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_csi2phytimer_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x17050,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x17050,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_csi2phytimer_clk",
			.parent_names = (const char *[]){
				"gcc_camss_csi2phytimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk0_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x18018,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x18018,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk0_clk",
			.parent_names = (const char *[]){
				"gcc_camss_mclk0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk1_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x18034,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x18034,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk1_clk",
			.parent_names = (const char *[]){
				"gcc_camss_mclk1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk2_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x18050,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x18050,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk2_clk",
			.parent_names = (const char *[]){
				"gcc_camss_mclk2_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_mclk3_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1806c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1806c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_mclk3_clk",
			.parent_names = (const char *[]){
				"gcc_camss_mclk3_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_nrt_axi_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1e04c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1e04c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_nrt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ope_ahb_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1b03c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1b03c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_ope_ahb_clk",
			.parent_names = (const char *[]){
				"gcc_camss_ope_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_ope_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1b01c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1b01c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_ope_clk",
			.parent_names = (const char *[]){
				"gcc_camss_ope_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_rt_axi_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1e054,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1e054,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_rt_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_0_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1901c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1901c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_0_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_0_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_0_cphy_rx_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1907c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1907c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_0_cphy_rx_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_0_csid_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x190ac,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x190ac,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_0_csid_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_0_csid_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_1_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1903c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1903c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_1_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_1_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_1_cphy_rx_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x19080,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x19080,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_1_cphy_rx_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_1_csid_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x190cc,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x190cc,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_1_csid_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_1_csid_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_2_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1905c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1905c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_2_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_2_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_2_cphy_rx_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x19084,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x19084,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_2_cphy_rx_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_cphy_rx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_tfe_2_csid_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x190ec,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x190ec,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_tfe_2_csid_clk",
			.parent_names = (const char *[]){
				"gcc_camss_tfe_2_csid_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_top_ahb_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1e028,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1e028,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_top_ahb_clk",
			.parent_names = (const char *[]){
				"gcc_camss_top_ahb_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_ufs_phy_axi_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x0a010,//TBD,JVCC_UFS_SYS_NOC_AXI_CBCR
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x0a010,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_ufs_phy_axi_clk",
			.parent_names = (const char *[]){
				"gcc_ufs_phy_axi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_clkref_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x8c000,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x8c000,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_clkref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_ahb_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = JVCC_REGISTER_TBD,
	.hwcg_bit = 1,
#else
	.halt_reg = 0x16014,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x16014,
	.hwcg_bit = 1,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x16014,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_axi_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = JVCC_REGISTER_TBD,
	.hwcg_bit = 1,
#else
	.halt_reg = 0x16010,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x16010,
	.hwcg_bit = 1,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x16010,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_axi_clk",
			.parent_names = (const char *[]){
				"gcc_ufs_phy_axi_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_phy_aux_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = JVCC_REGISTER_TBD,
	.hwcg_bit = 1,
#else
	.halt_reg = 0x1605c,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x1605c,
	.hwcg_bit = 1,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1605c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_phy_aux_clk",
			.parent_names = (const char *[]){
				"gcc_ufs_phy_phy_aux_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_rx_symbol_0_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1601c,
	.halt_check = BRANCH_HALT_SKIP,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1601c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_rx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_tx_symbol_0_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x16018,
	.halt_check = BRANCH_HALT_SKIP,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x16018,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_tx_symbol_0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_unipro_core_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = JVCC_REGISTER_TBD,
	.hwcg_bit = 1,
#else
	.halt_reg = 0x16040,
	.halt_check = BRANCH_HALT,
	.hwcg_reg = 0x16040,
	.hwcg_bit = 1,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x16040,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_unipro_core_clk",
			.parent_names = (const char *[]){
				"gcc_ufs_phy_unipro_core_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};


static struct clk_branch gcc_camss_nrt_pdxfifo_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x20000,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x20000,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_nrt_pdxfifo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_nrt_pdxfifo_top_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x2000c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(22),
		.enable_is_inverted = true,
	#else
		.enable_reg = 0x2000c,
		.enable_mask = BIT(22),
		.enable_is_inverted = true,
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_nrt_pdxfifo_top_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_rt_pdxfifo_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1f000,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x1f000,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_rt_pdxfifo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_camss_rt_pdxfifo_top_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x1f00c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(22),
		.enable_is_inverted = true,
	#else
		.enable_reg = 0x1f00c,
		.enable_mask = BIT(22),
		.enable_is_inverted = true,
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_camss_rt_pdxfifo_top_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_pdxfifo_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x21000,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x21000,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_pdxfifo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_ufs_phy_pdxfifo_top_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x2100c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(22),
		.enable_is_inverted = true,
	#else
		.enable_reg = 0x2100c,
		.enable_mask = BIT(22),
		.enable_is_inverted = true,
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ufs_phy_pdxfifo_top_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

const struct clk_ops clk_null_ops = {
};

static struct clk_regmap bi_tcxo = {
	.hw.init = &(struct clk_init_data){
		.name = "bi_tcxo",
		.parent_names = (const char *[]){
			"xo_board",
		},
		.num_parents = 1,
		.ops = &clk_null_ops,
	},
};

//gcc_sys_noc_clk_src
static const struct freq_tbl ftbl_gcc_sys_noc_clk_src[] = {
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	F(120000000, P_GPLL0_OUT_AUX2, 2.5, 0, 0),
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(200000000, P_GPLL0_OUT_AUX2, 1.5, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_sys_noc_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x0a03c,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_sys_noc_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_sys_noc_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 4,
		//.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 75000000,
			[VDD_LOW] = 120000000,
			[VDD_LOW_L1] = 150000000,
			[VDD_NOMINAL] = 200000000,
			[VDD_NOMINAL_L1] = 240000000},
	},
};

//gcc_mm_snoc_rt_qx_clk_src
static const struct freq_tbl ftbl_gcc_mm_snoc_rt_qx_clk_src[] = {
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	F(300000000, P_GPLL0_OUT_AUX2, 1, 0, 0),
	F(355333333, P_GPLL3_OUT_EARLY, 3, 0, 0),
	F(426400000, P_GPLL3_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_mm_snoc_rt_qx_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x0b004,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_mm_snoc_rt_qx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_mm_snoc_rt_qx_clk_src",
		.parent_names = gcc_parent_names_11,
		.num_parents = 6,
		//.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 150000000,
			[VDD_LOW] = 240000000,
			[VDD_LOW_L1] = 300000000,
			[VDD_NOMINAL] = 355333333,
			[VDD_NOMINAL_L1] = 426400000},
	},
};

//gcc_mm_snoc_nrt_qx_clk_src
static const struct freq_tbl ftbl_gcc_mm_snoc_nrt_qx_clk_src[] = {
	F(150000000, P_GPLL0_OUT_AUX2, 2, 0, 0),
	F(240000000, P_GPLL0_OUT_EARLY, 2.5, 0, 0),
	F(300000000, P_GPLL0_OUT_AUX2, 1, 0, 0),
	F(355333333, P_GPLL3_OUT_EARLY, 3, 0, 0),
	F(426400000, P_GPLL3_OUT_EARLY, 2.5, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_mm_snoc_nrt_qx_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x0c004,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_11,
	.freq_tbl = ftbl_gcc_mm_snoc_nrt_qx_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_mm_snoc_nrt_qx_clk_src",
		.parent_names = gcc_parent_names_11,
		.num_parents = 6,
		//.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 150000000,
			[VDD_LOW] = 240000000,
			[VDD_LOW_L1] = 300000000,
			[VDD_NOMINAL] = 355333333,
			[VDD_NOMINAL_L1] = 426400000},
	},
};

//gcc_config_noc_clk_src
static const struct freq_tbl ftbl_gcc_config_noc_clk_src[] = {
	F(33333333, P_GPLL0_OUT_AUX2, 9, 0, 0),
	F(37500000, P_GPLL0_OUT_AUX2, 8, 0, 0),
	F(75000000, P_GPLL0_OUT_AUX2, 4, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_config_noc_clk_src = {
#ifdef JVCC_HAPS
	.cmd_rcgr = JVCC_REGISTER_TBD,
#else
	.cmd_rcgr = 0x0a020,
#endif
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gcc_parent_map_0,
	.freq_tbl = ftbl_gcc_config_noc_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_config_noc_clk_src",
		.parent_names = gcc_parent_names_0,
		.num_parents = 4,
		//.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 33333333,
			[VDD_LOW] = 37500000,
			[VDD_NOMINAL] = 75000000},
	},
};

static struct clk_branch gcc_sys_noc_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x0a00c,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x0a00c,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_clk",
			.parent_names = (const char *[]){
				"gcc_sys_noc_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_rt_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x0a014,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x0a014,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_rt_clk",
			.parent_names = (const char *[]){
				"gcc_mm_snoc_rt_qx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sys_noc_nrt_clk = {
#ifdef JVCC_HAPS
	.halt_reg = JVCC_REGISTER_TBD,
	.halt_check = BRANCH_HALT,
#else
	.halt_reg = 0x0a018,
	.halt_check = BRANCH_HALT,
#endif
	.clkr = {
	#ifdef JVCC_HAPS
		.enable_reg = JVCC_REGISTER_TBD,
		.enable_mask = BIT(0),
	#else
		.enable_reg = 0x0a018,
		.enable_mask = BIT(0),
	#endif
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sys_noc_nrt_clk",
			.parent_names = (const char *[]){
				"gcc_mm_snoc_nrt_qx_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gcc_bengal_clocks[] = {
	[GCC_CAMERA_AHB_CLK] = &gcc_camera_ahb_clk.clkr,
	[GCC_CAMERA_XO_CLK] = &gcc_camera_xo_clk.clkr,
	[GCC_CAMSS_AXI_CLK] = &gcc_camss_axi_clk.clkr,
	[GCC_CAMSS_AXI_CLK_SRC] = &gcc_camss_axi_clk_src.clkr,
	[GCC_CAMSS_CCI_0_CLK] = &gcc_camss_cci_0_clk.clkr,
	[GCC_CAMSS_CCI_CLK_SRC] = &gcc_camss_cci_clk_src.clkr,
	[GCC_CAMSS_CPHY_0_CLK] = &gcc_camss_cphy_0_clk.clkr,
	[GCC_CAMSS_CPHY_1_CLK] = &gcc_camss_cphy_1_clk.clkr,
	[GCC_CAMSS_CPHY_2_CLK] = &gcc_camss_cphy_2_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK] = &gcc_camss_csi0phytimer_clk.clkr,
	[GCC_CAMSS_CSI0PHYTIMER_CLK_SRC] = &gcc_camss_csi0phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK] = &gcc_camss_csi1phytimer_clk.clkr,
	[GCC_CAMSS_CSI1PHYTIMER_CLK_SRC] = &gcc_camss_csi1phytimer_clk_src.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK] = &gcc_camss_csi2phytimer_clk.clkr,
	[GCC_CAMSS_CSI2PHYTIMER_CLK_SRC] = &gcc_camss_csi2phytimer_clk_src.clkr,
	[GCC_CAMSS_MCLK0_CLK] = &gcc_camss_mclk0_clk.clkr,
	[GCC_CAMSS_MCLK0_CLK_SRC] = &gcc_camss_mclk0_clk_src.clkr,
	[GCC_CAMSS_MCLK1_CLK] = &gcc_camss_mclk1_clk.clkr,
	[GCC_CAMSS_MCLK1_CLK_SRC] = &gcc_camss_mclk1_clk_src.clkr,
	[GCC_CAMSS_MCLK2_CLK] = &gcc_camss_mclk2_clk.clkr,
	[GCC_CAMSS_MCLK2_CLK_SRC] = &gcc_camss_mclk2_clk_src.clkr,
	[GCC_CAMSS_MCLK3_CLK] = &gcc_camss_mclk3_clk.clkr,
	[GCC_CAMSS_MCLK3_CLK_SRC] = &gcc_camss_mclk3_clk_src.clkr,
	[GCC_CAMSS_NRT_AXI_CLK] = &gcc_camss_nrt_axi_clk.clkr,
	[GCC_CAMSS_OPE_AHB_CLK] = &gcc_camss_ope_ahb_clk.clkr,
	[GCC_CAMSS_OPE_AHB_CLK_SRC] = &gcc_camss_ope_ahb_clk_src.clkr,
	[GCC_CAMSS_OPE_CLK] = &gcc_camss_ope_clk.clkr,
	[GCC_CAMSS_OPE_CLK_SRC] = &gcc_camss_ope_clk_src.clkr,
	[GCC_CAMSS_RT_AXI_CLK] = &gcc_camss_rt_axi_clk.clkr,
	[GCC_CAMSS_TFE_0_CLK] = &gcc_camss_tfe_0_clk.clkr,
	[GCC_CAMSS_TFE_0_CLK_SRC] = &gcc_camss_tfe_0_clk_src.clkr,
	[GCC_CAMSS_TFE_0_CPHY_RX_CLK] = &gcc_camss_tfe_0_cphy_rx_clk.clkr,
	[GCC_CAMSS_TFE_0_CSID_CLK] = &gcc_camss_tfe_0_csid_clk.clkr,
	[GCC_CAMSS_TFE_0_CSID_CLK_SRC] = &gcc_camss_tfe_0_csid_clk_src.clkr,
	[GCC_CAMSS_TFE_1_CLK] = &gcc_camss_tfe_1_clk.clkr,
	[GCC_CAMSS_TFE_1_CLK_SRC] = &gcc_camss_tfe_1_clk_src.clkr,
	[GCC_CAMSS_TFE_1_CPHY_RX_CLK] = &gcc_camss_tfe_1_cphy_rx_clk.clkr,
	[GCC_CAMSS_TFE_1_CSID_CLK] = &gcc_camss_tfe_1_csid_clk.clkr,
	[GCC_CAMSS_TFE_1_CSID_CLK_SRC] = &gcc_camss_tfe_1_csid_clk_src.clkr,
	[GCC_CAMSS_TFE_2_CLK] = &gcc_camss_tfe_2_clk.clkr,
	[GCC_CAMSS_TFE_2_CLK_SRC] = &gcc_camss_tfe_2_clk_src.clkr,
	[GCC_CAMSS_TFE_2_CPHY_RX_CLK] = &gcc_camss_tfe_2_cphy_rx_clk.clkr,
	[GCC_CAMSS_TFE_2_CSID_CLK] = &gcc_camss_tfe_2_csid_clk.clkr,
	[GCC_CAMSS_TFE_2_CSID_CLK_SRC] = &gcc_camss_tfe_2_csid_clk_src.clkr,
	[GCC_CAMSS_TFE_CPHY_RX_CLK_SRC] = &gcc_camss_tfe_cphy_rx_clk_src.clkr,
	[GCC_CAMSS_TOP_AHB_CLK] = &gcc_camss_top_ahb_clk.clkr,
	[GCC_CAMSS_TOP_AHB_CLK_SRC] = &gcc_camss_top_ahb_clk_src.clkr,
	[GCC_SYS_NOC_UFS_PHY_AXI_CLK] = &gcc_sys_noc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_CLKREF_CLK] = &gcc_ufs_clkref_clk.clkr,
	[GCC_UFS_PHY_AHB_CLK] = &gcc_ufs_phy_ahb_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK] = &gcc_ufs_phy_axi_clk.clkr,
	[GCC_UFS_PHY_AXI_CLK_SRC] = &gcc_ufs_phy_axi_clk_src.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK] = &gcc_ufs_phy_phy_aux_clk.clkr,
	[GCC_UFS_PHY_PHY_AUX_CLK_SRC] = &gcc_ufs_phy_phy_aux_clk_src.clkr,
	[GCC_UFS_PHY_RX_SYMBOL_0_CLK] = &gcc_ufs_phy_rx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_TX_SYMBOL_0_CLK] = &gcc_ufs_phy_tx_symbol_0_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK] = &gcc_ufs_phy_unipro_core_clk.clkr,
	[GCC_UFS_PHY_UNIPRO_CORE_CLK_SRC] =
		&gcc_ufs_phy_unipro_core_clk_src.clkr,
	[GPLL0] = &gpll0.clkr,
	[GPLL0_OUT_AUX2] = &gpll0_out_aux2.clkr,
	[GPLL0_OUT_MAIN] = &gpll0_out_main.clkr,
	[GPLL1] = &gpll1.clkr,
	[GPLL2] = &gpll2.clkr,
	[GPLL2_OUT_MAIN] = &gpll2_out_main.clkr,
	[GPLL3] = &gpll3.clkr,
	[GPLL4] = &gpll4.clkr,
	[GPLL4_OUT_MAIN] = &gpll4_out_main.clkr,
	[GPLL5] = &gpll5.clkr,
	[GPLL5_OUT_MAIN] = &gpll5_out_main.clkr,
	[GPLL6] = &gpll6.clkr,
	[GCC_BI_TCXO] = &bi_tcxo,

	[GCC_CAMSS_RT_PDXFIFO_CLK] = &gcc_camss_rt_pdxfifo_clk.clkr,
	[GCC_CAMSS_RT_PDXFIFO_TOP_CLK] = &gcc_camss_rt_pdxfifo_top_clk.clkr,
	[GCC_CAMSS_NRT_PDXFIFO_CLK] = &gcc_camss_nrt_pdxfifo_clk.clkr,
	[GCC_CAMSS_NRT_PDXFIFO_TOP_CLK] = &gcc_camss_nrt_pdxfifo_top_clk.clkr,
	[GCC_UFS_PHY_PDXFIFO_CLK] = &gcc_ufs_phy_pdxfifo_clk.clkr,
	[GCC_UFS_PHY_PDXFIFO_TOP_CLK] = &gcc_ufs_phy_pdxfifo_top_clk.clkr,

	[GCC_SYS_NOC_CLK] = &gcc_sys_noc_clk.clkr,
	[GCC_SYS_NOC_CLK_SRC] = &gcc_sys_noc_clk_src.clkr,
	[GCC_MM_SNOC_RT_QX_CLK_SRC] = &gcc_mm_snoc_rt_qx_clk_src.clkr,
	[GCC_MM_SNOC_NRT_QX_CLK_SRC] = &gcc_mm_snoc_nrt_qx_clk_src.clkr,
	[GCC_SYS_NOC_RT_CLK] = &gcc_sys_noc_rt_clk.clkr,
	[GCC_SYS_NOC_NRT_CLK] = &gcc_sys_noc_nrt_clk.clkr,
	[GCC_CONFIG_NOC_CLK] = &gcc_config_noc_clk_src.clkr,
};

static const struct qcom_reset_map gcc_bengal_resets[] = {
	[GCC_UFS_PHY_BCR] = { 0x16000 },
};

static const struct clk_rcg_dfs_data gcc_dfs_clocks[] = {
};

static const struct regmap_config gcc_bengal_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xc7000,
	.fast_io = true,
};

static const struct qcom_cc_desc gcc_bengal_desc = {
	.config = &gcc_bengal_regmap_config,
	.clks = gcc_bengal_clocks,
	.num_clks = ARRAY_SIZE(gcc_bengal_clocks),
	.resets = gcc_bengal_resets,
	.num_resets = ARRAY_SIZE(gcc_bengal_resets),
};

static const struct of_device_id gcc_bengal_match_table[] = {
	{ .compatible = "qcom,bengal-gcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_bengal_match_table);

static int gcc_bengal_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	regmap = qcom_cc_map(pdev, &gcc_bengal_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	vdd_cx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_cx");
	if (IS_ERR(vdd_cx.regulator[0])) {
		if (!(PTR_ERR(vdd_cx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_cx regulator\n");
		return PTR_ERR(vdd_cx.regulator[0]);
	}

	vdd_cx_ao.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_cx_ao");
	if (IS_ERR(vdd_cx_ao.regulator[0])) {
		if (!(PTR_ERR(vdd_cx_ao.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_cx_ao regulator\n");
		return PTR_ERR(vdd_cx_ao.regulator[0]);
	}

	vdd_mx.regulator[0] = devm_regulator_get(&pdev->dev, "vdd_mx");
	if (IS_ERR(vdd_mx.regulator[0])) {
		if (!(PTR_ERR(vdd_mx.regulator[0]) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_mx regulator\n");
		return PTR_ERR(vdd_mx.regulator[0]);
	}

	ret = qcom_cc_register_rcg_dfs(regmap, gcc_dfs_clocks,
			ARRAY_SIZE(gcc_dfs_clocks));
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GCC dfs clocks\n");
		return ret;
	}

	/* Disable the GPLL0 active input to NPU and GPU via MISC registers */
#ifndef JVCC_HAPS
	clk_alpha_pll_configure(&gpll0, regmap, &gpll0_config);
	clk_alpha_pll_configure(&gpll1, regmap, &gpll1_config);
	clk_alpha_pll_configure(&gpll2, regmap, &gpll2_config);
	clk_alpha_pll_configure(&gpll3, regmap, &gpll3_config);
	clk_alpha_pll_configure(&gpll4, regmap, &gpll4_config);
	clk_alpha_pll_configure(&gpll5, regmap, &gpll5_config);
	clk_alpha_pll_configure(&gpll6, regmap, &gpll6_config);
#endif
	ret = qcom_cc_really_probe(pdev, &gcc_bengal_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GCC clocks\n");
		return ret;
	}

	gcc_ipc_log_init();

	dev_info(&pdev->dev, "Registered GCC clocks\n");
	return ret;
}

static struct platform_driver gcc_bengal_driver = {
	.probe = gcc_bengal_probe,
	.driver = {
		.name = "gcc-bengal",
		.of_match_table = gcc_bengal_match_table,
	},
};

static int __init gcc_bengal_init(void)
{
	return platform_driver_register(&gcc_bengal_driver);
}
subsys_initcall(gcc_bengal_init);

static void __exit gcc_bengal_exit(void)
{
	platform_driver_unregister(&gcc_bengal_driver);
}
module_exit(gcc_bengal_exit);

MODULE_DESCRIPTION("QTI GCC BENGAL Driver");
MODULE_LICENSE("GPL v2");
