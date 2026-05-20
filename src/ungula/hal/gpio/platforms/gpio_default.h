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

#include "ungula/hal/core/compiler_attrs.h" // IWYU pragma: export
#include "ungula/hal/gpio/gpio_types.h"

#define DEFAULT_GPIO_STATE false

namespace ungula::hal::gpio
{

namespace detail
{

        /// Records the LAST input-config call applied to a pin on the host
        /// backend. Test-only: lets host-side unit tests assert that library
        /// code wired the right pull mode for the configured polarity. ESP32
        /// builds use `gpio_esp32.h` and do not include this tracker. Default
        /// is `None`; reset is not provided — tests that need a clean slate
        /// run their pin numbers in their own range or work modulo this table.
        enum class HostInputMode : uint8_t {
                None = 0,
                Plain, // configInput(pin)
                Pullup, // configInputPullup(pin)
                Pulldown, // configInputPulldown(pin)
                Interrupt, // configInputInterrupt(pin, edge, pull)
        };

        inline HostInputMode &hostInputModeSlot(uint8_t pin)
        {
                static HostInputMode table[64] = {};
                return table[pin & 63];
        }

        inline HostInputMode lastInputMode(uint8_t pin)
        {
                return hostInputModeSlot(pin);
        }

        inline void resetInputMode(uint8_t pin)
        {
                hostInputModeSlot(pin) = HostInputMode::None;
        }

} // namespace detail

// ---- Pin configuration (always succeed) ----

inline bool configOutput(uint8_t /*pin*/)
{
        return true;
}
inline bool configInput(uint8_t pin)
{
        detail::hostInputModeSlot(pin) = detail::HostInputMode::Plain;
        return true;
}
inline bool configInputPullup(uint8_t pin)
{
        detail::hostInputModeSlot(pin) = detail::HostInputMode::Pullup;
        return true;
}
inline bool configInputPulldown(uint8_t pin)
{
        detail::hostInputModeSlot(pin) = detail::HostInputMode::Pulldown;
        return true;
}
inline bool configOutputOpenDrain(uint8_t /*pin*/)
{
        return true;
}
inline bool configOutputRelay(uint8_t /*pin*/,
                              RelayPolarity /*active_low*/ = RelayPolarity::ActiveLow)
{
        return true;
}

// ---- Digital read/write — unchecked (no-op) ----

inline bool read(uint8_t /*pin*/)
{
        return DEFAULT_GPIO_STATE;
}
inline void setHigh(uint8_t /*pin*/)
{
}
inline void setLow(uint8_t /*pin*/)
{
}
inline void write(uint8_t /*pin*/, bool /*high*/)
{
}
inline void writeHigh(uint8_t /*pin*/)
{
}
inline void writeLow(uint8_t /*pin*/)
{
}
inline void toggle(uint8_t /*pin*/)
{
}

// ---- Convenience helpers (unchecked) ----
inline void on(uint8_t /*pin*/)
{
}
inline void off(uint8_t /*pin*/)
{
}

inline bool isOn(uint8_t /*pin*/)
{
        return DEFAULT_GPIO_STATE;
}
inline bool isOff(uint8_t /*pin*/)
{
        return !DEFAULT_GPIO_STATE;
}
inline bool isHigh(uint8_t /*pin*/)
{
        return DEFAULT_GPIO_STATE;
}
inline bool isLow(uint8_t /*pin*/)
{
        return !DEFAULT_GPIO_STATE;
}
inline bool isEnabled(uint8_t pin)
{
        return DEFAULT_GPIO_STATE;
}
inline bool isDisabled(uint8_t pin)
{
        return !DEFAULT_GPIO_STATE;
}
inline bool isOpen(uint8_t pin)
{
        return !DEFAULT_GPIO_STATE;
}
inline bool isClosed(uint8_t pin)
{
        return DEFAULT_GPIO_STATE;
}

// ---- Digital read/write — checked (no-op, always succeed) ----

inline bool checkedRead(uint8_t /*pin*/, bool &out)
{
        out = DEFAULT_GPIO_STATE;
        return true;
}
inline bool checkedSetHigh(uint8_t /*pin*/)
{
        return true;
}
inline bool checkedSetLow(uint8_t /*pin*/)
{
        return true;
}
inline bool checkedWrite(uint8_t /*pin*/, bool /*high*/)
{
        return true;
}

// ---- Interrupt / ISR support ----

enum class InterruptEdge : uint8_t { EDGE_RISING = 0, EDGE_FALLING = 1, EDGE_ANY = 2 };
enum class PullMode : uint8_t { NONE = 0, UP = 1, DOWN = 2 };

using GpioIsrHandler = void (*)(void *);

inline bool configInputInterrupt(uint8_t pin, InterruptEdge /*edge*/,
                                 PullMode /*pull*/ = PullMode::NONE)
{
        detail::hostInputModeSlot(pin) = detail::HostInputMode::Interrupt;
        return true;
}
inline bool installIsrService()
{
        return true;
}
inline bool addIsrHandler(uint8_t /*pin*/, GpioIsrHandler /*handler*/, void * /*ctx*/)
{
        return true;
}
inline bool removeIsrHandler(uint8_t /*pin*/)
{
        return true;
}

// ---- PWM (no-op) ----

inline bool configPwm(uint8_t /*pin*/, uint32_t /*freqHz*/ = 1000, uint8_t /*resBits*/ = 8)
{
        return true;
}
inline bool writePwm(uint8_t /*pin*/, uint32_t /*duty*/)
{
        return true;
}

// ADC available at <ungula/hal/adc/adc_manager.h> — see ungula::adc::AdcManager.

} // namespace ungula::hal::gpio
