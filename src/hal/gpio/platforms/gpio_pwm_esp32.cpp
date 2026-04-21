// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

/// @brief ESP32 PWM (LEDC) implementation — single TU owns the channel/timer state.

#if defined(ESP_PLATFORM)

#include "gpio_access_esp32.h"

namespace ungula {
    namespace gpio {

        namespace detail {

            static constexpr uint8_t MAX_PWM_CHANNELS = SOC_LEDC_CHANNEL_NUM;
            static constexpr uint8_t MAX_PWM_TIMERS = LEDC_TIMER_MAX;
            static constexpr uint8_t MAX_RESOLUTION_BITS = SOC_LEDC_TIMER_BIT_WIDTH;
            static constexpr uint8_t SLOT_UNUSED = 0xFF;

            struct PwmChannelInfo {
                    uint8_t pin;
                    uint8_t timer;
                    uint8_t resolutionBits;
                    bool configured;
            };

            struct PwmTimerInfo {
                    uint32_t freqHz;
                    uint8_t resBits;
                    bool configured;
            };

            struct PwmState {
                    PwmChannelInfo channels[MAX_PWM_CHANNELS];
                    PwmTimerInfo timers[MAX_PWM_TIMERS];
                    uint8_t channelCount;
                    uint8_t timerCount;
            };

            // Single global instance — not per-TU.
            static PwmState s_pwmState = {};

            static uint8_t findChannel(uint8_t pin) {
                for (uint8_t idx = 0; idx < s_pwmState.channelCount; idx++) {
                    if (s_pwmState.channels[idx].configured &&
                        s_pwmState.channels[idx].pin == pin) {
                        return idx;
                    }
                }
                return SLOT_UNUSED;
            }

            // Find an existing timer with matching freq+res, or configure a new one.
            static uint8_t findOrCreateTimer(uint32_t freqHz, uint8_t resBits) {
                for (uint8_t idx = 0; idx < s_pwmState.timerCount; idx++) {
                    if (s_pwmState.timers[idx].configured &&
                        s_pwmState.timers[idx].freqHz == freqHz &&
                        s_pwmState.timers[idx].resBits == resBits) {
                        return idx;
                    }
                }

                if (s_pwmState.timerCount >= MAX_PWM_TIMERS) {
                    return SLOT_UNUSED;
                }

                uint8_t slot = s_pwmState.timerCount;

                ledc_timer_config_t timerCfg = {};
                timerCfg.speed_mode = LEDC_LOW_SPEED_MODE;
                timerCfg.timer_num = static_cast<ledc_timer_t>(slot);
                timerCfg.duty_resolution = static_cast<ledc_timer_bit_t>(resBits);
                timerCfg.freq_hz = freqHz;
                timerCfg.clk_cfg = LEDC_AUTO_CLK;

                if (ledc_timer_config(&timerCfg) != ESP_OK) {
                    return SLOT_UNUSED;
                }

                s_pwmState.timers[slot].freqHz = freqHz;
                s_pwmState.timers[slot].resBits = resBits;
                s_pwmState.timers[slot].configured = true;
                s_pwmState.timerCount++;
                return slot;
            }

        }  // namespace detail

        bool configPwm(uint8_t pin, uint32_t freqHz, uint8_t resolutionBits) {
            if (!detail::isValidOutputGpio(pin)) {
                return false;
            }
            if (resolutionBits < 1 || resolutionBits > detail::MAX_RESOLUTION_BITS) {
                return false;
            }

            // Single-assignment: reject if already configured
            if (detail::findChannel(pin) != detail::SLOT_UNUSED) {
                return false;
            }

            uint8_t timr = detail::findOrCreateTimer(freqHz, resolutionBits);
            if (timr == detail::SLOT_UNUSED) {
                return false;
            }

            if (detail::s_pwmState.channelCount >= detail::MAX_PWM_CHANNELS) {
                return false;
            }
            uint8_t slot = detail::s_pwmState.channelCount;

            ledc_channel_config_t chCfg = {};
            chCfg.speed_mode = LEDC_LOW_SPEED_MODE;
            chCfg.channel = static_cast<ledc_channel_t>(slot);
            chCfg.timer_sel = static_cast<ledc_timer_t>(timr);
            chCfg.intr_type = LEDC_INTR_DISABLE;
            chCfg.gpio_num = pin;
            chCfg.duty = 0;
            chCfg.hpoint = 0;

            if (ledc_channel_config(&chCfg) != ESP_OK) {
                return false;
            }

            detail::s_pwmState.channels[slot].pin = pin;
            detail::s_pwmState.channels[slot].timer = timr;
            detail::s_pwmState.channels[slot].resolutionBits = resolutionBits;
            detail::s_pwmState.channels[slot].configured = true;
            detail::s_pwmState.channelCount++;
            return true;
        }

        bool writePwm(uint8_t pin, uint32_t duty) {
            uint8_t ch = detail::findChannel(pin);
            if (ch == detail::SLOT_UNUSED) {
                return false;
            }

            // Clamp duty to (2^resolutionBits - 1). Full range 2^N causes
            // hardware overflow on classic ESP32 at max resolution.
            uint32_t maxDuty = (1U << detail::s_pwmState.channels[ch].resolutionBits) - 1;
            if (duty > maxDuty) {
                duty = maxDuty;
            }

            ledc_channel_t ledcCh = static_cast<ledc_channel_t>(ch);
            if (ledc_set_duty(LEDC_LOW_SPEED_MODE, ledcCh, duty) != ESP_OK) {
                return false;
            }
            return ledc_update_duty(LEDC_LOW_SPEED_MODE, ledcCh) == ESP_OK;
        }

    }  // namespace gpio
}  // namespace ungula

#endif  // ESP_PLATFORM
