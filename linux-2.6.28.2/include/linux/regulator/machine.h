/*
 * machine.h -- SoC Regulator support, machine/board driver API.
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lg@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Regulator Machine/Board Interface.
 */

#ifndef __LINUX_REGULATOR_MACHINE_H_
#define __LINUX_REGULATOR_MACHINE_H_

#include <linux/regulator/consumer.h>
#include <linux/suspend.h>

struct regulator;

/*
 * Regulator operation constraint flags. These flags are used to enable
 * certain regulator operations and can be OR'ed together.
 *
 * VOLTAGE:  Regulator output voltage can be changed by software on this
 *           board/machine.
 * CURRENT:  Regulator output current can be changed by software on this
 *           board/machine.
 * MODE:     Regulator operating mode can be changed by software on this
 *           board/machine.
 * STATUS:   Regulator can be enabled and disabled.
 * DRMS:     Dynamic Regulator Mode Switching is enabled for this regulator.
 */

#define REGULATOR_CHANGE_VOLTAGE	0x1
#define REGULATOR_CHANGE_CURRENT	0x2
#define REGULATOR_CHANGE_MODE		0x4
#define REGULATOR_CHANGE_STATUS		0x8
#define REGULATOR_CHANGE_DRMS		0x10

/**
 * struct regulator_state - regulator state during low power syatem states
 *
 * This describes a regulators state during a system wide low power state.
 */
struct regulator_state {
	int uV;	/* suspend voltage */
	unsigned int mode; /* suspend regulator operating mode */
	int enabled; /* is regulator enabled in this suspend state */
};

/**
 * struct regulation_constraints - regulator operating constraints.
 *
 * This struct describes regulator and board/machine specific constraints.
 */
struct regulation_constraints {

	char *name;

	/* voltage output range (inclusive) - for voltage control */
	int min_uV;
	int max_uV;

	/* current output range (inclusive) - for current control */
	int min_uA;
	int max_uA;

	/* valid regulator operating modes for this machine */
	unsigned int valid_modes_mask;

	/* valid operations for regulator on this machine */
	unsigned int valid_ops_mask;

	/* regulator input voltage - only if supply is another regulator */
	int input_uV;

	/* regulator suspend states for global PMIC STANDBY/HIBERNATE */
	struct regulator_state state_disk;
	struct regulator_state state_mem;
	struct regulator_state state_standby;
	suspend_state_t initial_state; /* suspend state to set at init */

	/* constriant flags */
	unsigned always_on:1;	/* regulator never off when system is on */
	unsigned boot_on:1;	/* bootloader/firmware enabled regulator */
	unsigned apply_uV:1;	/* apply uV constraint iff min == max */
};

/**
 * struct regulator_consumer_supply - supply -> device mapping
 *
 * This maps a supply name to a device.  Only one of dev or dev_name
 * can be specified.  Use of dev_name allows support for buses which
 * make struct device available late such as I2C and is the preferred
 * form.
 *
 * @dev: Device structure for the consumer.
 * @dev_name: Result of dev_name() for the consumer.
 * @supply: Name for the supply.
 */
struct regulator_consumer_supply {
	struct device *dev;	/* consumer */
	const char *dev_name;   /* dev_name() for consumer */
	const char *supply;	/* consumer supply - e.g. "vcc" */
};

/* Initialize struct regulator_consumer_supply */
#define REGULATOR_SUPPLY(_name, _dev_name)			\
{								\
	.supply		= _name,				\
	.dev_name	= _dev_name,				\
}
#if 0
/**
 * struct regulator_consumer_supply - supply -> device mapping
 *
 * This maps a supply name to a device.
 */
struct regulator_consumer_supply {
	struct device *dev;	/* consumer */
	const char *supply;	/* consumer supply - e.g. "vcc" */
};

#endif
/**
 * struct regulator_init_data - regulator platform initialisation data.
 *
 * Initialisation constraints, our supply and consumers supplies.
 */
struct regulator_init_data {
	struct device *supply_regulator_dev; /* or NULL for LINE */

	struct regulation_constraints constraints;

	int num_consumer_supplies;
	struct regulator_consumer_supply *consumer_supplies;

	/* optional regulator machine specific init */
	int (*regulator_init)(void *driver_data);
	void *driver_data;	/* core does not touch this */
};

int regulator_suspend_prepare(suspend_state_t state);

#endif
