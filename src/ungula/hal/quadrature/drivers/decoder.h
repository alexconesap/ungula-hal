// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include "ungula/hal/quadrature/i_decoder.h"

/// @brief Platform-dispatched quadrature decoder.
///
/// On ESP32 the backend uses the PCNT (pulse counter) peripheral with
/// hardware quadrature decoding. On host (and other unsupported
/// platforms) the backend is a counter you can move with `reset()` —
/// useful for compile-checks but not for simulating motion. Tests of
/// consumer code should use `DecoderFake` to script counts.

namespace ungula::hal::quadrature::drivers
{

class Decoder final : public IDecoder {
    public:
        Decoder();
        ~Decoder() override;

        bool begin(uint8_t pinA, uint8_t pinB, int32_t initialCount = 0) override;
        bool stop() override;
        int32_t count() const override;
        bool reset(int32_t value = 0) override;

        uint8_t pinA() const override
        {
                return pinA_;
        }
        uint8_t pinB() const override
        {
                return pinB_;
        }

    private:
        bool installed_ = false;
        uint8_t pinA_ = 0xFF;
        uint8_t pinB_ = 0xFF;
        mutable int32_t count_ = 0; // mutable so const accessor on host can latch hardware state
        void *unit_ = nullptr;
};

} // namespace ungula::hal::quadrature::drivers
