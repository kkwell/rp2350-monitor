#include "rpmon/core/event_bus.h"

#include <cstdio>
#include <cstring>

#include "pico/time.h"
#include "rpmon/util/hex.h"
#include "rpmon/util/json.h"

namespace rpmon {

void EventBus::add_sink(LineSink *sink) {
    if (!sink || sink_count_ >= 4) {
        return;
    }
    sinks_[sink_count_++] = sink;
}

void EventBus::publish_response(LineSink &sink, bool ok, const char *cmd, const char *message, const char *extra_json) {
    char escaped[160];
    json_escape(message ? message : "", escaped, sizeof(escaped));
    static char line[4800];
    snprintf(line, sizeof(line), "{\"type\":\"resp\",\"ok\":%s,\"cmd\":\"%s\",\"msg\":\"%s\"%s%s}",
             ok ? "true" : "false",
             cmd ? cmd : "",
             escaped,
             extra_json ? "," : "",
             extra_json ? extra_json : "");
    sink.send_line(line);
}

void EventBus::publish_status(const char *component, const char *message, const char *extra_json) {
    uint32_t seq = ++sequence_;
    char escaped[160];
    json_escape(message ? message : "", escaped, sizeof(escaped));
    char line[kEventLineMax];
    snprintf(line, sizeof(line), "{\"type\":\"status\",\"seq\":%lu,\"ts_us\":%llu,\"component\":\"%s\",\"msg\":\"%s\"%s%s}",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long long>(time_us_64()),
             component ? component : "system",
             escaped,
             extra_json ? "," : "",
             extra_json ? extra_json : "");
    enqueue_line(line, seq, false, 0);
    broadcast(line);
}

void EventBus::publish_data(int channel_id, ProtocolType proto, const char *direction, const uint8_t *data, size_t len) {
    if (!data && len > 0) {
        publish_error("buffer", "data event had null payload");
        return;
    }
    size_t offset = 0;
    do {
        size_t chunk = len - offset;
        if (chunk > kMaxPayloadBytes) {
            chunk = kMaxPayloadBytes;
        }
        char hex[kMaxPayloadBytes * 2 + 1];
        bytes_to_hex(data ? data + offset : nullptr, chunk, hex, sizeof(hex));
        uint32_t seq = ++sequence_;
        char line[kEventLineMax];
        snprintf(line, sizeof(line),
                 "{\"type\":\"event\",\"seq\":%lu,\"ts_us\":%llu,\"channel\":%d,\"proto\":\"%s\",\"dir\":\"%s\",\"len\":%u,\"offset\":%u,\"hex\":\"%s\"}",
                 static_cast<unsigned long>(seq),
                 static_cast<unsigned long long>(time_us_64()),
                 channel_id,
                 protocol_name(proto),
                 direction ? direction : "data",
                 static_cast<unsigned>(chunk),
                 static_cast<unsigned>(offset),
                 hex);
        enqueue_line(line, seq, true, chunk);
        broadcast(line);
        offset += chunk;
    } while (offset < len);
}

void EventBus::publish_error(const char *component, const char *message) {
    publish_status(component, message, "\"level\":\"error\"");
}

size_t EventBus::replay_buffered(LineSink &sink, size_t max_count, uint32_t since_seq) const {
    if (max_count == 0 || max_count > kEventReplayMax) {
        max_count = kEventReplayMax;
    }
    size_t sent = 0;
    size_t start = (queue_head_ + kEventQueueCapacity - queue_count_) % kEventQueueCapacity;
    for (size_t i = 0; i < queue_count_ && sent < max_count; ++i) {
        const BufferedLine &entry = queue_[(start + i) % kEventQueueCapacity];
        if (entry.seq <= since_seq || entry.len == 0) {
            continue;
        }
        if (!sink.send_line(entry.line)) {
            break;
        }
        ++sent;
    }
    return sent;
}

void EventBus::stats_json(char *out, size_t out_len) const {
    snprintf(out, out_len,
             "\"buffers\":{\"event_capacity\":%u,\"event_line_max\":%u,\"event_depth\":%u,\"event_max_depth\":%u,\"oldest_seq\":%lu,\"newest_seq\":%lu,\"total_events\":%lu,\"data_events\":%lu,\"data_bytes\":%lu,\"dropped_events\":%lu,\"dropped_bytes\":%lu,\"overflow_notices\":%lu}",
             static_cast<unsigned>(kEventQueueCapacity),
             static_cast<unsigned>(kEventLineMax),
             static_cast<unsigned>(queue_count_),
             static_cast<unsigned>(queue_max_depth_),
             static_cast<unsigned long>(oldest_seq()),
             static_cast<unsigned long>(newest_seq()),
             static_cast<unsigned long>(total_events_),
             static_cast<unsigned long>(data_events_),
             static_cast<unsigned long>(data_bytes_),
             static_cast<unsigned long>(dropped_events_),
             static_cast<unsigned long>(dropped_bytes_),
             static_cast<unsigned long>(overflow_notices_));
}

void EventBus::enqueue_line(const char *line, uint32_t seq, bool data, size_t payload_len, bool allow_overflow_notice) {
    if (!line) {
        return;
    }
    bool should_notice = false;
    if (queue_count_ == kEventQueueCapacity) {
        BufferedLine &dropped = queue_[queue_head_];
        ++dropped_events_;
        dropped_bytes_ += dropped.payload_len;
        queue_head_ = (queue_head_ + 1) % kEventQueueCapacity;
        --queue_count_;
        if (allow_overflow_notice &&
            (dropped_events_ == 1 || dropped_events_ >= last_overflow_notice_drop_count_ + 16)) {
            should_notice = true;
        }
    }

    BufferedLine &entry = queue_[queue_head_];
    std::strncpy(entry.line, line, sizeof(entry.line) - 1);
    entry.line[sizeof(entry.line) - 1] = '\0';
    entry.len = static_cast<uint16_t>(std::strlen(entry.line));
    entry.seq = seq;
    entry.data = data;
    entry.payload_len = static_cast<uint16_t>(payload_len);
    queue_head_ = (queue_head_ + 1) % kEventQueueCapacity;
    ++queue_count_;
    if (queue_count_ > queue_max_depth_) {
        queue_max_depth_ = queue_count_;
    }
    ++total_events_;
    if (data) {
        ++data_events_;
        data_bytes_ += static_cast<uint32_t>(payload_len);
    }
    if (should_notice) {
        publish_overflow_notice();
    }
}

void EventBus::publish_overflow_notice() {
    if (publishing_overflow_notice_) {
        return;
    }
    publishing_overflow_notice_ = true;
    last_overflow_notice_drop_count_ = dropped_events_;
    ++overflow_notices_;
    uint32_t seq = ++sequence_;
    char line[kEventLineMax];
    snprintf(line, sizeof(line),
             "{\"type\":\"status\",\"seq\":%lu,\"ts_us\":%llu,\"component\":\"buffer\",\"msg\":\"event queue overflow\",\"level\":\"warning\",\"dropped_events\":%lu,\"dropped_bytes\":%lu,\"capacity\":%u}",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long long>(time_us_64()),
             static_cast<unsigned long>(dropped_events_),
             static_cast<unsigned long>(dropped_bytes_),
             static_cast<unsigned>(kEventQueueCapacity));
    enqueue_line(line, seq, false, 0, false);
    broadcast(line);
    publishing_overflow_notice_ = false;
}

void EventBus::broadcast(const char *line) const {
    for (size_t i = 0; i < sink_count_; ++i) {
        if (sinks_[i]) {
            sinks_[i]->send_line(line);
        }
    }
}

uint32_t EventBus::oldest_seq() const {
    if (queue_count_ == 0) {
        return 0;
    }
    size_t start = (queue_head_ + kEventQueueCapacity - queue_count_) % kEventQueueCapacity;
    return queue_[start].seq;
}

uint32_t EventBus::newest_seq() const {
    if (queue_count_ == 0) {
        return 0;
    }
    size_t newest = (queue_head_ + kEventQueueCapacity - 1) % kEventQueueCapacity;
    return queue_[newest].seq;
}

} // namespace rpmon
