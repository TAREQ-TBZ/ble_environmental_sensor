/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_BLE_SVC_H_
#define APP_BLE_SVC_H_

/**
 * @brief Updates BLE humidity value and notifies clients.
 *
 * @param hum_value Humidity in % (0.0 - 100.0).
 *
 * @return 0 on success, -EINVAL if out of range, or error code.
 */
int ble_svc_update_humidity_value(float hum_value);

/**
 * @brief Updates BLE temperature value and notifies clients.
 *
 * @param temp_value Temperature in °C (-20.0 to 125.0).
 *
 * @return 0 on success, -EINVAL if out of range, or error code.
 */
int ble_svc_update_temperature_value(float temp_value);

/**
 * @brief Temporaily function to demonstrare updateing the ble advertisement data manually at rum
 * time
 */
void ble_svc_increase_button_press_cnt(void);

/**
 * @brief Enables BLE and start advertising.
 *
 * @return 0 on success, or error code on failure.
 */
int ble_svc_enable_ble(void);

/**
 * @brief Initialize BLE service.
 */
void ble_svc_init(void);

#endif /* APP_BLE_SVC_H_ */
