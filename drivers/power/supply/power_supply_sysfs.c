/*
 *  Sysfs interface for the universal power supply monitor class
 *
 *  Copyright © 2007  David Woodhouse <dwmw2@infradead.org>
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 *
 *  You may use this code as per GPL version 2
 */

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/stat.h>

#include "power_supply.h"

/*
 * This is because the name "current" breaks the device attr macro.
 * The "current" word resolves to "(get_current())" so instead of
 * "current" "(get_current())" appears in the sysfs.
 *
 * The source of this definition is the device.h which calls __ATTR
 * macro in sysfs.h which calls the __stringify macro.
 *
 * Only modification that the name is not tried to be resolved
 * (as a macro let's say).
 */

#define POWER_SUPPLY_ATTR(_name)					\
{									\
	.attr = { .name = #_name },					\
	.show = power_supply_show_property,				\
	.store = power_supply_store_property,				\
}

static struct device_attribute power_supply_attrs[];

static const char * const power_supply_type_text[] = {
	"Unknown", "Battery", "UPS", "Mains", "USB",
	"USB_DCP", "USB_CDP", "USB_ACA", "Wireless", "USB_C",
	"USB_PD", "USB_PD_DRP", "BrickID"
};

static const char * const power_supply_status_text[] = {
	"Unknown", "Charging", "Discharging", "Not charging", "Full",
	"Cmd discharging"
};

static const char * const power_supply_charge_type_text[] = {
	"Unknown", "N/A", "Trickle", "Fast"
};

static const char * const power_supply_health_text[] = {
	"Unknown", "Good", "Overheat", "Dead", "Over voltage",
	"Unspecified failure", "Cold", "Watchdog timer expire",
	"Safety timer expire"
};

static const char * const power_supply_technology_text[] = {
	"Unknown", "NiMH", "Li-ion", "Li-poly", "LiFe", "NiCd",
	"LiMn"
};

static const char * const power_supply_capacity_level_text[] = {
	"Unknown", "Critical", "Low", "Normal", "High", "Full"
};

static const char * const power_supply_scope_text[] = {
	"Unknown", "System", "Device"
};

static ssize_t power_supply_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf) {
	ssize_t ret = 0;
	struct power_supply *psy = dev_get_drvdata(dev);
	const ptrdiff_t off = attr - power_supply_attrs;
	union power_supply_propval value;

	if (off == POWER_SUPPLY_PROP_TYPE) {
		value.intval = psy->desc->type;
	} else {
		ret = power_supply_get_property(psy, off, &value);

		if (ret < 0) {
			if (ret == -ENODATA)
				dev_dbg(dev, "driver has no data for `%s' property\n",
					attr->attr.name);
			else if (ret != -ENODEV && ret != -EAGAIN)
				dev_err_ratelimited(dev,
					"driver failed to report `%s' property: %zd\n",
					attr->attr.name, ret);
			return ret;
		}
	}

	if (off == POWER_SUPPLY_PROP_STATUS)
		return sprintf(buf, "%s\n",
			       power_supply_status_text[value.intval]);
	else if (off == POWER_SUPPLY_PROP_CHARGE_TYPE)
		return sprintf(buf, "%s\n",
			       power_supply_charge_type_text[value.intval]);
	else if (off == POWER_SUPPLY_PROP_HEALTH)
		return sprintf(buf, "%s\n",
			       power_supply_health_text[value.intval]);
	else if (off == POWER_SUPPLY_PROP_TECHNOLOGY)
		return sprintf(buf, "%s\n",
			       power_supply_technology_text[value.intval]);
	else if (off == POWER_SUPPLY_PROP_CAPACITY_LEVEL)
		return sprintf(buf, "%s\n",
			       power_supply_capacity_level_text[value.intval]);
	else if (off == POWER_SUPPLY_PROP_TYPE)
		return sprintf(buf, "%s\n",
			       power_supply_type_text[value.intval]);
	else if (off == POWER_SUPPLY_PROP_SCOPE)
		return sprintf(buf, "%s\n",
			       power_supply_scope_text[value.intval]);
	else if (off >= POWER_SUPPLY_PROP_MODEL_NAME)
		return sprintf(buf, "%s\n", value.strval);

	if (off == POWER_SUPPLY_PROP_CHARGE_COUNTER_EXT)
		return sprintf(buf, "%lld\n", value.int64val);
	else
		return sprintf(buf, "%d\n", value.intval);
}

static ssize_t power_supply_store_property(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count) {
	ssize_t ret;
	struct power_supply *psy = dev_get_drvdata(dev);
	const ptrdiff_t off = attr - power_supply_attrs;
	union power_supply_propval value;

	/* maybe it is a enum property? */
	switch (off) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = sysfs_match_string(power_supply_status_text, buf);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = sysfs_match_string(power_supply_charge_type_text, buf);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = sysfs_match_string(power_supply_health_text, buf);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		ret = sysfs_match_string(power_supply_technology_text, buf);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		ret = sysfs_match_string(power_supply_capacity_level_text, buf);
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		ret = sysfs_match_string(power_supply_scope_text, buf);
		break;
	default:
		ret = -EINVAL;
	}

	/*
	 * If no match was found, then check to see if it is an integer.
	 * Integer values are valid for enums in addition to the text value.
	 */
	if (ret < 0) {
		long long_val;

		ret = kstrtol(buf, 10, &long_val);
		if (ret < 0)
			return ret;

		ret = long_val;
	}

	value.intval = ret;

	ret = power_supply_set_property(psy, off, &value);
	if (ret < 0)
		return ret;

	return count;
}

/* Must be in the same order as POWER_SUPPLY_PROP_* */
static struct device_attribute power_supply_attrs[] = {
	/* Properties of type `int' */
	#ifdef VENDOR_EDIT
	/* Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2017/11/19, Add for charging */
	POWER_SUPPLY_ATTR(authenticate),
	POWER_SUPPLY_ATTR(charge_timeout),
	POWER_SUPPLY_ATTR(battery_request_poweroff),
	POWER_SUPPLY_ATTR(charge_technology),
	POWER_SUPPLY_ATTR(fastcharger),
	POWER_SUPPLY_ATTR(mmi_charging_enable),
	POWER_SUPPLY_ATTR(stop_charging_enable),
	POWER_SUPPLY_ATTR(otg_switch),
	POWER_SUPPLY_ATTR(otg_online),
	POWER_SUPPLY_ATTR(batt_fcc),
	POWER_SUPPLY_ATTR(batt_soh),
	POWER_SUPPLY_ATTR(batt_cc),
	POWER_SUPPLY_ATTR(batt_rm),
	POWER_SUPPLY_ATTR(notify_code),
	POWER_SUPPLY_ATTR(cool_down),              //zhangchao@ODM.HQ.Charger 2019/12/04 modified for limit charging current in vooc when calling
	POWER_SUPPLY_ATTR(charger_ic),
	POWER_SUPPLY_ATTR(hmac),
	#endif /* VENDOR_EDIT */

	POWER_SUPPLY_ATTR(status),
	POWER_SUPPLY_ATTR(charge_type),
	POWER_SUPPLY_ATTR(health),
	POWER_SUPPLY_ATTR(present),
	POWER_SUPPLY_ATTR(online),
	POWER_SUPPLY_ATTR(authentic),
	POWER_SUPPLY_ATTR(technology),
	POWER_SUPPLY_ATTR(cycle_count),
	POWER_SUPPLY_ATTR(voltage_max),
	POWER_SUPPLY_ATTR(voltage_min),
	POWER_SUPPLY_ATTR(voltage_max_design),
	POWER_SUPPLY_ATTR(voltage_min_design),
	POWER_SUPPLY_ATTR(voltage_now),
	POWER_SUPPLY_ATTR(voltage_avg),
	POWER_SUPPLY_ATTR(voltage_ocv),
	POWER_SUPPLY_ATTR(voltage_boot),
	POWER_SUPPLY_ATTR(current_max),
	POWER_SUPPLY_ATTR(current_now),
	POWER_SUPPLY_ATTR(current_avg),
	POWER_SUPPLY_ATTR(current_boot),
	POWER_SUPPLY_ATTR(power_now),
	POWER_SUPPLY_ATTR(power_avg),
	POWER_SUPPLY_ATTR(charge_full_design),
	POWER_SUPPLY_ATTR(charge_empty_design),
	POWER_SUPPLY_ATTR(charge_full),
	POWER_SUPPLY_ATTR(charge_empty),
	POWER_SUPPLY_ATTR(charge_now),
	POWER_SUPPLY_ATTR(charge_avg),
	POWER_SUPPLY_ATTR(charge_counter),
	POWER_SUPPLY_ATTR(constant_charge_current),
	POWER_SUPPLY_ATTR(constant_charge_current_max),
	POWER_SUPPLY_ATTR(constant_charge_voltage),
	POWER_SUPPLY_ATTR(constant_charge_voltage_max),
	POWER_SUPPLY_ATTR(charge_control_limit),
	POWER_SUPPLY_ATTR(charge_control_limit_max),
	POWER_SUPPLY_ATTR(input_current_limit),
	POWER_SUPPLY_ATTR(energy_full_design),
	POWER_SUPPLY_ATTR(energy_empty_design),
	POWER_SUPPLY_ATTR(energy_full),
	POWER_SUPPLY_ATTR(energy_empty),
	POWER_SUPPLY_ATTR(energy_now),
	POWER_SUPPLY_ATTR(energy_avg),
	POWER_SUPPLY_ATTR(capacity),
	POWER_SUPPLY_ATTR(capacity_alert_min),
	POWER_SUPPLY_ATTR(capacity_alert_max),
	POWER_SUPPLY_ATTR(capacity_level),
	POWER_SUPPLY_ATTR(temp),
	POWER_SUPPLY_ATTR(temp_max),
	POWER_SUPPLY_ATTR(temp_min),
	POWER_SUPPLY_ATTR(temp_alert_min),
	POWER_SUPPLY_ATTR(temp_alert_max),
	POWER_SUPPLY_ATTR(temp_ambient),
	POWER_SUPPLY_ATTR(temp_ambient_alert_min),
	POWER_SUPPLY_ATTR(temp_ambient_alert_max),
	POWER_SUPPLY_ATTR(time_to_empty_now),
	POWER_SUPPLY_ATTR(time_to_empty_avg),
	POWER_SUPPLY_ATTR(time_to_full_now),
	POWER_SUPPLY_ATTR(time_to_full_avg),
	POWER_SUPPLY_ATTR(type),
	POWER_SUPPLY_ATTR(scope),
	POWER_SUPPLY_ATTR(precharge_current),
	POWER_SUPPLY_ATTR(charge_term_current),
	POWER_SUPPLY_ATTR(calibrate),
	/* Local extensions */
	POWER_SUPPLY_ATTR(usb_hc),
	POWER_SUPPLY_ATTR(usb_otg),
	POWER_SUPPLY_ATTR(charge_enabled),
	/* Local extensions of type int64_t */
	POWER_SUPPLY_ATTR(charge_counter_ext),
#if CONFIG_MTK_GAUGE_VERSION == 10
	POWER_SUPPLY_ATTR(batt_vol),
	POWER_SUPPLY_ATTR(batt_temp),
	/* 20100405 Add for EM */
	POWER_SUPPLY_ATTR(TemperatureR),
	POWER_SUPPLY_ATTR(TempBattVoltage),
	POWER_SUPPLY_ATTR(InstatVolt),
	POWER_SUPPLY_ATTR(BatteryAverageCurrent),
	POWER_SUPPLY_ATTR(BatterySenseVoltage),
	POWER_SUPPLY_ATTR(ISenseVoltage),
	POWER_SUPPLY_ATTR(ChargerVoltage),
	/* Dual battery */
	POWER_SUPPLY_ATTR(status_smb),
	POWER_SUPPLY_ATTR(capacity_smb),
	POWER_SUPPLY_ATTR(present_smb),
	/* ADB CMD Discharging */
	POWER_SUPPLY_ATTR(adjust_power),
#endif
	#ifdef VENDOR_EDIT
	/* ChaoYing.Chen@EXP.BSP.CHG.basic, 2017/05/16, Add for adapter fwupdate */
	POWER_SUPPLY_ATTR(adapter_fw_update),
	#endif /* VENDOR_EDIT */

	#ifdef VENDOR_EDIT
	/* ChaoYing.Chen@EXP.BSP.CHG.basic, 2017/05/16, Add for capacity node */
	POWER_SUPPLY_ATTR(internal_capacity),
	#endif  /* VENDOR_EDIT */

	#ifdef VENDOR_EDIT
	/* ChaoYing.Chen@EXP.BSP.CHG.basic, 2017/05/16, Add for chargeid voltage */
	POWER_SUPPLY_ATTR(chargerid_volt),
	#endif  /* VENDOR_EDIT */

	#ifdef VENDOR_EDIT
	/* ChaoYing.Chen@EXP.BSP.CHG.basic, 2017/05/16, Add for voocchg_ing */
	POWER_SUPPLY_ATTR(voocchg_ing),
	#endif /* VENDOR_EDIT */

	#ifdef VENDOR_EDIT
	/* ChaoYing.Chen@EXP.BSP.CHG.basic, 2017/05/16, Add for critical log */
	POWER_SUPPLY_ATTR(primal_type),
	#endif /* VENDOR_EDIT */

	#ifdef CONFIG_OPPO_CALL_MODE_SUPPORT
	/* ChaoYing.Chen@EXP.BSP.CHG.basic, 2017/05/16, Add for calling */
	POWER_SUPPLY_ATTR(call_mode),
	#endif /* VENDOR_EDIT */
	#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
	/* Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2017/12/09, Add for ship mode */
	POWER_SUPPLY_ATTR(ship_mode),
	#endif /* CONFIG_OPPO_SHIP_MODE_SUPPORT */
	POWER_SUPPLY_ATTR(flashlight_temp),
	/* Properties of type `const char *' */
	#ifdef VENDOR_EDIT
	//tongfeng.huang@PSW.BSP.CHG, 2018/02/05, Add for battery info collect
	#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
	#ifdef CONFIG_OPPO_SHORT_USERSPACE
	POWER_SUPPLY_ATTR(short_c_batt_limit_chg),
	POWER_SUPPLY_ATTR(short_c_batt_limit_rechg),
	POWER_SUPPLY_ATTR(input_current_settled),
	#else
	POWER_SUPPLY_ATTR(short_c_batt_update_change),
	POWER_SUPPLY_ATTR(short_c_batt_in_idle),
	POWER_SUPPLY_ATTR(short_c_batt_cv_status),
	#endif /*CONFIG_OPPO_SHORT_USERSPACE*/
	#endif	
	#ifdef CONFIG_OPPO_SHORT_HW_CHECK
	POWER_SUPPLY_ATTR(short_c_hw_feature),
	POWER_SUPPLY_ATTR(short_c_hw_status),
	#endif
#ifdef CONFIG_OPPO_SHORT_IC_CHECK
	POWER_SUPPLY_ATTR(short_ic_otp_status),
	POWER_SUPPLY_ATTR(short_ic_volt_thresh),
	POWER_SUPPLY_ATTR(short_ic_otp_value),
#endif
	POWER_SUPPLY_ATTR(fast2normal_chg),
	#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2019-05-14  for soc node */
	POWER_SUPPLY_ATTR(chip_soc),
	POWER_SUPPLY_ATTR(smooth_soc),
#endif
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/15, sjc Add for typec */
	POWER_SUPPLY_ATTR(typec_cc_orientation),
	POWER_SUPPLY_ATTR(usb_status),
	POWER_SUPPLY_ATTR(usbtemp_volt_l),
	POWER_SUPPLY_ATTR(usbtemp_volt_r),
#endif

#ifdef ODM_HQ_EDIT
/* zhangchao@ODM.HQ.Charger 2019/09/4 modified for bring up charging */
	POWER_SUPPLY_ATTR(typec_sbu_voltage),
	POWER_SUPPLY_ATTR(water_detect_feature),
	POWER_SUPPLY_ATTR(fast_chg_type),
#endif /*ODM_HQ_EDIT*/

	POWER_SUPPLY_ATTR(model_name),
	POWER_SUPPLY_ATTR(manufacturer),
	POWER_SUPPLY_ATTR(serial_number),
};

static struct attribute *
__power_supply_attrs[ARRAY_SIZE(power_supply_attrs) + 1];

static umode_t power_supply_attr_is_visible(struct kobject *kobj,
					   struct attribute *attr,
					   int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct power_supply *psy = dev_get_drvdata(dev);
	umode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
	int i;

	if (attrno == POWER_SUPPLY_PROP_TYPE)
		return mode;

	for (i = 0; i < psy->desc->num_properties; i++) {
		int property = psy->desc->properties[i];

		if (property == attrno) {
			if (psy->desc->property_is_writeable &&
			    psy->desc->property_is_writeable(psy, property) > 0)
				mode |= S_IWUSR;

			return mode;
		}
	}

	return 0;
}

static struct attribute_group power_supply_attr_group = {
	.attrs = __power_supply_attrs,
	.is_visible = power_supply_attr_is_visible,
};

static const struct attribute_group *power_supply_attr_groups[] = {
	&power_supply_attr_group,
	NULL,
};

void power_supply_init_attrs(struct device_type *dev_type)
{
	int i;

	dev_type->groups = power_supply_attr_groups;

	for (i = 0; i < ARRAY_SIZE(power_supply_attrs); i++)
		__power_supply_attrs[i] = &power_supply_attrs[i].attr;
}

static char *kstruprdup(const char *str, gfp_t gfp)
{
	char *ret, *ustr;

	ustr = ret = kmalloc(strlen(str) + 1, gfp);

	if (!ret)
		return NULL;

	while (*str)
		*ustr++ = toupper(*str++);

	*ustr = 0;

	return ret;
}

int power_supply_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	int ret = 0, j;
	char *prop_buf;
	char *attrname;

	if (!psy || !psy->desc) {
		dev_dbg(dev, "No power supply yet\n");
		return ret;
	}

	ret = add_uevent_var(env, "POWER_SUPPLY_NAME=%s", psy->desc->name);
	if (ret)
		return ret;

	prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!prop_buf)
		return -ENOMEM;

	for (j = 0; j < psy->desc->num_properties; j++) {
		struct device_attribute *attr;
		char *line;

		attr = &power_supply_attrs[psy->desc->properties[j]];

		ret = power_supply_show_property(dev, attr, prop_buf);
		if (ret == -ENODEV || ret == -ENODATA) {
			/* When a battery is absent, we expect -ENODEV. Don't abort;
			   send the uevent with at least the the PRESENT=0 property */
			ret = 0;
			continue;
		}

		if (ret < 0)
			goto out;

		line = strchr(prop_buf, '\n');
		if (line)
			*line = 0;

		attrname = kstruprdup(attr->attr.name, GFP_KERNEL);
		if (!attrname) {
			ret = -ENOMEM;
			goto out;
		}

		ret = add_uevent_var(env, "POWER_SUPPLY_%s=%s", attrname, prop_buf);
		kfree(attrname);
		if (ret)
			goto out;
	}

out:
	free_page((unsigned long)prop_buf);

	return ret;
}
