// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include "ungula/hal/quadrature/i_decoder.h"

/// @brief Header-only quadrature decoder fake for host tests.
///
/// Tests script counts via `setCount()` (or `tick(delta)`) and the
/// consumer driver sees them as if the hardware decoder had advanced.
/// Optional Z (index) latching is available via `markIndex()` for
/// driver tests that exercise the homing path.

namespace ungula::hal::quadrature::drivers
{

class DecoderFake final : public IDecoder {
    public:
        DecoderFake() = default;

        bool begin(uint8_t pinA, uint8_t pinB, int32_t initialCount = 0) override
        {
                if (installed_) {
                        return false;
                }
                pinA_ = pinA;
                pinB_ = pinB;
                count_ = initialCount;
                installed_ = true;
                ++beginCallCount_;
                return true;
        }
        bool stop() override
        {
                installed_ = false;
                ++stopCallCount_;
                return true;
        }
        int32_t count() const override
        {
                return count_;
        }
        bool reset(int32_t value = 0) override
        {
                count_ = value;
                ++resetCallCount_;
                return true;
        }
        bool hasIndex() const override
        {
                return hasIndex_;
        }
        bool latchedAtIndex() const override
        {
                return atIndex_;
        }
        uint8_t pinA() const override
        {
                return pinA_;
        }
        uint8_t pinB() const override
        {
                return pinB_;
        }

        // ---- Test knobs ----
        void setCount(int32_t v)
        {
                count_ = v;
        }
        void tick(int32_t delta)
        {
                count_ += delta;
        }
        void setHasIndex(bool v)
        {
                hasIndex_ = v;
        }
        void markIndex(bool v)
        {
                atIndex_ = v;
        }

        unsigned beginCallCount() const
        {
                return beginCallCount_;
        }
        unsigned stopCallCount() const
        {
                return stopCallCount_;
        }
        unsigned resetCallCount() const
        {
                return resetCallCount_;
        }
        bool isInstalled() const
        {
                return installed_;
        }

    private:
        bool installed_ = false;
        uint8_t pinA_ = 0xFF;
        uint8_t pinB_ = 0xFF;
        int32_t count_ = 0;
        bool hasIndex_ = false;
        bool atIndex_ = false;

        unsigned beginCallCount_ = 0;
        unsigned stopCallCount_ = 0;
        unsigned resetCallCount_ = 0;
};

} // namespace ungula::hal::quadrature::drivers
