#pragma once

#include <cstddef>
#include <cstdint>

#include "lwip/err.h"
#include "rpmon/core/channel_manager.h"
#include "rpmon/core/event_bus.h"
#include "rpmon/core/logic_analyzer.h"
#include "rpmon/net/wifi_manager.h"

struct tcp_pcb;
struct pbuf;

namespace rpmon {

class HttpServer {
public:
    HttpServer(WifiManager &wifi, ChannelManager &channels, LogicAnalyzer &logic, EventBus &events, uint16_t port);
    bool start();
    void poll();

private:
    struct Client {
        HttpServer *server = nullptr;
        tcp_pcb *pcb = nullptr;
        char request[1536] = {};
        size_t request_len = 0;
    };

    static err_t accept_cb(void *arg, tcp_pcb *newpcb, err_t err);
    static err_t recv_cb(void *arg, tcp_pcb *tpcb, pbuf *p, err_t err);
    static void err_cb(void *arg, err_t err);
    static err_t poll_cb(void *arg, tcp_pcb *tpcb);

    Client *free_client();
    void close_client(Client &client);
    void reset_request(Client &client);
    void append_request(Client &client, const char *data, size_t len);
    bool request_ready(const Client &client) const;
    void handle_request(Client &client);
    void handle_form_action(Client &client, const char *path, const char *body);
    void send_page(Client &client, const char *message);
    void send_status_json(Client &client);
    void send_response(Client &client, int code, const char *reason, const char *content_type, const char *body);
    void build_status_json(char *out, size_t out_len) const;
    void build_profile_rows(char *out, size_t out_len) const;
    void build_profile_options(char *out, size_t out_len) const;
    void build_scan_options(char *out, size_t out_len) const;
    bool form_value(const char *body, const char *key, char *out, size_t out_len) const;
    bool form_slot(const char *body, const char *key, uint8_t &slot) const;
    void url_decode(const char *src, size_t len, char *out, size_t out_len) const;
    void html_escape(const char *src, char *out, size_t out_len) const;

    WifiManager &wifi_;
    ChannelManager &channels_;
    LogicAnalyzer &logic_;
    EventBus &events_;
    uint16_t port_;
    tcp_pcb *listen_pcb_ = nullptr;
    Client clients_[4];
};

} // namespace rpmon
