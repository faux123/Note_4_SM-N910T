/*
 * Copyright (C) 2013 Samsung Electronics Co, Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/switch.h>
#include <linux/extcon.h>
#include <linux/sec_class.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/irq.h>
#include <linux/power_supply.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

static int jig_state;
struct muic_cable {
	struct work_struct work;
	struct delayed_work cable_init;
	struct notifier_block nb;
	struct extcon_specific_cable_nb extcon_nb;
	struct extcon_dev *edev;
	enum extcon_cable_name cable_type;
	int cable_state;
};

int get_jig_state(void)
{
	return jig_state;
}

static struct switch_dev switch_dock = {
	.name = "dock",
};

struct device *switch_dev;
EXPORT_SYMBOL(switch_dev);
static struct muic_cable support_cable_list[] = {
	{ .cable_type = EXTCON_DESKDOCK, },
	{ .cable_type = EXTCON_AUDIODOCK, },
	{ .cable_type = EXTCON_SMARTDOCK_TA, },
	{ .cable_type = EXTCON_SMARTDOCK_USB, },
	{ .cable_type = EXTCON_MULTIMEDIADOCK, },
	{ .cable_type = EXTCON_JIG_USBON, },
	{ .cable_type = EXTCON_JIG_USBOFF, },
	{ .cable_type = EXTCON_JIG_UARTON, },
	{ .cable_type = EXTCON_JIG_UARTOFF, },
#if defined(CONFIG_MUIC_SUPPORT_HMT_DETECTION)
	{ .cable_type = EXTCON_HMT, },
#endif
};

static void muic_cable_event_worker(struct work_struct *work)
{
	struct muic_cable *cable =
			    container_of(work, struct muic_cable, work);

	pr_info("%s: '%s' is %s\n", __func__,
			extcon_cable_name[cable->cable_type],
			cable->cable_state ? "attached" : "detached");

	switch (cable->cable_type) {
	case EXTCON_DESKDOCK:
		switch_set_state(&switch_dock, cable->cable_state ? 1 : 0);
		break;
	case EXTCON_AUDIODOCK:
		switch_set_state(&switch_dock, cable->cable_state ? 7 : 0);
		break;
	case EXTCON_SMARTDOCK_TA:
	case EXTCON_SMARTDOCK_USB:
	case EXTCON_MULTIMEDIADOCK:
		switch_set_state(&switch_dock, cable->cable_state ? 8 : 0);
		break;
	case EXTCON_JIG_USBON:
	case EXTCON_JIG_USBOFF:
	case EXTCON_JIG_UARTON:
	case EXTCON_JIG_UARTOFF:
		jig_state = cable->cable_state;
		break;
#if defined(CONFIG_MUIC_SUPPORT_HMT_DETECTION)
	case EXTCON_HMT:
		switch_set_state(&switch_dock, cable->cable_state ? 11 : 0);
		break;
#endif
	default:
		pr_err("%s: invalid cable value (%d, %d)\n", __func__,
					cable->cable_type, cable->cable_state);
		break;
	}
}


static int muic_cable_notifier(struct notifier_block *nb,
					unsigned long stat, void *ptr)
{
	struct muic_cable *cable =
			container_of(nb, struct muic_cable, nb);

	cable->cable_state = stat;
	schedule_work(&cable->work);

	return NOTIFY_DONE;
}


static int muic_init_cable_notify(void)
{
	int i, ret;
	struct muic_cable *cable = NULL;


	for (i = 0; i < ARRAY_SIZE(support_cable_list); i++) {
		cable = &support_cable_list[i];
		INIT_WORK(&cable->work, muic_cable_event_worker);
		cable->nb.notifier_call = muic_cable_notifier;

		ret = extcon_register_interest(&cable->extcon_nb,
				EXTCON_DEV_NAME,
				extcon_cable_name[cable->cable_type],
				&cable->nb);
		if (ret)
			pr_err("%s: fail to register extcon notifier(%s, %d)\n",
				__func__, extcon_cable_name[cable->cable_type],
				ret);

		cable->edev = cable->extcon_nb.edev;
		if (!cable->edev)
			pr_err("%s: fail to get extcon device\n", __func__);
	}

	return 0;
}

static __init int muic_init_switch(void)
{
	int ret;

	pr_info("register extcon notifier for JIG and docks\n");
	switch_dev = device_create(sec_class, NULL, 0, NULL, "switch");
	if (IS_ERR(switch_dev)) {
		pr_err("(%s): failed to created device (switch_dev)!\n",
				__func__);
		return -ENODEV;
	}

	ret = switch_dev_register(&switch_dock);
	if (ret < 0) {
		pr_err("Failed to register dock switch. %d\n",
				ret);
		return ret;
	}
	return 0;
}
device_initcall(muic_init_switch);
late_initcall(muic_init_cable_notify);
