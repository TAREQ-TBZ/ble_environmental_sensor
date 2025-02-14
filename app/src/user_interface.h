/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_USER_INTERFACE_H_
#define APP_USER_INTERFACE_H_

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
 * @brief Register a callback function for the user button.
 *
 * @param callback Function pointer to the user callback function.
 */
void ui_register_button_callback(void (*callback)(enum button_evt evt));

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
