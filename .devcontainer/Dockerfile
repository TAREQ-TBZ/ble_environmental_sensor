FROM debian:stable-20241016-slim

ARG ZEPHYR_SDK_VERSION=0.17.4
ARG HOSTTYPE="x86_64"
ARG TOOLCHAIN_LIST="-t riscv64-zephyr-elf -t xtensa-espressif_esp32s3_zephyr-elf -t arm-zephyr-eabi"

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000

SHELL ["/bin/bash", "-c"]
ENV DEBIAN_FRONTEND=noninteractive

# Base packages
RUN apt-get -y update && \
    apt-get install --no-install-recommends -y \
      ca-certificates git \
      build-essential cmake ninja-build gperf \
      device-tree-compiler \
      wget curl \
      python3 python3-pip python3-venv python3-dev \
      xz-utils \
      ccache dfu-util \
      libncurses-dev \
      file locales dos2unix \
      openocd usbutils udev libglib2.0-0 libusb-1.0-0 \
      tio \
    && apt-get clean -y \
    && apt-get autoremove --purge -y \
    && rm -rf /var/lib/apt/lists/*

# Create user
RUN groupadd --gid ${USER_GID} ${USERNAME} \
 && useradd  --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME} \
 && usermod -aG dialout ${USERNAME}

# Install Zephyr SDK (minimal) + selected toolchains + host tools
RUN mkdir -p /opt/toolchains
WORKDIR /opt/toolchains

RUN wget -q --show-progress --progress=bar:force:noscroll \
      https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-${HOSTTYPE}_minimal.tar.xz \
 && tar xf zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-${HOSTTYPE}_minimal.tar.xz \
 && rm zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-${HOSTTYPE}_minimal.tar.xz

RUN cd /opt/toolchains/zephyr-sdk-${ZEPHYR_SDK_VERSION} \
 && bash setup.sh -c ${TOOLCHAIN_LIST}

RUN cd /opt/toolchains/zephyr-sdk-${ZEPHYR_SDK_VERSION} \
 && wget -q --show-progress --progress=bar:force:noscroll \
      https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/hosttools_linux-${HOSTTYPE}.tar.xz \
 && tar xf hosttools_linux-${HOSTTYPE}.tar.xz \
 && rm hosttools_linux-${HOSTTYPE}.tar.xz \
 && bash zephyr-sdk-${HOSTTYPE}-hosttools-standalone-*.sh -y -d .

ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-${ZEPHYR_SDK_VERSION}

# Prepare user home layout
RUN mkdir -p /home/${USERNAME}/application \
 && chown -R ${USERNAME}:${USERNAME} /home/${USERNAME}

# Python venv with west
ENV VIRTUAL_ENV=/home/${USERNAME}/.venv
RUN python3 -m venv ${VIRTUAL_ENV} \
 && chown -R ${USERNAME}:${USERNAME} ${VIRTUAL_ENV}
ENV PATH="${VIRTUAL_ENV}/bin:$PATH"

USER ${USERNAME}
WORKDIR /home/${USERNAME}

RUN python3 -m pip install --no-cache-dir -U pip setuptools wheel \
 && python3 -m pip install --no-cache-dir west pyocd pyserial esptool

# Copy the application's west manifest and init the workspace.
# At runtime the full repo is bind-mounted over /home/vscode/application,
# so west.yml stays consistent between build and runtime.
COPY --chown=${USERNAME}:${USERNAME} west.yml /home/${USERNAME}/application/west.yml

RUN west init -l application \
 && west update --narrow --fetch-opt="--depth=1" \
 && west packages pip --install \
 && west zephyr-export \
 && west blobs fetch hal_espressif

# Quality of life: make Zephyr tools easy to find
ENV ZEPHYR_BASE=/home/${USERNAME}/zephyr
ENV PATH="${ZEPHYR_BASE}/scripts:${ZEPHYR_SDK_INSTALL_DIR}/riscv64-zephyr-elf/bin:${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin:${PATH}"

WORKDIR /home/${USERNAME}
CMD ["bash"]
