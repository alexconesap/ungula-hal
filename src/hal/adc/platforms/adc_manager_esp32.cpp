// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "adc_manager_esp32.h"

#include "esp_adc/adc_cali_scheme.h"
#include "soc/soc_caps.h"

namespace ungula {
    namespace adc {

        AdcManager::~AdcManager() {
            deinit();
        }

        void AdcManager::deinit() noexcept {
            // Delete calibration first — it references the unit, so must go before.
            // Pick the delete function that matches the scheme we created with.
            for (size_t u = 0; u < UNIT_COUNT; u++) {
                for (size_t a = 0; a < ATTEN_COUNT; a++) {
                    CaliEntry& entry = cali_[u][a];
                    if (entry.handle == nullptr) {
                        continue;
                    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
                    if (entry.scheme == CaliScheme::CurveFitting) {
                        adc_cali_delete_scheme_curve_fitting(entry.handle);
                    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
                    if (entry.scheme == CaliScheme::LineFitting) {
                        adc_cali_delete_scheme_line_fitting(entry.handle);
                    }
#endif
                    entry.handle = nullptr;
                    entry.scheme = CaliScheme::None;
                }
            }

            for (size_t u = 0; u < UNIT_COUNT; u++) {
                if (units_[u] != nullptr) {
                    adc_oneshot_del_unit(units_[u]);
                    units_[u] = nullptr;
                }
            }

            for (size_t i = 0; i < MAX_CHANNELS; i++) {
                channels_[i] = {};
            }
            channelCount_ = 0;
        }

        bool AdcManager::configure(uint8_t pin, Attenuation atten) {
            adc_unit_t unit;
            adc_channel_t channel;
            if (adc_oneshot_io_to_channel(static_cast<int>(pin), &unit, &channel) != ESP_OK) {
                return false;
            }

            if (findChannel(pin) != MAX_CHANNELS) {
                return false;
            }
            if (channelCount_ >= MAX_CHANNELS) {
                return false;
            }

            const adc_atten_t idfAtten = toIdfAttenuation(atten);
            if (!ensureUnit(unit)) {
                return false;
            }

            size_t unitIdx = 0;
            if (!unitToIndex(unit, unitIdx)) {
                return false;
            }

            adc_oneshot_chan_cfg_t chCfg = {};
            chCfg.atten = idfAtten;
            chCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
            if (adc_oneshot_config_channel(units_[unitIdx], channel, &chCfg) != ESP_OK) {
                return false;
            }

            // Calibration failure is non-fatal — the read path has a fallback.
            ensureCalibration(unit, idfAtten);

            ChannelInfo& info = channels_[channelCount_];
            info.pin = pin;
            info.unit = unit;
            info.channel = channel;
            info.atten = idfAtten;
            info.configured = true;
            channelCount_++;
            return true;
        }

        bool AdcManager::readRaw(uint8_t pin, int& raw) const {
            raw = 0;
            const size_t idx = findChannel(pin);
            if (idx == MAX_CHANNELS) {
                return false;
            }
            const ChannelInfo& info = channels_[idx];
            size_t unitIdx = 0;
            if (!unitToIndex(info.unit, unitIdx)) {
                return false;
            }
            int local = 0;
            if (adc_oneshot_read(units_[unitIdx], info.channel, &local) != ESP_OK) {
                return false;
            }
            if (local < 0) {
                local = 0;
            }
            raw = local;
            return true;
        }

        bool AdcManager::readMv(uint8_t pin, uint32_t& mv) const {
            mv = 0;
            int raw = 0;
            if (!readRaw(pin, raw)) {
                return false;
            }

            const size_t idx = findChannel(pin);  // re-used — cheap linear probe.
            const ChannelInfo& info = channels_[idx];

            size_t unitIdx = 0;
            unitToIndex(info.unit, unitIdx);
            const size_t attenIdx = attenToIndex(info.atten);
            const CaliEntry& entry = cali_[unitIdx][attenIdx];

            if (entry.handle != nullptr) {
                int calibratedMv = 0;
                if (adc_cali_raw_to_voltage(entry.handle, raw, &calibratedMv) == ESP_OK) {
                    mv = static_cast<uint32_t>(calibratedMv);
                    return true;
                }
            }

            // Attenuation-aware rough fallback. 5–10 % error typical.
            mv = rawToMvFallback(raw, info.atten);
            return true;
        }

        // ---- Internals ----

        bool AdcManager::ensureUnit(adc_unit_t unit) {
            size_t idx = 0;
            if (!unitToIndex(unit, idx)) {
                return false;
            }
            if (units_[idx] != nullptr) {
                return true;
            }
            adc_oneshot_unit_init_cfg_t initCfg = {};
            initCfg.unit_id = unit;
            initCfg.ulp_mode = ADC_ULP_MODE_DISABLE;
            return adc_oneshot_new_unit(&initCfg, &units_[idx]) == ESP_OK;
        }

        // Best-effort. Calibration failure leaves entry.handle == nullptr and
        // the read path uses the fallback approximation.
        void AdcManager::ensureCalibration(adc_unit_t unit, adc_atten_t atten) {
            size_t unitIdx = 0;
            if (!unitToIndex(unit, unitIdx)) {
                return;
            }
            const size_t attenIdx = attenToIndex(atten);
            CaliEntry& entry = cali_[unitIdx][attenIdx];
            if (entry.handle != nullptr) {
                return;
            }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            {
                adc_cali_curve_fitting_config_t cfg = {};
                cfg.unit_id = unit;
                cfg.atten = atten;
                cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
                if (adc_cali_create_scheme_curve_fitting(&cfg, &entry.handle) == ESP_OK) {
                    entry.scheme = CaliScheme::CurveFitting;
                    return;
                }
                entry.handle = nullptr;
            }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            {
                adc_cali_line_fitting_config_t cfg = {};
                cfg.unit_id = unit;
                cfg.atten = atten;
                cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
                if (adc_cali_create_scheme_line_fitting(&cfg, &entry.handle) == ESP_OK) {
                    entry.scheme = CaliScheme::LineFitting;
                    return;
                }
                entry.handle = nullptr;
            }
#endif
            entry.scheme = CaliScheme::None;
        }

        size_t AdcManager::findChannel(uint8_t pin) const {
            for (size_t i = 0; i < channelCount_; i++) {
                if (channels_[i].configured && channels_[i].pin == pin) {
                    return i;
                }
            }
            return MAX_CHANNELS;
        }

        bool AdcManager::unitToIndex(adc_unit_t unit, size_t& idx) {
            switch (unit) {
                case ADC_UNIT_1:
                    idx = 0;
                    return true;
                case ADC_UNIT_2:
                    idx = 1;
                    return true;
                default:
                    return false;
            }
        }

        size_t AdcManager::attenToIndex(adc_atten_t atten) {
            switch (atten) {
                case ADC_ATTEN_DB_0:
                    return 0;
                case ADC_ATTEN_DB_2_5:
                    return 1;
                case ADC_ATTEN_DB_6:
                    return 2;
                case ADC_ATTEN_DB_12:
                    return 3;
                default:
                    return 3;
            }
        }

        adc_atten_t AdcManager::toIdfAttenuation(Attenuation atten) {
            switch (atten) {
                case Attenuation::DB_0:
                    return ADC_ATTEN_DB_0;
                case Attenuation::DB_2_5:
                    return ADC_ATTEN_DB_2_5;
                case Attenuation::DB_6:
                    return ADC_ATTEN_DB_6;
                case Attenuation::DB_12:
                default:
                    return ADC_ATTEN_DB_12;
            }
        }

        // Rough nominal full-scale input voltage per attenuation. Only used
        // when eFuse calibration is unavailable. Numbers are typical design-
        // guide values, not per-chip — expect 5–10 % error.
        uint32_t AdcManager::fallbackFullScaleMv(adc_atten_t atten) {
            switch (atten) {
                case ADC_ATTEN_DB_0:
                    return 950U;
                case ADC_ATTEN_DB_2_5:
                    return 1250U;
                case ADC_ATTEN_DB_6:
                    return 1750U;
                case ADC_ATTEN_DB_12:
                default:
                    return 3100U;
            }
        }

        uint32_t AdcManager::rawToMvFallback(int raw, adc_atten_t atten) {
            if (raw < 0) {
                raw = 0;
            }
            static constexpr uint32_t MAX_RAW = 4095U;  // 12-bit effective
            const uint32_t full = fallbackFullScaleMv(atten);
            return (static_cast<uint32_t>(raw) * full) / MAX_RAW;
        }

    }  // namespace adc
}  // namespace ungula

#endif  // ESP_PLATFORM
