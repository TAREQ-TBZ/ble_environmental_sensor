import logging
from bumble.device import Device, Peer
from bumble.hci import Address
from bumble.gatt import show_services
from bumble.transport import open_transport_or_link

logging.basicConfig(
    level=logging.DEBUG, format="%(asctime)s - %(levelname)s - %(message)s"
)
logger = logging.getLogger(__name__)


async def initialize_bluetooth_device(transport_type):
    """Initialize and return a Bluetooth device."""
    hci_transport = await open_transport_or_link(transport_type)
    hci_device = Device.with_hci(
        "Bumble",
        Address("F0:F1:F2:F3:F4:F5"),
        hci_transport.source,
        hci_transport.sink,
    )
    await hci_device.power_on()
    return hci_device, hci_transport


class BleClient:
    def __init__(self, transport_type):
        self.device = None
        self.transport_type = transport_type
        self.hci_transport = None
        self.connection = None
        self.peer = None
        self.services = None

    async def initialize(self):
        """Initialize the BLE client with a Bluetooth device."""
        self.device, self.hci_transport = await initialize_bluetooth_device(
            self.transport_type
        )
        return self.device

    async def register_listener_callback(self, on_type: str, callback_fn):
        self.device.on(on_type, callback_fn)

    async def start_scanning(self):
        """Scan for BLE devices."""
        await self.device.start_scanning()
        logger.info("Scanning has been started")

    async def stop_scanning(self):
        """Stop scanning for BLE devices."""
        await self.device.stop_scanning()
        logger.info("Scanning has been stopped")

    async def connect(self, address):
        """Establish a connection with a remote BLE device."""
        self.connection = await self.device.connect(address)
        logger.info(f"=== Connected to {address}")
        return self.connection

    async def get_connection_parameters(self):
        if self.connection:
            return self.connection.parameters
        return None

    async def discover_services(self):
        """Discover and display services on the connected device."""
        if not self.connection:
            raise RuntimeError("Device not connected")

        logger.info("=== Discovering services")
        self.peer = Peer(self.connection)
        await self.peer.discover_services()

        for service in self.peer.services:
            await service.discover_characteristics()
            for characteristic in service.characteristics:
                await characteristic.discover_descriptors()

        logger.info("=== Services discovered")
        show_services(self.peer.services)
        self.services = self.peer.services

    async def read_characteristic(self, characteristic_uuid):
        """Read data from a specific characteristic."""
        for service in self.services:
            for characteristic in service.characteristics:
                if characteristic.uuid == characteristic_uuid:
                    return await characteristic.read_value()
        raise RuntimeError("Characteristic not found")

    async def subscribe_to_characteristics(
        self, characteristic_uuid, notification_handler
    ):
        """Subscribe to temperature and humidity updates."""
        for service in self.services:
            for characteristic in service.characteristics:
                if characteristic.uuid == characteristic_uuid:
                    await characteristic.subscribe(notification_handler, True)

    async def disconnect(self):
        if self.connection:
            try:
                await self.connection.disconnect()
                logger.info("Disconnected from BLE device.")
            except Exception as e:
                logger.warning(f"Error during BLE disconnection: {e}")

    async def close(self):
        """Properly shut down the BLE client and transport."""
        if self.device:
            try:
                await self.device.power_off()
                logger.info("BLE device powered off.")
            except Exception as e:
                logger.warning(f"Error during BLE device shutdown: {e}")

        if self.hci_transport:
            try:
                await self.hci_transport.close()
                logger.info("HCI transport closed.")
            except Exception as e:
                logger.warning(f"Error while closing HCI transport: {e}")
