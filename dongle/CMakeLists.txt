# SPDX-License-Identifier: MIT
#
# nRF52833 ESB dongle (PRX) - receives HID reports from ZMK keyboard
# and forwards them as USB HID. Companion firmware for the zmk-esb-endpoint
# ZMK module.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zmk_esb_dongle)

# nRF ESB library source
set(NRF_ESB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/nrf-esb)
zephyr_include_directories(${NRF_ESB_DIR}/include)
zephyr_include_directories(${NRF_ESB_DIR}/subsys)

# Protocol shared headers
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_sources(app PRIVATE
    src/main.c
)

if(CONFIG_HAS_HW_NRF_PPI)
    target_sources(app PRIVATE
        ${NRF_ESB_DIR}/subsys/esb.c
        ${NRF_ESB_DIR}/subsys/esb_ppi.c
    )
else()
    target_sources(app PRIVATE
        ${NRF_ESB_DIR}/subsys/esb.c
        ${NRF_ESB_DIR}/subsys/esb_dppi.c
    )
endif()

zephyr_link_libraries(-Wl,--undefined=z_arm_irq_direct_dynamic_dispatch_reschedule)
