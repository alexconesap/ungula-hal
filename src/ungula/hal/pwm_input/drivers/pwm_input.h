// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include "ungula/hal/pwm_input/i_pwm_input.h"

/// @brief Platform-dispatched PWM-input capture.
///
/// Concrete `IPwmInput` for the host MCU. On ESP32 the backend uses a
/// GPIO interrupt + `esp_timer_get_time()` to time both edges; on host
/// (and other unsupported platforms) the backend is a no-op stub —
/// `begin()` succeeds, `hasSample()` stays false. Tests of consumer
/// code should use `PwmInputFake` to script samples.

namespace ungula::hal::pwm_input::drivers {

    class PwmInput final : public IPwmInput {
        public:
            PwmInput();
            ~PwmInput() override;

            bool begin(uint8_t pin) override;
            bool stop() override;

            uint32_t lastHighTimeUs() const override;
            uint32_t lastPeriodUs() const override;
            bool hasSample() const override;
            uint32_t sampleAgeUs() const override;
            uint8_t pin() const override {
                return pin_;
            }

        private:
            bool installed_ = false;
            uint8_t pin_ = 0;
            // Opaque per-platform state. Lives at file scope inside each
            // platform .cpp; the public class just tracks lifecycle.
    };

}  // namespace ungula::hal::pwm_input::drivers
