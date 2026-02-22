/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_ENVIRONMENTAL_SENSORS_H_
#define APP_ENVIRONMENTAL_SENSORS_H_

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/slist.h>

/* Measurements ranges for SHT40 sensor */
#define SENSOR_TEMP_CELSIUS_MIN       -40
#define SENSOR_TEMP_CELSIUS_MAX       125
#define SENSOR_TEMP_CELSIUS_TOLERANCE 0.2
#define SENSOR_HUMIDITY_PERCENT_MIN   0
#define SENSOR_HUMIDITY_PERCENT_MAX   100

/**
 * Callback Pattern: Measurement-ready callback struct.
 *
 * Observers embed this struct in their own data and use CONTAINER_OF
 * in the handler to recover their private context. The handler receives
 * the measured values directly as parameters (data via callback arguments).
 */
struct humidity_temperature_callback {
	sys_snode_t node;
	void (*on_measurement)(struct humidity_temperature_callback *cb,
			       float temperature, float humidity);
};

struct humidity_temperature_data {
	const struct device *dev;
	struct sensor_value humidity;
	struct sensor_value temperature;
	/** Callback Pattern: list of measurement observers (one-to-many) */
	sys_slist_t callbacks;
};

/**
 * @brief Initialize the humidity and temperature sensor.
 *
 * @param[in] self Pointer to the sensor data object.
 * @param[in] dev  Pointer to the sensor device.
 *
 * @return 0 on success, or -ENODEV on failure
 */
int humidity_temperature_svc_init(struct humidity_temperature_data *self, const struct device *dev);

/**
 * @brief Triggers a new measurement for humidity and temperature.
 *
 * This function fetches new sensor data and caches the values internally.
 * Call get_temperature() and get_humidity() after this returns successfully.
 *
 * @param[in] self Pointer to the sensor data object.
 *
 * @return 0 on success, or a negative error code if the measurement fails.
 */
int humidity_temperature_svc_trigger_measurement(struct humidity_temperature_data *self);

/**
 * @brief Get the last measured humidity value.
 *
 * @note Call trigger_measurement() first to get fresh data.
 *
 * @param[in]  self Pointer to the sensor data object.
 * @param[out] hum  Pointer to store the humidity value in percent (0.0 - 100.0).
 *
 * @return 0 on success, or a negative error code on failure.
 */
int humidity_temperature_svc_get_humidity(struct humidity_temperature_data *self, float *hum);

/**
 * @brief Get the last measured temperature value.
 *
 * @note Call trigger_measurement() first to get fresh data.
 *
 * @param[in]  self Pointer to the sensor data object.
 * @param[out] temp Pointer to store the temperature value in degrees Celsius.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int humidity_temperature_svc_get_temperature(struct humidity_temperature_data *self, float *temp);

/**
 * @brief Register a measurement callback (supports multiple observers).
 *
 * @param[in] self Pointer to the sensor data object.
 * @param[in] cb   Pointer to the callback struct. Must remain valid for the
 *                 lifetime of the registration.
 */
void humidity_temperature_svc_add_callback(struct humidity_temperature_data *self,
					   struct humidity_temperature_callback *cb);

/**
 * @brief Remove a previously registered measurement callback.
 *
 * @param[in] self Pointer to the sensor data object.
 * @param[in] cb   Pointer to the callback struct to remove.
 */
void humidity_temperature_svc_remove_callback(struct humidity_temperature_data *self,
					      struct humidity_temperature_callback *cb);

#endif /* APP_ENVIRONMENTAL_SENSORS_H_ */
