// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — Wycheproof Ed25519 driver (C port AND C++ header).
// Copyright (C) 2026 James H. All rights reserved.
//
// Reads lines "PUBHEX MSGHEX SIGHEX" (MSGHEX may be "-" for empty) and
// prints "<c> <cpp>" (1 = accept, 0 = reject) per line.
#include "starboard/crypto/ed25519_verify.hpp"
extern "C" {
#include "starboard_registry.h"
}
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> unhex(const std::string& h) {
    std::vector<uint8_t> out;
    if (h == "-") { return out; }
    for (size_t i = 0; i + 1 < h.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
    }
    return out;
}

int main() {
    static char a[512], b[65536], c[512];
    while (std::scanf("%511s %65535s %511s", a, b, c) == 3) {
        const auto pk = unhex(a), msg = unhex(b), sig = unhex(c);
        int rc = 0, rp = 0;
        if (pk.size() == 32 && sig.size() == 64) {
            rc = sb_ed25519_verify(sig.data(), msg.empty() ? nullptr : msg.data(),
                                   msg.size(), pk.data()) ? 1 : 0;
            rp = starboard::crypto::ed25519_verify(
                     sig.data(), msg.empty() ? nullptr : msg.data(), msg.size(),
                     pk.data()) ? 1 : 0;
        }
        std::printf("%d %d\n", rc, rp);
    }
    return 0;
}
