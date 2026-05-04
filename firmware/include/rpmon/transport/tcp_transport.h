#pragma once

#include "rpmon/core/command_processor.h"
#include "rpmon/core/event_bus.h"

#include "lwip/err.h"

struct tcp_pcb;
struct pbuf;

namespace rpmon {

class TcpTransport final : public LineSink {
public:
    TcpTransport(CommandProcessor &processor, uint16_t port);
    bool start();
    void poll();
    bool send_line(const char *line) override;

private:
    static err_t accept_cb(void *arg, tcp_pcb *newpcb, err_t err);
    static err_t recv_cb(void *arg, tcp_pcb *tpcb, pbuf *p, err_t err);
    static void err_cb(void *arg, err_t err);
    static err_t poll_cb(void *arg, tcp_pcb *tpcb);
    void close_client();
    void push_char(char c);

    CommandProcessor &processor_;
    uint16_t port_;
    tcp_pcb *listen_pcb_ = nullptr;
    tcp_pcb *client_pcb_ = nullptr;
    char line_[kLineBufferSize] = {};
    size_t pos_ = 0;
};

} // namespace rpmon
