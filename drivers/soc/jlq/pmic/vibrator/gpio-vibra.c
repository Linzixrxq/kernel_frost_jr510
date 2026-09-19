// SPDX-License-Identifier: GPL-2.0+
/*
 *  GPIO vibrator driver
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/notifier.h>
#include <linux/delay.h>

/*
 * Define vibration periods: default(5sec), min(50ms), max(15sec) and
 * overdrive(30ms).
 */
#define JLQ_VIB_MIN_PLAY_MS		50
#define JLQ_VIB_PLAY_MS		5000
#define JLQ_VIB_MAX_PLAY_MS		15000
#define JLQ_VIB_OVERDRIVE_PLAY_MS	30

struct gpio_vibrator {
	struct input_dev *input;
	struct gpio_desc *gpio;
	struct led_classdev	cdev;
	struct work_struct play_work;
	struct mutex		lock;
	struct hrtimer		stop_timer;
	struct work_struct	vib_work;
	u64			vib_play_ms;
	bool			running;
	bool			vib_enabled;
};

struct gpio_vibrator *g_gpio_vib;

static int gpio_vibrator_start(struct gpio_vibrator *vibrator)
{
	gpiod_set_value_cansleep(vibrator->gpio, 1);

	return 0;
}

static void gpio_vibrator_stop(struct gpio_vibrator *vibrator)
{
	gpiod_set_value_cansleep(vibrator->gpio, 0);
}

static void gpio_vibrator_play_work(struct work_struct *work)
{
	struct gpio_vibrator *vibrator =
		container_of(work, struct gpio_vibrator, play_work);

	if (vibrator->running)
		gpio_vibrator_start(vibrator);
	else
		gpio_vibrator_stop(vibrator);
}

static int gpio_vibrator_play_effect(struct input_dev *dev, void *data,
				     struct ff_effect *effect)
{
	struct gpio_vibrator *vibrator = input_get_drvdata(dev);
	int level;

	level = effect->u.rumble.strong_magnitude;
	if (!level)
		level = effect->u.rumble.weak_magnitude;

	vibrator->running = level;
	schedule_work(&vibrator->play_work);

	return 0;
}

static void gpio_vibrator_close(struct input_dev *input)
{
	struct gpio_vibrator *vibrator = input_get_drvdata(input);

	cancel_work_sync(&vibrator->play_work);
	gpio_vibrator_stop(vibrator);
	vibrator->running = false;
}

static inline int jlq_vib_ldo_enable(struct gpio_vibrator *vibrator, bool enable)
{
	if (vibrator->vib_enabled == enable)
		return 0;

	if (enable)
		gpiod_set_value_cansleep(vibrator->gpio, 1);
	else
		gpiod_set_value_cansleep(vibrator->gpio, 0);

	vibrator->vib_enabled = enable;

	return 0;
}

static int jlq_vibrator_play_on(struct gpio_vibrator *vibrator)
{
	int ret;

	ret = jlq_vib_ldo_enable(vibrator, true);
	if (ret < 0) {
		pr_err("vibration enable failed, ret=%d\n", ret);
		return ret;
	}

	return ret;
}

static void jlq_vib_work(struct work_struct *work)
{
	struct gpio_vibrator *vibrator = container_of(work, struct gpio_vibrator,
						vib_work);
	int ret = 0;

	if (vibrator->running) {
		if (!vibrator->vib_enabled)
			ret = jlq_vibrator_play_on(vibrator);

		if (ret == 0)
			hrtimer_start(&vibrator->stop_timer,
				      ms_to_ktime(vibrator->vib_play_ms),
				      HRTIMER_MODE_REL);
	} else {
		jlq_vib_ldo_enable(vibrator, false);
	}
}

static enum hrtimer_restart vib_stop_timer(struct hrtimer *timer)
{
	struct gpio_vibrator *vibrator = container_of(timer, struct gpio_vibrator,
					     stop_timer);

	vibrator->running = 0;
	schedule_work(&vibrator->vib_work);
	return HRTIMER_NORESTART;
}

static ssize_t jlq_vib_show_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct gpio_vibrator *vibrator = container_of(cdev, struct gpio_vibrator,
						cdev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", vibrator->vib_enabled);
}

static ssize_t jlq_vib_store_state(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	/* At present, nothing to do with setting state */
	return count;
}

static ssize_t jlq_vib_show_duration(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct gpio_vibrator *vibrator = container_of(cdev, struct gpio_vibrator,
						cdev);
	ktime_t time_rem;
	s64 time_ms = 0;

	if (hrtimer_active(&vibrator->stop_timer)) {
		time_rem = hrtimer_get_remaining(&vibrator->stop_timer);
		time_ms = ktime_to_ms(time_rem);
	}

	return scnprintf(buf, PAGE_SIZE, "%lld\n", time_ms);
}

static ssize_t jlq_vib_store_duration(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct gpio_vibrator *vibrator = container_of(cdev, struct gpio_vibrator,
						cdev);
	u32 val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret < 0)
		return ret;

	/* setting 0 on duration is NOP for now */
	if (val <= 0)
		return count;

	if (val < JLQ_VIB_MIN_PLAY_MS)
		val = JLQ_VIB_MIN_PLAY_MS;

	if (val > JLQ_VIB_MAX_PLAY_MS)
		val = JLQ_VIB_MAX_PLAY_MS;

	mutex_lock(&vibrator->lock);
	vibrator->vib_play_ms = val;
	mutex_unlock(&vibrator->lock);

	return count;
}

static ssize_t jlq_vib_show_activate(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* For now nothing to show */
	return scnprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t jlq_vib_store_activate(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct gpio_vibrator *vibrator = container_of(cdev, struct gpio_vibrator,
						cdev);
	u32 val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val != 0 && val != 1)
		return count;

	mutex_lock(&vibrator->lock);
	hrtimer_cancel(&vibrator->stop_timer);
	vibrator->running = val;
	mutex_unlock(&vibrator->lock);
	schedule_work(&vibrator->vib_work);

	return count;
}

static struct device_attribute jlq_vib_attrs[] = {
	__ATTR(state, 0664, jlq_vib_show_state, jlq_vib_store_state),
	__ATTR(duration, 0664, jlq_vib_show_duration, jlq_vib_store_duration),
	__ATTR(activate, 0664, jlq_vib_show_activate, jlq_vib_store_activate),
};

/* Dummy functions for brightness */
static enum led_brightness jlq_vib_brightness_get(struct led_classdev *cdev)
{
	return 0;
}

static void jlq_vib_brightness_set(struct led_classdev *cdev,
			enum led_brightness level)
{
}

static int vibrator_panic_notifier(struct notifier_block *nb,
	unsigned long action, void *p)
{
	pr_info("play vibrator when system panic.\n");

	gpio_vibrator_start(g_gpio_vib);
	mdelay(100);
	gpio_vibrator_stop(g_gpio_vib);

	return NOTIFY_DONE;
}

static struct notifier_block force_vibrator_nb = {
	.notifier_call = vibrator_panic_notifier,
	.priority = INT_MAX,
};

static int gpio_vibrator_probe(struct platform_device *pdev)
{
	struct gpio_vibrator *vibrator;
	int err, i;

	vibrator = devm_kzalloc(&pdev->dev, sizeof(*vibrator), GFP_KERNEL);
	if (!vibrator)
		return -ENOMEM;

	vibrator->input = devm_input_allocate_device(&pdev->dev);
	if (!vibrator->input)
		return -ENOMEM;

	vibrator->gpio = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
	err = PTR_ERR_OR_ZERO(vibrator->gpio);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to request main gpio: %d\n",
				err);
		return err;
	}

	INIT_WORK(&vibrator->play_work, gpio_vibrator_play_work);

	vibrator->input->name = "gpio-vibrator";
	vibrator->input->id.bustype = BUS_HOST;
	vibrator->input->close = gpio_vibrator_close;

	input_set_drvdata(vibrator->input, vibrator);
	input_set_capability(vibrator->input, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(vibrator->input, NULL,
				      gpio_vibrator_play_effect);
	if (err) {
		dev_err(&pdev->dev, "Couldn't create FF dev: %d\n", err);
		return err;
	}

	err = input_register_device(vibrator->input);
	if (err) {
		dev_err(&pdev->dev, "Couldn't register input dev: %d\n", err);
		return err;
	}

	vibrator->cdev.name = "gpio-vibrator";
	vibrator->cdev.brightness_get = jlq_vib_brightness_get;
	vibrator->cdev.brightness_set = jlq_vib_brightness_set;
	vibrator->cdev.max_brightness = 100;
	err = devm_led_classdev_register(&pdev->dev, &vibrator->cdev);
	if (err < 0)
		pr_err("Error in registering led class device, err=%d\n", err);

	for (i = 0; i < ARRAY_SIZE(jlq_vib_attrs); i++) {
		err = sysfs_create_file(&vibrator->cdev.dev->kobj,
				&jlq_vib_attrs[i].attr);
		if (err < 0) {
			dev_err(&pdev->dev, "Error in creating sysfs file, err=%d\n",
				err);
			goto sysfs_fail;
		}
	}
	vibrator->vib_play_ms = JLQ_VIB_PLAY_MS;
	mutex_init(&vibrator->lock);
	hrtimer_init(&vibrator->stop_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vibrator->stop_timer.function = vib_stop_timer;
	INIT_WORK(&vibrator->vib_work, jlq_vib_work);
	platform_set_drvdata(pdev, vibrator);
	dev_set_drvdata(&pdev->dev, vibrator);
	g_gpio_vib  = vibrator;
	atomic_notifier_chain_register(&panic_notifier_list, &force_vibrator_nb);

	return 0;

sysfs_fail:
	for (--i; i >= 0; i--)
		sysfs_remove_file(&vibrator->cdev.dev->kobj,
				&jlq_vib_attrs[i].attr);
	return err;
}

static int __maybe_unused gpio_vibrator_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_vibrator *vibrator = platform_get_drvdata(pdev);

	cancel_work_sync(&vibrator->play_work);
	if (vibrator->running)
		gpio_vibrator_stop(vibrator);

	cancel_work_sync(&vibrator->vib_work);
	jlq_vib_ldo_enable(vibrator, false);
	return 0;
}

static int __maybe_unused gpio_vibrator_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_vibrator *vibrator = platform_get_drvdata(pdev);

	if (vibrator->running)
		gpio_vibrator_start(vibrator);

	return 0;
}

static SIMPLE_DEV_PM_OPS(gpio_vibrator_pm_ops,
			 gpio_vibrator_suspend, gpio_vibrator_resume);

#ifdef CONFIG_OF
static const struct of_device_id gpio_vibra_dt_match_table[] = {
	{ .compatible = "gpio-vibrator" },
	{}
};
MODULE_DEVICE_TABLE(of, gpio_vibra_dt_match_table);
#endif

static struct platform_driver gpio_vibrator_driver = {
	.probe	= gpio_vibrator_probe,
	.driver	= {
		.name	= "gpio-vibrator",
		.pm	= &gpio_vibrator_pm_ops,
		.of_match_table = of_match_ptr(gpio_vibra_dt_match_table),
	},
};
module_platform_driver(gpio_vibrator_driver);

MODULE_AUTHOR("Luca Weiss <luca@z3ntu.xy>");
MODULE_DESCRIPTION("GPIO vibrator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-vibrator");
