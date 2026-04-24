// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

/// @brief ESP32 ADC manager — oneshot reads with per-(unit, attenuation)
/// calibration.
///
/// Owns the esp_adc unit and calibration handles. One instance per firmware
/// is the normal pattern (ADC is a singleton hardware resource).
///
/// ## Why a class
///
/// - Calibration is per unit AND per attenuation. Two channels on the same
///   unit with different attenuations each need their own cali handle.
/// - Ownership is explicit: deinit() (or the destructor) deletes handles in
///   the right order and with the matching scheme-specific delete function.
/// - Channel metadata carries its own attenuation so readMv() looks up the
///   exact cali entry for that channel, never the wrong one.
///
/// ## Threading
///
/// Not thread-safe. Configure every channel at boot, from a single context,
/// before any task starts polling. readMv()/readRaw() from multiple tasks
/// against an already-configured channel is fine (ESP-IDF oneshot_read is
/// internally synchronised) but changing the channel table at runtime is not.
///
/// ## ADC2 + Wi-Fi
///
/// Classic ESP32 (and some siblings) share ADC2 channels with the Wi-Fi PHY.
/// Reads may fail or return zero while Wi-Fi is active. Prefer ADC1 channels
/// (GPIO 32..39 on the original ESP32) when the radio is in use.

#include <stddef.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"

#ifndef UNGULA_HAL_MAX_ADC_CHANNELS
#define UNGULA_HAL_MAX_ADC_CHANNELS 16
#endif

namespace ungula {
    namespace adc {

        enum class Attenuation : uint8_t {
                DB_0 = 0,    /// ~0–950 mV input range, best precision near 0 V
                DB_2_5 = 1,  /// ~0–1.25 V
                DB_6 = 2,    /// ~0–1.75 V
                DB_12 = 3,   /// ~0–3.3 V (full-scale on a 3.3 V rail) — default
        };

        enum class CaliScheme : uint8_t { None, CurveFitting, LineFitting };

        class AdcManager {
            public:
                static constexpr size_t MAX_CHANNELS = UNGULA_HAL_MAX_ADC_CHANNELS;
                static constexpr size_t UNIT_COUNT = 2;
                static constexpr size_t ATTEN_COUNT = 4;

                AdcManager() = default;
                ~AdcManager();

                AdcManager(const AdcManager&) = delete;
                AdcManager& operator=(const AdcManager&) = delete;

                /// @brief Configure an ADC channel for a given GPIO.
                /// Single-assignment per pin — a second call for the same pin
                /// returns false. Succeeds even if calibration can't be
                /// created (the read path falls back to an attenuation-aware
                /// linear approximation).
                bool configure(uint8_t pin, Attenuation atten = Attenuation::DB_12);

                /// @brief Read calibrated voltage in millivolts.
                /// Returns false (mv = 0) if the pin is unknown or the
                /// underlying driver read failed. A successful read always
                /// sets mv, including to 0 on a grounded input — use the
                /// return value, not the magnitude of mv, to detect errors.
                bool readMv(uint8_t pin, uint32_t& mv) const;

                /// @brief Read the raw ADC count (0..max bitwidth).
                /// Useful for diagnostics and for callers that prefer to do
                /// their own conversion.
                bool readRaw(uint8_t pin, int& raw) const;

                /// @brief Release unit + calibration handles. Safe to call
                /// multiple times. The destructor calls this for you.
                void deinit() noexcept;

            private:
                struct ChannelInfo {
                        uint8_t pin = 0;
                        adc_unit_t unit{};
                        adc_channel_t channel{};
                        adc_atten_t atten{};
                        bool configured = false;
                };

                struct CaliEntry {
                        adc_cali_handle_t handle = nullptr;
                        CaliScheme scheme = CaliScheme::None;
                };

                ChannelInfo channels_[MAX_CHANNELS] = {};
                size_t channelCount_ = 0;

                adc_oneshot_unit_handle_t units_[UNIT_COUNT] = {};
                CaliEntry cali_[UNIT_COUNT][ATTEN_COUNT] = {};

                bool ensureUnit(adc_unit_t unit);
                void ensureCalibration(adc_unit_t unit, adc_atten_t atten);
                size_t findChannel(uint8_t pin) const;

                static bool unitToIndex(adc_unit_t unit, size_t& idx);
                static size_t attenToIndex(adc_atten_t atten);
                static adc_atten_t toIdfAttenuation(Attenuation atten);
                static uint32_t fallbackFullScaleMv(adc_atten_t atten);
                static uint32_t rawToMvFallback(int raw, adc_atten_t atten);
        };

    }  // namespace adc
}  // namespace ungula
