#pragma once

#include <cstdint>

#include "lwip/ip_addr.h"

struct udp_pcb;
struct pbuf;

namespace rpmon {

class DnsServer {
public:
    bool start();
    void stop();

private:
    static void recv_cb(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port);
    void handle_packet(udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port);
    udp_pcb *pcb_ = nullptr;
};

} // namespace rpmon
