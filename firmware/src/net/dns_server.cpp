#include "rpmon/net/dns_server.h"

#include <cstring>

#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"

namespace rpmon {

namespace {

constexpr uint32_t kPortalIp = 0xc0a80401; // 192.168.4.1
constexpr uint16_t kDnsPort = 53;
constexpr uint16_t kTypeA = 1;
constexpr uint16_t kClassIn = 1;

uint16_t read_u16(const uint8_t *buf, size_t off) {
    return static_cast<uint16_t>((buf[off] << 8) | buf[off + 1]);
}

void write_u16(uint8_t *buf, size_t &pos, uint16_t value) {
    buf[pos++] = static_cast<uint8_t>(value >> 8);
    buf[pos++] = static_cast<uint8_t>(value & 0xff);
}

void write_u32(uint8_t *buf, size_t &pos, uint32_t value) {
    buf[pos++] = static_cast<uint8_t>(value >> 24);
    buf[pos++] = static_cast<uint8_t>((value >> 16) & 0xff);
    buf[pos++] = static_cast<uint8_t>((value >> 8) & 0xff);
    buf[pos++] = static_cast<uint8_t>(value & 0xff);
}

} // namespace

bool DnsServer::start() {
    if (pcb_) {
        return true;
    }
    cyw43_arch_lwip_begin();
    pcb_ = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb_) {
        cyw43_arch_lwip_end();
        return false;
    }
    err_t err = udp_bind(pcb_, IP_ANY_TYPE, kDnsPort);
    if (err != ERR_OK) {
        udp_remove(pcb_);
        pcb_ = nullptr;
        cyw43_arch_lwip_end();
        return false;
    }
    udp_recv(pcb_, recv_cb, this);
    cyw43_arch_lwip_end();
    return true;
}

void DnsServer::stop() {
    if (!pcb_) {
        return;
    }
    cyw43_arch_lwip_begin();
    udp_remove(pcb_);
    pcb_ = nullptr;
    cyw43_arch_lwip_end();
}

void DnsServer::recv_cb(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port) {
    auto *server = static_cast<DnsServer *>(arg);
    if (server && p && addr) {
        server->handle_packet(pcb, p, addr, port);
    }
    if (p) {
        pbuf_free(p);
    }
}

void DnsServer::handle_packet(udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port) {
    if (p->tot_len < 12 || p->tot_len > 320) {
        return;
    }

    uint8_t req[320] = {};
    pbuf_copy_partial(p, req, p->tot_len, 0);
    if (read_u16(req, 4) == 0) {
        return;
    }

    size_t q_end = 12;
    while (q_end < p->tot_len && req[q_end] != 0) {
        q_end += static_cast<size_t>(req[q_end]) + 1;
    }
    if (q_end + 5 > p->tot_len) {
        return;
    }
    q_end += 1;
    uint16_t q_type = read_u16(req, q_end);
    uint16_t q_class = read_u16(req, q_end + 2);
    size_t question_len = q_end + 4 - 12;

    uint8_t resp[384] = {};
    std::memcpy(resp, req, 2);
    size_t pos = 2;
    write_u16(resp, pos, q_type == kTypeA && q_class == kClassIn ? 0x8180 : 0x8183);
    write_u16(resp, pos, 1);
    write_u16(resp, pos, q_type == kTypeA && q_class == kClassIn ? 1 : 0);
    write_u16(resp, pos, 0);
    write_u16(resp, pos, 0);
    std::memcpy(resp + pos, req + 12, question_len);
    pos += question_len;

    if (q_type == kTypeA && q_class == kClassIn) {
        write_u16(resp, pos, 0xc00c);
        write_u16(resp, pos, kTypeA);
        write_u16(resp, pos, kClassIn);
        write_u32(resp, pos, 60);
        write_u16(resp, pos, 4);
        write_u32(resp, pos, kPortalIp);
    }

    auto *reply = pbuf_alloc(PBUF_TRANSPORT, pos, PBUF_RAM);
    if (!reply) {
        return;
    }
    std::memcpy(reply->payload, resp, pos);
    udp_sendto(pcb, reply, addr, port);
    pbuf_free(reply);
}

} // namespace rpmon
