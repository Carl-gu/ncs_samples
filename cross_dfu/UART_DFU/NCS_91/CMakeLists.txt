#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#
cmake_minimum_required(VERSION 3.8.2)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracker_dfu)

zephyr_include_directories(src)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/app_uart.c)
target_sources(app PRIVATE src/app_cmd.c)
target_sources(app PRIVATE src/app_flash.c)
target_sources(app PRIVATE src/app_flash_cmd.c)
target_sources(app PRIVATE src/button.c)
target_sources(app PRIVATE src/led.c)
target_sources(app PRIVATE src/http_client.c)

# Add serial dfu module
zephyr_include_directories(src/serial_dfu)
target_sources(app PRIVATE src/serial_dfu/serial_dfu.c)
target_sources(app PRIVATE src/serial_dfu/dfu_drv.c)
target_sources(app PRIVATE src/serial_dfu/dfu_file.c)
target_sources(app PRIVATE src/serial_dfu/dfu_host.c)
target_sources(app PRIVATE src/serial_dfu/slip.c)
target_sources(app PRIVATE src/serial_dfu/crc32.c)

