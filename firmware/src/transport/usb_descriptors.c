/*
 * Composite USB descriptors for RP2350 Monitor.
 *
 * CDC remains the monitor JSONL transport. The Raspberry Pi reset vendor
 * interface is preserved for picotool. CMSIS-DAP v2 bulk is added as a
 * dedicated vendor interface for OpenOCD/GDB.
 *
 * Based on Pico SDK stdio USB descriptors and Raspberry Pi debugprobe
 * descriptors.
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "pico/stdio_usb/reset_interface.h"
#include "pico/unique_id.h"
#include "tusb.h"

#define USBD_VID 0x2E8A
#define USBD_PID 0x0009
#define USBD_BCD 0x0900

#define TUD_RPI_RESET_DESC_LEN 9
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_RPI_RESET_DESC_LEN + TUD_VENDOR_DESC_LEN)

#define ITF_CDC 0
#define ITF_CDC_DATA 1
#define ITF_RPI_RESET 2
#define ITF_DAP 3
#define ITF_TOTAL 4

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_DAP_OUT 0x03
#define EP_DAP_IN 0x83

#define STR_LANG 0
#define STR_MANUFACTURER 1
#define STR_PRODUCT 2
#define STR_SERIAL 3
#define STR_CDC 4
#define STR_RESET 5
#define STR_DAP 6

#define TUD_RPI_RESET_DESCRIPTOR(_itfnum, _stridx) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, _stridx,

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USBD_VID,
    .idProduct = USBD_PID,
    .bcdDevice = USBD_BCD,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_RPI_RESET_DESCRIPTOR(ITF_RPI_RESET, STR_RESET)
    TUD_VENDOR_DESCRIPTOR(ITF_DAP, STR_DAP, EP_DAP_OUT, EP_DAP_IN, 64),
};

static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const desc_strings[] = {
    [STR_MANUFACTURER] = "kkwell",
    [STR_PRODUCT] = "RP2350 Monitor CMSIS-DAP",
    [STR_SERIAL] = serial,
    [STR_CDC] = "RP2350 Monitor CDC JSONL",
    [STR_RESET] = "Reset",
    [STR_DAP] = "CMSIS-DAP v2 Interface",
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc_str[48];

    if (!serial[0]) {
        pico_get_unique_board_id_string(serial, sizeof(serial));
    }

    uint8_t count = 0;
    if (index == STR_LANG) {
        desc_str[1] = 0x0409;
        count = 1;
    } else {
        if (index >= (sizeof(desc_strings) / sizeof(desc_strings[0])) || !desc_strings[index]) {
            return NULL;
        }
        const char *str = desc_strings[index];
        while (str[count] && count < 47) {
            desc_str[1 + count] = (uint8_t)str[count];
            ++count;
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc_str;
}
