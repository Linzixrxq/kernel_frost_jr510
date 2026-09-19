// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2019, Linaro Ltd.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sys_soc.h>
#include <linux/types.h>
#include <soc/jlq/socinfo.h>
#include <soc/jlq/jlq_sip.h>
#include <linux/soc/qcom/smem.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

/* proc fs name for cpu_id */
#define SERAIL_NUM	"serial_num"

/*
 * SoC version type with major number in the upper 16 bits and minor
 * number in the lower 16 bits.
 */
#define SOCINFO_MAJOR(ver) (((ver) >> 16) & 0xffff)
#define SOCINFO_MINOR(ver) ((ver) & 0xffff)
#define SOCINFO_VERSION(maj, min)  ((((maj) & 0xffff) << 16)|((min) & 0xffff))

#define SMEM_SOCINFO_BUILD_ID_LENGTH           32
#define SMEM_SOCINFO_CHIP_ID_LENGTH		7
#define EFUSE_MASK				GENMASK(5, 4)

/*
 * SMEM item id, used to acquire handles to respective
 * SMEM region.
 */
#define SMEM_ID_VENDOR2					136
#define SMEM_HW_SW_BUILD_ID            137
#define SMEM_IMAGE_VERSION_TABLE	469

#define MAX_CUSTOM_SOCINFO_ATTR			12

/* cpu id length is from length of member cpuid of struct DalVendor2InfoSMemType */
#define CPU_ID_LENGTH	16

struct socinfo {
	__le32 fmt;
	__le32 id;
	__le32 ver;
	__le32 raw_id;
	__le32 pmic_model;
	__le32 pmic_die_rev;
	__le32 foundry_id;
	__le32 serial_num;
	__le32 fuse_state;
	__le32 board_id;
	__le64 product_id;
	__le32 major;
	__le32 minor;
	char chip_name[SMEM_SOCINFO_CHIP_ID_LENGTH];
	char cpu_id[CPU_ID_LENGTH];
};

struct jlq_socinfo {
	struct soc_device *soc_dev;
	struct socinfo info;
	struct soc_device_attribute attr;
	struct rw_semaphore current_image_rwsem;
	struct proc_dir_entry *proc_entry;
};

struct soc_id {
	unsigned int id;
	const char *name;
};

static const struct soc_id soc_id[] = {
	{ 494, "JLQ JR510"},
	{},
};

static struct jlq_socinfo *jlq_socinfo_data;

static struct attribute *jlq_custom_socinfo_attrs[MAX_CUSTOM_SOCINFO_ATTR];
static struct socinfo *socinfo;
/* sysfs attributes */
#define ATTR_DEFINE(param)	\
	static DEVICE_ATTR(param, 0644,	\
		   jlq_get_##param,	\
		   NULL)

#define BUILD_ID_LENGTH 32
#define CHIP_ID_LENGTH 32
#define SMEM_IMAGE_VERSION_BLOCKS_COUNT 32
#define SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE 128
#define SMEM_IMAGE_VERSION_NAME_SIZE 75
#define SMEM_IMAGE_VERSION_VARIANT_SIZE 20
#define SMEM_IMAGE_VERSION_VARIANT_OFFSET 75
#define SMEM_IMAGE_VERSION_OEM_SIZE 33
#define SMEM_IMAGE_VERSION_OEM_OFFSET 95
#define SMEM_IMAGE_VERSION_PARTITION_APPS 10

typedef struct {
	__le32 nKey;
	__le32 nValue;
} PlatformInfoKVPSType;

struct DalPlatformInfoSMemType {
	/*
	* nFormatVersion: It contains the shared memory format version information,
	* which helps JV and Qualcomm internal shared memory structure to be in sync.
	* If nFormatVersion >= 2 then the subtype is read from the SMEM. If
	* nFormatVersion >= 3 then key value pairs (KVPS) is read from SMEM.
	*/
	u8 nFormatVersion;

	/*
	* ePlatformType: Platform type refers to physically different hardware that
	* is fundamentally incompatible with the existing platforms. ePlatformType is
	* used to identify different platform and differential dependent code.
	*/
	__le32 ePlatformType;

	/*
	* nPlatformSubtype: Subtypes map approximately to a use case of a platform.
	* This is the most common type of a new platform variant request.
	*/
	__le32 nPlatformSubtype;

	/*
	* nHWVersionMajor and nHWVersionMinor: Indicates variations of an existing
	* subtype, such as board workarounds or different displays. Major revisions
	* are used for major platform differences from the base subtype, for example,
	* board workarounds or power grids for a certain subtype.
	*/
	u8 nHWVersionMinor;
	u8 nHWVersionMajor;

	/*
	* bFusion: Set as TRUE for fusion targets type.
	*/
	u8 bFusion;

	/*
	* nNumKVPS: Specifies the number of key value pairs. It is reserved for
	* future use.
	*/
	__le32 nNumKVPS;

	/*
	* aKVPS: Specifies the key value pairs. It is reserved for future use.
	*/
	PlatformInfoKVPSType aKVPS[];
} ;

struct DalVendor2InfoSMemType {
	__le32 hwversion;
	__le32 hwlevel;
	u8 cpuid[32];
};

static int socinfo_get_pmic_model(void)
{
	return socinfo ? le32_to_cpu(socinfo->pmic_model) : 0xFFFFFFFF;
}

static uint32_t socinfo_get_pmic_die_revision(void)
{
	return socinfo ? le32_to_cpu(socinfo->pmic_die_rev) : 0;
}

static uint32_t socinfo_get_foundry_id(void)
{
	return socinfo ? le32_to_cpu(socinfo->foundry_id) : 0;
}

uint32_t socinfo_get_serial_number(void)
{
	return socinfo ? le32_to_cpu(socinfo->serial_num) : 0;
}
EXPORT_SYMBOL(socinfo_get_serial_number);

static uint64_t socinfo_get_product_id(void)
{
	return socinfo ? le64_to_cpu(socinfo->product_id) : 0;
}

static char *socinfo_get_chip_name(void)
{
	return socinfo ? socinfo->chip_name : "N/A";
}

static uint32_t socinfo_get_fuse_state(void)
{
	return socinfo ? le32_to_cpu(socinfo->fuse_state) : 0;
}

static ssize_t jlq_get_fuse_state(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", socinfo_get_fuse_state());
}
ATTR_DEFINE(fuse_state);


static uint32_t socinfo_get_board_id(void)
{
	return socinfo ? le32_to_cpu(socinfo->board_id) : 0;
}

static ssize_t jlq_get_board_id(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", socinfo_get_board_id());
}
ATTR_DEFINE(board_id);

static uint32_t socinfo_get_major(void)
{
	return socinfo ? le32_to_cpu(socinfo->major) : 0;
}

static uint32_t socinfo_get_minor(void)
{
	return socinfo ? le32_to_cpu(socinfo->minor) : 0;
}

static ssize_t jlq_get_hwversion(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	uint32_t board_id = socinfo_get_board_id();
	uint32_t id;

	board_id = board_id >> 8;
	board_id = board_id & 0xffff;
	if (board_id == 0x201)
		id = 1;
	else if (board_id == 0x205)
		id = 2;
	else
		id = 0;
	return snprintf(buf, PAGE_SIZE, "1.%u%u.%u\n", id, socinfo_get_major(),
			socinfo_get_minor());
}
ATTR_DEFINE(hwversion);

static ssize_t jlq_get_pmic_model(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", socinfo_get_pmic_model());
}
ATTR_DEFINE(pmic_model);

static ssize_t jlq_get_pmic_die_revision(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
			socinfo_get_pmic_die_revision());
}
ATTR_DEFINE(pmic_die_revision);

static ssize_t jlq_get_foundry_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", socinfo_get_foundry_id());
}
ATTR_DEFINE(foundry_id);

static ssize_t jlq_get_serial_number(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", socinfo_get_serial_number());
}
ATTR_DEFINE(serial_number);

static ssize_t jlq_get_product_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%llx\n", socinfo_get_product_id());
}
ATTR_DEFINE(product_id);

static ssize_t jlq_get_chip_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%-.32s\n", socinfo_get_chip_name());
}
ATTR_DEFINE(chip_name);

static umode_t soc_info_attribute(struct kobject *kobj,
		struct attribute *attr, int index)
{
	return attr->mode;
}

static const struct attribute_group custom_soc_attr_group = {
	.attrs = jlq_custom_socinfo_attrs,
	.is_visible = soc_info_attribute,
};

static void socinfo_populate_sysfs(struct jlq_socinfo *jlq_socinfo)
{
	int i = 0;

	jlq_custom_socinfo_attrs[i++] = &dev_attr_product_id.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_chip_name.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_serial_number.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_foundry_id.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_pmic_model.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_pmic_die_revision.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_board_id.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_hwversion.attr;
	jlq_custom_socinfo_attrs[i++] = &dev_attr_fuse_state.attr;
	jlq_custom_socinfo_attrs[i++] = NULL;

	WARN_ON(i >= MAX_CUSTOM_SOCINFO_ATTR);
	jlq_socinfo->attr.custom_attr_group = &custom_soc_attr_group;
}

static void socinfo_print(void)
{
	uint32_t f_maj = SOCINFO_MAJOR(le32_to_cpu(socinfo->fmt));
	uint32_t f_min = SOCINFO_MINOR(le32_to_cpu(socinfo->fmt));
	uint32_t v_maj = SOCINFO_MAJOR(le32_to_cpu(socinfo->ver));
	uint32_t v_min = SOCINFO_MINOR(le32_to_cpu(socinfo->ver));

	pr_info("v%u.%u, id=%u, ver=%u.%u, raw_id=%u, pmic_model=%u \
		pmic_die_rev =%u foundry_id=%u serial_num %u product_id %llu\n",
		f_maj, f_min, socinfo->id, v_maj, v_min,
		socinfo->raw_id,
		socinfo->pmic_model,
		socinfo->pmic_die_rev,
		socinfo->foundry_id,
		socinfo->serial_num,
		socinfo->product_id);
}

static const char *socinfo_machine(unsigned int id)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(soc_id); idx++) {
		if (soc_id[idx].id == id)
			return soc_id[idx].name;
	}

	return NULL;
}


static unsigned char hexToChar(unsigned char ch)
{
	if (ch >= 0x00 && ch <= 0x09)
		return ('0' + (ch - 0x0));
	else if (ch >= 0x0a && ch <= 0x0f)
		return ('a' + (ch - 0x0a));

	return '0';
}

static int sn_read(struct seq_file *m, void *v)
{
	int i;
	unsigned char ch1, ch2;
	char cpu_id_string[CPU_ID_LENGTH * 2 + 3] = "0x\0";
	char *cpu_id_ptr = jlq_socinfo_data->info.cpu_id;

	for (i = 0; i < CPU_ID_LENGTH; i++) {
		ch1 = (cpu_id_ptr[i] >> 4) & 0x0F;
		ch2 = cpu_id_ptr[i] & 0x0F;
		cpu_id_string[2 * i + 2] = hexToChar(ch1);
		cpu_id_string[2 * i + 3] = hexToChar(ch2);
	}

	seq_printf(m, "%s\n", cpu_id_string);

	return 0;
}

static int sn_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sn_read, NULL);
}

static const struct file_operations sn_fops = {
	.open		= sn_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

uint32_t socinfo_get_id(void)
{
	return (socinfo) ? le32_to_cpu(socinfo->id) : 0;
}
EXPORT_SYMBOL(socinfo_get_id);

const char *socinfo_get_id_string(void)
{
	uint32_t id = socinfo_get_id();

	return socinfo_machine(id);
}
EXPORT_SYMBOL(socinfo_get_id_string);

static int jlq_socinfo_probe(struct platform_device *pdev)
{
	struct jlq_socinfo *js;
	struct DalPlatformInfoSMemType *info;
	struct DalVendor2InfoSMemType *vendor_info;
	size_t item_size;

	js = devm_kzalloc(&pdev->dev, sizeof(*js), GFP_KERNEL);
	if (!js)
		return -ENOMEM;

	socinfo = &js->info;

	socinfo->foundry_id = jlq_sip_call(EFUSE_FOUNDRY_ECO_ID, 0, 0);
	socinfo->serial_num = jlq_sip_call(EFUSE_CHIP_ID, 0, 0);
	socinfo->product_id = jlq_sip_call(EFUSE_FOUNDRY_ECO_ID, 0, 0);
	socinfo->fuse_state = jlq_sip_call(EFUSE_COMMON_ID, 0xe4, 0);
	socinfo->fuse_state = !! (socinfo->fuse_state & EFUSE_MASK);
	// default soc_id, get it from sip call later.
	socinfo->id = 494;

	memcpy((void *)socinfo->chip_name, (void *)&socinfo->product_id,
			SMEM_SOCINFO_CHIP_ID_LENGTH);

	js->attr.machine = socinfo_machine(le32_to_cpu(socinfo->id));
	js->attr.family = "Jmobile";
	js->attr.soc_id = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u",
					le32_to_cpu(socinfo->id));
	js->attr.revision = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u.%u",
				SOCINFO_MAJOR(le32_to_cpu(socinfo->ver)),
				SOCINFO_MINOR(le32_to_cpu(socinfo->ver)));
	js->attr.soc_id = kasprintf(GFP_KERNEL, "%d", socinfo_get_id());

	init_rwsem(&js->current_image_rwsem);
	socinfo_populate_sysfs(js);
	socinfo_print();

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_HW_SW_BUILD_ID, &item_size);
	if (IS_ERR(info)) {
		dev_err(&pdev->dev, "Couldn't find socinfo\n");
	}

	socinfo->board_id = info->nPlatformSubtype;
	dev_info(&pdev->dev, "SMEM boardid=0x%x\n", info->nPlatformSubtype);

	socinfo->major = info->nHWVersionMajor;
	socinfo->minor = info->nHWVersionMinor;

	vendor_info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR2, &item_size);
	if (IS_ERR(vendor_info)) {
		dev_err(&pdev->dev, "Couldn't find vendor_info\n");
	} else {
		//cpuid 16Bytes
		memcpy((void *)socinfo->cpu_id, vendor_info->cpuid, 16);
		dev_info(&pdev->dev, "SMEM cpu_id acquired\n");
	}

	js->proc_entry = proc_create(SERAIL_NUM, 0, NULL, &sn_fops);
	if (!js->proc_entry)
		dev_err(&pdev->dev, "create proc %s failed\n", SERAIL_NUM);

	js->soc_dev = soc_device_register(&js->attr);
	if (IS_ERR(js->soc_dev))
		return PTR_ERR(js->soc_dev);

	platform_set_drvdata(pdev, js);

	jlq_socinfo_data = js;

	return 0;
}

static int jlq_socinfo_remove(struct platform_device *pdev)
{
	struct jlq_socinfo *js = platform_get_drvdata(pdev);

	if (js->proc_entry)
		proc_remove(js->proc_entry);

	soc_device_unregister(js->soc_dev);

	return 0;
}

const static struct of_device_id serial_jlq_match_table[] = {
	{ .compatible = "jlq,socinfo", },
	{}
};
static struct platform_driver jlq_socinfo_driver = {
	.probe = jlq_socinfo_probe,
	.remove = jlq_socinfo_remove,
	.driver  = {
		.name = "jlq-socinfo",
		.of_match_table = serial_jlq_match_table,
	},
};

//module_platform_driver(jlq_socinfo_driver);
static int __init jlq_soc_init(void)
{
    int r = platform_driver_register(&jlq_socinfo_driver);

    if (r < 0)
        pr_err("register failed %d", r);

    return r;
}

static void __exit jlq_soc_exit(void)
{
   platform_driver_unregister(&jlq_socinfo_driver);
}

module_init(jlq_soc_init);
module_exit(jlq_soc_exit);

MODULE_DESCRIPTION("JLQ SoCinfo driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:jlq-socinfo");
