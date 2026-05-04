#include "rpmon/net/dhcp_server.h"

#include <cstring>

#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"

namespace rpmon {

namespace {

constexpr uint8_t kDhcpBootRequest = 1;
constexpr uint8_t kDhcpBootReply = 2;
constexpr uint8_t kDhcpDiscover = 1;
constexpr uint8_t kDhcpOffer = 2;
constexpr uint8_t kDhcpRequest = 3;
constexpr uint8_t kDhcpAck = 5;
constexpr uint32_t kMagicCookie = 0x63825363;
constexpr uint32_t kServerIp = 0xc0a80401; // 192.168.4.1
constexpr uint32_t kClientIpBase = 0xc0a80414; // 192.168.4.20
constexpr uint32_t kSubnet = 0xffffff00;
constexpr uint32_t kLeaseSeconds = 3600;

struct DhcpPacket {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} __attribute__((packed));

uint8_t option_message_type(const DhcpPacket &pkt) {
    if (pkt.magic != PP_HTONL(kMagicCookie)) {
        return 0;
    }
    const uint8_t *opt = pkt.options;
    const uint8_t *end = pkt.options + sizeof(pkt.options);
    while (opt < end && *opt != 255) {
        uint8_t code = *opt++;
        if (code == 0) {
            continue;
        }
        if (opt >= end) {
            break;
        }
        uint8_t len = *opt++;
        if (opt + len > end) {
            break;
        }
        if (code == 53 && len == 1) {
            return opt[0];
        }
        opt += len;
    }
    return 0;
}

void add_option_u8(uint8_t *&p, uint8_t code, uint8_t value) {
    *p++ = code;
    *p++ = 1;
    *p++ = value;
}

void add_option_u32(uint8_t *&p, uint8_t code, uint32_t value) {
    *p++ = code;
    *p++ = 4;
    uint32_t net = PP_HTONL(value);
    std::memcpy(p, &net, sizeof(net));
    p += 4;
}

} // namespace

bool DhcpServer::start() {
    if (pcb_) {
        return true;
    }
    cyw43_arch_lwip_begin();
    pcb_ = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb_) {
        cyw43_arch_lwip_end();
        return false;
    }
    ip_set_option(pcb_, SOF_BROADCAST);
    err_t err = udp_bind(pcb_, IP_ANY_TYPE, 67);
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

void DhcpServer::stop() {
    if (!pcb_) {
        return;
    }
    cyw43_arch_lwip_begin();
    udp_remove(pcb_);
    pcb_ = nullptr;
    std::memset(leases_, 0, sizeof(leases_));
    cyw43_arch_lwip_end();
}

void DhcpServer::recv_cb(void *arg, udp_pcb *pcb, pbuf *p, const ip_addr_t *addr, uint16_t port) {
    (void)addr;
    (void)port;
    auto *server = static_cast<DhcpServer *>(arg);
    if (server && p) {
        server->handle_packet(pcb, p);
    }
    if (p) {
        pbuf_free(p);
    }
}

void DhcpServer::handle_packet(udp_pcb *pcb, pbuf *p) {
    if (p->tot_len < 240) {
        return;
    }
    DhcpPacket req{};
    pbuf_copy_partial(p, &req, sizeof(req), 0);
    if (req.op != kDhcpBootRequest) {
        return;
    }
    uint8_t msg = option_message_type(req);
    if (msg != kDhcpDiscover && msg != kDhcpRequest) {
        return;
    }
    uint32_t client_ip = lease_ip_for(req.chaddr);

    auto *reply_pbuf = pbuf_alloc(PBUF_TRANSPORT, sizeof(DhcpPacket), PBUF_RAM);
    if (!reply_pbuf) {
        return;
    }
    auto *reply = static_cast<DhcpPacket *>(reply_pbuf->payload);
    std::memset(reply, 0, sizeof(DhcpPacket));
    reply->op = kDhcpBootReply;
    reply->htype = 1;
    reply->hlen = 6;
    reply->xid = req.xid;
    reply->flags = req.flags;
    reply->yiaddr = PP_HTONL(client_ip);
    reply->siaddr = PP_HTONL(kServerIp);
    std::memcpy(reply->chaddr, req.chaddr, sizeof(reply->chaddr));
    reply->magic = PP_HTONL(kMagicCookie);

    uint8_t *opt = reply->options;
    add_option_u8(opt, 53, msg == kDhcpDiscover ? kDhcpOffer : kDhcpAck);
    add_option_u32(opt, 54, kServerIp);
    add_option_u32(opt, 51, kLeaseSeconds);
    add_option_u32(opt, 1, kSubnet);
    add_option_u32(opt, 3, kServerIp);
    add_option_u32(opt, 6, kServerIp);
    add_option_u32(opt, 28, 0xc0a804ff); // 192.168.4.255
    *opt++ = 255;

    ip_addr_t dest;
    IP_ADDR4(&dest, 255, 255, 255, 255);
    udp_sendto(pcb, reply_pbuf, &dest, 68);
    pbuf_free(reply_pbuf);
}

uint32_t DhcpServer::lease_ip_for(const uint8_t *mac) {
    if (!mac) {
        return kClientIpBase;
    }
    for (const Lease &lease : leases_) {
        if (lease.used && std::memcmp(lease.mac, mac, sizeof(lease.mac)) == 0) {
            return lease.ip;
        }
    }
    for (size_t i = 0; i < sizeof(leases_) / sizeof(leases_[0]); ++i) {
        Lease &lease = leases_[i];
        if (!lease.used) {
            lease.used = true;
            std::memcpy(lease.mac, mac, sizeof(lease.mac));
            lease.ip = kClientIpBase + static_cast<uint32_t>(i);
            return lease.ip;
        }
    }
    uint8_t suffix = static_cast<uint8_t>(20 + (mac[5] % 80));
    return 0xc0a80400u | suffix;
}

} // namespace rpmon
