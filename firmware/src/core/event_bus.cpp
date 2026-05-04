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
    static char line[7200];
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
        enqueue_channel_line(channel_id, line, seq, chunk);
        broadcast(line);
        offset += chunk;
    } while (offset < len);
}

void EventBus::publish_error(const char *component, const char *message) {
    publish_status(component, message, "\"level\":\"error\"");
}

size_t EventBus::replay_buffered(LineSink &sink, size_t max_count, uint32_t since_seq, int channel_id) const {
    if (max_count == 0 || max_count > kEventReplayMax) {
        max_count = kEventReplayMax;
    }
    const BufferedLine *queue = queue_;
    size_t capacity = kEventQueueCapacity;
    size_t head = queue_head_;
    size_t count = queue_count_;
    if (channel_id >= 0) {
        const ChannelQueue *channel = channel_queue_for(channel_id);
        if (!channel) {
            return 0;
        }
        queue = channel->queue;
        capacity = kChannelEventQueueCapacity;
        head = channel->head;
        count = channel->count;
    }
    size_t sent = 0;
    size_t start = (head + capacity - count) % capacity;
    for (size_t i = 0; i < count && sent < max_count; ++i) {
        const BufferedLine &entry = queue[(start + i) % capacity];
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

void EventBus::release_channel(int channel_id) {
    ChannelQueue *channel = channel_queue_for(channel_id);
    if (channel) {
        *channel = ChannelQueue{};
    }
}

void EventBus::stats_json(char *out, size_t out_len) const {
    int written = snprintf(out, out_len,
             "\"buffers\":{\"event_capacity\":%u,\"channel_event_capacity\":%u,\"event_line_max\":%u,\"event_depth\":%u,\"event_max_depth\":%u,\"oldest_seq\":%lu,\"newest_seq\":%lu,\"total_events\":%lu,\"data_events\":%lu,\"data_bytes\":%lu,\"dropped_events\":%lu,\"dropped_bytes\":%lu,\"overflow_notices\":%lu,\"channels\":[",
             static_cast<unsigned>(kEventQueueCapacity),
             static_cast<unsigned>(kChannelEventQueueCapacity),
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
    if (written < 0) {
        return;
    }
    size_t pos = static_cast<size_t>(written);
    bool first = true;
    for (const ChannelQueue &channel : channel_queues_) {
        if (channel.channel_id == 0) {
            continue;
        }
        written = snprintf(out + pos, pos < out_len ? out_len - pos : 0,
                           "%s{\"id\":%d,\"depth\":%u,\"max_depth\":%u,\"oldest_seq\":%lu,\"newest_seq\":%lu,\"dropped_events\":%lu,\"dropped_bytes\":%lu}",
                           first ? "" : ",",
                           channel.channel_id,
                           static_cast<unsigned>(channel.count),
                           static_cast<unsigned>(channel.max_depth),
                           static_cast<unsigned long>(queue_oldest_seq(channel.queue, kChannelEventQueueCapacity, channel.head, channel.count)),
                           static_cast<unsigned long>(queue_newest_seq(channel.queue, kChannelEventQueueCapacity, channel.head, channel.count)),
                           static_cast<unsigned long>(channel.dropped_events),
                           static_cast<unsigned long>(channel.dropped_bytes));
        if (written < 0) {
            break;
        }
        pos += static_cast<size_t>(written);
        first = false;
        if (pos >= out_len) {
            break;
        }
    }
    snprintf(out + (pos < out_len ? pos : out_len - 1), pos < out_len ? out_len - pos : 1, "]}");
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

void EventBus::enqueue_channel_line(int channel_id, const char *line, uint32_t seq, size_t payload_len) {
    ChannelQueue *channel = channel_queue_for(channel_id);
    if (!channel) {
        return;
    }
    if (channel->count == kChannelEventQueueCapacity) {
        BufferedLine &dropped = channel->queue[channel->head];
        ++channel->dropped_events;
        channel->dropped_bytes += dropped.payload_len;
        channel->head = (channel->head + 1) % kChannelEventQueueCapacity;
        --channel->count;
    }

    BufferedLine &entry = channel->queue[channel->head];
    std::strncpy(entry.line, line, sizeof(entry.line) - 1);
    entry.line[sizeof(entry.line) - 1] = '\0';
    entry.len = static_cast<uint16_t>(std::strlen(entry.line));
    entry.seq = seq;
    entry.data = true;
    entry.payload_len = static_cast<uint16_t>(payload_len);
    channel->head = (channel->head + 1) % kChannelEventQueueCapacity;
    ++channel->count;
    if (channel->count > channel->max_depth) {
        channel->max_depth = channel->count;
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
    return queue_oldest_seq(queue_, kEventQueueCapacity, queue_head_, queue_count_);
}

uint32_t EventBus::newest_seq() const {
    return queue_newest_seq(queue_, kEventQueueCapacity, queue_head_, queue_count_);
}

uint32_t EventBus::queue_oldest_seq(const BufferedLine *queue, size_t capacity, size_t head, size_t count) const {
    if (!queue || count == 0) {
        return 0;
    }
    size_t start = (head + capacity - count) % capacity;
    return queue[start].seq;
}

uint32_t EventBus::queue_newest_seq(const BufferedLine *queue, size_t capacity, size_t head, size_t count) const {
    if (!queue || count == 0) {
        return 0;
    }
    size_t newest = (head + capacity - 1) % capacity;
    return queue[newest].seq;
}

EventBus::ChannelQueue *EventBus::channel_queue_for(int channel_id) {
    if (channel_id <= 0) {
        return nullptr;
    }
    ChannelQueue *empty = nullptr;
    for (ChannelQueue &channel : channel_queues_) {
        if (channel.channel_id == channel_id) {
            return &channel;
        }
        if (!empty && channel.channel_id == 0) {
            empty = &channel;
        }
    }
    if (empty) {
        empty->channel_id = channel_id;
    }
    return empty;
}

const EventBus::ChannelQueue *EventBus::channel_queue_for(int channel_id) const {
    if (channel_id <= 0) {
        return nullptr;
    }
    for (const ChannelQueue &channel : channel_queues_) {
        if (channel.channel_id == channel_id) {
            return &channel;
        }
    }
    return nullptr;
}

} // namespace rpmon
