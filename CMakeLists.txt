cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Target board")

if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "/Users/kvell/Library/development-board-toolchain/families/rp2350/shared/board-environments/RP2350SDKCore/sdk_core/RP2350/pico-sdk" CACHE PATH "Pico SDK path")
endif()

if(NOT DEFINED PICO_TOOLCHAIN_PATH AND NOT DEFINED ENV{PICO_TOOLCHAIN_PATH})
    set(_DBT_ARM_TOOLCHAIN "/Users/kvell/Library/development-board-toolchain/families/rp2350/shared/board-environments/RP2350SDKCore/sdk_core/RP2350/toolchains/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi/bin")
    if(EXISTS "${_DBT_ARM_TOOLCHAIN}/arm-none-eabi-gcc")
        set(PICO_TOOLCHAIN_PATH "${_DBT_ARM_TOOLCHAIN}" CACHE PATH "Arm GNU toolchain path")
    endif()
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(rp2350_monitor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(rp2350_monitor
    firmware/src/main.cpp
    firmware/src/core/config.cpp
    firmware/src/core/channel_manager.cpp
    firmware/src/core/command_processor.cpp
    firmware/src/core/config_store.cpp
    firmware/src/core/event_bus.cpp
    firmware/src/core/logic_analyzer.cpp
    firmware/src/core/pin_manager.cpp
    firmware/src/drivers/can_channel.cpp
    firmware/src/drivers/gpio_channel.cpp
    firmware/src/drivers/i2c_channel.cpp
    firmware/src/drivers/spi_channel.cpp
    firmware/src/drivers/uart_channel.cpp
    firmware/src/net/dhcp_server.cpp
    firmware/src/net/dns_server.cpp
    firmware/src/net/wifi_manager.cpp
    firmware/src/transport/http_server.cpp
    firmware/src/transport/tcp_transport.cpp
    firmware/src/transport/usb_transport.cpp
    firmware/src/util/hex.cpp
    firmware/src/util/json.cpp
)

target_include_directories(rp2350_monitor PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/firmware/include
    ${PICO_SDK_PATH}/src/rp2_common/pico_cyw43_arch/include
    ${PICO_SDK_PATH}/src/rp2_common/pico_cyw43_driver/include
)

target_compile_definitions(rp2350_monitor PRIVATE
    RPMON_FW_VERSION="0.5.0"
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_FLASH_BOOT=1
    PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1
)

pico_configure_ip4_address(rp2350_monitor PRIVATE CYW43_DEFAULT_IP_AP_ADDRESS "192.168.4.1")
pico_configure_ip4_address(rp2350_monitor PRIVATE CYW43_DEFAULT_IP_AP_GATEWAY "192.168.4.1")
pico_configure_ip4_address(rp2350_monitor PRIVATE CYW43_DEFAULT_IP_MASK "255.255.255.0")
pico_configure_ip4_address(rp2350_monitor PRIVATE CYW43_DEFAULT_IP_DNS "192.168.4.1")

target_link_libraries(rp2350_monitor
    pico_stdlib
    pico_bootrom
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_flash
    hardware_clocks
    hardware_dma
    hardware_i2c
    hardware_pio
    hardware_spi
    hardware_sync
    hardware_uart
)

pico_enable_stdio_usb(rp2350_monitor 1)
pico_enable_stdio_uart(rp2350_monitor 0)
pico_add_extra_outputs(rp2350_monitor)
