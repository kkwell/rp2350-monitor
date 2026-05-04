#include "rpmon/util/json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rpmon {

namespace {

const char *find_key(const char *json, const char *key) {
    if (!json || !key) {
        return nullptr;
    }
    char pattern[40];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(pattern)) {
        return nullptr;
    }
    const char *p = json;
    while ((p = std::strstr(p, pattern)) != nullptr) {
        const char *after = p + n;
        while (*after && std::isspace(static_cast<unsigned char>(*after))) {
            ++after;
        }
        if (*after == ':') {
            ++after;
            while (*after && std::isspace(static_cast<unsigned char>(*after))) {
                ++after;
            }
            return after;
        }
        p += n;
    }
    return nullptr;
}

} // namespace

bool json_get_string(const char *json, const char *key, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';
    const char *p = find_key(json, key);
    if (!p || *p != '"') {
        return false;
    }
    ++p;
    size_t pos = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char esc = *p++;
            switch (esc) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            default:
                c = esc;
                break;
            }
        }
        if (pos + 1 >= out_len) {
            return false;
        }
        out[pos++] = c;
    }
    if (*p != '"') {
        return false;
    }
    out[pos] = '\0';
    return true;
}

bool json_get_int(const char *json, const char *key, int &out) {
    const char *p = find_key(json, key);
    if (!p) {
        return false;
    }
    char *end = nullptr;
    long value = strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool json_get_uint32(const char *json, const char *key, uint32_t &out) {
    const char *p = find_key(json, key);
    if (!p) {
        return false;
    }
    char *end = nullptr;
    unsigned long value = strtoul(p, &end, 10);
    if (end == p) {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool json_get_bool(const char *json, const char *key, bool &out) {
    const char *p = find_key(json, key);
    if (!p) {
        return false;
    }
    if (std::strncmp(p, "true", 4) == 0) {
        out = true;
        return true;
    }
    if (std::strncmp(p, "false", 5) == 0) {
        out = false;
        return true;
    }
    return false;
}

void json_escape(const char *in, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    size_t pos = 0;
    while (in && *in && pos + 1 < out_len) {
        char c = *in++;
        const char *replacement = nullptr;
        switch (c) {
        case '"':
            replacement = "\\\"";
            break;
        case '\\':
            replacement = "\\\\";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            break;
        }
        if (replacement) {
            while (*replacement && pos + 1 < out_len) {
                out[pos++] = *replacement++;
            }
        } else {
            out[pos++] = c;
        }
    }
    out[pos] = '\0';
}

} // namespace rpmon
