#pragma once

#include <cstddef>
#include <cstdint>

namespace rpmon {

bool json_get_string(const char *json, const char *key, char *out, size_t out_len);
bool json_get_int(const char *json, const char *key, int &out);
bool json_get_uint32(const char *json, const char *key, uint32_t &out);
bool json_get_bool(const char *json, const char *key, bool &out);
void json_escape(const char *in, char *out, size_t out_len);

} // namespace rpmon

