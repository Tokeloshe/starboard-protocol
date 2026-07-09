// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — umbrella header. Header-only, dependency-free, allocation-free,
// exception-free. See docs/DESIGN.md for the specification this implements.
// ============================================================================

#include "canon.hpp"       // canonical constants, lattice, canon_exp/sincos
#include "id.hpp"          // canonical object identity
#include "geometry.hpp"    // Layer 0: encounter kernel geometry
#include "kepler.hpp"      // Layer 0: canonical two-body ephemeris
#include "covariance.hpp"  // Layer 0: Starboard-Pc
#include "screening.hpp"   // Layer 0: canonical conjunction search
#include "trust.hpp"       // adversarial model: assertions & downgrades
#include "rules.hpp"       // Layer 1: the Rules of the Orbital Road
#include "frames.hpp"      // Hill frame & Clohessy-Wiltshire dynamics
#include "maneuver.hpp"    // give-way burn & return-to-slot
#include "multibody.hpp"   // multi-craft window resolution
