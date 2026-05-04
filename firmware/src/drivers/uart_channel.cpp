#include "rpmon/drivers/uart_channel.h"

#include <cstdio>

#include "hardware/gpio.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/pin_manager.h"

namespace rpmon {

uart_inst_t *UartChannel::uart() const {
    return config_.instance == 1 ? uart1 : uart0;
}

bool UartChannel::configure(const ChannelConfig &config, PinManager &pins, char *err, size_t err_len) {
    if (!pins.validate_uart(config.instance, config.pins.tx, config.pins.rx)) {
        snprintf(err, err_len, "invalid UART%d TX/RX pin mapping", config.instance);
        return false;
    }
    if (!pins.claim(config.pins.tx, config.id, PinRole::UartTx) ||
        !pins.claim(config.pins.rx, config.id, PinRole::UartRx)) {
        pins.release_channel(config.id);
        snprintf(err, err_len, "UART pins already in use or not exposed");
        return false;
    }
    config_ = config;
    configured_ = true;
    active_ = false;
    return true;
}

bool UartChannel::start(char *err, size_t err_len) {
    if (!configured_) {
        snprintf(err, err_len, "UART channel not configured");
        return false;
    }
    uart_init(uart(), config_.baud);
    gpio_set_function(config_.pins.tx, UART_FUNCSEL_NUM(config_.instance, config_.pins.tx));
    gpio_set_function(config_.pins.rx, UART_FUNCSEL_NUM(config_.instance, config_.pins.rx));
    uart_set_format(uart(), 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart(), true);
    if (config_.loopback) {
        uart_get_hw(uart())->cr |= UART_UARTCR_LBE_BITS;
    }
    active_ = true;
    return true;
}

void UartChannel::stop() {
    if (active_) {
        uart_get_hw(uart())->cr &= ~UART_UARTCR_LBE_BITS;
        uart_deinit(uart());
    }
    if (configured_) {
        gpio_set_function(config_.pins.tx, GPIO_FUNC_NULL);
        gpio_set_function(config_.pins.rx, GPIO_FUNC_NULL);
    }
    active_ = false;
}

void UartChannel::poll(EventBus &events) {
    if (!active_) {
        return;
    }
    uint8_t buf[kMaxPayloadBytes];
    size_t len = 0;
    while (len < sizeof(buf) && uart_is_readable(uart())) {
        buf[len++] = uart_getc(uart());
    }
    if (len > 0) {
        events.publish_data(config_.id, ProtocolType::Uart, "rx", buf, len);
    }
}

bool UartChannel::write(const uint8_t *data, size_t len, EventBus &events, char *err, size_t err_len) {
    if (!active_) {
        snprintf(err, err_len, "UART channel is not active");
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        uart_putc_raw(uart(), static_cast<char>(data[i]));
    }
    events.publish_data(config_.id, ProtocolType::Uart, "tx", data, len);
    return true;
}

bool UartChannel::transfer(uint8_t address, const uint8_t *tx, size_t tx_len, size_t rx_len, EventBus &events, char *err, size_t err_len) {
    (void)address;
    (void)tx;
    (void)tx_len;
    (void)rx_len;
    (void)events;
    snprintf(err, err_len, "UART uses channel_write, not transfer");
    return false;
}

void UartChannel::describe_json(char *out, size_t out_len) const {
    snprintf(out, out_len,
             "{\"id\":%d,\"type\":\"uart\",\"instance\":%d,\"active\":%s,\"baud\":%lu,\"tx\":%d,\"rx\":%d,\"loopback\":%s}",
             config_.id,
             config_.instance,
             active_ ? "true" : "false",
             static_cast<unsigned long>(config_.baud),
             config_.pins.tx,
             config_.pins.rx,
             config_.loopback ? "true" : "false");
}

} // namespace rpmon
