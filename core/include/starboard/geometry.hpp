#pragma once

// ============================================================================
// Starboard — Layer 0: conjunction geometry
//
// Vec3 and the relative-motion screening math. Seeded from the SolarLens
// physics/orbital_mechanics.hpp Vec3 and formation-control collision screening.
//
// Everything here is computed from the *relative* state of two craft, so both
// satellites derive identical values from symmetric inputs.
// ============================================================================

#include <cmath>
#include <cstdint>

namespace starboard {

struct Vec3 {
    double x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double norm() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const { double n = norm(); return (n > 0) ? *this / n : Vec3{}; }
};

// Published screening constants (see docs/DESIGN.md §6). All conformant
// implementations MUST use these values.
namespace limits {
constexpr double R_SCREEN_M = 1000.0;   // declare a conjunction below this miss distance
constexpr double R_SAFE_M   = 500.0;    // required miss distance after a give-way maneuver
constexpr double OVERTAKE_COS = 0.3827; // cos(67.5°): approach cone defining "overtaking"
}

// Minimal orbital state a craft needs to know about itself and a neighbour.
struct CraftState {
    Vec3 position;   // ECI position (m)
    Vec3 velocity;   // ECI velocity (m/s)
};

// Result of Layer-0 screening between two craft (A = self, B = neighbour).
struct Conjunction {
    bool   detected;      // true if a conjunction is declared within the horizon
    double t_ca;          // time to closest approach (s); valid only if detected
    double miss_distance; // predicted miss distance at TCA (m)
    Vec3   miss_vector;   // relative position at TCA (B relative to A)
};

// Screen a single pair using locally-linear relative motion over `horizon_s`.
// Symmetric: screen(A,B) and screen(B,A) agree on t_ca and miss_distance.
inline Conjunction screen(const CraftState& a, const CraftState& b,
                          double horizon_s,
                          double r_screen = limits::R_SCREEN_M) {
    const Vec3 dr = b.position - a.position;   // relative position
    const Vec3 dv = b.velocity - a.velocity;   // relative velocity
    const double dv2 = dv.dot(dv);

    Conjunction c;
    if (dv2 < 1e-9) {
        // No relative motion: closest approach is now.
        c.t_ca = 0.0;
        c.miss_vector = dr;
    } else {
        c.t_ca = -(dr.dot(dv)) / dv2;
        if (c.t_ca < 0.0) c.t_ca = 0.0;             // already past closest approach
        if (c.t_ca > horizon_s) c.t_ca = horizon_s; // clamp to screening window
        c.miss_vector = dr + dv * c.t_ca;
    }
    c.miss_distance = c.miss_vector.norm();
    c.detected = (c.t_ca >= 0.0 && c.t_ca <= horizon_s &&
                  c.miss_distance < r_screen);
    return c;
}

} // namespace starboard
