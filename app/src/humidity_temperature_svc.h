/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_ENVIRONMENTAL_SENSORS_H_
#define APP_ENVIRONMENTAL_SENSORS_H_

#include <zephyr/drivers/sensor.h>

/* Measurements ranges for SHT40 sensor */
#define SENSOR_TEMP_CELSIUS_MIN       -40
#define SENSOR_TEMP_CELSIUS_MAX       125
#define SENSOR_TEMP_CELSIUS_TOLERANCE 0.2
#define SENSOR_HUMIDITY_PERCENT_MIN   0
#define SENSOR_HUMIDITY_PERCENT_MAX   100

struct humidity_temperature_data {
	const struct device *dev;
	struct sensor_value humidity;
	struct sensor_value temperature;
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

#endif /* APP_ENVIRONMENTAL_SENSORS_H_ */
