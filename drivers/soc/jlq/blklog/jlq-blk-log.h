/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __JLQFILELOG_LIB_H__
#define __JLQFILELOG_LIB_H__

struct jlq_blklog_buffer_head_s {
	unsigned int Sig;
	unsigned int  Flag;
	unsigned int  BufferMaxSize;
	unsigned int  Pos;

	unsigned int  OutPos;
	unsigned int  LastRebootTime;
	unsigned long FlashFun;
	unsigned int  Resv[8];
	char Buffer[0];
}__attribute__((packed));

#define JLQFLOGFLAG_BOOT_VALID    (1 << 0)
#define JLQFLOGFLAG_LOADER_VALID  (1 << 1)
#define JLQFLOGFLAG_BL3_VALID     (1 << 2)
#define JLQFLOGFLAG_BL2_VALID     (1 << 3)
#define JLQFLOGFLAG_RAMDUMP_VALID (1 << 4)

#define JLQFLOGFLAG_FULL          (1 << 29)
#define JLQFLOGFLAG_FINISH        (1 << 30)
#define JLQFLOGFLAG_VALID         (1 << 31)

#define JLQFLOGTYPE_CURRENT    (1)
#define JLQFLOGTYPE_HISTORY    (2)

/*"FlOg"*/
#define JLQFILELOGSIG		(0x4A664C67)

#define JLQ_BLKLOG_MAXSIZE  (2 << 20)

#define JLQ_BLKLOG_YEAR_SHIFT  (25)
#define JLQ_BLKLOG_YEAR_MASK   (0x7f)
#define JLQ_BLKLOG_YEAR(_tm_) \
	((((_tm_) >> JLQ_BLKLOG_YEAR_SHIFT) & JLQ_BLKLOG_YEAR_MASK) + 2000)

#define JLQ_BLKLOG_MON_SHIFT   (21)
#define JLQ_BLKLOG_MON_MASK    (0x1f)
#define JLQ_BLKLOG_MON(_tm_) \
	(((_tm_) >> JLQ_BLKLOG_MON_SHIFT) & JLQ_BLKLOG_MON_MASK)

#define JLQ_BLKLOG_DAY_SHIFT   (16)
#define JLQ_BLKLOG_DAY_MASK    (0x1f)
#define JLQ_BLKLOG_DAY(_tm_) \
	(((_tm_) >> JLQ_BLKLOG_DAY_SHIFT) & JLQ_BLKLOG_DAY_MASK)

#define JLQ_BLKLOG_HOUR_SHIFT  (11)
#define JLQ_BLKLOG_HOUR_MASK   (0x1f)
#define JLQ_BLKLOG_HOUR(_tm_) \
	(((_tm_) >> JLQ_BLKLOG_HOUR_SHIFT) & JLQ_BLKLOG_HOUR_MASK)

#define JLQ_BLKLOG_MIN_SHIFT   (5)
#define JLQ_BLKLOG_MIN_MASK    (0x3f)
#define JLQ_BLKLOG_MIN(_tm_) \
	(((_tm_) >> JLQ_BLKLOG_MIN_SHIFT) & JLQ_BLKLOG_MIN_MASK)

#define JLQ_BLKLOG_SEC_LSHIFT  (1)
#define JLQ_BLKLOG_SEC_MASK    (0x3f)
#define JLQ_BLKLOG_SEC(_tm_) \
	(((_tm_) << JLQ_BLKLOG_SEC_LSHIFT) & JLQ_BLKLOG_SEC_MASK)

#define JLQ_BLKLOG_TIME_SET(_year, _mon, _day, _hour, _min, _sec) \
			(((_year) & JLQ_BLKLOG_YEAR_MASK) << JLQ_BLKLOG_YEAR_SHIFT | \
			((_mon) & JLQ_BLKLOG_MON_MASK) << JLQ_BLKLOG_MON_SHIFT | \
			((_day) & JLQ_BLKLOG_DAY_MASK) << JLQ_BLKLOG_DAY_SHIFT | \
			((_hour) & JLQ_BLKLOG_HOUR_MASK) << JLQ_BLKLOG_HOUR_SHIFT | \
			((_min) & JLQ_BLKLOG_MIN_MASK) << JLQ_BLKLOG_MIN_SHIFT | \
			((_sec) & JLQ_BLKLOG_SEC_MASK) >> JLQ_BLKLOG_SEC_LSHIFT)

#endif
