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

/**
 * @brief Triggers a new measurement for humidity and temperature.
 *
 * @return 0 on success, or a negative error code if the measurement fails.
 */
int humidity_temperature_svc_trigger_measurement(void);

/**
 * @brief Get humidity value.
 *
 * @return Humidity value, or an error code if an error occurs.
 */
float humidity_temperature_svc_get_humidity(void);

/**
 * @brief Get temperature value.
 *
 * @return Temperature value, or an error code if an error occurs.
 */
float humidity_temperature_svc_get_temperature(void);

/**
 * @brief Initialize the humidity and temperature sensor.
 *
 * @return 0 on success, or -ENODEV on failure
 */
int humidity_temperature_svc_init(void);

#endif /* APP_ENVIRONMENTAL_SENSORS_H_ */
