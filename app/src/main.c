/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ============================================================================
 * Callback Pattern Demonstration
 * ============================================================================
 *
 * This file demonstrates ALL possibilities of the Callback Pattern as
 * described by Martin Schroder's "Design Patterns - Callback Pattern":
 *
 * 1. CALLBACK AS STRUCT (not a bare function pointer)
 *    Every callback is a struct containing a function pointer that receives
 *    a pointer to itself. This enables CONTAINER_OF and list linkage.
 *    -> See: struct ui_button_callback, struct ble_svc_conn_callback,
 *            struct humidity_temperature_callback
 *
 * 2. CONTAINER_OF TO RECOVER OBSERVER CONTEXT
 *    Each observer embeds the callback struct in its own data. The handler
 *    uses CONTAINER_OF(cb, struct my_observer, cb_field) to recover the
 *    observer's private state. No global data, no "void *user_data".
 *    -> See: btn_callback(), led_on_button_cb(), on_ble_connected(),
 *            on_ble_disconnected(), on_measurement_ready()
 *
 * 3. SINGLE CALLBACK (Alternative 1 from the PDF)
 *    A subject stores a pointer to one callback struct.
 *    -> See: The original user_interface design (now extended to slist)
 *
 * 4. MULTIPLE CALLBACKS VIA sys_slist_t (Alternative 2 - one-to-many)
 *    A subject maintains a linked list of callback structs, notifying all
 *    observers in sequence. Each callback struct has a sys_snode_t node.
 *    -> See: user_interface button_callbacks list (notifies both main_data
 *            and led_observer), ble_svc conn_callbacks list,
 *            humidity_temperature_svc callbacks list
 *
 * 5. ADD / REMOVE LIFECYCLE MANAGEMENT
 *    Observers register with *_add_callback() and unregister with
 *    *_remove_callback(). The callback struct must never go out of scope
 *    while registered.
 *    -> See: ui_register_button_callback / ui_remove_button_callback,
 *            ble_svc_add_conn_callback / ble_svc_remove_conn_callback,
 *            humidity_temperature_svc_add_callback / _remove_callback
 *
 * 6. MULTIPLE OPERATIONS IN ONE CALLBACK STRUCT
 *    A single callback struct can hold multiple function pointers for
 *    different events from the same subject (like an "ops" struct).
 *    -> See: struct ble_svc_conn_callback has both .connected and
 *            .disconnected handlers
 *
 * 7. WORK QUEUE + CALLBACK (context-safe deferred processing)
 *    BLE callbacks fire in the Bluetooth thread context. The handler must
 *    not do heavy work there. Instead, it submits a k_work item to the
 *    system work queue where the actual processing happens safely.
 *    -> See: on_ble_connected() submits ble_connected_work,
 *            on_ble_disconnected() submits ble_disconnected_work
 *
 * 8. FULL DECOUPLING (subject knows nothing about observers)
 *    - ble_svc does not #include or depend on main.c types
 *    - humidity_temperature_svc does not know about ble_svc
 *    - user_interface does not know about main_data or led_observer
 *    The only shared contract is the callback struct definition.
 *
 * ============================================================================
 */

#include <app_version.h>
#include "ble_svc.h"
#include "humidity_temperature_svc.h"
#include "user_interface.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define MEASUREMENT_PERIOD_MSEC             (1000 * CONFIG_MEASURING_PERIOD_SECONDS)
#define FIRST_MEASUREMENT_DELAY_MSEC        (1000 * CONFIG_FIRST_MEASUREMENT_DELAY_SECONDS)
#define STATUS_LED_ON_TIME_FOR_STARTUP_MSEC 250

/* ============================================================================
 * Pattern 7: Work queue handler for periodic measurements
 * ============================================================================
 * This work item is scheduled/cancelled from deferred BLE connection handlers
 * (themselves triggered by callbacks). It triggers the sensor which then
 * notifies its own measurement callbacks.
 */
static struct humidity_temperature_data ht_sensor;

static void measuring_work_handler(struct k_work *_work)
{
	int ret;
	struct k_work_delayable *work = k_work_delayable_from_work(_work);

	ret = humidity_temperature_svc_trigger_measurement(&ht_sensor);
	if (ret != 0) {
		LOG_ERR("Failed to trigger measurement: %d", ret);
	}

	k_work_reschedule(work, K_MSEC(MEASUREMENT_PERIOD_MSEC));
}
K_WORK_DELAYABLE_DEFINE(measuring_work, measuring_work_handler);

/* ============================================================================
 * Pattern 2 + 4: Observer #1 for button events (main application logic)
 * ============================================================================
 * Demonstrates CONTAINER_OF to recover main_data context from the embedded
 * callback struct. This is one of multiple observers for the same subject.
 */
struct main_data {
	bool ble_is_connected;
	bool measuring_started;

	/* Callback Pattern: button callback embedded in observer struct */
	struct ui_button_callback btn_cb;

	/* Callback Pattern: BLE connection callback with multiple ops */
	struct ble_svc_conn_callback ble_conn_cb;

	/* Callback Pattern: sensor measurement callback */
	struct humidity_temperature_callback measurement_cb;

	/* Callback Pattern: work items for deferring from BT thread context */
	struct k_work ble_connected_work;
	struct k_work ble_disconnected_work;
};

static struct main_data data;

static void btn_callback(struct ui_button_callback *cb, enum button_evt evt)
{
	/* Pattern 2: CONTAINER_OF recovers observer context from callback pointer.
	 * No global data access, no "void *user_data" - just the struct layout. */
	struct main_data *self = CONTAINER_OF(cb, struct main_data, btn_cb);
	int ret;

	ARG_UNUSED(self);

	switch (evt) {
	case BUTTON_EVT_PRESSED_1_SEC:
		ret = ble_svc_increase_button_press_cnt();
		if (ret != 0) {
			LOG_WRN("Failed to update button press count: %d", ret);
		}
		break;

	case BUTTON_EVT_PRESSED_10_SEC:
		/* TODO: Trigger factory Reset */
		break;

	default:
		break;
	}
}

/* ============================================================================
 * Pattern 4: Observer #2 for button events (LED feedback)
 * ============================================================================
 * Demonstrates one-to-many: a SECOND independent observer for the same button
 * subject. Both led_observer and main_data receive button notifications, but
 * neither knows about the other. The button subject knows about neither.
 */
struct led_observer {
	struct ui_button_callback btn_cb;
};

static struct led_observer led_obs;

static void led_on_button_cb(struct ui_button_callback *cb, enum button_evt evt)
{
	/* Pattern 2: recover led_observer context via CONTAINER_OF */
	struct led_observer *self = CONTAINER_OF(cb, struct led_observer, btn_cb);

	ARG_UNUSED(self);

	/* Flash LED on any button press to give visual feedback */
	LOG_INF("LED observer: button event %d, flashing LED", evt);
	ui_flash_status_led(STATUS_LED_ON_TIME_FOR_STARTUP_MSEC);
}

/* ============================================================================
 * Pattern 6 + 7: BLE connection callback (multiple ops + work queue deferral)
 * ============================================================================
 * Demonstrates:
 * - Multiple ops: struct ble_svc_conn_callback has .connected AND .disconnected
 * - Work queue deferral: callbacks fire in BT thread, so we submit k_work items
 *   to defer the actual processing to the system work queue context.
 */
static void ble_connected_work_handler(struct k_work *work)
{
	struct main_data *self = CONTAINER_OF(work, struct main_data, ble_connected_work);

	LOG_INF("BLE connected (deferred to work queue context)");
	self->ble_is_connected = true;
	k_work_reschedule(&measuring_work, K_MSEC(FIRST_MEASUREMENT_DELAY_MSEC));
	self->measuring_started = true;
}

static void ble_disconnected_work_handler(struct k_work *work)
{
	struct main_data *self = CONTAINER_OF(work, struct main_data, ble_disconnected_work);
	struct k_work_sync sync;

	LOG_INF("BLE disconnected (deferred to work queue context)");
	self->ble_is_connected = false;
	if (self->measuring_started) {
		k_work_cancel_delayable_sync(&measuring_work, &sync);
		self->measuring_started = false;
	}
}

static void on_ble_connected(struct ble_svc_conn_callback *cb)
{
	/* Pattern 2: CONTAINER_OF recovers main_data from BLE callback struct */
	struct main_data *self = CONTAINER_OF(cb, struct main_data, ble_conn_cb);

	/* Pattern 7: Defer work from BT thread to system work queue.
	 * The PDF warns: "avoid time consuming operations in callback".
	 * BLE callbacks run in the BT thread - we must not block it. */
	k_work_submit(&self->ble_connected_work);
}

static void on_ble_disconnected(struct ble_svc_conn_callback *cb)
{
	/* Pattern 2: CONTAINER_OF from the same callback struct, different op */
	struct main_data *self = CONTAINER_OF(cb, struct main_data, ble_conn_cb);

	/* Pattern 7: defer to work queue */
	k_work_submit(&self->ble_disconnected_work);
}

/* ============================================================================
 * Pattern 5 + 8: Sensor measurement callback (data via parameters + decoupling)
 * ============================================================================
 * Demonstrates:
 * - The sensor service notifies observers with measured data as parameters
 * - Full decoupling: humidity_temperature_svc does not know about ble_svc,
 *   yet the measurement data flows to BLE through this callback chain.
 * - The callback runs in work queue context (measuring_work_handler calls
 *   trigger_measurement which fires this callback).
 */
static void on_measurement_ready(struct humidity_temperature_callback *cb,
				 float temperature, float humidity)
{
	/* Pattern 2: CONTAINER_OF to get observer context */
	struct main_data *self = CONTAINER_OF(cb, struct main_data, measurement_cb);
	int ret;

	if (!self->ble_is_connected) {
		return;
	}

	/* Pattern 8: Full decoupling - sensor doesn't know about BLE.
	 * The observer bridges the two services without either depending on the other. */
	ret = ble_svc_update_temperature_value(temperature);
	if (ret != 0) {
		LOG_WRN("Failed to update temperature over BLE: %d", ret);
	}

	ret = ble_svc_update_humidity_value(humidity);
	if (ret != 0) {
		LOG_WRN("Failed to update humidity over BLE: %d", ret);
	}
}

/* ============================================================================
 * Application entry point
 * ============================================================================
 * All event handling is callback-driven. After initialization, the main
 * thread sleeps forever - all work happens in callbacks and work queue items.
 */
int main(void)
{
	int ret;

	LOG_INF("Starting up .. .. ..");
	LOG_INF("Application Version: %s", APP_VERSION_STRING);

	/* Initialize sensor service */
	ret = humidity_temperature_svc_init(&ht_sensor,
					   DEVICE_DT_GET_ONE(sensirion_sht4x));
	if (ret != 0) {
		LOG_ERR("Failed to initialize humidity and temperature service!");
		return ret;
	}

	/* Pattern 5: Register measurement callback with sensor service */
	data.measurement_cb.on_measurement = on_measurement_ready;
	humidity_temperature_svc_add_callback(&ht_sensor, &data.measurement_cb);

	/* Initialize user interface (GPIO) */
	ret = ui_gpio_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize user interface service!");
		return ret;
	}

	/* Pattern 1+2: Register button callback #1 (main logic - CONTAINER_OF) */
	data.btn_cb.handler = btn_callback;
	ui_register_button_callback(&data.btn_cb);

	/* Pattern 4: Register button callback #2 (LED feedback - one-to-many)
	 * Both observers are notified for every button event. Neither knows
	 * about the other. The button subject knows about neither. */
	led_obs.btn_cb.handler = led_on_button_cb;
	ui_register_button_callback(&led_obs.btn_cb);

	/* Initialize BLE service */
	ret = ble_svc_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize BLE service: %d", ret);
		return ret;
	}

	/* Pattern 6: Register BLE connection callback (multiple ops in one struct).
	 * The same struct has both .connected and .disconnected handlers. */
	data.ble_conn_cb.connected = on_ble_connected;
	data.ble_conn_cb.disconnected = on_ble_disconnected;
	ble_svc_add_conn_callback(&data.ble_conn_cb);

	/* Pattern 7: Initialize work items for deferred BLE event processing.
	 * Callbacks from BT thread submit these to the system work queue. */
	k_work_init(&data.ble_connected_work, ble_connected_work_handler);
	k_work_init(&data.ble_disconnected_work, ble_disconnected_work_handler);

	/* Enable BLE and start advertising */
	ret = ble_svc_enable_ble();
	if (ret != 0) {
		LOG_ERR("Failed to enable BLE: %d", ret);
		return ret;
	}

	ret = ui_flash_status_led(STATUS_LED_ON_TIME_FOR_STARTUP_MSEC);
	if (ret != 0) {
		LOG_WRN("Failed to flash status LED: %d", ret);
		return ret;
	}

	/* All event handling is now callback-driven.
	 * The main thread has nothing more to do - sleep forever.
	 * Work happens in: BT thread callbacks -> k_work -> work queue handlers. */
	k_sleep(K_FOREVER);

	return 0;
}
