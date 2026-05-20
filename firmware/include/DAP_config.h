/*
 * Based on Raspberry Pi debugprobe/include/DAP_config.h and Arm CMSIS-DAP.
 * Copyright (c) 2013-2021 ARM Limited. All rights reserved.
 * Copyright (c) 2022 Raspberry Pi Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

#include <string.h>

#include "cmsis_compiler.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "probe.h"

#ifndef RPMON_FW_VERSION
#define RPMON_FW_VERSION "unknown"
#endif

#define probe_info(...) ((void)0)
#define probe_debug(...) ((void)0)
#define probe_dump(...) ((void)0)

#define CPU_CLOCK clock_get_hz(clk_sys)
#define IO_PORT_WRITE_CYCLES 1U
#define DELAY_SLOW_CYCLES 1U

#define DAP_SWD 1
#define DAP_JTAG 0
#define DAP_JTAG_DEV_CNT 8U
#define DAP_DEFAULT_PORT 1U
#define DAP_DEFAULT_SWJ_CLOCK 1000000U
#define DAP_PACKET_SIZE 64U
#define DAP_PACKET_COUNT 8U

#define SWO_UART 0
#define SWO_UART_DRIVER 0
#define SWO_UART_MAX_BAUDRATE 10000000U
#define SWO_MANCHESTER 0
#define SWO_BUFFER_SIZE 4096U
#define SWO_STREAM 0
#define TIMESTAMP_CLOCK 1000000U

#define DAP_UART 0
#define DAP_UART_DRIVER 0
#define DAP_UART_RX_BUFFER_SIZE 1024U
#define DAP_UART_TX_BUFFER_SIZE 1024U
#define DAP_UART_USB_COM_PORT 0

#define TARGET_FIXED 0

static __INLINE uint8_t rpmon_dap_copy_string(char *str, const char *value) {
    strcpy(str, value);
    return (uint8_t)(strlen(value) + 1U);
}

__STATIC_INLINE uint8_t DAP_GetVendorString(char *str) {
    return rpmon_dap_copy_string(str, "kkwell");
}

__STATIC_INLINE uint8_t DAP_GetProductString(char *str) {
    return rpmon_dap_copy_string(str, "RP2350 Monitor CMSIS-DAP");
}

__STATIC_INLINE uint8_t DAP_GetSerNumString(char *str) {
    (void)str;
    return 0U;
}

__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString(char *str) {
    (void)str;
    return 0U;
}

__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString(char *str) {
    (void)str;
    return 0U;
}

__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString(char *str) {
    (void)str;
    return 0U;
}

__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString(char *str) {
    (void)str;
    return 0U;
}

__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString(char *str) {
    return rpmon_dap_copy_string(str, RPMON_FW_VERSION);
}

__STATIC_INLINE void PORT_JTAG_SETUP(void) {
}

extern volatile uint32_t cached_delay;
__STATIC_INLINE void PORT_SWD_SETUP(void) {
    probe_init();
    cached_delay = 0;
}

__STATIC_INLINE void PORT_OFF(void) {
    probe_deinit();
}

__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void) {
    return 0U;
}

__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void) {
}

__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void) {
}

__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void) {
    return 0U;
}

__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void) {
}

__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void) {
}

__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void) {
    return 0U;
}

__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit) {
    (void)bit;
}

__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void) {
    probe_write_mode();
}

__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void) {
    probe_read_mode();
}

__STATIC_FORCEINLINE uint32_t PIN_TDI_IN(void) {
    return 0U;
}

__STATIC_FORCEINLINE void PIN_TDI_OUT(uint32_t bit) {
    (void)bit;
}

__STATIC_FORCEINLINE uint32_t PIN_TDO_IN(void) {
    return 0U;
}

__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN(void) {
    return 0U;
}

__STATIC_FORCEINLINE void PIN_nTRST_OUT(uint32_t bit) {
    (void)bit;
}

__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void) {
    int level = probe_reset_level();
    return level > 0 ? 1U : 0U;
}

__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit) {
    probe_assert_reset(bit != 0U);
}

__STATIC_INLINE void LED_CONNECTED_OUT(uint32_t bit) {
    (void)bit;
}

__STATIC_INLINE void LED_RUNNING_OUT(uint32_t bit) {
    (void)bit;
}

__STATIC_INLINE uint32_t TIMESTAMP_GET(void) {
    return time_us_32();
}

__STATIC_INLINE void DAP_SETUP(void) {
}

__STATIC_INLINE uint8_t RESET_TARGET(void) {
    return 0U;
}

#endif /* __DAP_CONFIG_H__ */
