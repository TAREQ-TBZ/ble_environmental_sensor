/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_BLE_SVC_H_
#define APP_BLE_SVC_H_

#include <zephyr/sys/slist.h>

/**
 * Callback Pattern: Connection event callback struct with multiple operations.
 *
 * This demonstrates a callback struct with multiple function pointers (ops),
 * allowing observers to handle different events from the same subject.
 * The sys_snode_t enables one-to-many notifications via a linked list.
 * Observers use CONTAINER_OF on the callback pointer to recover their context.
 *
 * IMPORTANT: These callbacks fire in the Bluetooth thread context.
 * Observers must not perform time-consuming operations directly in
 * the handler. Use k_work to defer heavy processing to a work queue.
 */
struct ble_svc_conn_callback {
	sys_snode_t node;
	void (*connected)(struct ble_svc_conn_callback *cb);
	void (*disconnected)(struct ble_svc_conn_callback *cb);
};

/**
 * @brief Register a connection event callback (supports multiple observers).
 *
 * @param cb Pointer to the callback struct. Must remain valid for the
 *           lifetime of the registration.
 */
void ble_svc_add_conn_callback(struct ble_svc_conn_callback *cb);

/**
 * @brief Remove a previously registered connection event callback.
 *
 * @param cb Pointer to the callback struct to remove.
 */
void ble_svc_remove_conn_callback(struct ble_svc_conn_callback *cb);

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
 *
 * @return 0 on success, or error code on failure.
 */
int ble_svc_increase_button_press_cnt(void);

/**
 * @brief Enables BLE and start advertising.
 *
 * @return 0 on success, or error code on failure.
 */
int ble_svc_enable_ble(void);

/**
 * @brief Initialize BLE service.
 *
 * @return 0 on success, or error code on failure.
 */
int ble_svc_init(void);

#endif /* APP_BLE_SVC_H_ */
