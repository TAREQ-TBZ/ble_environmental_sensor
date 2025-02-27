import pytest
import asyncio
from bumble.core import UUID, AdvertisingData
from ble_client import BleClient
import logging
from bumble.colors import color
import time

logging.basicConfig(
    level=logging.DEBUG, format="%(asctime)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)

ENVIRONMENTAL_SENSING_SERVICE = UUID.from_16_bits(0x181A)
TEMPERATURE_CHARACTERISTIC = UUID.from_16_bits(0x2A6E)
HUMIDITY_CHARACTERISTIC = UUID.from_16_bits(0x2A6F)
EXPECTED_MANUFACTURER_DATA = bytes.fromhex("59000000")  # Nordic (0x0059) + data 0000
EXPECTED_URL = "https://github.com/TAREQ-TBZ"

target_device_address = None
temp_notifications = []
humid_notifications = []
temp_notification_timestamps = []
humid_notification_timestamps = []
start_time = 0


def on_advertisement(advertisement):
    global target_device_address
    if target_device_address:
        return  # Already found device

    # Verify device name from advertising data
    device_name = advertisement.data.get(AdvertisingData.COMPLETE_LOCAL_NAME)

    if str(device_name) != "TBZ_SHAM_SENSOR":
        return

    logger.info(f"Found device name: {device_name}")
    target_device_address = advertisement.address
    logger.info(f"Found target device: {target_device_address}")


def temp_notification_handler(value):
    current_time = time.time()

    temp_value = int.from_bytes(value, "little") / 100
    logger.info(f"Received temperature notification: {temp_value}")
    temp_notifications.append(temp_value)
    temp_notification_timestamps.append(current_time)

    if len(temp_notification_timestamps) > 1:
        interval = temp_notification_timestamps[-1] - temp_notification_timestamps[-2]
        logger.info(f"Temperature notification interval: {interval:.2f} seconds")
        assert (
            29 <= interval <= 31
        ), f"Unexpected temperature notification interval: {interval:.2f} seconds"

    if len(temp_notification_timestamps) == 1:
        first_interval = temp_notification_timestamps[0] - start_time
        logger.info(f"Temperature notification interval: {first_interval:.2f} seconds")
        assert (
            9 <= first_interval <= 11
        ), f"First temperature notification did not arrive in expected time: {first_interval:.2f} seconds"


def humid_notification_handler(value):
    current_time = time.time()

    humid_value = int.from_bytes(value, "little") / 100
    logger.info(f"Received humidity notification: {humid_value}")
    humid_notifications.append(humid_value)
    humid_notification_timestamps.append(current_time)

    if len(humid_notification_timestamps) > 1:
        interval = humid_notification_timestamps[-1] - humid_notification_timestamps[-2]
        logger.info(f"Humidity notification interval: {interval:.2f} seconds")
        assert (
            29 <= interval <= 31
        ), f"Unexpected humidity notification interval: {interval:.2f} seconds"

    if len(humid_notification_timestamps) == 1:
        first_interval = humid_notification_timestamps[0] - start_time
        logger.info(
            f"Humidity notification first interval: {first_interval:.2f} seconds"
        )
        assert (
            9 <= first_interval <= 11
        ), f"First humidity notification did not arrive in expected time: {first_interval:.2f} seconds"


@pytest.mark.asyncio
async def test_ble_device(get_board, get_hci_transport_type):
    # 1. Reset device
    get_board.hard_reset()
    await asyncio.sleep(1)

    # 2. Check advertising started
    assert get_board.wait_for_regex_in_line(
        r"Advertising successfully started"
    ), "Device failed to start advertising"

    # 3. Scan and verify advertising data
    ble_client = BleClient(get_hci_transport_type)
    try:
        await ble_client.initialize()
        await ble_client.register_listener_callback("advertisement", on_advertisement)

        # Scan for device
        await ble_client.start_scanning()
        scanning_time = asyncio.get_event_loop().time()
        while not target_device_address and (
            asyncio.get_event_loop().time() - scanning_time < 12
        ):
            await asyncio.sleep(0.1)
        await ble_client.stop_scanning()
        assert target_device_address, "Target device not found during scanning"

        # 4. Connect and check parameters
        connection = await ble_client.connect(target_device_address)
        logger.info(f"Connection parameters: {connection.parameters}")
        assert (
            15 <= connection.parameters.connection_interval <= 30
        ), "Invalid connection interval"

        # Start timer to measure first notification delay
        global start_time
        start_time = time.time()

        # 5. Discover services
        await ble_client.discover_services()
        env_service = next(
            (s for s in ble_client.services if s.uuid == ENVIRONMENTAL_SENSING_SERVICE),
            None,
        )
        assert env_service, "Environmental Sensing service missing"

        # 6. Verify characteristics
        temp_char = next(
            (
                c
                for c in env_service.characteristics
                if c.uuid == TEMPERATURE_CHARACTERISTIC
            ),
            None,
        )
        humid_char = next(
            (
                c
                for c in env_service.characteristics
                if c.uuid == HUMIDITY_CHARACTERISTIC
            ),
            None,
        )
        assert temp_char and humid_char, "Characteristics missing"

        # 7. Subscribe to notifications
        await ble_client.subscribe_to_characteristics(
            TEMPERATURE_CHARACTERISTIC, temp_notification_handler
        )

        await ble_client.subscribe_to_characteristics(
            HUMIDITY_CHARACTERISTIC, humid_notification_handler
        )

        await asyncio.sleep(40)

        # Check if at least 2 notifications were received for proper interval validation
        assert (
            len(temp_notifications) >= 2
        ), "Not enough temperature notifications received"
        assert (
            len(humid_notifications) >= 2
        ), "Not enough humidity notifications received"

        for temp in temp_notifications:
            logger.info(f"Temperature: {temp}")

        for humid in humid_notifications:
            logger.info(f"Humidity: {humid}")

        await asyncio.sleep(5)

        # 8. Read and validate values
        temp = int.from_bytes(await temp_char.read_value(), "little") / 100
        humid = int.from_bytes(await humid_char.read_value(), "little") / 100
        logger.info(f"Temperature value (read operation): {temp}")
        logger.info(f"Humidity value (read operation): {humid}")
        assert 18 <= temp <= 25, f"Invalid temperature: {temp}°C"
        assert 45 <= humid <= 65, f"Invalid humidity: {humid}%"

    finally:
        # 9. Cleanup
        await ble_client.disconnect()
        await ble_client.close()
        get_board.close()
    logger.info("=== Validation Successful ===")
