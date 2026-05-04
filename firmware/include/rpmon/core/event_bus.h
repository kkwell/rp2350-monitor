#pragma once

#include <cstddef>
#include <cstdint>

#include "rpmon/config.h"

namespace rpmon {

class LineSink {
public:
    virtual ~LineSink() = default;
    virtual bool send_line(const char *line) = 0;
};

class EventBus {
public:
    void add_sink(LineSink *sink);
    void publish_response(LineSink &sink, bool ok, const char *cmd, const char *message, const char *extra_json = nullptr);
    void publish_status(const char *component, const char *message, const char *extra_json = nullptr);
    void publish_data(int channel_id, ProtocolType proto, const char *direction, const uint8_t *data, size_t len);
    void publish_error(const char *component, const char *message);
    size_t replay_buffered(LineSink &sink, size_t max_count, uint32_t since_seq, int channel_id = -1) const;
    void release_channel(int channel_id);
    void stats_json(char *out, size_t out_len) const;

private:
    struct BufferedLine {
        char line[kEventLineMax] = {};
        uint32_t seq = 0;
        uint16_t len = 0;
        uint16_t payload_len = 0;
        bool data = false;
    };

    struct ChannelQueue {
        int channel_id = 0;
        BufferedLine queue[kChannelEventQueueCapacity];
        size_t head = 0;
        size_t count = 0;
        size_t max_depth = 0;
        uint32_t dropped_events = 0;
        uint32_t dropped_bytes = 0;
    };

    void enqueue_line(const char *line, uint32_t seq, bool data, size_t payload_len, bool allow_overflow_notice = true);
    void enqueue_channel_line(int channel_id, const char *line, uint32_t seq, size_t payload_len);
    void publish_overflow_notice();
    void broadcast(const char *line) const;
    uint32_t oldest_seq() const;
    uint32_t newest_seq() const;
    uint32_t queue_oldest_seq(const BufferedLine *queue, size_t capacity, size_t head, size_t count) const;
    uint32_t queue_newest_seq(const BufferedLine *queue, size_t capacity, size_t head, size_t count) const;
    ChannelQueue *channel_queue_for(int channel_id);
    const ChannelQueue *channel_queue_for(int channel_id) const;

    LineSink *sinks_[4] = {};
    size_t sink_count_ = 0;
    uint32_t sequence_ = 0;
    BufferedLine queue_[kEventQueueCapacity];
    size_t queue_head_ = 0;
    size_t queue_count_ = 0;
    size_t queue_max_depth_ = 0;
    uint32_t total_events_ = 0;
    uint32_t data_events_ = 0;
    uint32_t dropped_events_ = 0;
    uint32_t overflow_notices_ = 0;
    uint32_t dropped_bytes_ = 0;
    uint32_t data_bytes_ = 0;
    uint32_t last_overflow_notice_drop_count_ = 0;
    bool publishing_overflow_notice_ = false;
    ChannelQueue channel_queues_[kMaxChannels];
};

} // namespace rpmon
