/*
 * qcom batt unify sysfs file
 *
 * Copyright (C) 2021 SinoWealth
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <jlq_charger_manager.h>

#define INPUT_SUSPEND_VLIMIT 12000000
#define INPUT_RESUME_VLIMIT 0
#define MAX_UEVENT_LENGTH 50

static ssize_t fake_temp_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);

    if (charger->fake_temp >= JCM_FAKE_TEMP_ERRVAL)
        return sprintf(buf, "%s\n", "NULL");

    return sprintf(buf, "%d\n", charger->fake_temp);
}

static ssize_t fake_temp_store(struct class *c, struct class_attribute *attr,
            const char *buf, size_t count)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);
    int ret;
    int fake_temp;

    ret = kstrtoint(buf, 0, &fake_temp);
    if (ret < 0) {
        ret = -EINVAL;
        schedule_work(&charger->cm->psys_status_change_work);
        charger->fake_temp = JCM_FAKE_TEMP_ERRVAL;
        return ret;
    }
    if (fake_temp >= JCM_FAKE_TEMP_ERRVAL)
        charger->fake_temp = JCM_FAKE_TEMP_ERRVAL;
    else
        charger->fake_temp = fake_temp;
    schedule_work(&charger->cm->psys_status_change_work);

    return count;
}

static struct class_attribute class_attr_fake_temp =
    __ATTR(fake_temp, 0644, fake_temp_show, fake_temp_store);

static ssize_t fake_soc_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);

    if (charger->fake_soc >= JCM_FAKE_SOC_ERRVAL)
        return sprintf(buf, "%s\n", "NULL");
    return sprintf(buf, "%d\n", charger->fake_soc);
}

static ssize_t fake_soc_store(struct class *c, struct class_attribute *attr,
            const char *buf, size_t count)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);
    int ret;
    int fake_soc;

    ret = kstrtoint(buf, 0, &fake_soc);
    if (ret < 0) {
        ret = -EINVAL;
        charger->fake_soc = JCM_FAKE_SOC_ERRVAL;
        jcm_update_msoc_force_flush(charger->cm);
        return ret;
    }
    if (fake_soc >= JCM_FAKE_SOC_ERRVAL)
        charger->fake_soc = JCM_FAKE_SOC_ERRVAL;
    else
        charger->fake_soc = fake_soc;
    jcm_update_msoc_force_flush(charger->cm);
    return count;
}

static struct class_attribute class_attr_fake_soc =
    __ATTR(fake_soc, 0644, fake_soc_show, fake_soc_store);

static ssize_t input_suspend_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);
    union power_supply_propval val;

    jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &val);
    if(val.intval == INPUT_SUSPEND_VLIMIT)
        return sprintf(buf, "%d\n", 1);
    else
        return sprintf(buf, "%d\n", 0);
}

static ssize_t input_suspend_store(struct class *c, struct class_attribute *attr,
            const char *buf, size_t count)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);
    int ret;
    unsigned int mode;
    union power_supply_propval val;

    ret = kstrtoint(buf, 0, &mode);
    if (ret < 0) {
        ret = -EINVAL;
        return ret;
    }
    if(mode)
        val.intval = INPUT_SUSPEND_VLIMIT;
    else
        val.intval = INPUT_RESUME_VLIMIT;
    jcm_set_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &val);

    return count;
}

static struct class_attribute class_attr_input_suspend =
    __ATTR(input_suspend, 0644, input_suspend_show, input_suspend_store);

static ssize_t shutdown_delay_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);
    unsigned int val;
    if(!charger->cm->shutdown_delay_en)
        val = 0;
    else
        val = charger->cm->shutdown_delay;
    return sprintf(buf, "%u", val);
}

static ssize_t shutdown_delay_store(struct class *c, struct class_attribute *attr,
            const char *buf, size_t count)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);
    int ret;
    int val;

    ret = kstrtoint(buf, 0, &val);
    if (ret < 0) {
        ret = -EINVAL;
        return ret;
    }
    charger->cm->shutdown_delay_en = val;
    return count;
}

static struct class_attribute class_attr_shutdown_delay =
    __ATTR(shutdown_delay, 0644, shutdown_delay_show, shutdown_delay_store);

static ssize_t quick_charge_type_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);
    union power_supply_propval val;

    jcm_get_charger_psy_prop(charger->chips, POWER_SUPPLY_PROP_QUICK_CHARGE_TYPE, &val);
    return sprintf(buf, "%d\n", val.intval);
}

static CLASS_ATTR_RO(quick_charge_type);

static ssize_t real_type_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);

    switch (charger->chg_type) {
        case POWER_SUPPLY_TYPE_UNKNOWN:
            return sprintf(buf, "%s\n", "No input");
        break;
        case POWER_SUPPLY_TYPE_USB:
            return sprintf(buf, "%s\n", "USB");
        break;
        case POWER_SUPPLY_TYPE_USB_CDP:
            return sprintf(buf, "%s\n", "USB_CDP");
        break;
        case POWER_SUPPLY_TYPE_USB_DCP:
            return sprintf(buf, "%s\n", "USB_DCP");
        break;
        case POWER_SUPPLY_TYPE_USB_HVDCP:
            return sprintf(buf, "%s\n", "USB_HVDCP");
        break;
        case POWER_SUPPLY_TYPE_USB_FLOAT:
            return sprintf(buf, "%s\n", "unknow");
        break;
        default:
            return sprintf(buf, "%s\n", "unknow");
        break;
    }
    return sprintf(buf, "%s\n", "unknow");
}

static CLASS_ATTR_RO(real_type);

static ssize_t resistance_id_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);

    return sprintf(buf, "%d\n", charger->batt_id_ohm);
}

static CLASS_ATTR_RO(resistance_id);

static ssize_t battery_type_show(struct class *c, struct class_attribute *attr,
            char *buf)
{
    struct jlq_charger_regulator *charger
       = container_of(c, struct jlq_charger_regulator, qcom_batt_unify_class);

    return sprintf(buf, "%s\n",charger->battery.battery_name);
}

static CLASS_ATTR_RO(battery_type);

static struct attribute *batt_class_attrs[] = {
    &class_attr_fake_temp.attr,
    &class_attr_fake_soc.attr,
    &class_attr_input_suspend.attr,
    &class_attr_shutdown_delay.attr,
    &class_attr_quick_charge_type.attr,
    &class_attr_real_type.attr,
    &class_attr_resistance_id.attr,
    &class_attr_battery_type.attr,
    NULL,
};
ATTRIBUTE_GROUPS(batt_class);

int qcom_battery_sysfs_init(struct jlq_charger_regulator *charger)
{
    int status;

    charger->qcom_batt_unify_class.name = "qcom-battery",
    charger->qcom_batt_unify_class.owner = THIS_MODULE,
    charger->qcom_batt_unify_class.class_groups = batt_class_groups;

    status = class_register(&charger->qcom_batt_unify_class);
    if (status < 0) {
        pr_err("%s:Fail to creat qcom-battery class file \n", __func__);
        return status;
    }

    return status;
}

void generate_xm_charge_uvent(struct jlq_charger_manager *cm)
{
    u32 cnt = 0, i = 0;

    static char uevent_string[][MAX_UEVENT_LENGTH+1] = {
        "POWER_SUPPLY_SHUTDOWN_DELAY=\n",//28+8
        "POWER_SUPPLY_QUICK_CHARGE_TYPE=\n",//31+1
    };
    char *envp[5] = { NULL };  //the length of array need adjust when uevent number increase
    char *prop_buf = NULL;

    prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
    if (!prop_buf)
        return;
    shutdown_delay_show( &(cm->charger->qcom_batt_unify_class), NULL, prop_buf);
    strncpy( uevent_string[0]+28, prop_buf,MAX_UEVENT_LENGTH-28);
    envp[cnt++] = uevent_string[0];

//    quick_charge_type_show( &(bcdev->battery_class), NULL, prop_buf);
//    strncpy( uevent_string[11]+31, prop_buf,MAX_UEVENT_LENGTH-31);
//    envp[cnt++] = uevent_string[11];

    envp[cnt] = NULL;
    /*add our prop end*/

    dev_err(cm->dev,"uevent test :");
    for(i = 0; i < cnt; ++i)
          dev_err(cm->dev," %s ", envp[i]);

    kobject_uevent_env(&cm->dev->kobj, KOBJ_CHANGE, envp);
    free_page((unsigned long)prop_buf);
    return;
}

