// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stdint.h>

#include "ungula/hal/pwm_input/i_pwm_input.h"

/// @brief Header-only PWM-input fake for host tests.
///
/// Implements `IPwmInput`. Tests script high-time / period samples with
/// `injectSample()` (or `setHighTimeUs()` / `setPeriodUs()`) and the
/// consumer driver sees them as if a real ISR had fired. `sampleAgeUs()`
/// is fully under test control via `setSampleAgeUs()` so a test can
/// simulate a stalled signal without sleeping.

namespace ungula::hal::pwm_input::drivers
{

    class PwmInputFake final : public IPwmInput {
    public:
        PwmInputFake() = default;

        // ---- IPwmInput ----
        bool begin(uint8_t pin) override
        {
            if (installed_) {
                return false;
            }
            pin_ = pin;
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
        uint32_t lastHighTimeUs() const override
        {
            return highTimeUs_;
        }
        uint32_t lastPeriodUs() const override
        {
            return periodUs_;
        }
        bool hasSample() const override
        {
            return hasSample_;
        }
        uint32_t sampleAgeUs() const override
        {
            return sampleAgeUs_;
        }
        uint8_t pin() const override
        {
            return pin_;
        }
        void setSampleCallback(SampleCallback cb, void *ctx) override
        {
            cb_ = cb;
            cbCtx_ = ctx;
            ++setCallbackCallCount_;
        }

        // ---- Test knobs ----
        void injectSample(uint32_t highTimeUs, uint32_t periodUs)
        {
            highTimeUs_ = highTimeUs;
            periodUs_ = periodUs;
            hasSample_ = true;
            sampleAgeUs_ = 0U;
            ++injectCallCount_;
        }
        /// @brief Inject a sample and fire the registered ISR-mode
        ///        callback (if any). Use this in tests that want
        ///        to exercise the callback path end-to-end without
        ///        a real interrupt source.
        void triggerSample(uint32_t highTimeUs, uint32_t periodUs)
        {
            injectSample(highTimeUs, periodUs);
            if (cb_ != nullptr) {
                cb_(cbCtx_);
                ++callbackInvocationCount_;
            }
        }
        void setHighTimeUs(uint32_t v)
        {
            highTimeUs_ = v;
        }
        void setPeriodUs(uint32_t v)
        {
            periodUs_ = v;
        }
        void setHasSample(bool v)
        {
            hasSample_ = v;
        }
        void setSampleAgeUs(uint32_t v)
        {
            sampleAgeUs_ = v;
        }

        unsigned beginCallCount() const
        {
            return beginCallCount_;
        }
        unsigned stopCallCount() const
        {
            return stopCallCount_;
        }
        unsigned injectCallCount() const
        {
            return injectCallCount_;
        }
        unsigned setCallbackCallCount() const
        {
            return setCallbackCallCount_;
        }
        unsigned callbackInvocationCount() const
        {
            return callbackInvocationCount_;
        }
        bool isInstalled() const
        {
            return installed_;
        }

    private:
        bool installed_ = false;
        uint8_t pin_ = 0;
        uint32_t highTimeUs_ = 0;
        uint32_t periodUs_ = 0;
        uint32_t sampleAgeUs_ = 0;
        bool hasSample_ = false;

        SampleCallback cb_ = nullptr;
        void *cbCtx_ = nullptr;

        unsigned beginCallCount_ = 0;
        unsigned stopCallCount_ = 0;
        unsigned injectCallCount_ = 0;
        unsigned setCallbackCallCount_ = 0;
        unsigned callbackInvocationCount_ = 0;
    };

} // namespace ungula::hal::pwm_input::drivers
