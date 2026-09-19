/*
 * drivers/input/touchscreen/gslX680.c
 *
 * Sileadinc gslX680 TouchScreen driver.
 *
 * Copyright (c) 2012  Sileadinc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION		DATE			AUTHOR
 *   1.0		2012-04-18		leweihua
 *
 * note: only support mulititouch	Wenfs 2010-10-01
 */
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_OF
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#endif
#include "gsl_ts_driver.h"

#define MAX_FINGERS         10
#define MAX_CONTACTS        10
#define DMA_TRANS_LEN       0x20
#define GSL_PAGE_REG        0xf0

#define PRESS_MAX           255
#define GSLX680_NAME		"gslX680"
#define GSLX680_TS_DEVICE	"gslX680"
#define GSLX680_TS_NAME	    "gslX680"
#define GSLX680_TS_ADDR	    0x40

static struct mutex gsl_i2c_lock;
static volatile int gsl_sw_flag;
static volatile int gsl_halt_flag;
#ifdef GSL_TIMER

#define GSL_TIMER_CHECK_CIRCLE        200
static struct delayed_work gsl_timer_check_work;
static struct workqueue_struct *gsl_timer_workqueue;
static u32 gsl_timer_data;
// 0:first test  1:second test  2:doing gsl_load_fw
static volatile int gsl_timer_flag;
#endif

#ifdef GSL_COMPATIBLE_CHIP
static int gsl_compatible_flag;
#endif

struct sprd_i2c_setup_data {
	unsigned int i2c_bus;
	unsigned short i2c_address;
	int irq;
	char type[I2C_NAME_SIZE];
};

#if defined(INCAR_SP7731E)
#define GSL_GPIO_RST   63
#define GSL_GPIO_IRQ   64
#else
#define GSL_GPIO_RST	145
#define GSL_GPIO_IRQ	144
#endif

static u32 id_sign[MAX_CONTACTS+1] = {0};
static u8 id_state_flag[MAX_CONTACTS+1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS+1] = {0};
static u16 x_old[MAX_CONTACTS+1] = {0};
static u16 y_old[MAX_CONTACTS+1] = {0};
static u16 x_new;
static u16 y_new;
static struct i2c_client *this_client;

#ifdef HAVE_TOUCH_KEY
static u16 key;
static int key_state_flag;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;
};

const u16 key_array[] = {
	KEY_BACK,
	KEY_HOMEPAGE,
	KEY_MENU,
	//KEY_SEARCH,
};
#define MAX_KEY_NUM ARRAY_SIZE(key_array)

struct key_data gsl_key_data[MAX_KEY_NUM] = {
	{KEY_BACK, 2048, 2048, 2048, 2048},
	{KEY_HOMEPAGE, 0, 1280, 800, 1000},
	{KEY_MENU, 2048, 2048, 2048, 2048},
	//{KEY_SEARCH, 2048, 2048, 2048, 2048},
};

#endif

struct gslX680_ts_data {
	struct input_dev *input_dev;
	u8 touch_data[44];
	struct i2c_client *client;
	struct work_struct pen_event_work;
	struct workqueue_struct *ts_workqueue;
	struct gslx680_ts_platform_data	*platform_data;
};

static struct gslX680_ts_data *g_gslx680_ts;

#ifdef TOUCH_VIRTUAL_KEYS
static ssize_t virtual_keys_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
	__stringify(EV_KEY) ":" __stringify(KEY_BACK) ":300:1500:60:30"
	":" __stringify(EV_KEY) ":" __stringify(KEY_HOMEPAGE) ":100:1500:60:30"
	":" __stringify(EV_KEY) ":" __stringify(KEY_MENU) ":500:1500:60:30"
	"\n");
}

static struct kobj_attribute virtual_keys_attr = {
	.attr = {
		.name = "virtualkeys.gslX680",
		.mode = '444',
	},
	.show = &virtual_keys_show,
};

static struct attribute *properties_attrs[] = {
	&virtual_keys_attr.attr,
	NULL
};

static struct attribute_group properties_attr_group = {
	.attrs = properties_attrs,
};

static void gsl_ts_virtual_keys_init(void)
{
	int ret;
	struct kobject *properties_kobj;

	properties_kobj = kobject_create_and_add("board_properties", NULL);
	if (properties_kobj)
		ret = sysfs_create_group(properties_kobj,
					 &properties_attr_group);
	if (!properties_kobj || ret)
		pr_err("failed to create board_properties\n");
}

#endif

static inline u16 join_bytes(u8 a, u8 b)
{
	u16 ab = 0;

	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}

static int gsl_ts_write(struct i2c_client *client, u8 addr,
						u8 *pdata, int datalen)
{
	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen = 0;

	if (datalen > 125) {
		dev_info(&client->dev, "%s,%d!\n", __func__, datalen);
		return -1;
	}

	tmp_buf[0] = addr;
	bytelen++;

	if (datalen != 0 && pdata != NULL) {
		memcpy(&tmp_buf[bytelen], pdata, datalen);
		bytelen += datalen;
	}

	ret = i2c_master_send(client, tmp_buf, bytelen);
	return ret;
}

static int gsl_ts_read(struct i2c_client *client, u8 addr,
						u8 *pdata, unsigned int datalen)
{
	int ret = 0;

	if (datalen > 126) {
		dev_info(&client->dev, "%s,%d!\n", __func__, datalen);
		return -1;
	}

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0) {
		dev_info(&client->dev, "%s set data address fail!\n", __func__);
		return ret;
	}

	return i2c_master_recv(client, pdata, datalen);
}


static int gsl_read_interface(struct i2c_client *client,
					u8 reg, u8 *buf, u32 num)
{

	int err = 0;
	int i;
	u8 temp = reg;
	mutex_lock(&gsl_i2c_lock);
	if (temp < 0x80) {
		temp = (temp + 8) & 0x5c;
			i2c_master_send(client, &temp, 1);
			err = i2c_master_recv(client, &buf[0], 4);
		temp = reg;
		i2c_master_send(client, &temp, 1);
		err = i2c_master_recv(client, &buf[0], 4);
	}

	for (i = 0; i < num;) {
		temp = reg + i;
		i2c_master_send(client, &temp, 1);
		if ((i + 8) < num)
			err = i2c_master_recv(client, (buf + i), 8);
		else
			err = i2c_master_recv(client, (buf + i), (num - i));
		i += 8;
	}
	mutex_unlock(&gsl_i2c_lock);

	return err;

}

static int gsl_write_interface(struct i2c_client *client,
		const u8 reg, u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[1];
	int err;
	u8 tmp_buf[5];

	tmp_buf[0] = reg;
	memcpy(tmp_buf + 1, buf, num);
	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = tmp_buf;

	mutex_lock(&gsl_i2c_lock);
	err = i2c_transfer(client->adapter, xfer_msg, 1);
	mutex_unlock(&gsl_i2c_lock);
	return err;
}

static void gsl_load_fw(struct i2c_client *client,
			const struct fw_data *DOWNLOAD_DATA, int data_len)
{
	u8 buf[4] = {0};
	u8 addr = 0;
	u32 source_line = 0;
	u32 source_len = data_len;

	dev_info(&client->dev, "%s start\n", __func__);

	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		addr = DOWNLOAD_DATA[source_line].offset;
		memcpy(buf, &DOWNLOAD_DATA[source_line].val, 4);
		gsl_write_interface(client, addr, buf, 4);
	}

	dev_info(&client->dev, "%s over\n", __func__);
}

static void gsl_start_core(struct i2c_client *client)
{
	u8 buf[4] = {0};

	buf[0] = 0;
	gsl_write_interface(client, 0xe0, buf, 4);
#ifdef GSL_ALG_ID
	{
		gsl_DataInit(gsl_config_data_id);
	}
#endif
}

static void gsl_reset_core(struct i2c_client *client)
{
	u8 buf[4] = {0x00};

	pr_debug("%s,++++++++++++++++++++++\n", __func__);

	buf[0] = 0x88;
	gsl_write_interface(client, 0xe0, buf, 4);

	mdelay(5);

	buf[0] = 0x04;
	gsl_write_interface(client, 0xe4, buf, 4);

	mdelay(5);

	buf[0] = 0;
	gsl_write_interface(client, 0xbc, buf, 4);

	mdelay(5);

}

static int gsl_clear_reg(struct i2c_client *client)
{
	int ret;

	u8 buf[4]  = {0};

	pr_debug("%s,++++++++++++++++++++++\n", __func__);

	buf[0] = 0x88;
	ret = gsl_write_interface(client, 0xe0, buf, 4);
	mdelay(20);

	buf[0] = 0x3;
	ret = gsl_write_interface(client, 0x80, buf, 4);
	mdelay(5);

	buf[0] = 0x4;
	ret = gsl_write_interface(client, 0xe4, buf, 4);
	mdelay(5);

	buf[0] = 0x0;
	ret = gsl_write_interface(client, 0xe0, buf, 4);
	mdelay(20);

	if (ret < 0)
		return -ENODEV;
	return 0;
}

static int gsl_sw_init(struct i2c_client *client)

{

	struct gslx680_ts_platform_data *pdata = client->dev.platform_data;
	u32 temp;
	int err;

	if (gsl_sw_flag == 1)
		return 0;

	pr_debug("%s,++++++++++++++++++++++\n", __func__);

	gsl_sw_flag = 1;
	gpio_direction_output(pdata->reset_gpio_number, 0);
	mdelay(20);
	gpio_direction_output(pdata->reset_gpio_number, 1);
	mdelay(20);

	err = gsl_clear_reg(client);
	if (err < 0)
		return -ENODEV;

	gsl_reset_core(client);

#ifdef LOADFIRMWARE
	temp = ARRAY_SIZE(GSL1680E_FW);
	gsl_load_fw(client, GSL1680E_FW, temp);
#endif

	gsl_start_core(client);
	gsl_sw_flag = 0;
	return 0;
}

static void check_mem_data(struct i2c_client *client)
{
	u8 read_buf[4]  = {0};

	mdelay(30);

	gsl_read_interface(client, 0xb0, read_buf, 4);
	pr_info("%s,page:%x offset:%x val:%x %x %x %x\n", __func__, 0x0, 0x0,
				read_buf[3], read_buf[2],
				read_buf[1], read_buf[0]);

	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a ||
		read_buf[1] != 0x5a || read_buf[0] != 0x5a) {
		pr_info("page:%x offset:%x val:%x %x %x %x\n", 0x0, 0x0,
				read_buf[3], read_buf[2],
				read_buf[1], read_buf[0]);
		gsl_sw_init(client);
	}
}

#ifdef GSL_COMPATIBLE_CHIP
static int gsl_compatible_id(struct i2c_client *client)
{
	int err, i;
	u8 buf[4] = {0};

	for (i = 0; i < 3; i++) {
		err = gsl_read_interface(client, 0xfc, buf, 4);
		if (!(err < 0)) {
			err = 1;
			break;
		}
	}
	return err;
}
#endif

#ifdef GSL_TIMER
static void gsl_timer_check_func(struct work_struct *work)
{
	u8 buf[4] = {0};
	u32 tmp;
	int i, flag = 0;
	static int timer_count;

	if (gsl_halt_flag == 1)
		return;

	pr_info("%s,++++++++++++++++++++++\n", __func__);

	gsl_read_interface(this_client, 0xb0, buf, 4);
	tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
	pr_info("[cur] 0xb0 = %x\n", tmp);

	gsl_read_interface(this_client, 0xb4, buf, 4);
	tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
	pr_info("[cur] 0xb4 = %x\n", tmp);

	gsl_read_interface(this_client, 0xbc, buf, 4);
	tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
	pr_info("[cur] 0xbc = %x\n", tmp);

	queue_delayed_work(gsl_timer_workqueue,
					&gsl_timer_check_work, 25);

}
#endif

#ifdef FILTER_POINT
static void filter_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;
	u16 filter_step_x = 0, filter_step_y = 0;

	id_sign[id] = id_sign[id] + 1;
	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x_err = x > x_old[id] ? (x - x_old[id]) : (x_old[id] - x);
	y_err = y > y_old[id] ? (y - y_old[id]) : (y_old[id] - y);

	if ((x_err > FILTER_MAX && y_err > FILTER_MAX / 3) ||
			(x_err > FILTER_MAX / 3 && y_err > FILTER_MAX)) {
		filter_step_x = x_err;
		filter_step_y = y_err;
	} else {
		if (x_err > FILTER_MAX)
			filter_step_x = x_err;
		if (y_err > FILTER_MAX)
			filter_step_y = y_err;
	}

	if (x_err <= 2 * FILTER_MAX && y_err <= 2 * FILTER_MAX) {
		filter_step_x >>= 2;
		filter_step_y >>= 2;
	} else if (x_err <= 3 * FILTER_MAX && y_err <= 3 * FILTER_MAX) {
		filter_step_x >>= 1;
		filter_step_y >>= 1;
	} else if (x_err <= 4 * FILTER_MAX && y_err <= 4 * FILTER_MAX) {
		filter_step_x = filter_step_x*3/4;
		filter_step_y = filter_step_y*3/4;
	}

	x_new = x > x_old[id] ? (x_old[id] + filter_step_x) :
					(x_old[id] - filter_step_x);
	y_new = y > y_old[id] ? (y_old[id] + filter_step_y) :
					(y_old[id] - filter_step_y);

	x_old[id] = x_new;
	y_old[id] = y_new;
}
#else

static void record_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;

	id_sign[id] = id_sign[id]+1;

	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x = (x_old[id] + x)/2;
	y = (y_old[id] + y)/2;

	if (x > x_old[id])
		x_err = x - x_old[id];
	else
		x_err = x_old[id] - x;

	if (y > y_old[id])
		y_err = y - y_old[id];
	else
		y_err = y_old[id] - y;

	if ((x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3)) {
		x_new = x;
		x_old[id] = x;
		y_new = y;
		y_old[id] = y;
	} else {
		if (x_err > 3) {
			x_new = x;
			x_old[id] = x;
		} else
			x_new = x_old[id];

		if (y_err > 3) {
			y_new = y;
			y_old[id] = y;
		} else
			y_new = y_old[id];
	}

	if (id_sign[id] == 1) {
		x_new = x_old[id];
		y_new = y_old[id];
	}
}
#endif

#ifdef HAVE_TOUCH_KEY
static void report_key(struct gslX680_ts_data *ts, u16 x, u16 y)
{
	u16 i = 0;

	for (i = 0; i < MAX_KEY_NUM; i++) {
		if ((gsl_key_data[i].x_min < x) &&
			(x < gsl_key_data[i].x_max) &&
			(gsl_key_data[i].y_min < y) &&
			(y < gsl_key_data[i].y_max)) {
			key = gsl_key_data[i].key;
			pr_info("**********key is %d\n", key);
			input_report_key(ts->input_dev, key, 1);
			key_state_flag = 1;
			break;
		}
	}
}
#endif

static void report_data(struct gslX680_ts_data *ts, u16 x, u16 y,
				u8 pressure, u8 id)
{
	input_report_key(ts->input_dev, BTN_TOUCH, 1);

#if defined(INCAR_S1067E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 600-x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, 1024-y);
#elif defined(TOUCH_MODUEL_S706A_JIUZHOU_YQ)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 600 - x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
#elif defined(INCAR_S7182A)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 800 - x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, 1280 - y);
#elif defined(INCAR_S7172A)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 800 - x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, 1280 - y);
#elif defined(INCAR_S1082A)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 1280 - y);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, x);
#elif defined(INCAR_S1087E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 1280 - x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,  y);
#elif defined(INCAR_S1083A)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 1280 - x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,  800-y);
#elif defined(INCAR_S1082E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 1280 - x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,  800-y);
#elif defined(INCAR_S1022A)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, y);
#elif defined(INCAR_S8638E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
#elif defined(INCAR_S86382E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
#elif defined(INCAR_S8657E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, 1024-y);
#elif defined(INCAR_S960E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, 800-y);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
#elif defined(INCAR_S7082E)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 600-x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
#elif defined(TOUCH_MODUEL_S8631E_GSL3670_ZJ)
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 1280-x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
#else
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
#endif

	input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
	input_mt_sync(ts->input_dev);
#ifdef HAVE_TOUCH_KEY
	if (y > TS_HEIGHT_MAX)
		report_key(ts, x, y);
#endif
}

static void gslX680_ts_worker(struct work_struct *work)
{
	int rc, i, tmp;
	u8 id, touches;
	u16 x, y;

	struct gslX680_ts_data *ts = i2c_get_clientdata(this_client);

	pr_debug("%s,++++++++++++++++++++++\n", __func__);

#ifdef GSL_ALG_ID
	struct gsl_touch_info cinfo;
	u32 tmp1;
	u8 buf[4] = {0};
#endif

#ifdef GSL_MONITOR
		if (i2c_lock_flag != 0)
			goto i2c_lock_schedule;
		else
			i2c_lock_flag = 1;
#endif

	rc = gsl_ts_read(this_client, 0x80, ts->touch_data,
					sizeof(ts->touch_data));
		if (rc < 0) {
			dev_err(&this_client->dev, "read failed\n");
			goto schedule;
		}

		touches = ts->touch_data[0];
#ifdef GSL_ALG_ID
		cinfo.finger_num = touches;
		dev_dbg(&this_client->dev, "tp-gsl	finger_num = %d\n",
				cinfo.finger_num);

		tmp = touches < MAX_CONTACTS ? touches : MAX_CONTACTS;
		for (i = 0; i < tmp; i++) {
			u8 a, b;

			a = ts->touch_data[4 * i + 7] & 0xf;
			b = ts->touch_data[4 * i + 6];
			cinfo.x[i] = join_bytes(a, b);

			a = ts->touch_data[4 * i + 5];
			b = ts->touch_data[4 * i + 4];
			cinfo.y[i] = join_bytes(a, b);

			cinfo.id[i] = ((ts->touch_data[4 * i + 7] & 0xf0) >> 4);

			dev_dbg(&this_client->dev, "x[%d]=%d,y[%d]=%d,id[%d]=%d\n",
						i, cinfo.x[i],
						i, cinfo.y[i],
						i, cinfo.id[i]);
		}

		cinfo.finger_num = (ts->touch_data[3] << 24) |
						(ts->touch_data[2] << 16) |
						(ts->touch_data[1]  <<  8) |
						(ts->touch_data[0]);
		gsl_alg_id_main(&cinfo);
		tmp1 = gsl_mask_tiaoping();
		dev_dbg(&this_client->dev, "[tp-gsl] tmp1=%x\n", tmp1);
		if (tmp1 > 0 && tmp1 < 0xffffffff) {
			buf[0] = 0xa;
			buf[1] = 0;
			buf[2] = 0;
			buf[3] = 0;
			gsl_ts_write(this_client, 0xf0, buf, 4);
			buf[0] = (u8)(tmp1 & 0xff);
			buf[1] = (u8)((tmp1>>8) & 0xff);
			buf[2] = (u8)((tmp1>>16) & 0xff);
			buf[3] = (u8)((tmp1>>24) & 0xff);
			dev_dbg(&this_client->dev,
				"tmp1=%08x,buf[0-3]=%02x,%02x,%02x,%02x\n",
				tmp1, buf[0], buf[1], buf[2], buf[3]);
			gsl_ts_write(this_client, 0x8, buf, 4);
		}
		touches = cinfo.finger_num;
#endif
		for (i = 1; i <= MAX_CONTACTS; i++) {
			if (!touches)
				id_sign[i] = 0;
			id_state_flag[i] = 0;
		}

		tmp = (touches > MAX_FINGERS ? MAX_FINGERS : touches);
		for (i = 0; i < tmp; i++) {
#ifdef GSL_ALG_ID
			id = cinfo.id[i];
			x =  cinfo.x[i];
			y =  cinfo.y[i];
#else
			id = ts->touch_data[4 * i + 7] >> 4;
			x = join_bytes((ts->touch_data[4 * i + 7] & 0xf),
						ts->touch_data[4 * i + 6]);
			y = join_bytes(ts->touch_data[4 * i + 5],
						ts->touch_data[4 * i + 4]);
#endif

			if (id >= 1 && id <= MAX_CONTACTS) {
#ifdef FILTER_POINT
				filter_point(x, y, id);
#else
#ifdef TOUCH_MODUEL_S7069A_ONCELL_XINPENG
#else
				record_point(x, y, id);
#endif
#endif

#ifdef TOUCH_MODUEL_S7069A_ONCELL_XINPENG
				report_data(ts, x, y, 3, id);
		#else
				report_data(ts, x_new, y_new, 3, id);
		#endif
				id_state_flag[id] = 1;
			}
		}

		for (i = 1; i <= MAX_CONTACTS; i++) {
			if ((!touches) || (id_state_old_flag[i] &&
						!id_state_flag[i])) {
				id_sign[i] = 0;
			}
			id_state_old_flag[i] = id_state_flag[i];
		}

		if (!touches) {
			input_report_key(ts->input_dev, BTN_TOUCH, 0);
			input_mt_sync(ts->input_dev);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
#ifdef HAVE_TOUCH_KEY
			if (key_state_flag) {
				input_report_key(ts->input_dev, key, 0);
				key_state_flag = 0;
			}
#endif
		}
		input_sync(ts->input_dev);

schedule:
#ifdef GSL_MONITOR
		i2c_lock_flag = 0;
i2c_lock_schedule:
#endif
		enable_irq(this_client->irq);
}

static irqreturn_t gslX680_ts_interrupt(int irq, void *dev_id)
{

	struct gslX680_ts_data *gslX680_ts = (struct gslX680_ts_data *)dev_id;

	disable_irq_nosync(this_client->irq);
	if (!work_pending(&gslX680_ts->pen_event_work))
		queue_work(gslX680_ts->ts_workqueue,
					&gslX680_ts->pen_event_work);
	return IRQ_HANDLED;
}

static int gslX680_ts_suspend(struct device *dev)
{
#ifdef GSL_ALG_ID
	u32 tmp;
#endif

	struct gslX680_ts_data *ts = i2c_get_clientdata(this_client);
	struct gslx680_ts_platform_data *pdata = ts->platform_data;

	pr_info("%s\n", __func__);
#ifdef GSL_ALG_ID
	tmp = gsl_version_id();
	pr_info("[tp-gsl]the version of alg_id:%x\n", tmp);
#endif
	gsl_halt_flag = 1;
#ifdef GSL_TIMER
	cancel_delayed_work_sync(&gsl_timer_check_work);
	if (gsl_timer_flag == 2)
		return 0;
#endif
	disable_irq_nosync(this_client->irq);
	gpio_direction_output(pdata->reset_gpio_number, 0);
	gpio_direction_output(pdata->enable_gpio_number, 0);

	return 0;
}

static int gslX680_ts_resume(struct device *dev)
{
	struct gslX680_ts_data *ts = i2c_get_clientdata(this_client);
	struct gslx680_ts_platform_data *pdata = ts->platform_data;

	pr_info("%s\n", __func__);
	gpio_direction_output(pdata->enable_gpio_number, 1);
	gpio_direction_output(pdata->reset_gpio_number, 1);
#ifdef GSL_TIMER
	if (gsl_timer_flag == 2) {
		gsl_halt_flag = 0;
		enable_irq(this_client->irq);
		return 0;
	}
#endif
	gsl_reset_core(this_client);
	gsl_start_core(this_client);
	msleep(20);
	check_mem_data(this_client);
	enable_irq(this_client->irq);
#ifdef GSL_TIMER
	queue_delayed_work(gsl_timer_workqueue, &gsl_timer_check_work,
						GSL_TIMER_CHECK_CIRCLE);
	gsl_timer_flag = 0;
#endif
	gsl_halt_flag = 0;

	return 0;
}

static void gslx680_ts_hw_init(struct gslX680_ts_data *gslx680_ts)
{
	struct gslx680_ts_platform_data *pdata = gslx680_ts->platform_data;

	gpio_request(pdata->irq_gpio_number, "ts_irq_pin");
	gpio_request(pdata->reset_gpio_number, "ts_rst_pin");
	gpio_request(pdata->enable_gpio_number, "ts_enable_pin");
	gpio_direction_input(pdata->irq_gpio_number);
	gpio_direction_output(pdata->enable_gpio_number, 1);
}

#ifdef CONFIG_OF
static struct gslx680_ts_platform_data *gsl168x_ts_parse_dt(struct device *dev)
{
	struct gslx680_ts_platform_data *pdata;
	struct device_node *np = dev->of_node;
	int ret;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->reset_gpio_number = of_get_named_gpio(np, "gpio_rst", 0);
	if (pdata->reset_gpio_number < 0) {
		dev_err(dev, "fail to get reset_gpio_number\n");
		goto fail;
	}

	pdata->enable_gpio_number = of_get_named_gpio(np, "gpio_enable", 0);
	if (pdata->reset_gpio_number < 0) {
		dev_err(dev, "fail to get reset_gpio_number\n");
		goto fail;
	}

	pdata->irq_gpio_number = of_get_named_gpio(np, "gpio_irq", 0);
	if (pdata->reset_gpio_number < 0) {
		dev_err(dev, "fail to get reset_gpio_number\n");
		goto fail;
	}

	pdata->tp_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(pdata->tp_pinctrl)) {
		dev_dbg(dev, "touchscreen need to use pinctrl\n");
		pdata->tp_pinctrl = NULL;
		goto fail;
	}

	pdata->tp_power =  devm_regulator_get_optional(dev, "power");
	if (IS_ERR(pdata->tp_power)) {
		if (PTR_ERR(pdata->tp_power) == -EPROBE_DEFER) {
			goto fail;
		}
		dev_info(dev, "fail to get regulator\n");
		pdata->tp_power = NULL;
	}
	if (pdata->tp_power) {
		ret = regulator_set_voltage(pdata->tp_power, 3300000, 3300000);
		if (ret) {
			dev_err(dev, "set voltage for lcdcore failed\n");
			regulator_put(pdata->tp_power);
			goto fail;
		}

		ret = regulator_enable(pdata->tp_power);
		if (ret) {
			dev_err(dev, "fail to enable regulator\n");
			goto fail;
		}
	}
	mdelay(10);

#if 0
	ret = of_property_read_u32_array(np, "virtualkeys",
					pdata->virtualkeys, 12);
	if (ret) {
		dev_err(dev, "fail to get virtualkeys\n");
		goto fail;
	}
#endif

	ret = of_property_read_u32(np, "TP_MAX_X", &pdata->TP_MAX_X);
	if (ret) {
		dev_err(dev, "fail to get TP_MAX_X\n");
		goto fail;
	}

	ret = of_property_read_u32(np, "TP_MAX_Y", &pdata->TP_MAX_Y);
	if (ret) {
		dev_err(dev, "fail to get TP_MAX_Y\n");
		goto fail;
	}

	return pdata;
fail:
	kfree(pdata);
	return NULL;
}
#endif

static int gslX680_ts_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct gslX680_ts_data *gslX680_ts;
	struct input_dev *input_dev;
	struct gslx680_ts_platform_data *pdata = client->dev.platform_data;
	int err = 0;
#ifdef HAVE_TOUCH_KEY
	int retry1 = 0;
#endif
#ifdef CONFIG_OF
	struct device_node *np = client->dev.of_node;
#endif
	dev_info(&client->dev, "%s", __func__);
#ifdef CONFIG_OF
	if (np && !pdata) {
		pdata = gsl168x_ts_parse_dt(&client->dev);
		if (pdata) {
			client->dev.platform_data = pdata;
		} else {
			err = -ENOMEM;
			goto exit_check_functionality_failed;
		}
	}
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	gslX680_ts = kzalloc(sizeof(*gslX680_ts), GFP_KERNEL);
	if (!gslX680_ts)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
	mutex_init(&gsl_i2c_lock);

#ifdef TOUCH_VIRTUAL_KEYS
	gsl_ts_virtual_keys_init();
#endif

	g_gslx680_ts = gslX680_ts;
	gslX680_ts->platform_data = pdata;
	this_client = client;
	i2c_set_clientdata(client, gslX680_ts);
	gslX680_ts->client = client;
	gslx680_ts_hw_init(gslX680_ts);
	client->irq = gpio_to_irq(pdata->irq_gpio_number);

	dev_info(&client->dev, "i2c addr:%x,irq:%d", client->addr, client->irq);

	INIT_WORK(&gslX680_ts->pen_event_work, gslX680_ts_worker);

	gslX680_ts->ts_workqueue =
			create_singlethread_workqueue(dev_name(&client->dev));
	if (!gslX680_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

	err = request_irq(client->irq, gslX680_ts_interrupt,
					IRQF_TRIGGER_RISING|IRQF_NO_SUSPEND,
					client->name, gslX680_ts);
	if (err < 0) {
		dev_err(&client->dev, "gslX680_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	disable_irq(client->irq);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

	gslX680_ts->input_dev = input_dev;

	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

#ifdef HAVE_TOUCH_KEY
		for (retry1 = 0; retry1 < MAX_KEY_NUM; retry1++) {
			input_set_capability(gslX680_ts->input_dev, EV_KEY,
						key_array[retry1]);
		}
#endif

	__set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	__set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	__set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	__set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

	__set_bit(KEY_MENU,  input_dev->keybit);
	__set_bit(KEY_BACK,  input_dev->keybit);
	__set_bit(KEY_HOME,  input_dev->keybit);
	__set_bit(KEY_SEARCH,  input_dev->keybit);

	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, TS_WIDTH_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, TS_HEIGHT_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_WIDTH_MAJOR, 0, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 5, 0, 0);

	input_dev->name = GSLX680_NAME;
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev, "failed to register input device: %s\n",
				dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

	err = gsl_sw_init(this_client);
	if (err < 0)
		goto exit_input_register_device_failed;
	msleep(20);
	check_mem_data(this_client);

#ifdef GSL_TIMER
	INIT_DELAYED_WORK(&gsl_timer_check_work, gsl_timer_check_func);
	gsl_timer_workqueue = create_workqueue("gsl_esd_check");
	queue_delayed_work(gsl_timer_workqueue, &gsl_timer_check_work,
						GSL_TIMER_CHECK_CIRCLE);
#endif

#ifdef GSL_ALG_ID
	gsl_DataInit(gsl_config_data_id);
#endif

	enable_irq(client->irq);
	dev_info(&client->dev, "%s, over\n", __func__);
	return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);
exit_input_dev_alloc_failed:
	free_irq(client->irq, gslX680_ts);
exit_irq_request_failed:
	cancel_work_sync(&gslX680_ts->pen_event_work);
	destroy_workqueue(gslX680_ts->ts_workqueue);
exit_create_singlethread:
	i2c_set_clientdata(client, NULL);
	kfree(gslX680_ts);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int  gslX680_ts_remove(struct i2c_client *client)
{

	struct gslX680_ts_data *gslX680_ts = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);

	free_irq(client->irq, gslX680_ts);
	input_unregister_device(gslX680_ts->input_dev);
	kfree(gslX680_ts);
	cancel_work_sync(&gslX680_ts->pen_event_work);
	destroy_workqueue(gslX680_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);

	return 0;
}

const static struct dev_pm_ops gsl_pm_ops = {
	.suspend = gslX680_ts_suspend,
	.resume  = gslX680_ts_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id gsl_of_match[] = {
	{ .compatible = "gsl,gsl1691_ts", },
	{ }
};
MODULE_DEVICE_TABLE(of, gsl_of_match);
#endif
static const struct i2c_device_id gslX680_ts_id[] = {
	{ GSLX680_NAME, 0 }, {}
};

MODULE_DEVICE_TABLE(i2c, gslX680_ts_id);

static struct i2c_driver gslX680_ts_driver = {
	.probe		= gslX680_ts_probe,
	.remove		= gslX680_ts_remove,
	.id_table	= gslX680_ts_id,
	.driver	= {
		.name	= GSLX680_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = gsl_of_match,
#endif
		.pm	= &gsl_pm_ops,
	},
};

static int __init gslX680_ts_init(void)
{
	int ret;

	ret = i2c_add_driver(&gslX680_ts_driver);
	return ret;
}

static void __exit gslX680_ts_exit(void)
{
	i2c_del_driver(&gslX680_ts_driver);
}
module_init(gslX680_ts_init);
module_exit(gslX680_ts_exit);

MODULE_AUTHOR("leweihua");
MODULE_DESCRIPTION("GSLX680 TouchScreen Driver");
MODULE_LICENSE("GPL");
