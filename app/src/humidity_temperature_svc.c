/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "humidity_temperature_svc.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(humidity_temperature_svc, LOG_LEVEL_DBG);

static const struct device *const rh_temp_dev = DEVICE_DT_GET_ONE(sensirion_sht4x);

struct humidity_temperature_data {
	struct sensor_value humidity;
	struct sensor_value temperature;
};

static struct humidity_temperature_data data;

int humidity_temperature_svc_trigger_measurement(void)
{
	int ret;

	ret = sensor_sample_fetch(rh_temp_dev);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

float humidity_temperature_svc_get_temperature(void)
{
	int ret = sensor_channel_get(rh_temp_dev, SENSOR_CHAN_AMBIENT_TEMP, &data.temperature);
	if (ret) {
		LOG_ERR("Failed to get sensor channel: %d", ret);
		return ret;
	}

	LOG_DBG("Temperature: %3d.%06d [°C]", data.temperature.val1, data.temperature.val2);
	return sensor_value_to_float(&data.temperature);
}

float humidity_temperature_svc_get_humidity(void)
{
	int ret = sensor_channel_get(rh_temp_dev, SENSOR_CHAN_HUMIDITY, &data.humidity);
	if (ret) {
		LOG_ERR("Failed to get sensor channel: %d", ret);
		return ret;
	}

	LOG_DBG("Humidity: %3d.%06d [%%]", data.humidity.val1, data.humidity.val2);
	return sensor_value_to_float(&data.humidity);
}

int humidity_temperature_svc_init(void)
{
	if (!device_is_ready(rh_temp_dev)) {
		LOG_ERR("Failed to initialize humidity and temperature sensor!");
		return -ENODEV;
	}

	LOG_DBG("Humidity and temperature sensor initialized successfully");
	return 0;
}
