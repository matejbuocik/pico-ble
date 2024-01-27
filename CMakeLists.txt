cmake_minimum_required(VERSION 3.12)
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(Kolokvium C CXX ASM)
pico_sdk_init()
add_subdirectory(temp)


#
# Bluetooth server
#
set(SERVER bt_server)

add_executable(${SERVER} ${SERVER}.c)
target_link_libraries(${SERVER} pico_stdlib pico_btstack_ble pico_btstack_cyw43
                                pico_cyw43_arch_none temp)

# This is needed to find btstack_config.h
target_include_directories(${SERVER} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# Runs BTStack compile_gatt tool to make a GATT header file from a BTstack GATT file
# https://github.com/bluekitchen/btstack/blob/master/tool/compile_gatt.py
pico_btstack_make_gatt_header(${SERVER} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/temp_service.gatt")

# Used in btstack_config.h to distinguish between server and client
target_compile_definitions(${SERVER} PRIVATE IS_SERVER)

pico_add_extra_outputs(${SERVER})
pico_enable_stdio_usb(${SERVER} 1)
pico_enable_stdio_uart(${SERVER} 0)


#
# Bluetooth client
#
set(CLIENT bt_client)

add_executable(${CLIENT} ${CLIENT}.c)
target_link_libraries(${CLIENT} pico_stdlib pico_btstack_ble pico_btstack_cyw43 pico_cyw43_arch_none)

# This is needed to find btstack_config.h
target_include_directories(${CLIENT} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

pico_add_extra_outputs(${CLIENT})
pico_enable_stdio_usb(${CLIENT} 1)
pico_enable_stdio_uart(${CLIENT} 0)
