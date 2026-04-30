// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

/// @brief Default (no-op) GPIO implementation for non-hardware builds.
///
/// All functions return safe defaults. Config functions succeed, reads
/// return false (LOW), writes do nothing. This allows the library to
/// compile and link on desktop (tests, tools) without any platform SDK.

#include <stdint.h>

#define UNGULA_ISR_ATTR
#define DEFAULT_GPIO_STATE false

namespace ungula {
    namespace gpio {

        // ---- Pin configuration (always succeed) ----

        inline bool configOutput(uint8_t /*pin*/) {
            return true;
        }
        inline bool configInput(uint8_t /*pin*/) {
            return true;
        }
        inline bool configInputPullup(uint8_t /*pin*/) {
            return true;
        }
        inline bool configInputPulldown(uint8_t /*pin*/) {
            return true;
        }
        inline bool configOutputOpenDrain(uint8_t /*pin*/) {
            return true;
        }

        // ---- Digital read/write — unchecked (no-op) ----

        inline bool read(uint8_t /*pin*/) {
            return DEFAULT_GPIO_STATE;
        }
        inline void setHigh(uint8_t /*pin*/) {}
        inline void setLow(uint8_t /*pin*/) {}
        inline void write(uint8_t /*pin*/, bool /*high*/) {}
        inline void writeHigh(uint8_t /*pin*/) {}
        inline void writeLow(uint8_t /*pin*/) {}
        inline void toggle(uint8_t /*pin*/) {}

        // ---- Convenience helpers (unchecked) ----

        inline bool isHigh(uint8_t /*pin*/) {
            return DEFAULT_GPIO_STATE;
        }
        inline bool isLow(uint8_t /*pin*/) {
            return !DEFAULT_GPIO_STATE;
        }
        inline bool isEnabled(uint8_t pin) {
            return DEFAULT_GPIO_STATE;
        }
        inline bool isDisabled(uint8_t pin) {
            return !DEFAULT_GPIO_STATE;
        }
        inline bool isOpen(uint8_t pin) {
            return !DEFAULT_GPIO_STATE;
        }
        inline bool isClosed(uint8_t pin) {
            return DEFAULT_GPIO_STATE;
        }

        // ---- Digital read/write — checked (no-op, always succeed) ----

        inline bool checkedRead(uint8_t /*pin*/, bool& out) {
            out = DEFAULT_GPIO_STATE;
            return true;
        }
        inline bool checkedSetHigh(uint8_t /*pin*/) {
            return true;
        }
        inline bool checkedSetLow(uint8_t /*pin*/) {
            return true;
        }
        inline bool checkedWrite(uint8_t /*pin*/, bool /*high*/) {
            return true;
        }

        // ---- Interrupt / ISR support ----

        enum class InterruptEdge : uint8_t { EDGE_RISING = 0, EDGE_FALLING = 1, EDGE_ANY = 2 };
        enum class PullMode : uint8_t { NONE = 0, UP = 1, DOWN = 2 };

        using GpioIsrHandler = void (*)(void*);

        inline bool configInputInterrupt(uint8_t /*pin*/, InterruptEdge /*edge*/,
                                         PullMode /*pull*/ = PullMode::NONE) {
            return true;
        }
        inline bool installIsrService() {
            return true;
        }
        inline bool addIsrHandler(uint8_t /*pin*/, GpioIsrHandler /*handler*/, void* /*ctx*/) {
            return true;
        }
        inline bool removeIsrHandler(uint8_t /*pin*/) {
            return true;
        }

        // ---- PWM (no-op) ----

        inline bool configPwm(uint8_t /*pin*/, uint32_t /*freqHz*/ = 1000,
                              uint8_t /*resBits*/ = 8) {
            return true;
        }
        inline bool writePwm(uint8_t /*pin*/, uint32_t /*duty*/) {
            return true;
        }

        // ADC available at <hal/adc/adc_manager.h> — see ungula::adc::AdcManager.

    }  // namespace gpio
}  // namespace ungula
