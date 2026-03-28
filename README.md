# Environmental Sensor - BLE

A BLE environmental sensor firmware built on [Zephyr RTOS](https://www.zephyrproject.org/) v4.3.0. Measures temperature and humidity using a Sensirion SHT4x sensor and transmits readings over Bluetooth Low Energy (Environmental Sensing Service).

The Zigbee version can be found at: https://github.com/TAREQ-TBZ/environmental_sensor

## Key Features

- **Ultra-low power** - ~6 uA connected, ~11 uA advertising, powered by CR2032 coin cell
- **BLE peripheral** with Environmental Sensing Service (temperature + humidity notifications)
- **OTA DFU** via MCUboot + MCUmgr over BLE
- **Custom PCB** with nRF52833 + SHT40 + 2.45 GHz PCB monopole antenna

| Board top side | Board bottom side |
|---|---|
| ![Board top side](docs/images/Board_top.jpeg?s=300) | ![Board bottom side](docs/images/Board_bottom.jpeg?s=300) |

### Current consumption

| Advertising | Connected |
|---|---|
| ![advertising](docs/images/device_is_advertising.png?s=300) | ![connected](docs/images/device_is_connected.png?s=300) |

## Supported Boards

| Board | Type | Description |
|---|---|---|
| `sham_nrf52833` | Custom board | Target hardware (nRF52833 + SHT40). Full support including MCUboot OTA. |
| `esp32s3_devkitc/esp32s3/procpu` | Dev board | ESP32-S3 DevKitC with SHT3xD sensor for development/testing. |

## Getting Started

There are two ways to set up the development environment:

1. **VS Code Dev Container** (recommended) - all toolchains and dependencies pre-installed
2. **Manual setup** - install Zephyr SDK and tools on your host machine

### Option 1: VS Code Dev Container

**Prerequisites:** [Docker](https://docs.docker.com/get-docker/) and [VS Code](https://code.visualstudio.com/) with the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension.

1. Clone the repository:
   ```shell
   git clone git@github.com:TAREQ-TBZ/ble_environmental_sensor.git
   ```

2. Open the repository folder in VS Code:
   ```shell
   code ble_environmental_sensor
   ```

3. When prompted **"Reopen in Container"**, click yes. Or use the command palette: `Dev Containers: Rebuild and Reopen in Container`.

4. Wait for the container to build. On first run this downloads the Zephyr SDK, toolchains, and all Zephyr modules (~20 min depending on network speed). Subsequent starts are instant.

5. Build from the VS Code terminal:
   ```shell
   west build -p always -b sham_nrf52833 app
   ```

The container mounts the repository at `/home/vscode/application` and uses its `west.yml` directly as the west workspace manifest. All Zephyr modules, toolchains (ARM, RISC-V, Xtensa), and host tools are pre-installed.

> **Building the container image locally:** The `devcontainer.json` defaults to a pre-built image. To build locally (e.g., to customize toolchains), uncomment the `"build"` section in `.devcontainer/devcontainer.json` and comment out the `"image"` line.

### Option 2: Manual Setup (Terminal)

**Prerequisites:** Linux host with Python 3.10+, `cmake`, `ninja-build`, `git`, `device-tree-compiler`, and the [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

```shell
# Create workspace and virtual environment
mkdir ws && cd ws
python3 -m venv .venv
source .venv/bin/activate
pip install west

# Initialize workspace from the application manifest
west init -m git@github.com:TAREQ-TBZ/ble_environmental_sensor.git --mr main
west update
pip install -r zephyr/scripts/requirements.txt

# For ESP32 targets: fetch required binary blobs
west blobs fetch hal_espressif

# Build
west build -b sham_nrf52833 application/app
```

> **Note:** When using the manual setup, build commands run from the workspace root (`ws/`) and the application path is `application/app`. In the Dev Container, you are already inside the `application/` directory, so the path is just `app`.

## Building

All build commands below assume you are inside the Dev Container or in the `application/` directory. When using the manual setup from the workspace root, replace `app` with `application/app`.

### sham_nrf52833 (Target Hardware)

```shell
# Standard build
west build -p always -b sham_nrf52833 app

# With MCUboot bootloader (required for OTA DFU)
west build -p always --sysbuild -b sham_nrf52833 app

# Debug build (enables UART logging at 115200 baud)
west build -p always -b sham_nrf52833 app -DEXTRA_CONF_FILE=debug.conf
```

### ESP32-S3 DevKitC

```shell
west build -p always -b esp32s3_devkitc/esp32s3/procpu app
```

The board overlay at `app/boards/esp32s3_devkitc_esp32s3_procpu.overlay` defines the LED pin and SHT3xD sensor for this dev board.

## Flashing

### sham_nrf52833

The default flash runner is `nrfjprog`. Supported runners: `nrfjprog`, `nrfutil`, `jlink`, `pyocd`, `openocd`.

```shell
# Flash using the default runner (nrfjprog)
west flash

# Or specify a runner explicitly
west flash -r jlink
west flash -r openocd
```

When using sysbuild, specify the app domain to flash the application (MCUboot + app are both flashed):

```shell
west flash -d build
```

> **J-Link:** Requires [SEGGER J-Link](https://www.segger.com/downloads/jlink/) installed on the host. In the Dev Container, it is bind-mounted from `/opt/SEGGER/JLink`.

### ESP32-S3 DevKitC

```shell
# Flash over USB
west flash -r esp32

# Monitor serial output
west espressif monitor
```

## OTA DFU (Over-The-Air Firmware Update)

The firmware supports OTA updates via MCUmgr over BLE when built with `--sysbuild` (which includes MCUboot).

Build with sysbuild to produce a signed firmware image:

```shell
west build -p always --sysbuild -b sham_nrf52833 app
```

The signed application image is at `build/app/zephyr/zephyr.signed.bin`. Use any MCUmgr-compatible client (e.g., the [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) mobile app or the [mcumgr CLI](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html)) to upload the image over BLE.

## Configuration

Application-specific Kconfig options are defined in `app/Kconfig`:

| Option | Default | Description |
|---|---|---|
| `CONFIG_MEASURING_PERIOD_SECONDS` | 30 | Sensor sampling interval (seconds) |
| `CONFIG_FIRST_MEASUREMENT_DELAY_SECONDS` | 10 | Delay before first measurement after boot |
| `CONFIG_MIN_ADV_INTERVAL_MS` | 500 | Minimum BLE advertising interval (ms) |
| `CONFIG_MAX_ADV_INTERVAL_MS` | 501 | Maximum BLE advertising interval (ms) |

Override at build time:

```shell
west build -p always -b sham_nrf52833 app -DCONFIG_MEASURING_PERIOD_SECONDS=60
```

## Code Formatting

CI enforces formatting on all pull requests.

```shell
# C code (uses .clang-format: LLVM-based, 8-space tabs, 100-column limit)
clang-format -i app/src/*.c app/src/*.h

# Python
black systemtest/
```

## Hardware

The KiCad design files (schematic, PCB, BOM) are in `Hardware_files/sham_nrf52833/`.

Key components:
- Nordic nRF52833 (QDAA package)
- Sensirion SHT40 temperature/humidity sensor (I2C, address 0x44)
- 2.45 GHz PCB monopole antenna
- CR2032 coin cell holder
- Status LED + user button

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
