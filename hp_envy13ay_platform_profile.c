#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/platform_profile.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/types.h>

#define DRV_NAME "hp_envy13ay_platform_profile"
#define HP_ENVY13AY_EC_PROFILE_OFFSET 0x29

/*
 * Some kernel headers may not expose prototypes for external modules.
 * The symbols themselves are expected to be exported by drivers/acpi/ec.c.
 */
extern int ec_read(u8 addr, u8 *val);
extern int ec_write(u8 addr, u8 val);

static enum platform_profile_option current_profile =
	PLATFORM_PROFILE_BALANCED;

static struct platform_device *pdev;
static struct device *profile_dev;

static int hp_envy13ay_probe_choices(void *drvdata, unsigned long *choices)
{
	set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);
	set_bit(PLATFORM_PROFILE_BALANCED, choices);
	set_bit(PLATFORM_PROFILE_COOL, choices);
	set_bit(PLATFORM_PROFILE_QUIET, choices);

	return 0;
}

static int hp_envy13ay_ec_value_to_profile(u8 value,
					   enum platform_profile_option *profile)
{
	switch (value) {
	case 0x00:
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		return 0;
	case 0x01:
		*profile = PLATFORM_PROFILE_BALANCED;
		return 0;
	case 0x02:
		*profile = PLATFORM_PROFILE_COOL;
		return 0;
	case 0x03:
		*profile = PLATFORM_PROFILE_QUIET;
		return 0;
	default:
		return -EINVAL;
	}
}

static int hp_envy13ay_profile_to_ec_value(enum platform_profile_option profile,
					   u8 *value)
{
	switch (profile) {
	case PLATFORM_PROFILE_PERFORMANCE:
		*value = 0x00; /* power / performance */
		return 0;
	case PLATFORM_PROFILE_BALANCED:
		*value = 0x01; /* recommended */
		return 0;
	case PLATFORM_PROFILE_COOL:
		*value = 0x02; /* cool */
		return 0;
	case PLATFORM_PROFILE_QUIET:
		*value = 0x03; /* silent / quiet */
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int hp_envy13ay_profile_get(struct device *dev,
				   enum platform_profile_option *profile)
{
	u8 value;
	int ret;

	ret = ec_read(HP_ENVY13AY_EC_PROFILE_OFFSET, &value);
	if (ret) {
		pr_err(DRV_NAME ": EC read at 0x%02x failed: %d\n",
		       HP_ENVY13AY_EC_PROFILE_OFFSET, ret);
		*profile = current_profile;
		return 0;
	}

	ret = hp_envy13ay_ec_value_to_profile(value, profile);
	if (ret) {
		pr_warn(DRV_NAME ": unknown EC profile value 0x%02x, using cached profile\n",
			value);
		*profile = current_profile;
		return 0;
	}

	current_profile = *profile;
	return 0;
}

static int hp_envy13ay_apply_ec_value(u8 value)
{
	int ret;
	u8 readback;

	ret = ec_write(HP_ENVY13AY_EC_PROFILE_OFFSET, value);
	if (ret) {
		pr_err(DRV_NAME ": EC write 0x%02x to offset 0x%02x failed: %d\n",
		       value, HP_ENVY13AY_EC_PROFILE_OFFSET, ret);
		return ret;
	}

	ret = ec_read(HP_ENVY13AY_EC_PROFILE_OFFSET, &readback);
	if (ret) {
		pr_warn(DRV_NAME ": EC readback failed after write: %d\n", ret);
		return 0;
	}

	pr_info(DRV_NAME ": EC offset 0x%02x set to 0x%02x, readback 0x%02x\n",
		HP_ENVY13AY_EC_PROFILE_OFFSET, value, readback);

	return 0;
}

static int hp_envy13ay_profile_set(struct device *dev,
				   enum platform_profile_option profile)
{
	u8 value;
	int ret;

	ret = hp_envy13ay_profile_to_ec_value(profile, &value);
	if (ret)
		return ret;

	ret = hp_envy13ay_apply_ec_value(value);
	if (ret)
		return ret;

	current_profile = profile;
	return 0;
}

static const struct platform_profile_ops hp_envy13ay_profile_ops = {
	.probe = hp_envy13ay_probe_choices,
	.profile_get = hp_envy13ay_profile_get,
	.profile_set = hp_envy13ay_profile_set,
};

static const struct dmi_system_id hp_envy13ay_dmi_table[] = {
	{
		.ident = "HP ENVY x360 Convertible 13-ay0xxx",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_BOARD_NAME, "876E"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, hp_envy13ay_dmi_table);

static int __init hp_envy13ay_init(void)
{
	int ret;
	u8 value;

	if (!dmi_check_system(hp_envy13ay_dmi_table)) {
		pr_info(DRV_NAME ": unsupported DMI system\n");
		return -ENODEV;
	}

	ret = ec_read(HP_ENVY13AY_EC_PROFILE_OFFSET, &value);
	if (ret) {
		pr_warn(DRV_NAME ": initial EC read failed: %d\n", ret);
	} else {
		enum platform_profile_option profile;

		if (!hp_envy13ay_ec_value_to_profile(value, &profile))
			current_profile = profile;

		pr_info(DRV_NAME ": initial EC offset 0x%02x value is 0x%02x\n",
			HP_ENVY13AY_EC_PROFILE_OFFSET, value);
	}

	pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	profile_dev = devm_platform_profile_register(
		&pdev->dev,
		DRV_NAME,
		NULL,
		&hp_envy13ay_profile_ops
	);

	if (IS_ERR(profile_dev)) {
		ret = PTR_ERR(profile_dev);
		platform_device_unregister(pdev);
		return ret;
	}

	pr_info(DRV_NAME ": registered platform profile driver\n");

	return 0;
}

static void __exit hp_envy13ay_exit(void)
{
	platform_device_unregister(pdev);
	pr_info(DRV_NAME ": unloaded\n");
}

module_init(hp_envy13ay_init);
module_exit(hp_envy13ay_exit);

MODULE_AUTHOR("local");
MODULE_DESCRIPTION("HP Envy 13-ay platform profile driver");
MODULE_LICENSE("GPL");
