// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// ccsds.c — CCSDS 133.0-B-2 Space Packet framing for Starboard telemetry,
// plus the two integrity codes the flight layer uses: CRC-16/CCITT-FALSE
// (packet error control, as in CCSDS/ECSS PUS) and CRC-32 (IEEE 802.3,
// stored-input integrity).

#include "starboard_fsw.h"

uint16_t sb_crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    for (i = 0U; i < len; ++i)
    {
        uint32_t b;
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (b = 0U; b < 8U; ++b)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint32_t sb_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    for (i = 0U; i < len; ++i)
    {
        uint32_t b;
        crc ^= (uint32_t)data[i];
        for (b = 0U; b < 8U; ++b)
        {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

/* Primary header (6 B): version 000 | type 0 (TM) | secondary header 1 |
 * APID (11 b) | sequence flags 11 (unsegmented) | sequence count (14 b) |
 * data length (16 b, = bytes after the primary header - 1). Secondary
 * header: 8-byte big-endian GPS milliseconds. Trailer: CRC-16 over the
 * whole packet. Returns the packet length, or 0 if it does not fit. */
uint32_t sb_ccsds_tm_packet(uint8_t *buf, uint32_t cap, uint16_t apid,
                            uint16_t seq, int64_t time_gps_ms,
                            const uint8_t *payload, uint32_t plen)
{
    const uint32_t total = 6U + 8U + plen + 2U;
    const uint32_t dlen = total - 7U;
    uint32_t i;
    uint16_t crc;
    if ((total > cap) || (dlen > 0xFFFFU) || (apid > 0x7FFU))
    {
        return 0U;
    }
    buf[0] = (uint8_t)(0x08U | ((apid >> 8) & 0x07U));
    buf[1] = (uint8_t)(apid & 0xFFU);
    buf[2] = (uint8_t)(0xC0U | ((seq >> 8) & 0x3FU));
    buf[3] = (uint8_t)(seq & 0xFFU);
    buf[4] = (uint8_t)(dlen >> 8);
    buf[5] = (uint8_t)(dlen & 0xFFU);
    for (i = 0U; i < 8U; ++i)
    {
        buf[6U + i] = (uint8_t)((uint64_t)time_gps_ms >> (56U - (8U * i)));
    }
    for (i = 0U; i < plen; ++i)
    {
        buf[14U + i] = payload[i];
    }
    crc = sb_crc16_ccitt(buf, total - 2U);
    buf[total - 2U] = (uint8_t)(crc >> 8);
    buf[total - 1U] = (uint8_t)(crc & 0xFFU);
    return total;
}
