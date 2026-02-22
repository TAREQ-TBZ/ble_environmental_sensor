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

int humidity_temperature_svc_trigger_measurement(struct humidity_temperature_data *self)
{
	int ret;
	sys_snode_t *node;
	float temperature;
	float humidity;

	ret = sensor_sample_fetch(self->dev);
	if (ret != 0) {
		return ret;
	}

	ret = sensor_channel_get(self->dev, SENSOR_CHAN_AMBIENT_TEMP, &self->temperature);
	if (ret != 0) {
		LOG_ERR("Failed to get temperature channel: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(self->dev, SENSOR_CHAN_HUMIDITY, &self->humidity);
	if (ret != 0) {
		LOG_ERR("Failed to get humidity channel: %d", ret);
		return ret;
	}

	LOG_DBG("Temperature: %3d.%06d [°C]", self->temperature.val1, self->temperature.val2);
	LOG_DBG("Humidity: %3d.%06d [%%]", self->humidity.val1, self->humidity.val2);

	/* Callback Pattern: notify all measurement observers with new data */
	temperature = sensor_value_to_float(&self->temperature);
	humidity = sensor_value_to_float(&self->humidity);

	SYS_SLIST_FOR_EACH_NODE(&self->callbacks, node) {
		struct humidity_temperature_callback *cb =
			CONTAINER_OF(node, struct humidity_temperature_callback, node);
		if (cb->on_measurement) {
			cb->on_measurement(cb, temperature, humidity);
		}
	}

	return 0;
}

int humidity_temperature_svc_get_temperature(struct humidity_temperature_data *self, float *temp)
{
	if (temp == NULL) {
		return -EINVAL;
	}

	*temp = sensor_value_to_float(&self->temperature);
	return 0;
}

int humidity_temperature_svc_get_humidity(struct humidity_temperature_data *self, float *hum)
{
	if (hum == NULL) {
		return -EINVAL;
	}

	*hum = sensor_value_to_float(&self->humidity);
	return 0;
}

void humidity_temperature_svc_add_callback(struct humidity_temperature_data *self,
					   struct humidity_temperature_callback *cb)
{
	sys_slist_append(&self->callbacks, &cb->node);
}

void humidity_temperature_svc_remove_callback(struct humidity_temperature_data *self,
					      struct humidity_temperature_callback *cb)
{
	sys_slist_find_and_remove(&self->callbacks, &cb->node);
}

int humidity_temperature_svc_init(struct humidity_temperature_data *self, const struct device *dev)
{
	memset(self, 0, sizeof(*self));
	self->dev = dev;
	sys_slist_init(&self->callbacks);

	if (!device_is_ready(self->dev)) {
		LOG_ERR("Failed to initialize humidity and temperature sensor!");
		return -ENODEV;
	}

	LOG_DBG("Humidity and temperature sensor initialized successfully");
	return 0;
}
