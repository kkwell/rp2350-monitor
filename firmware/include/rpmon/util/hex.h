#pragma once

#include <cstddef>
#include <cstdint>

namespace rpmon {

bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_cap, size_t &out_len);
void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_len);

} // namespace rpmon

