/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ble_svc.h"
#include "events_svc.h"
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
		ret = ble_svc_update_humidity_value(humidity_temperature_svc_get_humidity());
		if (ret != 0) {
			LOG_WRN("Failed to update temperature measurement over BLE: %d", ret);
		}

		ret = ble_svc_update_temperature_value(humidity_temperature_svc_get_temperature());
		if (ret != 0) {
			LOG_WRN("Failed to update humidity measurement over BLE: %d", ret);
		}
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

	ble_svc_init();

	ret = ble_svc_enable_ble();
	if (ret != 0) {
		LOG_ERR("Failed to enable BLE: %d", ret);
	}

	while (true) {
		struct event evt;

		/* Wait for the next event */
		ret = events_svc_get_event(&evt);
		if (ret != 0) {
			LOG_WRN("Unable to get event. Err: %d", ret);
			continue;
		}

		LOG_INF("Event: %s", events_svc_type_to_text(evt.type));

		switch (evt.type) {
		case EVENT_BLE_CONNECTED:
			k_work_reschedule(&measuring_work,
					  K_MSEC(CONFIG_FIRST_MEASUREMENT_DELAY_SECONDS));
			break;

		case EVENT_BLE_NOT_CONNECTED:
			struct k_work_sync sync;
			k_work_cancel_delayable_sync(&measuring_work, &sync);
			break;

		default:
			break;
		}
	}

	k_work_schedule(&measuring_work, K_MSEC(CONFIG_FIRST_MEASUREMENT_DELAY_SECONDS));

	return 0;
}
