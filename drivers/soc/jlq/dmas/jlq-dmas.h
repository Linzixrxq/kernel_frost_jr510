#ifndef __JLQ_DMAS__
#define __JLQ_DMAS__

#define DMAS_EN				(0x00)
#define DMAS_CLR			(0x04)
#define DMAS_STA			(0x08)
#define DMAS_INT_RAW0			(0x0c)
#define DMAS_INT_EN0_1			(0x14)
#define DMAS_INT0_1			(0x20)
#define DMAS_INT_CLR0			(0x28)
#define DMAS_INTV_UNIT			(0x2c)
#define DMAS_INT_RAW1			(0x30c)
#define DMAS_INT_EN1_1			(0x314)
#define DMAS_INT1_1			(0x320)
#define DMAS_INT_CLR1			(0x328)
#define DMAS_LP_CTL			(0x3fc)

/* old reg map for lc181x/lc1860 ap_dmas */
#define DMAS_INT_EN0_0		(0x10)
#define DMAS_INT0_0			(0x1c)
#define DMAS_INT_EN1_0		(0x310)
#define DMAS_INT1_0			(0x31c)

#define DMAS_CH_REG(chip, index)	(chip->regbase + 0x40 + (index) * 0x20)
#define DMAS_CH_SAR(desc)		(desc->regbase)
#define DMAS_CH_DAR(desc)		(desc->regbase + 0x4)
#define DMAS_CH_CTL0(desc)		(desc->regbase + 0x8)
#define DMAS_CH_CTL1(desc)		(desc->regbase + 0xC)
#define DMAS_CH_CA(desc)		(desc->regbase + 0x10)
#define DMAS_CH_INTA(desc)		(desc->regbase + 0x14)
#define DMAS_CH_WD(chip, index)		(chip->regbase + 0x240 + (index) * 4)

#define DMAS_REG(chip, offset)		(chip->regbase + offset)

#define DMAS_NR_CHANNELS	DMAS_CH_MAX

#define DMAS_RX_CHANNEL_BEGIN	(8)

#define DMAS_TIMEOUT_UNIT	(1) /* unit : us. */
#define DMAS_BLK_SIZE_MAX	(64 * 1024 * 2)
					/* Special TOP DMAS 0 Unit:Word */

#define DMAS_INT_CHANNEL(bit)	(bit % 16)
#define DMAS_INT_TYPE(bit)	(1 << (bit / 16))

#define DMAS_BIT_VALUE(reg, mask, off, val) \
	reg = ((reg) & (~((mask) << (off)))) | (val << off)

#define DMAS_CH_REQUESTED	(0x00000001)
#define DMAS_CH_CONFIGURED	(0x00000002)
#define DMAS_CH_BLK_SIZE_UNIT_WORD	(0x00000004)

struct channel_desc;

struct dmas_chip {
	const char		*name;
	void __iomem		*regbase;
	unsigned int		irq;
	spinlock_t		lock;
	struct device		*dev;
	int			base;
	unsigned int		nchannel;
	unsigned int reg_map_type;
	struct channel_desc	*desc;
	struct list_head	list;

	int special_channel;
	unsigned int block_size_max;

	unsigned long		int_en0_reg;
	unsigned long		int0_reg;
	unsigned long		int_en1_reg;
	unsigned long		int1_reg;

	struct			device_node *of_node;
	int			of_dmas_n_cells;
	int (*of_xlate)(struct dmas_chip *dc,
		       const struct of_phandle_args *dmasspec, u32 *flags);
};

struct channel_desc {
	const char		*name;
	struct dmas_chip	*chip;
	unsigned int		index;
	unsigned long		flags;
	bool			is_rx;
	unsigned int		reg_ctl1;
	void __iomem		*regbase;
	void			(*irq_handler)(int, int, void *);
	void			*irq_data;
};

struct of_dmas_data {
	enum of_dmas_flags *flags;
	struct of_phandle_args dmasspec;
	int out_channel;
};

#endif
