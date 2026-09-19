#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct i2c_pmic {
	struct device		*dev;
	struct regmap		*regmap;
	bool			resume_completed;
};

static struct regmap_config wl2866d_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= 0xF,
};

static int wl2866d_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct i2c_pmic *chip;
	int rc = 0;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &client->dev;
	chip->regmap = devm_regmap_init_i2c(client, &wl2866d_regmap_config);
	if (!chip->regmap)
		return -ENODEV;

	i2c_set_clientdata(client, chip);

	of_platform_populate(chip->dev->of_node, NULL, NULL, chip->dev);
	pr_info("I2C WL2866D PMIC Probe Successful\n");
	return rc;
}

static int wl2866d_remove(struct i2c_client *client)
{
	struct i2c_pmic *chip = i2c_get_clientdata(client);

	of_platform_depopulate(chip->dev);
	i2c_set_clientdata(client, NULL);
	return 0;
}
static const struct of_device_id wl2866d_match_table[] = {
	{ .compatible = "wl2866d", },
	{ },
};

static struct i2c_driver wl2866d_driver = {
	.driver		= {
		.name		= "wl2866d",
		.of_match_table	= wl2866d_match_table,
	},
	.probe		= wl2866d_probe,
	.remove		= wl2866d_remove,
};

module_i2c_driver(wl2866d_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("jlq,wl2866d");
MODULE_SOFTDEP("pre: i2c-designware-core");
