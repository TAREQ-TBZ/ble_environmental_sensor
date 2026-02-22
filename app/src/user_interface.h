/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_USER_INTERFACE_H_
#define APP_USER_INTERFACE_H_

#include <stdint.h>
#include <zephyr/sys/slist.h>

enum button_evt {
	BUTTON_EVT_NONE,
	BUTTON_EVT_PRESSED_1_SEC,
	BUTTON_EVT_PRESSED_2_SEC,
	BUTTON_EVT_PRESSED_3_SEC,
	BUTTON_EVT_PRESSED_4_SEC,
	BUTTON_EVT_PRESSED_5_SEC,
	BUTTON_EVT_PRESSED_6_SEC,
	BUTTON_EVT_PRESSED_7_SEC,
	BUTTON_EVT_PRESSED_8_SEC,
	BUTTON_EVT_PRESSED_9_SEC,
	BUTTON_EVT_PRESSED_10_SEC,
};

/**
 * Callback Pattern: Callback struct for button events.
 *
 * The callback is enclosed in a struct (not a bare function pointer) so that:
 *  - It can be linked into a list for one-to-many notifications (sys_snode_t).
 *  - The handler receives a pointer to this struct, allowing the observer to
 *    recover its own context via CONTAINER_OF without global data or "userdata".
 */
struct ui_button_callback {
	sys_snode_t node;
	void (*handler)(struct ui_button_callback *cb, enum button_evt evt);
};

/**
 * @brief Register a callback for the user button (supports multiple observers).
 *
 * @param cb Pointer to the callback struct. Must remain valid for the
 *           lifetime of the registration. The caller embeds this struct
 *           in its own data and uses CONTAINER_OF in the handler.
 */
void ui_register_button_callback(struct ui_button_callback *cb);

/**
 * @brief Remove a previously registered button callback.
 *
 * @param cb Pointer to the callback struct to remove.
 */
void ui_remove_button_callback(struct ui_button_callback *cb);

/**
 * @brief Toggle the status LED.
 *
 * @return 0 on success, negative error code on failure.
 */
int ui_toggle_status_led(void);

/**
 * @brief Turn on the status LED.
 *
 * @return 0 on success, negative error code on failure.
 */
int ui_set_status_led_on(void);

/**
 * @brief Turn off the status LED.
 *
 * @return 0 on success, negative error code on failure.
 */
int ui_set_status_led_off(void);

/**
 * @brief flash the status LED.
 *
 * @param on_time_ms On time in ms
 *
 * @return 0 on success, negative error code on failure.
 */
int ui_flash_status_led(uint32_t on_time_ms);

/**
 * @brief Initialize GPIOs for user button and status LED.
 *
 * @return 0 on success, negative error code on failure.
 */
int ui_gpio_init(void);

#endif /* APP_USER_INTERFACE_H_ */
