// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if !defined(ESP_PLATFORM)

#include "ungula/hal/quadrature/drivers/decoder.h"

namespace ungula::hal::quadrature::drivers
{

    Decoder::Decoder() = default;
    Decoder::~Decoder() = default;

    bool Decoder::begin(uint8_t pinA, uint8_t pinB, int32_t initialCount)
    {
        if (installed_) {
            return false;
        }
        pinA_ = pinA;
        pinB_ = pinB;
        count_ = initialCount;
        installed_ = true;
        return true;
    }

    bool Decoder::stop()
    {
        installed_ = false;
        return true;
    }

    int32_t Decoder::count() const
    {
        // Stub does not move on its own. Tests that need to script
        // counts should use `DecoderFake`.
        return count_;
    }

    bool Decoder::reset(int32_t value)
    {
        count_ = value;
        return true;
    }

} // namespace ungula::hal::quadrature::drivers

#endif // !ESP_PLATFORM
