#
# Copyright (c) 2024 Tareq Mhisen
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app LANGUAGES C)

target_sources(app PRIVATE
    src/events_svc.c
    src/main.c
    src/user_interface.c
)
