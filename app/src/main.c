/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "humidity_temperature_svc.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	LOG_INF("Starting up .. .. ..");

	ret = humidity_temperature_svc_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize humidity and temperature service!");
	}

	return 0;
}
