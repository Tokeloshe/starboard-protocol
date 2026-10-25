// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.

// ============================================================================
// The EXTENDED cross-language transcript (suite v2, informative).
//
// The normative v1 transcript (runner.cpp --bits) pins the verdict path. This
// one cross-checks the surfaces the C99 port gained afterwards — CW maneuver
// planning, the mirror+phase and quarter-period returns, the multi-body
// cascade and the broad-phase candidate SET — against the C++ reference, bit
// for bit. ports/c/ext_bits.c mirrors this file draw-for-draw.
//
// Scenario construction uses ONLY + - * / sqrt (no libm trig), so this
// transcript is also a clean cross-ARCHITECTURE probe: a divergence can only
// come from the library under test, never from the host's libm.
// Golden: conformance/expected_bits_ext.txt (tools/bitcheck.sh checks it).
// ============================================================================

#include "starboard/starboard.hpp"
#include "starboard/cdm.hpp"

#include "ext_cdm_gen.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace starboard;

namespace {

constexpr uint64_t EXT_EPOCH_MS = 1401000000000ull;

uint64_t bits_of(double x) {
    uint64_t u;
    std::memcpy(&u, &x, sizeof(u));
    return u;
}

struct Fold {
    uint64_t h = 14695981039346656037ull;
    void add(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFFull;
            h *= 1099511628211ull;
        }
    }
    void add(double d) { add(bits_of(d)); }
    void add(const Vec3& v) { add(v.x); add(v.y); add(v.z); }
};

void emit(const char* name, const Fold& f) {
    std::printf("%-14s %016llX\n", name, static_cast<unsigned long long>(f.h));
}

// --- scenario construction (mirrors ports/c/ext_scenarios.h) --------------
double ext_uniform(uint64_t* s) {
    *s = *s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<double>(*s >> 11) / 9007199254740992.0;
}
double ext_sym(uint64_t* s) { return 2.0 * ext_uniform(s) - 1.0; }

Vec3 ext_dir(uint64_t* s) {
    const double x = ext_sym(s);
    const double y = ext_sym(s);
    const double z = ext_sym(s) + 0.125;
    return Vec3(x, y, z).normalized();
}

CraftState ext_orbit_state(uint64_t* s) {
    const double r = canon::R_EARTH_M + 350.0e3 + 900.0e3 * ext_uniform(s);
    const Vec3 rhat = ext_dir(s);
    const Vec3 any = ext_dir(s);
    const Vec3 t = any - rhat * any.dot(rhat);
    const double v = std::sqrt(canon::MU_EARTH_M3S2 / r) *
                     (1.0 + 0.002 * ext_sym(s));
    CraftState st;
    st.position = rhat * r;
    st.velocity = t.normalized() * v;
    return st;
}

CraftState ext_linear_at(const CraftState& st, double t) {
    CraftState r;
    r.position = st.position + st.velocity * t;
    r.velocity = st.velocity;
    return r;
}

void ext_encounter(uint64_t* s, CraftState* a, CraftState* b, double* t_ca) {
    const CraftState a0 = ext_orbit_state(s);
    const Vec3 rhat = a0.position.normalized();
    const Vec3 vhat = a0.velocity.normalized();
    const Vec3 chat = rhat.cross(vhat);
    const double cx = ext_sym(s);
    const double sx = ext_sym(s);
    const Vec3 dir = (vhat * cx + chat * sx).normalized();
    const double miss = 1500.0 * ext_uniform(s);
    const Vec3 mdir = ext_dir(s);
    *t_ca = 100.0 + 800.0 * ext_uniform(s);
    CraftState b0;
    b0.position = a0.position + mdir * miss;
    b0.velocity = dir * a0.velocity.norm();
    *a = ext_linear_at(a0, -*t_ca);
    *b = ext_linear_at(b0, -*t_ca);
}

void ext_descriptor(uint64_t* s, int w, int k, CraftDescriptor* d) {
    char id[16];
    const double u_tier = ext_uniform(s);
    const double u_cap = ext_uniform(s);
    const double u_trust = ext_uniform(s);
    const double var = 1.0e3 + ext_uniform(s) * 1.0e6;
    std::snprintf(id, sizeof(id), "20%02d-%03d%c", w % 27, (w * 7) % 1000,
                  static_cast<char>('A' + k));
    d->object_id = make_object_id(id);
    d->tier = static_cast<Tier>(static_cast<int>(u_tier * 4.0));
    d->can_maneuver = (u_cap < 0.7);
    d->trust = static_cast<TrustLevel>(static_cast<int>(u_trust * 3.0));
    d->hard_body_radius_m = canon::DEFAULT_HBR_M;
    d->state = canonicalize(ext_orbit_state(s));
    d->covariance = Sym33::diagonal(var);
}

// --- the transcript sections -------------------------------------------------
void ext_maneuver() {
    Fold fm, fr;
    uint64_t s = 0xC0FFEEull;
    for (int i = 0; i < 2048; ++i) {
        CraftState a, b;
        double t_ca;
        ext_encounter(&s, &a, &b, &t_ca);
        const Conjunction c = assess(a, b, Sym33::diagonal(4.0e4),
                                     Sym33::diagonal(9.0e4), 20.0, 6000.0);
        const CraftState a_tca = ext_linear_at(canonicalize(a), c.t_ca);
        const CraftState b_tca = ext_linear_at(canonicalize(b), c.t_ca);
        const GiveWayPlan pa =
            plan_give_way(canonicalize(a), a_tca, c, true, canon::R_SAFE_M);
        Conjunction cb = c;
        cb.miss_vector = c.miss_vector * -1.0;
        cb.rel_velocity = c.rel_velocity * -1.0;
        const GiveWayPlan pb =
            plan_give_way(canonicalize(b), b_tca, cb, false, canon::R_SAFE_M);
        fm.add(static_cast<uint64_t>(pa.required ? 1u : 0u));
        fm.add(pa.dv_eci);
        fm.add(pa.dv_hill);
        fm.add(pa.dv_mag);
        fm.add(pa.displacement_m);
        fm.add(pb.dv_eci);
        fm.add(pb.displacement_m);

        const double n = mean_motion(canonicalize(a));
        const MirrorReturnPlan mr =
            plan_mirror_return(pa.dv_hill, n, canon::RETURN_PHASING_ORBITS);
        fr.add(mr.mirror_dv_hill);
        fr.add(mr.final_dv_hill);
        fr.add(mr.wait_s);
        fr.add(mr.transfer_s);
        fr.add(mr.total_dv);
        HillState off;
        off.r.x = ext_sym(&s) * 800.0;
        off.r.y = ext_sym(&s) * 3000.0;
        off.r.z = ext_sym(&s) * 400.0;
        off.v.x = ext_sym(&s) * 0.5;
        off.v.y = ext_sym(&s) * 0.5;
        off.v.z = ext_sym(&s) * 0.5;
        const ReturnPlan rp = plan_return(off, n);
        fr.add(rp.dv1_hill);
        fr.add(rp.dv2_hill);
        fr.add(rp.transfer_s);
        fr.add(rp.total_dv);
        const HillState x = cw_propagate(off, n, 0.37 * rp.transfer_s);
        fr.add(x.r);
        fr.add(x.v);
    }
    emit("maneuver", fm);
    emit("returns", fr);
}

void ext_window() {
    Fold fw;
    uint64_t s = 0x3B0D7ull;
    for (int w = 0; w < 512; ++w) {
        CraftDescriptor craft[8];
        WindowEntry win[12];
        WindowVerdict out[12];
        const uint32_t n_craft =
            3u + static_cast<uint32_t>(ext_uniform(&s) * 6.0);
        const uint32_t n_ent =
            2u + static_cast<uint32_t>(ext_uniform(&s) * 11.0);
        for (uint32_t k = 0u; k < n_craft; ++k) {
            ext_descriptor(&s, w, static_cast<int>(k), &craft[k]);
        }
        for (uint32_t k = 0u; k < n_ent; ++k) {
            const uint32_t ia =
                static_cast<uint32_t>(ext_uniform(&s) * static_cast<double>(n_craft));
            uint32_t ib =
                static_cast<uint32_t>(ext_uniform(&s) * static_cast<double>(n_craft));
            if (ia == ib) { ib = (ia + 1u) % n_craft; }
            win[k].a = ia;
            win[k].b = ib;
            win[k].tca_epoch_ms =
                EXT_EPOCH_MS + static_cast<uint64_t>(ext_uniform(&s) * 90000.0);
            win[k].a_tca = craft[ia].state;
            win[k].b_tca = craft[ib].state;
        }
        const uint32_t got = resolve_window(craft, n_craft, win, n_ent, out);
        fw.add(static_cast<uint64_t>(got));
        for (uint32_t k = 0u; k < got; ++k) {
            fw.add(out[k].conjunction_id);
            fw.add(static_cast<uint64_t>(out[k].give_way_index));
            fw.add(static_cast<uint64_t>(out[k].stand_on_index));
            fw.add(static_cast<uint64_t>(out[k].deciding_rule));
            fw.add(static_cast<uint64_t>(out[k].escalated ? 1u : 0u));
            fw.add(static_cast<uint64_t>(out[k].advisory ? 1u : 0u));
        }
    }
    emit("window", fw);
}

void ext_broadphase() {
    constexpr uint32_t N = 240u;
    std::vector<CraftState> objs(N);
    std::vector<broadphase::Band> bands(N);
    std::vector<uint32_t> active(N);
    std::vector<broadphase::Pair> out(N * N / 2u);
    uint64_t s = 0xB0A2Dull;
    for (uint32_t i = 0u; i < N; ++i) { objs[i] = ext_orbit_state(&s); }
    bool ovf = false;
    const uint32_t nc = broadphase::radial_band_pairs(
        objs.data(), N, bands.data(), active.data(), out.data(),
        static_cast<uint32_t>(out.size()), &ovf);
    std::vector<uint8_t> flag(N * N, 0u);
    for (uint32_t i = 0u; i < nc; ++i) { flag[out[i].i * N + out[i].j] = 1u; }
    Fold fb;
    fb.add(static_cast<uint64_t>(nc));
    fb.add(static_cast<uint64_t>(ovf ? 1u : 0u));
    for (uint32_t i = 0u; i < N; ++i) {
        for (uint32_t j = i + 1u; j < N; ++j) {
            if (flag[i * N + j] != 0u) {
                fb.add((static_cast<uint64_t>(i) << 32) | j);
            }
        }
    }
    emit("broadphase", fb);
}

const char* const EXT_DECIMALS[] = {
    "0.1", "1E22", "1e-22", "1E23", "1e-23", "999999999999999",
    "9999999999999999", "-0", "+.5", ".5e1", "5.", "1e+0003", "1e00001",
    "2570.097065", "-1.4372E+03", "000000000000000000123.45", "1.2.3", "e5",
    "4.835E-05", "6.000000000000000000", "123456789012345.0000", "", "-",
    "7e", "0.000000000000000000001", "3.14159265358979"};

void ext_cdm() {
    Fold fd, fc;
    for (const char* d : EXT_DECIMALS) {
        for (int sc = -3; sc <= 3; sc += 3) {
            double v = 0.0;
            const cdm::Status st = cdm::decimal_to_double(
                d, static_cast<uint32_t>(std::strlen(d)), sc, &v);
            fd.add(static_cast<uint64_t>(st));
            fd.add(v);
        }
    }
    emit("decimal", fd);
    uint64_t s = 0xCD3B1ull;
    static char text[8192];
    for (int k = 0; k < 400; ++k) {
        const uint32_t n = gen_cdm(&s, text, sizeof(text));
        cdm::Message m;
        const cdm::Status st = cdm::parse(text, n, &m);
        fc.add(static_cast<uint64_t>(st));
        if (st != cdm::Status::OK) { continue; }
        for (const cdm::Object& o : m.obj) {
            for (int j = 0; j < 12; ++j) {
                fc.add(static_cast<uint64_t>(static_cast<uint8_t>(o.id.c[j])));
            }
            fc.add(static_cast<uint64_t>(o.frame));
            fc.add(static_cast<uint64_t>(o.maneuverable));
            fc.add(static_cast<uint64_t>(o.object_type));
            fc.add(o.state_tca.position);
            fc.add(o.state_tca.velocity);
            fc.add(o.covariance.xx); fc.add(o.covariance.xy);
            fc.add(o.covariance.xz); fc.add(o.covariance.yy);
            fc.add(o.covariance.yz); fc.add(o.covariance.zz);
        }
        fc.add(static_cast<uint64_t>(m.tca_gps_ms));
        fc.add(m.cdm_miss_m);
        fc.add(m.hbr_m);
        OrbitConjunction oc;
        int64_t tca = 0;
        fc.add(static_cast<uint64_t>(cdm::screen(m, &oc, &tca) ? 1u : 0u));
        fc.add(oc.conj.t_ca);
        fc.add(oc.conj.miss_distance);
        fc.add(oc.conj.pc);
        fc.add(static_cast<uint64_t>(tca));
    }
    emit("cdm-binding", fc);
}

} // namespace

int main() {
    if (!canon::arithmetic_profile_ok()) {
        std::fprintf(stderr, "FATAL: FP contraction detected (SPEC §3.5)\n");
        return 2;
    }
    std::printf("starboard-ext-transcript v2 ruleset %08X\n", canon::RULESET_ID);
    ext_maneuver();
    ext_window();
    ext_broadphase();
    ext_cdm();
    return 0;
}
