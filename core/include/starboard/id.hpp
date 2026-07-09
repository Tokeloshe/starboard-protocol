// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
#pragma once

// ============================================================================
// Starboard — canonical object identity (SPEC §3.2).
//
// A craft's identity is a fixed 12-byte field: the COSPAR international
// designator, uppercase ASCII, left-justified, NUL-padded. Identity is
// compared bytewise as unsigned values. This ordering is the root of the
// Canonical Pair Transform (SPEC §5.2): every pairwise computation in the
// protocol is performed on the (lo, hi) ordering of this comparison, which is
// what makes the agreement theorem hold by construction.
//
// POD, allocation-free, trivially copyable — safe for flight software.
// ============================================================================

#include <cstdint>

namespace starboard {

struct ObjectId {
    char c[12];
};

// Build a canonical id from a NUL-terminated string (uppercased, NUL-padded).
inline ObjectId make_object_id(const char* s) {
    ObjectId id;
    int i = 0;
    for (; i < 12 && s[i] != '\0'; ++i) {
        char ch = s[i];
        if ((ch >= 'a') && (ch <= 'z')) {
            ch = static_cast<char>(ch - ('a' - 'A'));
        }
        id.c[i] = ch;
    }
    for (; i < 12; ++i) {
        id.c[i] = '\0';
    }
    return id;
}

// Bytewise unsigned comparison (SPEC §3.2). Total order; no locale, no libc.
inline int id_compare(const ObjectId& a, const ObjectId& b) {
    for (int i = 0; i < 12; ++i) {
        const unsigned char x = static_cast<unsigned char>(a.c[i]);
        const unsigned char y = static_cast<unsigned char>(b.c[i]);
        if (x != y) {
            return (x < y) ? -1 : 1;
        }
    }
    return 0;
}

inline bool id_equal(const ObjectId& a, const ObjectId& b) {
    return id_compare(a, b) == 0;
}

} // namespace starboard
