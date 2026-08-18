#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/platform_profile.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/types.h>

#define DRV_NAME "hp_envy13ay_platform_profile"
#define HP_ENVY13AY_EC_PROFILE_OFFSET 0x29

/*
 * EC shared-memory window.
 *
 * The DSDT declares it as
 *     OperationRegion (ECMP, SystemMemory, 0xFE0B0000, 0x1000)
 * and places the fan and thermal state inside it. The region is plain MMIO:
 * it is absent from the e820 map and unclaimed in /proc/iomem, so a read-only
 * mapping cannot collide with the ACPI EC driver.
 *
 * Byte positions were derived from the DSDT field list starting at
 * Offset (0x8A2) and cross-checked three ways:
 *   1. the running total lands exactly on the declared Offset (0x8B6)
 *   2. NPST reads 3, which is what EC0._REG and _WAK write
 *   3. CTMP tracks thermal_zone0 (TSZ0._TMP returns 0x0AAC + CTMP * 0x0A)
 *
 * EST1/EST2/EST3 are declared in the DSDT but never referenced by any AML;
 * they are EC firmware state exposed through the shared window. Load testing
 * on BIOS F.24 showed the fan starts the moment EST1/EST2 cross 37 degC and
 * stays off below it, even with the CPU at 97 degC. In other words these are
 * the skin temperatures that drive the EC fan curve, and CPU temperature on
 * its own does not start the fan.
 *
 * That matters because HP BIOS F.23/F.24 have a reported defect where the
 * skin temperature channel freezes at room temperature after long uptime,
 * leaving the fan off while the CPU runs away. Exposing these values through
 * hwmon makes the condition visible in ordinary tools.
 */
#define ECMP_PHYS_BASE	0xFE0B0000UL
#define ECMP_LEN	0x1000

#define ECMP_CTMP	0x8B0	/* CPU temperature, degC */
#define ECMP_EST3	0x8B2	/* skin temperature 3, degC */
#define ECMP_EST1	0x8B6	/* skin temperature 1, degC - fan curve input */
#define ECMP_EST2	0x8B7	/* skin temperature 2, degC - fan curve input */

/*
 * Fan speed comes from the ACPI methods the firmware provides for exactly
 * this purpose, so no hardcoded address is needed for it:
 *     FRSP() returns FRPM * 100, i.e. RPM
 *     FMAX() / FMIN() return the fan limits, also in RPM
 */
#define ACPI_FAN_RPM	"\\_TZ.TSZ0.FRSP"
#define ACPI_FAN_MAX	"\\_TZ.TSZ0.FMAX"
#define ACPI_FAN_MIN	"\\_TZ.TSZ0.FMIN"

/*
 * The EC updates the shared window without any locking against us, so a read
 * can occasionally catch a byte mid-update. A single bogus sample was observed
 * in practice (EST1 momentarily reading 1 while EST2 stayed at 42). Values
 * outside this range are treated as torn reads and the previous value is
 * reported instead.
 */
#define TEMP_PLAUSIBLE_MIN	5
#define TEMP_PLAUSIBLE_MAX	127

static bool hwmon_enable = true;
module_param_named(hwmon, hwmon_enable, bool, 0444);
MODULE_PARM_DESC(hwmon,
		 "Expose EC fan speed and skin temperatures via hwmon (default: 1)");

static void __iomem *ecmp_base;

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

/* ------------------------------------------------------------------ hwmon */

struct hp_envy13ay_temp {
	unsigned int offset;
	const char *label;
	long last;		/* last plausible reading, in degC */
};

static struct hp_envy13ay_temp hp_envy13ay_temps[] = {
	{ ECMP_CTMP, "CPU",    0 },
	{ ECMP_EST1, "Skin 1", 0 },
	{ ECMP_EST2, "Skin 2", 0 },
	{ ECMP_EST3, "Skin 3", 0 },
};

static const char * const hp_envy13ay_fan_label = "Fan";

static int hp_envy13ay_eval(const char *path, unsigned long long *out)
{
	acpi_status status;

	status = acpi_evaluate_integer(NULL, (acpi_string)path, NULL, out);
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

static int hp_envy13ay_read_temp(int channel, long *val)
{
	struct hp_envy13ay_temp *t;
	u8 raw;

	if (channel < 0 || channel >= ARRAY_SIZE(hp_envy13ay_temps))
		return -EINVAL;

	t = &hp_envy13ay_temps[channel];
	raw = readb(ecmp_base + t->offset);

	if (raw >= TEMP_PLAUSIBLE_MIN && raw <= TEMP_PLAUSIBLE_MAX)
		t->last = raw;
	else if (!t->last)
		return -EIO;

	*val = t->last * 1000;
	return 0;
}

static int hp_envy13ay_read_fan(u32 attr, long *val)
{
	unsigned long long rpm;
	const char *path;
	int ret;

	switch (attr) {
	case hwmon_fan_input:
		path = ACPI_FAN_RPM;
		break;
	case hwmon_fan_max:
		path = ACPI_FAN_MAX;
		break;
	case hwmon_fan_min:
		path = ACPI_FAN_MIN;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ret = hp_envy13ay_eval(path, &rpm);
	if (ret)
		return ret;

	*val = rpm;
	return 0;
}

static umode_t hp_envy13ay_hwmon_is_visible(const void *drvdata,
					    enum hwmon_sensor_types type,
					    u32 attr, int channel)
{
	return 0444;
}

static int hp_envy13ay_hwmon_read(struct device *dev,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		if (attr != hwmon_temp_input)
			return -EOPNOTSUPP;
		return hp_envy13ay_read_temp(channel, val);
	case hwmon_fan:
		return hp_envy13ay_read_fan(attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int hp_envy13ay_hwmon_read_string(struct device *dev,
					 enum hwmon_sensor_types type,
					 u32 attr, int channel,
					 const char **str)
{
	switch (type) {
	case hwmon_temp:
		if (channel < 0 || channel >= ARRAY_SIZE(hp_envy13ay_temps))
			return -EINVAL;
		*str = hp_envy13ay_temps[channel].label;
		return 0;
	case hwmon_fan:
		*str = hp_envy13ay_fan_label;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops hp_envy13ay_hwmon_ops = {
	.is_visible = hp_envy13ay_hwmon_is_visible,
	.read = hp_envy13ay_hwmon_read,
	.read_string = hp_envy13ay_hwmon_read_string,
};

static const struct hwmon_channel_info * const hp_envy13ay_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_MAX |
			   HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_chip_info hp_envy13ay_hwmon_chip_info = {
	.ops = &hp_envy13ay_hwmon_ops,
	.info = hp_envy13ay_hwmon_info,
};

static int hp_envy13ay_hwmon_init(struct platform_device *dev)
{
	struct device *hwmon_dev;
	unsigned long long rpm;

	ecmp_base = devm_ioremap(&dev->dev, ECMP_PHYS_BASE, ECMP_LEN);
	if (!ecmp_base) {
		pr_warn(DRV_NAME ": failed to map EC window at 0x%lx, hwmon disabled\n",
			ECMP_PHYS_BASE);
		return -ENOMEM;
	}

	/*
	 * Sanity check the mapping before advertising sensors. An unmapped or
	 * wrong window reads back as all-ones.
	 */
	if (readb(ecmp_base + ECMP_CTMP) == 0xFF &&
	    readb(ecmp_base + ECMP_EST1) == 0xFF) {
		pr_warn(DRV_NAME ": EC window reads all 0xFF, hwmon disabled\n");
		return -ENODEV;
	}

	if (hp_envy13ay_eval(ACPI_FAN_RPM, &rpm))
		pr_warn(DRV_NAME ": %s not evaluable, fan speed will read as error\n",
			ACPI_FAN_RPM);

	hwmon_dev = devm_hwmon_device_register_with_info(&dev->dev,
							 "hp_envy13ay", NULL,
							 &hp_envy13ay_hwmon_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	pr_info(DRV_NAME ": hwmon registered (fan + CPU/skin temperatures)\n");
	return 0;
}

/* ------------------------------------------------------------------------ */

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

	/*
	 * The sensors are informational; failing to expose them must not stop
	 * the platform_profile interface from working.
	 */
	if (hwmon_enable) {
		ret = hp_envy13ay_hwmon_init(pdev);
		if (ret)
			pr_warn(DRV_NAME ": hwmon init failed: %d\n", ret);
	}

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
