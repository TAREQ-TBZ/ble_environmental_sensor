/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "user_interface.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(user_interface, LOG_LEVEL_DBG);

#define LED_ON_TIME_MS 200

static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct gpio_callback user_button_cb_data;
static void (*button_callback)(enum button_evt evt) = NULL;

static enum button_evt btn_event = BUTTON_EVT_NONE;

static void button_increase_time(struct k_work *_work)
{
	struct k_work_delayable *work = k_work_delayable_from_work(_work);

	if (btn_event < BUTTON_EVT_PRESSED_10_SEC) /* Last press event */ {
		btn_event++;
		k_work_reschedule(work, K_SECONDS(1));
	}

	if (btn_event == BUTTON_EVT_PRESSED_10_SEC) {
		ui_set_status_led_on();
	}
}
K_WORK_DELAYABLE_DEFINE(button_increase_time_work, button_increase_time);

static void button_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	struct k_work_sync sync;

	if (button_callback) {
		int value = gpio_pin_get_dt(&user_button);
		if (value == 1) /* 1 = pressed, 0 = released */ {
			btn_event = BUTTON_EVT_PRESSED_1_SEC;
			k_work_reschedule(&button_increase_time_work, K_SECONDS(1));
			return;
		}
		/* Button released */
		k_work_cancel_delayable_sync(&button_increase_time_work, &sync);
		button_callback(btn_event);
		btn_event = BUTTON_EVT_NONE;
	} else {
		LOG_WRN("No registered user button callback!");
	}
}
static K_WORK_DELAYABLE_DEFINE(debouncing_work, button_handler);

static void button_pressed_callback(const struct device *dev, struct gpio_callback *cb,
				    uint32_t pins)
{
	/* Debounce the button */
	k_work_reschedule(&debouncing_work, K_MSEC(15));
}

void ui_register_button_callback(void (*callback)(enum button_evt evt))
{
	button_callback = callback;
}

int ui_toggle_status_led(void)
{
	return gpio_pin_toggle_dt(&status_led);
}

int ui_set_status_led_on(void)
{
	return gpio_pin_set_dt(&status_led, 1);
}

int ui_set_status_led_off(void)
{
	return gpio_pin_set_dt(&status_led, 0);
}

void led_timer_expiry_fn(struct k_timer *time)
{
	ui_set_status_led_off();
}
K_TIMER_DEFINE(led_timer, led_timer_expiry_fn, NULL);

int ui_flash_status_led(uint32_t on_time_ms)
{
	int ret = ui_set_status_led_on();
	if (ret == 0) {
		k_timer_start(&led_timer, K_MSEC(on_time_ms), K_NO_WAIT);
		return 0;
	}
	return ret;
}

int ui_gpio_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&status_led)) {
		LOG_ERR("Status LED device not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&user_button)) {
		LOG_ERR("User button device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE | GPIO_PULL_UP);
	if (ret < 0) {
		LOG_ERR("Failed to configure status LED: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&user_button, GPIO_INPUT | GPIO_PULL_UP);
	if (ret < 0) {
		LOG_ERR("Failed to configure user button: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&user_button, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR("Failed to configure button interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&user_button_cb_data, button_pressed_callback, BIT(user_button.pin));

	ret = gpio_add_callback(user_button.port, &user_button_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add button callback: %d", ret);
		return ret;
	}

	LOG_INF("GPIOs initialized successfully");

	return 0;
}
