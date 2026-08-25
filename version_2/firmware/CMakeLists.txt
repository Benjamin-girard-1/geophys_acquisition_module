cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/app"
    "${CMAKE_CURRENT_LIST_DIR}/boards/rev_1"
    "${CMAKE_CURRENT_LIST_DIR}/analog_cards"
    "${CMAKE_CURRENT_LIST_DIR}/analog_cards/magnetic"
    "${CMAKE_CURRENT_LIST_DIR}/analog_cards/acc_geoph"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/adc"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/gnss"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/imu"
    "${CMAKE_CURRENT_LIST_DIR}/drivers/gpio_expansion/shift_register"
    "${CMAKE_CURRENT_LIST_DIR}/protocol"
    "${CMAKE_CURRENT_LIST_DIR}/transports"
    "${CMAKE_CURRENT_LIST_DIR}/platform/esp32s3_devkit"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(geophys_acquisition_module_v2)
