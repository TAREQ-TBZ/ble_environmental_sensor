import pytest
from board import BOARD


def pytest_addoption(parser):
    parser.addoption(
        "--port", help="The port to which the device is attached (eg: /dev/ttyACM0)"
    )
    parser.addoption(
        "--baud",
        type=int,
        default=115200,
        help="Serial port baud rate (default: 115200)",
    )
    parser.addoption(
        "--fw-image", type=str, help="Firmware binary to program to device"
    )
    parser.addoption(
        "--hci-transport",
        type=str,
        help="The USB transport interfaces with a local Bluetooth USB dongle",
    )


@pytest.fixture(scope="session")
def get_port(request):
    return request.config.getoption("--port")


@pytest.fixture(scope="session")
def get_baud(request):
    return request.config.getoption("--baud")


@pytest.fixture(scope="session")
def get_fw_image(request):
    return request.config.getoption("--fw-image")


@pytest.fixture(scope="session")
def get_hci_transport_type(request):
    return request.config.getoption("--hci-transport")


@pytest.fixture(scope="session")
def get_board(get_port, get_baud, get_fw_image):
    board = BOARD(get_port, get_baud, get_fw_image)
    yield board
