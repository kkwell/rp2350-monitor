#pragma once

#include <cstdint>

#include "lwip/ip_addr.h"

struct udp_pcb;
struct pbuf;

namespace rpmon {

class DhcpServer {
public:
    bool start();
    void stop();

private:
    struct Lease {
        bool used = false;
        uint8_t mac[6] = {};
        uint32_t ip = 0;
    };

    static void recv_cb(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port);
    void handle_packet(udp_pcb *pcb, pbuf *p);
    uint32_t lease_ip_for(const uint8_t *mac);
    udp_pcb *pcb_ = nullptr;
    Lease leases_[8];
};

} // namespace rpmon
