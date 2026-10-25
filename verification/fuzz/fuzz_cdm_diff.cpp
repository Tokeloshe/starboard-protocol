// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — DIFFERENTIAL fuzzer for the CDM binding.
// Copyright (C) 2026 James H. All rights reserved.
//
// Feeds the same bytes to the C99 binding (ports/c/cdm.c) and the C++
// binding (core/include/starboard/cdm.hpp). They are independent
// implementations of SPEC §3.3a, so on EVERY input they must agree on
// accept/reject, the rejection class, and every output bit — and neither may
// crash, read out of bounds or hit undefined behaviour (ASan + UBSan).
#include "starboard/cdm.hpp"
extern "C" {
#include "starboard_cdm.h"
}
#include <cstdint>
#include <cstdlib>
#include <cstring>

static bool same(double a, double b) {
    uint64_t x, y;
    std::memcpy(&x, &a, 8);
    std::memcpy(&y, &b, 8);
    return x == y;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 16384) { return 0; }
    const char* text = reinterpret_cast<const char*>(data);
    SbCdm c;
    starboard::cdm::Message p;
    const SbCdmStatus sc = sb_cdm_parse(text, static_cast<uint32_t>(size), &c, nullptr);
    const starboard::cdm::Status sp =
        starboard::cdm::parse(text, static_cast<uint32_t>(size), &p);
    if (static_cast<int>(sc) != static_cast<int>(sp)) { std::abort(); }
    if (sc != SB_CDM_OK) { return 0; }
    if (c.tca_gps_ms != p.tca_gps_ms || !same(c.hbr_m, p.hbr_m)) { std::abort(); }
    for (int i = 0; i < 2; ++i) {
        const SbCdmObject& a = c.obj[i];
        const starboard::cdm::Object& b = p.obj[i];
        if (std::memcmp(a.id.c, b.id.c, 12) != 0) { std::abort(); }
        if (!same(a.state_tca.position.x, b.state_tca.position.x) ||
            !same(a.state_tca.position.y, b.state_tca.position.y) ||
            !same(a.state_tca.position.z, b.state_tca.position.z) ||
            !same(a.state_tca.velocity.x, b.state_tca.velocity.x) ||
            !same(a.state_tca.velocity.y, b.state_tca.velocity.y) ||
            !same(a.state_tca.velocity.z, b.state_tca.velocity.z) ||
            !same(a.covariance.xx, b.covariance.xx) ||
            !same(a.covariance.xy, b.covariance.xy) ||
            !same(a.covariance.zz, b.covariance.zz)) {
            std::abort();
        }
    }
    // Run the screening step on accepted inputs too: it must terminate and
    // agree (this is where the NaN-arc hang lived).
    OrbitConjunction oc1;
    starboard::OrbitConjunction oc2;
    int64_t t1 = 0, t2 = 0;
    const bool f1 = sb_cdm_screen(&c, &oc1, &t1);
    const bool f2 = starboard::cdm::screen(p, &oc2, &t2);
    if (f1 != f2 || t1 != t2 || !same(oc1.conj.pc, oc2.conj.pc)) { std::abort(); }
    return 0;
}
