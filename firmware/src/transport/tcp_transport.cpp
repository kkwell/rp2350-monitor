#include "rpmon/transport/tcp_transport.h"

#include <cstring>

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"

namespace rpmon {

TcpTransport::TcpTransport(CommandProcessor &processor, uint16_t port) : processor_(processor), port_(port) {}

bool TcpTransport::start() {
    cyw43_arch_lwip_begin();
    listen_pcb_ = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!listen_pcb_) {
        cyw43_arch_lwip_end();
        return false;
    }
    err_t err = tcp_bind(listen_pcb_, IP_ANY_TYPE, port_);
    if (err != ERR_OK) {
        tcp_close(listen_pcb_);
        listen_pcb_ = nullptr;
        cyw43_arch_lwip_end();
        return false;
    }
    listen_pcb_ = tcp_listen_with_backlog(listen_pcb_, 1);
    tcp_arg(listen_pcb_, this);
    tcp_accept(listen_pcb_, accept_cb);
    cyw43_arch_lwip_end();
    return true;
}

void TcpTransport::poll() {}

bool TcpTransport::send_line(const char *line) {
    if (!line || !client_pcb_) {
        return false;
    }
    bool ok = false;
    cyw43_arch_lwip_begin();
    if (client_pcb_) {
        size_t len = std::strlen(line);
        if (tcp_sndbuf(client_pcb_) > len + 2) {
            err_t err = tcp_write(client_pcb_, line, len, TCP_WRITE_FLAG_COPY);
            if (err == ERR_OK) {
                err = tcp_write(client_pcb_, "\n", 1, TCP_WRITE_FLAG_COPY);
            }
            if (err == ERR_OK) {
                tcp_output(client_pcb_);
                ok = true;
            }
        }
    }
    cyw43_arch_lwip_end();
    return ok;
}

err_t TcpTransport::accept_cb(void *arg, tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || !arg) {
        return ERR_VAL;
    }
    auto *transport = static_cast<TcpTransport *>(arg);
    transport->close_client();
    transport->client_pcb_ = newpcb;
    tcp_arg(newpcb, transport);
    tcp_recv(newpcb, recv_cb);
    tcp_err(newpcb, err_cb);
    tcp_poll(newpcb, poll_cb, 4);
    transport->send_line("{\"type\":\"status\",\"component\":\"tcp\",\"msg\":\"client connected\"}");
    return ERR_OK;
}

err_t TcpTransport::recv_cb(void *arg, tcp_pcb *tpcb, pbuf *p, err_t err) {
    if (!arg) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_VAL;
    }
    auto *transport = static_cast<TcpTransport *>(arg);
    if (!p) {
        transport->close_client();
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
    for (pbuf *q = p; q; q = q->next) {
        auto *bytes = static_cast<const char *>(q->payload);
        for (uint16_t i = 0; i < q->len; ++i) {
            transport->push_char(bytes[i]);
        }
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

void TcpTransport::err_cb(void *arg, err_t err) {
    (void)err;
    if (!arg) {
        return;
    }
    auto *transport = static_cast<TcpTransport *>(arg);
    transport->client_pcb_ = nullptr;
    transport->pos_ = 0;
}

err_t TcpTransport::poll_cb(void *arg, tcp_pcb *tpcb) {
    (void)tpcb;
    return arg ? ERR_OK : ERR_ABRT;
}

void TcpTransport::close_client() {
    if (!client_pcb_) {
        return;
    }
    tcp_arg(client_pcb_, nullptr);
    tcp_recv(client_pcb_, nullptr);
    tcp_err(client_pcb_, nullptr);
    tcp_poll(client_pcb_, nullptr, 0);
    err_t err = tcp_close(client_pcb_);
    if (err != ERR_OK) {
        tcp_abort(client_pcb_);
    }
    client_pcb_ = nullptr;
    pos_ = 0;
}

void TcpTransport::push_char(char c) {
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        line_[pos_] = '\0';
        if (pos_ > 0) {
            processor_.handle_line(line_, *this);
        }
        pos_ = 0;
        return;
    }
    if (pos_ + 1 < sizeof(line_)) {
        line_[pos_++] = c;
    } else {
        pos_ = 0;
    }
}

} // namespace rpmon
