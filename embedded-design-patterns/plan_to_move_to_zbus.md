 Plan: Refactor from Callback Pattern to Zephyr Zbus

 Context

 The firmware currently uses the Callback Pattern (sys_slist + CONTAINER_OF) for inter-module
 communication. The project is about to grow significantly (additional sensors, nRF9160 for
 LTE/MQTT). At that scale, zbus's publish-subscribe model is a better fit: modules communicate
 through named channels with no direct dependencies, observers are registered via macros, and
 adding new producers/consumers requires zero changes to existing modules.

 This refactoring replaces all hand-rolled callback lists with zbus channels and listeners
 (synchronous callbacks). The execution model stays the same: publishers fire listener callbacks
 in their own thread context. BLE events still defer via k_work.

 Follows the nRF MQTT sample pattern at nrf/samples/net/mqtt/src/common/message_channel.{h,c}.

 Channels

 ┌───────────────┬──────────────────────────────────────────────────────────┬─────────────────────────────────────────┬───────────────────────────────────────────┐
 │    Channel    │                       Message Type                       │                Publisher                │                 Listeners                 │
 ├───────────────┼──────────────────────────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────┤
 │ BLE_CONN_CHAN │ struct ble_conn_msg { bool connected; }                  │ ble_svc.c (BT thread)                   │ main_ble_listener                         │
 ├───────────────┼──────────────────────────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────┤
 │ SENSOR_CHAN   │ struct sensor_msg { float temperature; float humidity; } │ humidity_temperature_svc.c (work queue) │ main_sensor_listener                      │
 ├───────────────┼──────────────────────────────────────────────────────────┼─────────────────────────────────────────┼───────────────────────────────────────────┤
 │ BUTTON_CHAN   │ struct button_msg { enum button_evt evt; }               │ user_interface.c (work queue)           │ main_button_listener, led_button_listener │
 └───────────────┴──────────────────────────────────────────────────────────┴─────────────────────────────────────────┴───────────────────────────────────────────┘

 Critical Technical Detail

 Listeners run synchronously while the channel mutex is held by zbus_chan_pub().
 Therefore listeners must use zbus_chan_const_msg(chan) (zero-copy pointer) -- NOT
 zbus_chan_read() which would deadlock trying to re-acquire the mutex.
 Confirmed in nRF MQTT sample: nrf/samples/net/mqtt/src/modules/led/led.c:35.

 Files to Change

 ┌─────┬────────────────────────────────────┬─────────┬────────────────────────────────────────────────┐
 │  #  │                File                │ Action  │                    Summary                     │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 1   │ app/prj.conf                       │ Edit    │ Add CONFIG_ZBUS=y                              │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 2   │ app/src/message_channel.h          │ Create  │ Message structs + ZBUS_CHAN_DECLARE()          │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 3   │ app/src/message_channel.c          │ Create  │ All ZBUS_CHAN_DEFINE() with observer lists     │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 4   │ app/CMakeLists.txt                 │ Edit    │ Add src/message_channel.c                      │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 5   │ app/src/ble_svc.h                  │ Edit    │ Remove callback struct + registration API      │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 6   │ app/src/ble_svc.c                  │ Edit    │ Replace slist iteration with zbus_chan_pub()   │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 7   │ app/src/humidity_temperature_svc.h │ Edit    │ Remove callback struct + registration API      │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 8   │ app/src/humidity_temperature_svc.c │ Edit    │ Replace slist iteration with zbus_chan_pub()   │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 9   │ app/src/user_interface.h           │ Edit    │ Remove callback struct + registration API      │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 10  │ app/src/user_interface.c           │ Edit    │ Replace slist iteration with zbus_chan_pub()   │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 11  │ app/src/main.c                     │ Rewrite │ Replace all callback infra with zbus listeners │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 12  │ app/src/events_svc.c               │ Delete  │ Dead code (not in CMakeLists.txt)              │
 ├─────┼────────────────────────────────────┼─────────┼────────────────────────────────────────────────┤
 │ 13  │ app/src/events_svc.h               │ Delete  │ Dead code                                      │
 └─────┴────────────────────────────────────┴─────────┴────────────────────────────────────────────────┘

 Step-by-Step Details

 Step 1: prj.conf -- Enable zbus

 Append to end of file:
 #
 # Zbus publish-subscribe event bus
 #
 CONFIG_ZBUS=y

 Defaults are correct: PRIORITY_BOOST=y, no channel/observer names, no runtime observers,
 no msg_subscriber. Minimal overhead.

 Step 2: Create message_channel.h

 #ifndef APP_MESSAGE_CHANNEL_H_
 #define APP_MESSAGE_CHANNEL_H_

 #include <stdbool.h>
 #include <zephyr/zbus/zbus.h>
 #include "user_interface.h"   /* for enum button_evt */

 struct ble_conn_msg {
     bool connected;
 };

 struct sensor_msg {
     float temperature;
     float humidity;
 };

 struct button_msg {
     enum button_evt evt;
 };

 ZBUS_CHAN_DECLARE(BLE_CONN_CHAN, SENSOR_CHAN, BUTTON_CHAN);

 #endif

 Note: includes user_interface.h because C cannot forward-declare enums. This is fine --
 user_interface.h will NOT include message_channel.h (no circular dependency).

 Step 3: Create message_channel.c

 #include <zephyr/zbus/zbus.h>
 #include "message_channel.h"

 ZBUS_CHAN_DEFINE(BLE_CONN_CHAN, struct ble_conn_msg, NULL, NULL,
     ZBUS_OBSERVERS(main_ble_listener),
     ZBUS_MSG_INIT(.connected = false));

 ZBUS_CHAN_DEFINE(SENSOR_CHAN, struct sensor_msg, NULL, NULL,
     ZBUS_OBSERVERS(main_sensor_listener),
     ZBUS_MSG_INIT(0));

 ZBUS_CHAN_DEFINE(BUTTON_CHAN, struct button_msg, NULL, NULL,
     ZBUS_OBSERVERS(main_button_listener, led_button_listener),
     ZBUS_MSG_INIT(.evt = BUTTON_EVT_NONE));

 Observer names (e.g. main_ble_listener) are resolved at link time via iterable sections --
 they're defined by ZBUS_LISTENER_DEFINE() in main.c.

 Step 4: CMakeLists.txt -- Add new source

 target_sources(app PRIVATE
     src/ble_svc.c
     src/main.c
     src/message_channel.c
     src/humidity_temperature_svc.c
     src/user_interface.c
 )

 Step 5-6: ble_svc.h / ble_svc.c

 ble_svc.h -- Remove:
 - #include <zephyr/sys/slist.h>
 - struct ble_svc_conn_callback (entire struct + doc comment)
 - ble_svc_add_conn_callback() / ble_svc_remove_conn_callback() declarations

 ble_svc.c -- Changes:
 - Add #include "message_channel.h"
 - Remove sys_slist_t conn_callbacks from struct ble_svc_data
 - Remove sys_slist_init(&data.conn_callbacks) from ble_svc_init()
 - Remove ble_svc_add_conn_callback() / ble_svc_remove_conn_callback() functions
 - on_connected(): Replace SYS_SLIST_FOR_EACH_NODE block + sys_snode_t *node decl with:
 struct ble_conn_msg msg = {.connected = true};
 zbus_chan_pub(&BLE_CONN_CHAN, &msg, K_NO_WAIT);
 - on_disconnected(): Replace SYS_SLIST_FOR_EACH_NODE block + sys_snode_t *node decl with:
 struct ble_conn_msg msg = {.connected = false};
 zbus_chan_pub(&BLE_CONN_CHAN, &msg, K_NO_WAIT);

 K_NO_WAIT because BT thread must never block.

 Step 7-8: humidity_temperature_svc.h / .c

 Header -- Remove:
 - #include <zephyr/sys/slist.h>
 - struct humidity_temperature_callback (entire struct + doc comment)
 - sys_slist_t callbacks from struct humidity_temperature_data
 - humidity_temperature_svc_add_callback() / _remove_callback() declarations

 Source -- Changes:
 - Add #include "message_channel.h"
 - trigger_measurement(): Replace SYS_SLIST_FOR_EACH_NODE block + sys_snode_t *node with:
 struct sensor_msg msg = {.temperature = temperature, .humidity = humidity};
 zbus_chan_pub(&SENSOR_CHAN, &msg, K_MSEC(100));
 - Remove sys_slist_init(&self->callbacks) from init
 - Remove add_callback() / remove_callback() functions

 Step 9-10: user_interface.h / .c

 Header -- Remove:
 - #include <zephyr/sys/slist.h>
 - struct ui_button_callback (entire struct + doc comment)
 - ui_register_button_callback() / ui_remove_button_callback() declarations

 Keep: enum button_evt, LED functions, ui_gpio_init()

 Source -- Changes:
 - Add #include "message_channel.h"
 - Remove sys_slist_t button_callbacks from struct user_interface_data
 - button_handler(): Replace SYS_SLIST_FOR_EACH_NODE block + sys_snode_t *node with:
 struct button_msg msg = {.evt = data.btn_event};
 zbus_chan_pub(&BUTTON_CHAN, &msg, K_MSEC(100));
 - Remove ui_register_button_callback() / ui_remove_button_callback() functions

 Step 11: Rewrite main.c

 Remove entirely:
 - Big callback pattern doc comment (lines 7-67)
 - struct main_data with embedded callback structs + k_work items
 - static struct main_data data
 - struct led_observer / static struct led_observer led_obs
 - All CONTAINER_OF handlers: btn_callback(), led_on_button_cb(), on_ble_connected(),
 on_ble_disconnected(), on_measurement_ready()
 - All callback registration code in main()
 - k_work_init() calls (replaced by static K_WORK_DEFINE)

 Replace with:
 - Module-level state: static bool ble_is_connected, static bool measuring_started
 - static K_WORK_DEFINE(ble_connected_work, ble_connected_work_handler)
 - static K_WORK_DEFINE(ble_disconnected_work, ble_disconnected_work_handler)
 - 4 zbus listeners:

 /* BLE connection -- runs in BT thread, defers via k_work */
 static void on_ble_conn(const struct zbus_channel *chan)
 {
     const struct ble_conn_msg *msg = zbus_chan_const_msg(chan);
     if (msg->connected) {
         k_work_submit(&ble_connected_work);
     } else {
         k_work_submit(&ble_disconnected_work);
     }
 }
 ZBUS_LISTENER_DEFINE(main_ble_listener, on_ble_conn);

 /* Sensor data -- runs in work queue context */
 static void on_sensor_data(const struct zbus_channel *chan)
 {
     const struct sensor_msg *msg = zbus_chan_const_msg(chan);
     if (!ble_is_connected) return;
     ble_svc_update_temperature_value(msg->temperature);
     ble_svc_update_humidity_value(msg->humidity);
 }
 ZBUS_LISTENER_DEFINE(main_sensor_listener, on_sensor_data);

 /* Button press -- application logic */
 static void on_button(const struct zbus_channel *chan)
 {
     const struct button_msg *msg = zbus_chan_const_msg(chan);
     // switch on msg->evt (same logic as before)
 }
 ZBUS_LISTENER_DEFINE(main_button_listener, on_button);

 /* Button press -- LED feedback */
 static void on_led_button(const struct zbus_channel *chan)
 {
     ARG_UNUSED(chan);
     ui_flash_status_led(STATUS_LED_ON_TIME_FOR_STARTUP_MSEC);
 }
 ZBUS_LISTENER_DEFINE(led_button_listener, on_led_button);

 - Simplified main(): init sensor, init GPIO, init BLE, enable BLE, flash LED, sleep.
 No callback registration steps needed.

 Step 12: Delete dead code

 - app/src/events_svc.c -- old k_msgq event bus, not compiled
 - app/src/events_svc.h -- not referenced by any compiled file

 Verification

 # Clean build required (CONFIG_ZBUS changes kernel config)
 west build -b sham_nrf52833 application/app --pristine

 # Debug build
 west build -b sham_nrf52833 application/app -DEXTRA_CONF_FILE=debug.conf --pristine

 # Sysbuild (with MCUboot)
 west build --sysbuild -b sham_nrf52833 application/app --pristine

 # Code formatting
 clang-format -i ws/application/app/src/*.c ws/application/app/src/*.h
 clang-format --dry-run --Werror ws/application/app/src/*.c ws/application/app/src/*.h

