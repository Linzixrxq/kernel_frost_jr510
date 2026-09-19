// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define MAX_JLQBATT_ID_NUM		4

struct jlqbatt_ids {
	int kohm[MAX_JLQBATT_ID_NUM];
	int num;
};

static int of_jlq_battery_profile_read_batt_id_kohm(const struct device_node *np,
				const char *propname, struct jlqbatt_ids *batt_ids)
{
	struct property *prop;
	const __be32 *data;
	int num, i, *id_kohm = batt_ids->kohm;

	prop = of_find_property(np, "batt-id-kohm", NULL);
	if (!prop) {
		pr_err("%s: No battery id resistor found\n", np->name);
		return -EINVAL;
	} else if (!prop->value) {
		pr_err("%s: No battery id resistor value found, np->name\n",
						np->name);
		return -ENODATA;
	} else if (prop->length > MAX_JLQBATT_ID_NUM * sizeof(__be32)) {
		pr_err("%s: Too many battery id resistors\n", np->name);
		return -EINVAL;
	}

	num = prop->length/sizeof(__be32);
	batt_ids->num = num;
	data = prop->value;
	for (i = 0; i < num; i++)
		*id_kohm++ = be32_to_cpup(data++);

	return 0;
}


struct device_node *of_jlq_battery_profile_get_best_profile(
		const struct device_node *batterydata_container_node,
		int batt_id_kohm, const char *batt_type)
{
	struct jlqbatt_ids batt_ids;
	struct device_node *node, *best_node = NULL;
	struct device_node *default_node = NULL;
	const char *battery_type = NULL;
	int delta = 0, best_delta = 0, best_id_kohm = 0, id_range_pct,
		i = 0, rc = 0, limit = 0;
	bool in_range = false;

	/* read battery id range percentage for best profile */
	rc = of_property_read_u32(batterydata_container_node,
			"batt-id-range-pct", &id_range_pct);

	if (rc) {
		if (rc == -EINVAL) {
			id_range_pct = 0;
		} else {
			pr_err("failed to read battery id range\n");
			return ERR_PTR(-ENXIO);
		}
	}

	/*
	 * Find the battery data with a battery id resistor closest to this one
	 */
	for_each_child_of_node(batterydata_container_node, node) {
		if(!default_node) {
			default_node = node;
		}
		if (batt_type != NULL) {
			rc = of_property_read_string(node, "battery-name",
							&battery_type);
			if (!rc && strcmp(battery_type, batt_type) == 0) {
				best_node = node;
				best_id_kohm = batt_id_kohm;
				break;
			}
		} else {
			rc = of_jlq_battery_profile_read_batt_id_kohm(node,
							"batt-id-kohm",
							&batt_ids);
			if (rc)
				continue;
			for (i = 0; i < batt_ids.num; i++) {
				delta = abs(batt_ids.kohm[i] - batt_id_kohm);
				limit = (batt_ids.kohm[i] * id_range_pct) / 100;
				in_range = (delta <= limit);
				/*
				 * Check if the delta is the lowest one
				 * and also if the limits are in range
				 * before selecting the best node.
				 */
				if ((delta < best_delta || !best_node)
					&& in_range) {
					best_node = node;
					best_delta = delta;
					best_id_kohm = batt_ids.kohm[i];
				}
			}
		}
	}

	if (best_node == NULL) {
		best_node = default_node;
		if (best_node)
			pr_info("No battery profile found,use default battery profile[%s].\n",
				best_node->name);
		else
			pr_err("No battery profile found.\n");
		return best_node;
	}

	/* check that profile id is in range of the measured batt_id */
	if (abs(best_id_kohm - batt_id_kohm) >
			((best_id_kohm * id_range_pct) / 100)) {
		pr_err("out of range: profile id %d batt id %d pct %d\n",
			best_id_kohm, batt_id_kohm, id_range_pct);
		return NULL;
	}

	rc = of_property_read_string(best_node, "battery-name",
							&battery_type);
	if (!rc)
		pr_info("Battery[%s] found\n", battery_type);
	else
		pr_info("Battery[%s] found[%s]\n", best_node->name, battery_type);

	return best_node;
}

MODULE_DESCRIPTION("jlq battery profile");
MODULE_LICENSE("GPL");

