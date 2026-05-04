#include "rpmon/transport/http_server.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "rpmon/config.h"

namespace rpmon {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

const char *find_header_value(const char *headers, const char *name) {
    size_t name_len = std::strlen(name);
    const char *line = headers;
    while (line && *line) {
        const char *next = std::strstr(line, "\r\n");
        size_t line_len = next ? static_cast<size_t>(next - line) : std::strlen(line);
        if (line_len > name_len && line[name_len] == ':') {
            bool match = true;
            for (size_t i = 0; i < name_len; ++i) {
                char a = line[i];
                char b = name[i];
                if (a >= 'A' && a <= 'Z') {
                    a = static_cast<char>(a - 'A' + 'a');
                }
                if (b >= 'A' && b <= 'Z') {
                    b = static_cast<char>(b - 'A' + 'a');
                }
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match) {
                const char *value = line + name_len + 1;
                while (*value == ' ') {
                    ++value;
                }
                return value;
            }
        }
        if (!next) {
            break;
        }
        line = next + 2;
    }
    return nullptr;
}

void append_html(char *out, size_t out_len, size_t &pos, const char *fmt, ...) {
    if (pos >= out_len) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = std::vsnprintf(out + pos, out_len - pos, fmt, args);
    va_end(args);
    if (written > 0) {
        pos += static_cast<size_t>(written);
    }
}

} // namespace

HttpServer::HttpServer(WifiManager &wifi, ChannelManager &channels, EventBus &events, uint16_t port)
    : wifi_(wifi), channels_(channels), events_(events), port_(port) {}

bool HttpServer::start() {
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
    listen_pcb_ = tcp_listen_with_backlog(listen_pcb_, 4);
    tcp_arg(listen_pcb_, this);
    tcp_accept(listen_pcb_, accept_cb);
    cyw43_arch_lwip_end();
    return true;
}

void HttpServer::poll() {}

err_t HttpServer::accept_cb(void *arg, tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || !arg) {
        return ERR_VAL;
    }
    auto *server = static_cast<HttpServer *>(arg);
    Client *client = server->free_client();
    if (!client) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    client->server = server;
    client->pcb = newpcb;
    server->reset_request(*client);
    tcp_arg(newpcb, client);
    tcp_recv(newpcb, recv_cb);
    tcp_err(newpcb, err_cb);
    tcp_poll(newpcb, poll_cb, 4);
    return ERR_OK;
}

err_t HttpServer::recv_cb(void *arg, tcp_pcb *tpcb, pbuf *p, err_t err) {
    if (!arg) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_VAL;
    }
    auto *client = static_cast<Client *>(arg);
    HttpServer *server = client->server;
    if (!server) {
        if (p) {
            pbuf_free(p);
        }
        return ERR_VAL;
    }
    if (!p) {
        server->close_client(*client);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
    for (pbuf *q = p; q; q = q->next) {
        server->append_request(*client, static_cast<const char *>(q->payload), q->len);
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    if (server->request_ready(*client)) {
        server->handle_request(*client);
    }
    return ERR_OK;
}

void HttpServer::err_cb(void *arg, err_t err) {
    (void)err;
    if (!arg) {
        return;
    }
    auto *client = static_cast<Client *>(arg);
    client->server = nullptr;
    client->pcb = nullptr;
    client->request_len = 0;
    client->request[0] = '\0';
}

err_t HttpServer::poll_cb(void *arg, tcp_pcb *tpcb) {
    (void)tpcb;
    return arg ? ERR_OK : ERR_ABRT;
}

HttpServer::Client *HttpServer::free_client() {
    for (Client &client : clients_) {
        if (!client.pcb) {
            return &client;
        }
    }
    return nullptr;
}

void HttpServer::close_client(Client &client) {
    if (!client.pcb) {
        return;
    }
    tcp_arg(client.pcb, nullptr);
    tcp_recv(client.pcb, nullptr);
    tcp_err(client.pcb, nullptr);
    tcp_poll(client.pcb, nullptr, 0);
    err_t err = tcp_close(client.pcb);
    if (err != ERR_OK) {
        tcp_abort(client.pcb);
    }
    client.server = nullptr;
    client.pcb = nullptr;
    reset_request(client);
}

void HttpServer::reset_request(Client &client) {
    client.request_len = 0;
    client.request[0] = '\0';
}

void HttpServer::append_request(Client &client, const char *data, size_t len) {
    if (!data || len == 0) {
        return;
    }
    if (client.request_len + len >= sizeof(client.request)) {
        reset_request(client);
        return;
    }
    std::memcpy(client.request + client.request_len, data, len);
    client.request_len += len;
    client.request[client.request_len] = '\0';
}

bool HttpServer::request_ready(const Client &client) const {
    const char *headers_end = std::strstr(client.request, "\r\n\r\n");
    if (!headers_end) {
        return false;
    }
    if (std::strncmp(client.request, "POST ", 5) != 0) {
        return true;
    }
    const char *value = find_header_value(client.request, "content-length");
    int content_len = value ? std::atoi(value) : 0;
    size_t body_offset = static_cast<size_t>((headers_end + 4) - client.request);
    return client.request_len >= body_offset + static_cast<size_t>(content_len);
}

void HttpServer::handle_request(Client &client) {
    char method[8] = {};
    char target[96] = {};
    if (std::sscanf(client.request, "%7s %95s", method, target) != 2) {
        send_response(client, 400, "Bad Request", "text/plain", "bad request\n");
        return;
    }

    char path[96] = {};
    size_t i = 0;
    while (target[i] && target[i] != '?' && i + 1 < sizeof(path)) {
        path[i] = target[i];
        ++i;
    }
    path[i] = '\0';

    const char *headers_end = std::strstr(client.request, "\r\n\r\n");
    const char *body = headers_end ? headers_end + 4 : "";

    if (std::strcmp(method, "GET") == 0) {
        if (std::strcmp(path, "/") == 0 || std::strcmp(path, "/index.html") == 0) {
            send_page(client, "Ready");
        } else if (std::strcmp(path, "/api/status") == 0) {
            send_status_json(client);
        } else if (std::strcmp(path, "/generate_204") == 0 ||
                   std::strcmp(path, "/gen_204") == 0 ||
                   std::strcmp(path, "/hotspot-detect.html") == 0 ||
                   std::strcmp(path, "/library/test/success.html") == 0 ||
                   std::strcmp(path, "/connecttest.txt") == 0 ||
                   std::strcmp(path, "/ncsi.txt") == 0 ||
                   std::strcmp(path, "/fwlink") == 0) {
            send_page(client, "Open this page to configure RP2350 Monitor");
        } else if (std::strcmp(path, "/favicon.ico") == 0) {
            send_response(client, 204, "No Content", "text/plain", "");
        } else {
            send_response(client, 404, "Not Found", "text/plain", "not found\n");
        }
        return;
    }

    if (std::strcmp(method, "POST") == 0) {
        handle_form_action(client, path, body);
        return;
    }

    send_response(client, 405, "Method Not Allowed", "text/plain", "method not allowed\n");
}

void HttpServer::handle_form_action(Client &client, const char *path, const char *body) {
    if (std::strcmp(path, "/wifi-clear") == 0) {
        uint8_t slot = 0;
        form_slot(body, "slot", slot);
        bool ok = wifi_.clear_profile(slot, true);
        send_page(client, ok ? "Wi-Fi profile cleared" : "Failed to clear Wi-Fi profile");
        return;
    }

    if (std::strcmp(path, "/wifi-save") == 0 || std::strcmp(path, "/wifi-connect") == 0) {
        uint8_t slot = 0;
        form_slot(body, "slot", slot);
        char selected_ssid[33] = {};
        char manual_ssid[33] = {};
        char password[65] = {};
        form_value(body, "ssid", selected_ssid, sizeof(selected_ssid));
        form_value(body, "manual_ssid", manual_ssid, sizeof(manual_ssid));
        form_value(body, "password", password, sizeof(password));
        const char *ssid = manual_ssid[0] ? manual_ssid : selected_ssid;
        if (!ssid || ssid[0] == '\0') {
            send_page(client, "Missing Wi-Fi SSID");
            return;
        }
        if (!wifi_.set_credentials(ssid, password, true, slot)) {
            send_page(client, "Failed to save Wi-Fi credentials");
            return;
        }
        if (std::strcmp(path, "/wifi-connect") == 0) {
            wifi_.schedule_station_connect(slot);
            send_page(client, "Wi-Fi connect scheduled. AP will close if station mode connects.");
        } else {
            send_page(client, "Wi-Fi credentials saved");
        }
        return;
    }

    if (std::strcmp(path, "/connect") == 0) {
        uint8_t slot = wifi_.active_profile_index();
        form_slot(body, "slot", slot);
        if (!wifi_.select_profile(slot, true)) {
            send_page(client, "Selected Wi-Fi slot is empty");
            return;
        }
        wifi_.schedule_station_connect(slot);
        send_page(client, "Wi-Fi connect scheduled. AP will close if station mode connects.");
        return;
    }

    if (std::strcmp(path, "/scan") == 0) {
        char err[120] = {};
        bool ok = wifi_.scan_wifi(err, sizeof(err));
        send_page(client, ok ? "Wi-Fi scan started" : err);
        return;
    }

    if (std::strcmp(path, "/ap") == 0) {
        wifi_.schedule_ap();
        send_page(client, "AP mode scheduled");
        return;
    }

    if (std::strcmp(path, "/uart-loopback") == 0) {
        ChannelConfig config{};
        config.id = 7;
        config.type = ProtocolType::Uart;
        config.instance = 0;
        config.baud = 115200;
        config.loopback = true;
        config.pins.tx = 0;
        config.pins.rx = 1;
        char err[120] = {};
        const uint8_t payload[] = {'R', 'P', 'M', 'O', 'N', '-', 'L', 'O', 'O', 'P', 'B', 'A', 'C', 'K'};
        bool ok = channels_.configure(config, err, sizeof(err)) &&
                  channels_.start(config.id, err, sizeof(err)) &&
                  channels_.write(config.id, payload, sizeof(payload), err, sizeof(err));
        channels_.poll();
        channels_.stop(config.id, err, sizeof(err));
        send_page(client, ok ? "UART0 loopback frame sent" : err);
        return;
    }

    send_response(client, 404, "Not Found", "text/plain", "not found\n");
}

void HttpServer::send_page(Client &client, const char *message) {
    static char status[4300];
    static char escaped_status[6500];
    static char profile_rows[1200];
    static char profile_options[520];
    static char scan_options[1400];
    build_status_json(status, sizeof(status));
    html_escape(status, escaped_status, sizeof(escaped_status));
    build_profile_rows(profile_rows, sizeof(profile_rows));
    build_profile_options(profile_options, sizeof(profile_options));
    build_scan_options(scan_options, sizeof(scan_options));

    char escaped_message[180];
    html_escape(message ? message : "", escaped_message, sizeof(escaped_message));

    const char *refresh_script = wifi_.scan_active() ? "<script>setTimeout(function(){location.href='/'},2500)</script>" : "";
    static char body[12000];
    std::snprintf(body, sizeof(body),
                  "<!doctype html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>RP2350 Monitor</title>"
                  "<style>body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;margin:0;background:#f6f7f8;color:#182026}"
                  "main{max-width:760px;margin:0 auto;padding:18px}h1{font-size:24px;margin:10px 0 14px}"
                  "section{background:#fff;border:1px solid #d7dde2;border-radius:8px;padding:14px;margin:12px 0}"
                  "label{display:block;font-size:13px;margin:10px 0 4px}input,select{box-sizing:border-box;width:100%%;padding:10px;border:1px solid #b8c1ca;border-radius:6px;font-size:16px;background:white}"
                  "button,a.btn{display:inline-block;margin:8px 8px 0 0;padding:10px 12px;border:0;border-radius:6px;background:#1463ff;color:white;text-decoration:none;font-size:15px}"
                  "button.secondary{background:#425466}.msg{padding:10px;background:#eef6ff;border:1px solid #bdd7ff;border-radius:6px}"
                  "table{width:100%%;border-collapse:collapse;font-size:13px}td,th{text-align:left;border-bottom:1px solid #e4e8ec;padding:7px 4px}.bad{color:#b42318}"
                  "pre{white-space:pre-wrap;word-break:break-word;background:#101820;color:#e8f0f2;border-radius:6px;padding:10px;font-size:12px}</style></head>"
                  "<body><main><h1>RP2350 Monitor</h1><p class='msg'>%s</p>%s"
                  "<section><h2>Saved Wi-Fi</h2><table><thead><tr><th>Slot</th><th>SSID</th><th>Status</th></tr></thead><tbody>%s</tbody></table></section>"
                  "<section><h2>Configure Wi-Fi</h2><form method='post' action='/wifi-save'>"
                  "<label>Save Slot</label><select name='slot'>%s</select>"
                  "<label>Scanned SSID</label><select name='ssid'>%s</select>"
                  "<label>Manual SSID</label><input name='manual_ssid' maxlength='32' autocomplete='off'>"
                  "<label>Password</label><input name='password' type='password' maxlength='64'>"
                  "<button type='submit'>Save</button><button formaction='/wifi-connect' type='submit'>Save and Connect</button>"
                  "<button class='secondary' formaction='/wifi-clear' type='submit'>Clear Slot</button>"
                  "<button class='secondary' formaction='/scan' type='submit'>Scan Wi-Fi</button></form></section>"
                  "<section><h2>Actions</h2><form method='post'>"
                  "<label>Saved Connection</label><select name='slot'>%s</select>"
                  "<button formaction='/connect' type='submit'>Connect Saved Wi-Fi</button>"
                  "<button class='secondary' formaction='/ap' type='submit'>AP Mode</button>"
                  "<button class='secondary' formaction='/uart-loopback' type='submit'>UART Loopback</button>"
                  "<a class='btn' href='/'>Refresh</a></form></section>"
                  "<section><h2>Status</h2><pre>%s</pre></section>"
                  "<section><h2>API</h2><pre>GET /api/status\nTCP JSON port: %u\nAP: %s / %s</pre></section>"
                  "</main></body></html>",
                  escaped_message,
                  refresh_script,
                  profile_rows,
                  profile_options,
                  scan_options,
                  profile_options,
                  escaped_status,
                  static_cast<unsigned>(kTcpControlPort),
                  kApSsidPrefix,
                  kApPassword);
    send_response(client, 200, "OK", "text/html; charset=utf-8", body);
}

void HttpServer::send_status_json(Client &client) {
    static char body[4300];
    build_status_json(body, sizeof(body));
    send_response(client, 200, "OK", "application/json", body);
}

void HttpServer::send_response(Client &client, int code, const char *reason, const char *content_type, const char *body) {
    if (!client.pcb) {
        return;
    }
    char header[240];
    size_t body_len = std::strlen(body ? body : "");
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %u\r\n"
                  "Connection: close\r\n"
                  "Cache-Control: no-store\r\n\r\n",
                  code,
                  reason ? reason : "OK",
                  content_type ? content_type : "text/plain",
                  static_cast<unsigned>(body_len));

    cyw43_arch_lwip_begin();
    tcp_write(client.pcb, header, std::strlen(header), TCP_WRITE_FLAG_COPY);
    if (body_len > 0) {
        tcp_write(client.pcb, body, body_len, TCP_WRITE_FLAG_COPY);
    }
    tcp_output(client.pcb);
    cyw43_arch_lwip_end();
    close_client(client);
}

void HttpServer::build_status_json(char *out, size_t out_len) const {
    static char wifi[1800];
    static char channels[1600];
    char buffers[420];
    wifi_.status_json(wifi, sizeof(wifi));
    channels_.list_json(channels, sizeof(channels));
    events_.stats_json(buffers, sizeof(buffers));
    std::snprintf(out, out_len,
                  "{\"type\":\"status\",\"version\":\"%s\",\"http_port\":%u,%s,%s,%s}",
                  kFirmwareVersion,
                  static_cast<unsigned>(port_),
                  wifi,
                  channels,
                  buffers);
}

void HttpServer::build_profile_rows(char *out, size_t out_len) const {
    size_t pos = 0;
    for (uint8_t i = 0; i < kMaxWifiProfiles; ++i) {
        WifiProfile profile{};
        bool valid = wifi_.get_profile(i, profile);
        char ssid[96];
        char error[180];
        html_escape(valid ? profile.ssid : "-", ssid, sizeof(ssid));
        html_escape(wifi_.profile_error(i), error, sizeof(error));
        const bool active = wifi_.active_profile_index() == i;
        if (error[0]) {
            append_html(out, out_len, pos,
                        "<tr><td>%u%s</td><td>%s</td><td class='bad'>%s</td></tr>",
                        static_cast<unsigned>(i),
                        active ? " *" : "",
                        ssid,
                        error);
        } else {
            append_html(out, out_len, pos,
                        "<tr><td>%u%s</td><td>%s</td><td>%s</td></tr>",
                        static_cast<unsigned>(i),
                        active ? " *" : "",
                        ssid,
                        valid ? "saved" : "empty");
        }
    }
}

void HttpServer::build_profile_options(char *out, size_t out_len) const {
    size_t pos = 0;
    for (uint8_t i = 0; i < kMaxWifiProfiles; ++i) {
        WifiProfile profile{};
        bool valid = wifi_.get_profile(i, profile);
        char label[96];
        char escaped[120];
        std::snprintf(label, sizeof(label), "Slot %u - %s", static_cast<unsigned>(i), valid ? profile.ssid : "empty");
        html_escape(label, escaped, sizeof(escaped));
        append_html(out, out_len, pos,
                    "<option value=\"%u\"%s>%s</option>",
                    static_cast<unsigned>(i),
                    wifi_.active_profile_index() == i ? " selected" : "",
                    escaped);
    }
}

void HttpServer::build_scan_options(char *out, size_t out_len) const {
    size_t pos = 0;
    append_html(out, out_len, pos, "<option value=\"\">Manual SSID</option>");
    size_t count = wifi_.scan_result_count();
    if (count == 0) {
        append_html(out, out_len, pos, "<option value=\"\" disabled>No scan results</option>");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        char raw_ssid[33] = {};
        int16_t rssi = 0;
        uint16_t channel = 0;
        uint8_t auth = 0;
        if (!wifi_.get_scan_result(i, raw_ssid, sizeof(raw_ssid), rssi, channel, auth)) {
            continue;
        }
        (void)auth;
        char ssid[96];
        html_escape(raw_ssid, ssid, sizeof(ssid));
        append_html(out, out_len, pos,
                    "<option value=\"%s\">%s (%d dBm, ch %u)</option>",
                    ssid,
                    ssid,
                    static_cast<int>(rssi),
                    static_cast<unsigned>(channel));
    }
}

bool HttpServer::form_value(const char *body, const char *key, char *out, size_t out_len) const {
    if (!body || !key || !out || out_len == 0) {
        return false;
    }
    size_t key_len = std::strlen(key);
    const char *p = body;
    while (*p) {
        const char *eq = std::strchr(p, '=');
        if (!eq) {
            break;
        }
        const char *end = std::strchr(eq + 1, '&');
        if (!end) {
            end = body + std::strlen(body);
        }
        if (static_cast<size_t>(eq - p) == key_len && std::strncmp(p, key, key_len) == 0) {
            url_decode(eq + 1, static_cast<size_t>(end - (eq + 1)), out, out_len);
            return true;
        }
        p = *end ? end + 1 : end;
    }
    out[0] = '\0';
    return false;
}

bool HttpServer::form_slot(const char *body, const char *key, uint8_t &slot) const {
    char text[8] = {};
    if (!form_value(body, key, text, sizeof(text)) || text[0] == '\0') {
        return false;
    }
    int value = std::atoi(text);
    if (value < 0 || value >= static_cast<int>(kMaxWifiProfiles)) {
        return false;
    }
    slot = static_cast<uint8_t>(value);
    return true;
}

void HttpServer::url_decode(const char *src, size_t len, char *out, size_t out_len) const {
    if (!out || out_len == 0) {
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 1 < out_len; ++i) {
        if (src[i] == '+') {
            out[pos++] = ' ';
        } else if (src[i] == '%' && i + 2 < len) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[pos++] = static_cast<char>((hi << 4) | lo);
                i += 2;
            }
        } else {
            out[pos++] = src[i];
        }
    }
    out[pos] = '\0';
}

void HttpServer::html_escape(const char *src, char *out, size_t out_len) const {
    if (!out || out_len == 0) {
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; src && src[i] && pos + 1 < out_len; ++i) {
        const char *rep = nullptr;
        switch (src[i]) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        default:
            break;
        }
        if (rep) {
            size_t rep_len = std::strlen(rep);
            if (pos + rep_len >= out_len) {
                break;
            }
            std::memcpy(out + pos, rep, rep_len);
            pos += rep_len;
        } else {
            out[pos++] = src[i];
        }
    }
    out[pos] = '\0';
}

} // namespace rpmon
