/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "ble_svc.h"
#include "events_svc.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_svc, LOG_LEVEL_INF);

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

struct ble_svc_data {
	struct bt_conn *ble_connection;
	int16_t temperature;
	uint16_t humidity;
	struct k_mutex data_lock;
};

static struct ble_svc_data data;

static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_CONNECTABLE, 1600, /* Min Advertising Interval 1000ms (1600*0.625ms) */
	1602,                            /* Max Advertising Interval 1000.25ms (1602*0.625ms) */
	NULL);                           /* Set to NULL for undirected advertising */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static void ble_connected(struct bt_conn *conn, uint8_t ret)
{
	struct event evt;
	struct bt_conn_info info;
	char addr[BT_ADDR_LE_STR_LEN];

	k_mutex_lock(&data.data_lock, K_FOREVER);
	data.ble_connection = conn;
	k_mutex_unlock(&data.data_lock);

	if (ret != 0) {
		LOG_WRN("Connection failed (ret %u)", ret);
		return;
	} else if (bt_conn_get_info(conn, &info)) {
		LOG_WRN("Could not parse connection info");
	} else {
		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
		double connection_interval = info.le.interval * 1.25; /* in ms */
		uint16_t supervision_timeout = info.le.timeout * 10;  /* in ms */
		LOG_INF("Connection established! Connected to : %s ", addr);
		LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, "
			"timeout %d ms",
			connection_interval, info.le.latency, supervision_timeout);

		evt.type = EVENT_BLE_CONNECTED;
		events_svc_send_event(&evt);
	}
}

static void ble_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct event evt;
	LOG_INF("Disconnected (reason %u)", reason);

	k_mutex_lock(&data.data_lock, K_FOREVER);
	data.ble_connection = NULL;
	k_mutex_unlock(&data.data_lock);

	evt.type = EVENT_BLE_NOT_CONNECTED;
	events_svc_send_event(&evt);
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	/* TODO If acceptable params, return true, otherwise return false. */
	LOG_INF("BLE param was requested");
	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	double connection_interval = interval * 1.25; /* in ms */
	uint16_t supervision_timeout = timeout * 10;  /* in ms */
	LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d "
		"ms",
		connection_interval, latency, supervision_timeout);
}

static struct bt_conn_cb conn_callbacks = {.connected = ble_connected,
					   .disconnected = ble_disconnected,
					   .le_param_req = le_param_req,
					   .le_param_updated = le_param_updated};

static void temperature_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	LOG_INF("Temperature Notifications %s", notif_enabled ? "enabled" : "disabled");
}

static void humidity_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	LOG_INF("Humidity Notifications %s", notif_enabled ? "enabled" : "disabled");
}

static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				uint16_t len, uint16_t offset)
{
	k_mutex_lock(&data.data_lock, K_FOREVER);
	int16_t temperature = data.temperature;
	k_mutex_unlock(&data.data_lock);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &temperature, sizeof(temperature));
}

static ssize_t read_humidity(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	k_mutex_lock(&data.data_lock, K_FOREVER);
	uint16_t humidity = data.humidity;
	k_mutex_unlock(&data.data_lock);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &humidity, sizeof(humidity));
}

/* Constant values from the Assigned Numbers specification:
 * https://www.bluetooth.com/wp-content/uploads/Files/Specification/Assigned_Numbers.pdf?id=89
 */
static const struct bt_gatt_cpf temperature_cpf = {
	.format = 0x0E, /* signed 16-bit integer */
	.exponent = 0x0,
	.unit = 0x272F,        /* degree Celsius */
	.name_space = 0x01,    /* Bluetooth SIG */
	.description = 0x0106, /* "main" */
};

static const struct bt_gatt_cpf humidity_cpf = {
	.format = 0x06, /* unsigned 16-bit integer */
	.exponent = 0x0,
	.unit = 0x27AD,        /* Percentage */
	.name_space = 0x01,    /* Bluetooth SIG */
	.description = 0x0106, /* "main" */
};

BT_GATT_SERVICE_DEFINE(environmental_sensing_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
		       BT_GATT_CHARACTERISTIC(BT_UUID_HTS_TEMP_INT,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_READ, read_temperature, NULL, NULL),
		       BT_GATT_CCC(temperature_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
		       BT_GATT_CPF(&temperature_cpf),
		       BT_GATT_CHARACTERISTIC(BT_UUID_HUMIDITY,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_READ, read_humidity, NULL, NULL),
		       BT_GATT_CCC(humidity_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
		       BT_GATT_CPF(&humidity_cpf), );

int ble_svc_update_temperature_value(float temp_value)
{
	int ret;
	struct bt_conn *conn;

	if (temp_value > 125 || temp_value < -20) {
		return -EINVAL;
	}

	k_mutex_lock(&data.data_lock, K_FOREVER);
	conn = data.ble_connection;
	data.temperature = (int16_t)(temp_value * 100);
	k_mutex_unlock(&data.data_lock);

	ret = bt_gatt_notify(conn, &environmental_sensing_service.attrs[1], &data.temperature,
			     sizeof(data.temperature));

	return ret == -ENOTCONN ? 0 : ret;
}

int ble_svc_update_humidity_value(float hum_value)
{
	int ret;
	struct bt_conn *conn;

	if (hum_value > 100 || hum_value < 0) {
		return -EINVAL;
	}

	k_mutex_lock(&data.data_lock, K_FOREVER);
	conn = data.ble_connection;
	data.humidity = (uint16_t)(hum_value * 100);
	k_mutex_unlock(&data.data_lock);

	ret = bt_gatt_notify(conn, &environmental_sensing_service.attrs[5], &data.humidity,
			     sizeof(data.humidity));

	return ret == -ENOTCONN ? 0 : ret;
}

static void bt_ready(int ret)
{
	if (ret != 0) {
		LOG_ERR("Failed to initialize BLE %d", ret);
		return;
	}

	ret = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		LOG_ERR("Advertising failed to start %d", ret);
		return;
	}

	LOG_INF("Advertising successfully started\n");
}

int ble_svc_enable_ble(void)
{
	int ret = bt_enable(bt_ready);
	if (ret != 0) {
		LOG_ERR("Failed to enable BLE %d", ret);
		return ret;
	}

	return 0;
}

void ble_svc_init(void)
{
	k_mutex_init(&data.data_lock);
	bt_conn_cb_register(&conn_callbacks);
}