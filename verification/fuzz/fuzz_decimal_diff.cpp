// SPDX-License-Identifier: AGPL-3.0-or-later
// Differential fuzzer: exact decimal ingest, C vs C++, and an EXACTNESS
// oracle — every accepted value must be the correctly rounded binary64 of
// the decimal, checked against glibc strtod (correctly rounded on glibc).
#include "starboard/cdm.hpp"
extern "C" {
#include "starboard_cdm.h"
}
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if ((size < 1) || (size > 64)) { return 0; }
    const int scale = static_cast<int>(data[0] % 7) - 3;
    const char* s = reinterpret_cast<const char*>(data + 1);
    const uint32_t n = static_cast<uint32_t>(size - 1);
    double a = 0.0, b = 0.0;
    const SbCdmStatus sa = sb_decimal_to_double(s, n, scale, &a);
    const starboard::cdm::Status sb = starboard::cdm::decimal_to_double(s, n, scale, &b);
    if (static_cast<int>(sa) != static_cast<int>(sb)) { std::abort(); }
    if (sa != SB_CDM_OK) { return 0; }
    if (std::memcmp(&a, &b, 8) != 0) { std::abort(); }
    char buf[96];
    std::memcpy(buf, s, n);
    buf[n] = '\0';
    if (n > 0 && buf[n - 1] == 'e') { return 0; }
    char tail[16];
    std::snprintf(tail, sizeof(tail), "e%d", scale);
    // Only build an oracle string when the input has no exponent of its own.
    for (uint32_t i = 0; i < n; ++i) { if (buf[i] == 'e' || buf[i] == 'E') { return 0; } }
    std::strcat(buf, tail);
    const double ref = std::strtod(buf, nullptr);
    if (std::memcmp(&ref, &a, 8) != 0) { std::abort(); }
    return 0;
}
