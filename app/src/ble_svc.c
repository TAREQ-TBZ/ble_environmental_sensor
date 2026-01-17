/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>

#include "ble_svc.h"
#include "events_svc.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_svc, LOG_LEVEL_INF);

#define DEVICE_NAME                 CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN             (sizeof(DEVICE_NAME) - 1)
#define COMPANY_ID_CODE             CONFIG_BT_COMPANY_ID
#define ADV_INTERVAL_UNIT_MS        0.625
#define CONNECTION_INTERVAL_UNIT_MS 1.25
#define SUPERVISION_TIMEOUT_UNIT_MS 10
#define MIN_ADV_INTERVAL            (CONFIG_MIN_ADV_INTERVAL_MS / ADV_INTERVAL_UNIT_MS)
#define MAX_ADV_INTERVAL            (CONFIG_MAX_ADV_INTERVAL_MS / ADV_INTERVAL_UNIT_MS)
#define MAX_ADV_PAYLOAD             31

struct ble_svc_data {
	struct bt_conn *ble_connection;
	int16_t temperature;
	uint16_t humidity;
	struct k_mutex data_lock;
};

static struct ble_svc_data data;

static const struct bt_le_adv_param *adv_param =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, MIN_ADV_INTERVAL, MAX_ADV_INTERVAL,
			NULL); /* Set to NULL for undirected advertising */

struct adv_manufacture_data {
	uint16_t company_code;    /* Company Identifier Code. */
	uint16_t btn_press_count; /* Number of times Button is pressed as a dummy data for now */
};

static struct adv_manufacture_data manufacture_data = {COMPANY_ID_CODE, sys_cpu_to_le16(0x00)};

/* Advertising packet (Max size is 31 bytes) */
static const struct bt_data ad[] = {
	/* 3 bytes (Type, flag, length) */
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	/* 4 bytes (Type + Length + UUID) - Environmental Sensing Service UUID (0x181A in LE) */
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ESS_VAL)),
	/* [2 bytes (Type +  length)] + DEVICE_NAME_LEN bytes */
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	/* [2 bytes (Type +  length)] + size of adv_manufacture_data struct */
	BT_DATA(BT_DATA_MANUFACTURER_DATA, (unsigned char *)&manufacture_data,
		sizeof(manufacture_data)),

};

/* URL data to include in the scan response (This will increase current consumption by 2uA) */
/* 0x17 for https in URI Scheme */
static unsigned char url_data[] = {0x17, '/', '/', 'g', 'i', 't', 'h', 'u', 'b', '.', 'c', 'o',
				   'm',  '/', 'T', 'A', 'R', 'E', 'Q', '-', 'T', 'B', 'Z'};

/* Scan response packet (Max size is 31 bytes) */
static const struct bt_data sd[] = {
	/* [2 bytes (Type +  length)] + length of the data (URL) */
	BT_DATA(BT_DATA_URI, url_data, sizeof(url_data)),
};

static void update_phy(struct bt_conn *conn)
{
	int ret;
	const struct bt_conn_le_phy_param preferred_phy = {
		.options = BT_CONN_LE_PHY_OPT_NONE,
		.pref_rx_phy = BT_GAP_LE_PHY_2M,
		.pref_tx_phy = BT_GAP_LE_PHY_2M,
	};
	ret = bt_conn_le_phy_update(conn, &preferred_phy);
	if (ret) {
		LOG_ERR("Failed to update preferred PHY: %d", ret);
	}
}

static void update_data_length(struct bt_conn *conn)
{
	int ret;
	struct bt_conn_le_data_len_param my_data_len = {
		.tx_max_len = BT_GAP_DATA_LEN_MAX,
		.tx_max_time = BT_GAP_DATA_TIME_MAX,
	};
	ret = bt_conn_le_data_len_update(conn, &my_data_len);
	if (ret) {
		LOG_ERR("Failed to update data length parameter %d", ret);
	}
}

static void on_connected(struct bt_conn *conn, uint8_t ret)
{
	struct event evt;
	struct bt_conn_info info;
	double connection_interval;
	uint16_t supervision_timeout;
	char addr[BT_ADDR_LE_STR_LEN];

	k_mutex_lock(&data.data_lock, K_FOREVER);
	data.ble_connection = conn;
	k_mutex_unlock(&data.data_lock);

	if (ret != 0) {
		LOG_WRN("Connection failed (ret %u)", ret);
		return;
	}

	if (bt_conn_get_info(conn, &info) == 0) {
		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		connection_interval = info.le.interval * CONNECTION_INTERVAL_UNIT_MS;
		supervision_timeout = info.le.timeout * SUPERVISION_TIMEOUT_UNIT_MS;

		LOG_INF("Connection established! Connected to: %s", addr);
		LOG_DBG("Connection parameters: interval %.2f ms, latency %d, timeout %d ms",
			connection_interval, info.le.latency, supervision_timeout);
	} else {
		LOG_WRN("Could not parse connection info");
	}

	update_phy(conn);
	update_data_length(conn);

	evt.type = EVENT_BLE_CONNECTED;
	if (events_svc_send_event(&evt) != 0) {
		LOG_WRN("Event queue full, connected event dropped");
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct event evt;
	LOG_DBG("Disconnected (reason %u)", reason);

	k_mutex_lock(&data.data_lock, K_FOREVER);
	data.ble_connection = NULL;
	k_mutex_unlock(&data.data_lock);

	evt.type = EVENT_BLE_NOT_CONNECTED;
	if (events_svc_send_event(&evt) != 0) {
		LOG_WRN("Event queue full, disconnected event dropped");
	}
}

static bool on_le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	LOG_DBG("Connection parameters update request received.");
	LOG_DBG("Minimum interval: %d, Maximum interval: %d", param->interval_min,
		param->interval_max);
	LOG_DBG("Latency: %d, Timeout: %d", param->latency, param->timeout);

	return true;
}

static void on_le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
				uint16_t timeout)
{
	double connection_interval = interval * CONNECTION_INTERVAL_UNIT_MS;
	uint16_t supervision_timeout = timeout * SUPERVISION_TIMEOUT_UNIT_MS;
	LOG_DBG("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d "
		"ms",
		connection_interval, latency, supervision_timeout);
}

static void on_le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_1M) {
		LOG_DBG("PHY updated. New PHY: 1M");
	} else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_2M) {
		LOG_DBG("PHY updated. New PHY: 2M");
	} else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_CODED_S8) {
		LOG_DBG("PHY updated. New PHY: Long Range");
	}
}

static void on_le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
	uint16_t tx_len = info->tx_max_len;
	uint16_t tx_time = info->tx_max_time;
	uint16_t rx_len = info->rx_max_len;
	uint16_t rx_time = info->rx_max_time;
	LOG_DBG("Data length updated. Length %d/%d bytes, time %d/%d us", tx_len, rx_len, tx_time,
		rx_time);
}

static void ble_srv_att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	LOG_DBG("ATT MTU: TX = %u bytes, RX = %u bytes", tx, rx);
}

static struct bt_gatt_cb ble_srv_gatt_cb = {
	.att_mtu_updated = ble_srv_att_mtu_updated,
};

static struct bt_conn_cb conn_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.le_param_req = on_le_param_req,
	.le_param_updated = on_le_param_updated,
	.le_phy_updated = on_le_phy_updated,
	.le_data_len_updated = on_le_data_len_updated,
};

static void temperature_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	LOG_DBG("Temperature Notifications %s", notif_enabled ? "enabled" : "disabled");
}

static void humidity_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	LOG_DBG("Humidity Notifications %s", notif_enabled ? "enabled" : "disabled");
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
 * Per ESS spec: Temperature in 0.01°C, Humidity in 0.01% (exponent = -2)
 */
static const struct bt_gatt_cpf temperature_cpf = {
	.format = 0x0E,        /* signed 16-bit integer */
	.exponent = -2,        /* value = raw * 10^-2 (0.01°C resolution) */
	.unit = 0x272F,        /* degree Celsius */
	.name_space = 0x01,    /* Bluetooth SIG */
	.description = 0x0106, /* "main" */
};

static const struct bt_gatt_cpf humidity_cpf = {
	.format = 0x06,        /* unsigned 16-bit integer */
	.exponent = -2,        /* value = raw * 10^-2 (0.01% resolution) */
	.unit = 0x27AD,        /* Percentage */
	.name_space = 0x01,    /* Bluetooth SIG */
	.description = 0x0106, /* "main" */
};

BT_GATT_SERVICE_DEFINE(environmental_sensing_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
		       BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE,
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

static int ble_get_payload_size(const struct bt_data *data_array, size_t array_size)
{
	size_t total_size = 0;

	for (size_t i = 0; i < array_size; i++) {
		total_size += data_array[i].data_len + 2; /* 2 bytes for (Type + Length) */
	}

	return total_size;
}

static void bt_ready(int ret)
{
	if (ret != 0) {
		LOG_ERR("Failed to initialize BLE %d", ret);
		return;
	}

	ret = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (ret) {
		LOG_ERR("Advertising failed to start %d", ret);
		return;
	}

	LOG_INF("Advertising successfully started");
}

/*
 * Thread-safety: This function must only be called from the system workqueue context.
 * The manufacture_data is not mutex-protected; concurrent access from multiple contexts
 * would cause a race condition.
 */
void ble_svc_increase_button_press_cnt(void)
{
	manufacture_data.btn_press_count++;

	bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

int ble_svc_enable_ble(void)
{
	int ret;
	bt_addr_le_t addr;

	/* A Static Random Address is a 48-bit (6-byte) address structured as follows:
	 * Most significant two bits (MSBs) of the first byte must be 11 (bin) C0, D0, E0, F0 (HEX).
	 * The remaining 46 bits are randomly generated and remain constant. */
	ret = bt_addr_le_from_str("DE:8B:49:00:00:01", "random", &addr);
	if (ret) {
		LOG_ERR("Invalid BT address %d", ret);
		return ret;
	}

	ret = bt_id_create(&addr, NULL);
	if (ret < 0) {
		LOG_ERR("Creating new ID failed %d", ret);
		return ret;
	}

	ret = bt_enable(bt_ready);
	if (ret != 0) {
		LOG_ERR("Failed to enable BLE %d", ret);
		return ret;
	}

	return 0;
}

void ble_svc_init(void)
{
	size_t adv_size;
	size_t scan_resp_size;

	k_mutex_init(&data.data_lock);
	bt_conn_cb_register(&conn_callbacks);
	bt_gatt_cb_register(&ble_srv_gatt_cb);

	adv_size = ble_get_payload_size(ad, ARRAY_SIZE(ad));
	scan_resp_size = ble_get_payload_size(sd, ARRAY_SIZE(sd));

	__ASSERT(adv_size <= MAX_ADV_PAYLOAD,
		 "Advertisement payload size exceeded the maximum size (Max: 31 bytes), size: %zu",
		 adv_size);
	__ASSERT(scan_resp_size <= MAX_ADV_PAYLOAD,
		 "Scan response payload size exceeded the maximum size (Max: 31 bytes), size: %zu",
		 scan_resp_size);

	LOG_DBG("Actual Advertisement Packet Size: %zu bytes (Max: 31 bytes)", adv_size);
	LOG_DBG("Actual Scan Response Packet Size: %zu bytes (Max: 31 bytes)", scan_resp_size);
}
