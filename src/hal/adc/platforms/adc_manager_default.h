// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

/// @brief Default (no-op) ADC manager for non-hardware builds.
///
/// configure() succeeds, readMv()/readRaw() return 0 with true. Enough to
/// compile and link desktop tests that exercise the API surface.

#include <stddef.h>
#include <stdint.h>

#ifndef UNGULA_HAL_MAX_ADC_CHANNELS
#define UNGULA_HAL_MAX_ADC_CHANNELS 16
#endif

namespace ungula {
    namespace adc {

        enum class Attenuation : uint8_t {
            DB_0 = 0,
            DB_2_5 = 1,
            DB_6 = 2,
            DB_12 = 3,
        };

        enum class CaliScheme : uint8_t { None, CurveFitting, LineFitting };

        class AdcManager {
            public:
                static constexpr size_t MAX_CHANNELS = UNGULA_HAL_MAX_ADC_CHANNELS;

                AdcManager() = default;
                ~AdcManager() {
                    deinit();
                }

                AdcManager(const AdcManager&) = delete;
                AdcManager& operator=(const AdcManager&) = delete;

                bool configure(uint8_t pin, Attenuation /*atten*/ = Attenuation::DB_12) {
                    if (findSlot(pin) != MAX_CHANNELS) {
                        return false;  // single-assignment, match ESP32 contract.
                    }
                    if (count_ >= MAX_CHANNELS) {
                        return false;
                    }
                    pins_[count_] = pin;
                    configured_[count_] = true;
                    count_++;
                    return true;
                }

                bool readMv(uint8_t pin, uint32_t& mv) const {
                    mv = 0;
                    return findSlot(pin) != MAX_CHANNELS;
                }

                bool readRaw(uint8_t pin, int& raw) const {
                    raw = 0;
                    return findSlot(pin) != MAX_CHANNELS;
                }

                void deinit() noexcept {
                    for (size_t i = 0; i < MAX_CHANNELS; i++) {
                        pins_[i] = 0;
                        configured_[i] = false;
                    }
                    count_ = 0;
                }

            private:
                uint8_t pins_[MAX_CHANNELS] = {};
                bool configured_[MAX_CHANNELS] = {};
                size_t count_ = 0;

                size_t findSlot(uint8_t pin) const {
                    for (size_t i = 0; i < count_; i++) {
                        if (configured_[i] && pins_[i] == pin) {
                            return i;
                        }
                    }
                    return MAX_CHANNELS;
                }
        };

    }  // namespace adc
}  // namespace ungula
