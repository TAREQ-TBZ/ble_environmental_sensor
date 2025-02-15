/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "humidity_temperature_svc.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define MEASUREMENT_PERIOD_MSEC      (1000 * CONFIG_MEASURING_PERIOD_SECONDS)
#define FIRST_MEASUREMENT_DELAY_MSEC (1000 * CONFIG_FIRST_MEASUREMENT_DELAY_SECONDS)

static void measuring_work_handler(struct k_work *_work)
{
	int ret;
	struct k_work_delayable *work = k_work_delayable_from_work(_work);

	ret = humidity_temperature_svc_trigger_measurement();
	if (ret != 0) {
		LOG_ERR("Failed to trigger humidity and temperature measurement: %d", ret);
	} else {
		/* TODO: Consume the temperature and humidity measurements in ble service */
	}

	k_work_reschedule(work, K_MSEC(MEASUREMENT_PERIOD_MSEC));
}
K_WORK_DELAYABLE_DEFINE(measuring_work, measuring_work_handler);

int main(void)
{
	int ret;

	LOG_INF("Starting up .. .. ..");

	ret = humidity_temperature_svc_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize humidity and temperature service!");
	}

	k_work_schedule(&measuring_work, K_MSEC(CONFIG_FIRST_MEASUREMENT_DELAY_SECONDS));

	return 0;
}
