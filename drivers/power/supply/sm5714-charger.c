#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/property.h>

//SM5714-CHARGER reg
#define SM5714_CHG_REG_STATUS1            0x0D
#define SM5714_CHG_REG_STATUS2            0x0E
#define SM5714_CHG_REG_STATUS3            0x0F
#define SM5714_CHG_REG_STATUS4            0x10
#define SM5714_CHG_REG_STATUS5            0x11

#define SM5714_CHG_REG_CHGCNTL4           0x1A

struct sm5714_charger {
    struct power_supply *psy;
    struct regmap* regmap;
    bool use_autostop;
};

static enum power_supply_property sm5714_charger_props[] = {
    POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
};

static int sm5714_charger_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val) 
{
    int error;
    unsigned int value;
    struct sm5714_charger *drv;

    int status = POWER_SUPPLY_STATUS_UNKNOWN;
    int health = POWER_SUPPLY_HEALTH_UNKNOWN;
    unsigned int reg_st1, reg_st2, reg_st3;

    drv = power_supply_get_drvdata(psy);
    switch (psp) {
        case POWER_SUPPLY_PROP_PRESENT:
            error = regmap_read(drv->regmap, SM5714_CHG_REG_STATUS2, &value);
            val->intval = (value & (0x1 << 2)) ? 0 : 1;
            break;
        case POWER_SUPPLY_PROP_ONLINE:
            error = regmap_read(drv->regmap, SM5714_CHG_REG_STATUS1, &value);
            val->intval = value & 0x1 ? 1 : 0;
            break;
        case POWER_SUPPLY_PROP_STATUS:
            error = regmap_read(drv->regmap, SM5714_CHG_REG_STATUS1, &reg_st1);
            error = regmap_read(drv->regmap, SM5714_CHG_REG_STATUS2, &reg_st2);
            error = regmap_read(drv->regmap, SM5714_CHG_REG_STATUS3, &reg_st3);

            if (reg_st2 & (0x1 << 5))
                status = POWER_SUPPLY_STATUS_FULL;
            else if (reg_st2 & (0x1 << 3))
                status = POWER_SUPPLY_STATUS_CHARGING;
            else {
                if (reg_st1 & (0x1 << 0))
                    status = POWER_SUPPLY_STATUS_NOT_CHARGING;
                else
                    status = POWER_SUPPLY_STATUS_DISCHARGING;
            }
            val->intval = status;
            break;
        case POWER_SUPPLY_PROP_HEALTH:
            error = regmap_read(drv->regmap, SM5714_CHG_REG_STATUS1, &value);
            if (value & (0x1 << 0))
                health = POWER_SUPPLY_HEALTH_GOOD;
            else {
                if (value & (0x1 << 2)) {
                    health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
                } else if (value & (0x1 << 1)) {
                    //undervoltage
                    health = POWER_SUPPLY_HEALTH_UNKNOWN;
                }
            }
            val->intval = health;
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

static const struct power_supply_desc sm5714_charger_desc = {
	.name			= "sm5714_charger",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= sm5714_charger_props,
	.num_properties		= ARRAY_SIZE(sm5714_charger_props),
	.get_property		= sm5714_charger_get_property,
};

static const struct regmap_config sm5714_charger_regmap = {
	.reg_bits	= 8,
	.val_bits	= 16,
    .val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int sm5714_charger_probe (struct i2c_client* i2c) {
    int error;
    struct device* dev = &i2c->dev;
    struct power_supply_config charger_cfg = {};
    struct sm5714_charger *drv;

	drv = devm_kzalloc(&i2c->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

    charger_cfg.drv_data = drv;
	charger_cfg.of_node = i2c->dev.of_node;

    drv->regmap = devm_regmap_init_i2c(i2c, &sm5714_charger_regmap);
    if (IS_ERR(drv->regmap))
		return PTR_ERR(drv->regmap);

	drv->use_autostop = device_property_read_bool(dev, "siliconmitus,enable-autostop");

    error = regmap_update_bits(drv->regmap, SM5714_CHG_REG_CHGCNTL4, (0x1 << 6), 
                            (drv->use_autostop << 6));
    if (error)
         return dev_err_probe(dev, error, "Unable to set autostop register\n");

	drv->psy = devm_power_supply_register(dev, &sm5714_charger_desc,
						   &charger_cfg);

	if (IS_ERR(drv->psy)) {
		dev_err(dev, "failed to register power supply\n");
		return PTR_ERR(drv->psy);
	}

	return 0;
}

static const struct i2c_device_id sm5714_i2c_ids[] = {
        { "sm5714-charger", 0 },
        { },
};
MODULE_DEVICE_TABLE(i2c, sm5714_i2c_ids);

static struct of_device_id sm5714_of_match_table[] = {
	{ .compatible = "siliconmitus,sm5714-charger", },
	{ },
};
MODULE_DEVICE_TABLE(of, sm5714_of_match_table);

static struct i2c_driver sm5714_charger_driver = {
	.driver = {
		   .name = "sm5714-charger",
		   .of_match_table = sm5714_of_match_table,
	},
	.probe_new	= sm5714_charger_probe,
    .id_table   = sm5714_i2c_ids,
};

module_i2c_driver(sm5714_charger_driver);

MODULE_DESCRIPTION("Samsung SM5714-CHARGER");
MODULE_AUTHOR("map220v <map220v300@gmail.com>");
MODULE_LICENSE("GPL");