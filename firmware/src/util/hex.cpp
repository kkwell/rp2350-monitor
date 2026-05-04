#include "rpmon/util/hex.h"

namespace rpmon {

namespace {

int nibble(char c) {
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

} // namespace

bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_cap, size_t &out_len) {
    out_len = 0;
    if (!hex) {
        return true;
    }
    while (*hex == ' ' || *hex == '\t') {
        ++hex;
    }
    while (*hex) {
        if (*hex == ' ' || *hex == ':' || *hex == '-' || *hex == '_') {
            ++hex;
            continue;
        }
        int hi = nibble(*hex++);
        if (hi < 0 || !*hex) {
            return false;
        }
        int lo = nibble(*hex++);
        if (lo < 0) {
            return false;
        }
        if (out_len >= out_cap) {
            return false;
        }
        out[out_len++] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_len) {
    static constexpr char kHex[] = "0123456789abcdef";
    if (!out || out_len == 0) {
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < out_len; ++i) {
        out[pos++] = kHex[(data[i] >> 4) & 0x0f];
        out[pos++] = kHex[data[i] & 0x0f];
    }
    out[pos] = '\0';
}

} // namespace rpmon

