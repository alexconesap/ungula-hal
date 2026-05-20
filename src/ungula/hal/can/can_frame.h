// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstdint>

namespace ungula::hal::can
{

/// Standard CAN 2.0 frame. Up to 8 data bytes. Both 11-bit
/// (standard) and 29-bit (extended) IDs via the `extendedId` flag.
/// Used by every classic-CAN controller in the lib (`Can`, future
/// `Mcp2515Can`, ...). CAN-FD uses a separate frame type when added.
struct CanFrame {
        uint32_t id;            // 11-bit (extendedId=false) or 29-bit (extendedId=true)
        bool     extendedId;    // selects ID width
        bool     remote;        // remote transmission request (rarely used)
        uint8_t  dlc;           // 0..8
        uint8_t  data[8];
};

/// Common bitrates expressed in Hz so call sites read clearly.
/// Backends map these to their own internal presets; values not in
/// the table here may still work - drivers reject what they cannot
/// configure by returning `false` from `begin()`.
constexpr uint32_t BITRATE_25K  = 25'000;
constexpr uint32_t BITRATE_50K  = 50'000;
constexpr uint32_t BITRATE_100K = 100'000;
constexpr uint32_t BITRATE_125K = 125'000;
constexpr uint32_t BITRATE_250K = 250'000;
constexpr uint32_t BITRATE_500K = 500'000;
constexpr uint32_t BITRATE_800K = 800'000;
constexpr uint32_t BITRATE_1M   = 1'000'000;

} // namespace ungula::hal::can
