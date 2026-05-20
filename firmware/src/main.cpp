#include "rpmon/config.h"
#include "rpmon/core/channel_manager.h"
#include "rpmon/core/command_processor.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/logic_analyzer.h"
#include "rpmon/core/pin_manager.h"
#include "rpmon/net/wifi_manager.h"
#include "rpmon/probe/debug_probe.h"
#include "rpmon/transport/http_server.h"
#include "rpmon/transport/tcp_transport.h"
#include "rpmon/transport/usb_transport.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

int main() {
    stdio_init_all();
    sleep_ms(750);

    static rpmon::EventBus events;
    static rpmon::PinManager pins;
    static rpmon::WifiManager wifi(events);
    static rpmon::ChannelManager channels(pins, events);
    static rpmon::LogicAnalyzer logic(pins);
    static rpmon::DebugProbe probe(pins);
    static rpmon::CommandProcessor processor(wifi, channels, logic, probe, pins, events);
    static rpmon::UsbTransport usb(processor);
    static rpmon::TcpTransport tcp(processor, rpmon::kTcpControlPort);
    static rpmon::HttpServer http(wifi, channels, logic, probe, events, rpmon::kHttpPort);

    events.add_sink(&usb);

    bool wifi_ok = wifi.init();
    if (wifi_ok) {
        wifi.start_ap();
    }

    if (wifi_ok && tcp.start()) {
        events.add_sink(&tcp);
    } else {
        events.publish_error("tcp", "tcp server failed to start");
    }

    if (!wifi_ok || !http.start()) {
        events.publish_error("http", "http server failed to start");
    }

    char boot_extra[96];
    snprintf(boot_extra, sizeof(boot_extra), "\"version\":\"%s\",\"tcp_port\":%u",
             rpmon::kFirmwareVersion,
             static_cast<unsigned>(rpmon::kTcpControlPort));
    events.publish_status("system", "rp2350 monitor ready", boot_extra);

    uint32_t last_led_ms = 0;
    bool led = false;
    while (true) {
        usb.poll();
        tcp.poll();
        http.poll();
        wifi.poll();
        channels.poll();
        logic.poll(events);
        probe.poll_usb(events);

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (wifi_ok && now - last_led_ms >= 500) {
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
            last_led_ms = now;
        }
        sleep_ms(2);
    }
}
