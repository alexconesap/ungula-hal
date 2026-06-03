// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

/// @brief ESP32 GPIO implementation using ESP-IDF drivers and HAL low-level API.
///
/// Two API layers:
///
///   **Unchecked (hot-path)**: read(), write()
///   No pin validation — caller must have configured and validated the pin
///   beforehand via a config*() call. Use these in ISRs, timer callbacks,
///   PID loops, and anywhere nanoseconds matter.
///
///   Helpers provided around read() and write() to make code more readable:
///   setHigh(), setLow(), isHigh(), isLow(), isEnabled(), isDisabled(), isOpen(), isClosed().
///
///   **Checked**: checkedRead(), checkedSetHigh(), checkedSetLow(), checkedWrite().
///   Validate the pin against the SoC bitmask before every operation.
///   Return false if the pin is invalid. Use these when the pin number
///   comes from user input, configuration files, or any untrusted source.
///   Check functions are slower than unchecked — they do a GPIO validity check on every call, so
///   they are not suitable for hot paths. Use them in setup(), command handlers, or
///   anywhere you want to be defensive against invalid pins but don't want to pay the cost of
///   validation on every read/write.
///
/// Config functions (configOutput, configInput, etc.) always validate.
///
/// On ESP32, unchecked read/write uses the HAL low-level API (gpio_ll)
/// which compiles to direct register writes (W1TS/W1TC) — single-cycle
/// GPIO access safe for timer ISRs.
///
/// toggle() is NOT atomic — it does read() followed by write(). If the
/// same pin is accessed from both ISR and task context, the caller must
/// provide synchronisation.
///
/// PWM uses the LEDC peripheral (low-speed mode). State lives in a .cpp
/// file (gpio_pwm_esp32.cpp) — single instance shared across all TUs.
///
/// Looking for ADC input?? see <ungula/hal/adc/adc_manager.h>.
/// Conceptually ADC is its own peripheral, not a GPIO function.

#include <atomic>
#include <stdint.h>

#include "ungula/hal/core/compiler_attrs.h" // IWYU pragma: export

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "hal/gpio_ll.h"
#include "soc/soc_caps.h"
#include "ungula/hal/gpio/gpio_types.h"

namespace ungula::hal::gpio
{

// ---- Pin validation ----

namespace detail
{

        inline bool isValidGpio(uint8_t pin)
        {
                return GPIO_IS_VALID_GPIO(static_cast<int>(pin));
        }

        inline bool isValidOutputGpio(uint8_t pin)
        {
                return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<int>(pin));
        }

        // ESP32 only supports internal pull-ups on GPIOs 0-33. GPIOs 34-39 are input-only and do not have internal pull-up resistors.
        inline bool supportsInternalPulldown(uint8_t pin)
        {
                return GPIO_IS_VALID_GPIO(static_cast<int>(pin)) &&
                       pin < static_cast<int>(GPIO_NUM_34);
        }
        inline bool supportsInternalPullup(uint8_t pin)
        {
                return GPIO_IS_VALID_GPIO(static_cast<int>(pin)) &&
                       pin < static_cast<int>(GPIO_NUM_34);
        }
} // namespace detail

// ---- Pin configuration ----
// Uses gpio_config() to set direction + pulls atomically.
// Returns true on success, false on invalid pin or driver error.

inline bool configOutput(uint8_t pin)
{
        if (!detail::isValidOutputGpio(pin)) {
                return false;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&cfg) == ESP_OK;
}

inline bool configInput(uint8_t pin)
{
        if (!detail::isValidGpio(pin)) {
                return false;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&cfg) == ESP_OK;
}

inline bool configInputPullup(uint8_t pin)
{
        if (!detail::supportsInternalPullup(pin)) {
                return false;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&cfg) == ESP_OK;
}

inline bool configInputPulldown(uint8_t pin)
{
        if (!detail::supportsInternalPulldown(pin)) {
                return false;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&cfg) == ESP_OK;
}

inline bool configOutputRelay(uint8_t pin, RelayPolarity active_low = RelayPolarity::ActiveLow)
{
        if (!detail::isValidOutputGpio(pin)) {
                return false;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;

        if (gpio_config(&cfg) != ESP_OK) {
                return false;
        }

        const int off_level = active_low == RelayPolarity::ActiveLow ? 1 : 0;
        if (gpio_set_level(static_cast<gpio_num_t>(pin), off_level) != ESP_OK) {
                return false;
        }
        return gpio_get_level(static_cast<gpio_num_t>(pin)) == off_level;
}

inline bool configOutputOpenDrain(uint8_t pin)
{
        if (!detail::isValidOutputGpio(pin)) {
                return false;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_OUTPUT_OD;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&cfg) == ESP_OK;
}

// ============================================================
// Unchecked digital read/write — hot-path, no validation
// ============================================================
// Use gpio_ll for direct register access.
// gpio_ll_set_level compiles to a single W1TS/W1TC register write.
// No function call overhead, no pin check. Safe from ISR context.
// Caller MUST have configured the pin via a config*() call first.

inline bool read(uint8_t pin)
{
        return gpio_ll_get_level(&GPIO, static_cast<gpio_num_t>(pin)) != 0;
}

inline void setHigh(uint8_t pin)
{
        gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), 1);
}

inline void setLow(uint8_t pin)
{
        gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), 0);
}

inline void write(uint8_t pin, bool high)
{
        gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), high ? 1 : 0);
}

/// @brief Read-then-invert. NOT atomic — do not use if the pin is
/// shared between ISR and task code without external synchronisation.
inline void toggle(uint8_t pin)
{
        write(pin, !read(pin));
}

// ---- Convenience helpers (unchecked) ----

/// @brief Wrapper for setHigh(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
inline void writeHigh(uint8_t pin)
{
        setHigh(pin);
}

/// @brief Wrapper for setLow(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
inline void writeLow(uint8_t pin)
{
        setLow(pin);
}

/// @brief Wrapper for setHigh(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
inline void on(uint8_t pin)
{
        setHigh(pin);
}

/// @brief Wrapper for setLow(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
inline void off(uint8_t pin)
{
        setLow(pin);
}

/// @brief Wrapper for read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is HIGH, false otherwise.
inline bool isOn(uint8_t pin)
{
        return read(pin);
}

/// @brief Wrapper for read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is LOW, false otherwise.
inline bool isOff(uint8_t pin)
{
        return !read(pin);
}

/// @brief Wrapper for read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is HIGH, false otherwise.
inline bool isHigh(uint8_t pin)
{
        return read(pin);
}

/// @brief Wrapper for !read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is LOW, false otherwise.
inline bool isLow(uint8_t pin)
{
        return !read(pin);
}

/// @brief Wrapper for read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is HIGH, false otherwise.
inline bool isEnabled(uint8_t pin)
{
        return read(pin);
}

/// @brief Wrapper for !read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is LOW, false otherwise.
inline bool isDisabled(uint8_t pin)
{
        return !read(pin);
}

/// @brief Wrapper for !read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is LOW, false otherwise.
inline bool isOpen(uint8_t pin)
{
        return !read(pin);
}

/// @brief Wrapper for read(). Use only if your compiler optimizes inlined calls.
/// @param pin GPIO number.
/// @return true if the pin is HIGH, false otherwise.
inline bool isClosed(uint8_t pin)
{
        return read(pin);
}

// -----------------------------------------------------------
// Checked digital read/write — validates pin before each call
// -----------------------------------------------------------
// Use these when the pin number comes from configuration, user
// input, or any source not verified at compile time.
// Returns false if the pin is not a valid GPIO for the SoC.

/// @brief Read with pin validation. Returns false for invalid pins.
/// This is slower than unchecked read() due to the validation step, but safe for any pin
/// number.
/// @param pin GPIO number.
/// @param out Receives the pin level (true = HIGH) on success.
/// @return true if the pin is valid and was read, false otherwise.
inline bool checkedRead(uint8_t pin, bool &out)
{
        if (!detail::isValidGpio(pin)) {
                return false;
        }
        out = gpio_ll_get_level(&GPIO, static_cast<gpio_num_t>(pin)) != 0;
        return true;
}

/// @brief Set pin HIGH with validation.
/// This is slower than unchecked setHigh() due to the validation step, but safe for any pin
/// number.
/// @return true if the pin is a valid output GPIO, false otherwise.
inline bool checkedSetHigh(uint8_t pin)
{
        if (!detail::isValidOutputGpio(pin)) {
                return false;
        }
        gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), 1);
        return true;
}

/// @brief Set pin LOW with validation.
/// This is slower than unchecked setLow() due to the validation step, but safe for any pin
/// number.
/// @return true if the pin is a valid output GPIO, false otherwise.
inline bool checkedSetLow(uint8_t pin)
{
        if (!detail::isValidOutputGpio(pin)) {
                return false;
        }
        gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), 0);
        return true;
}

/// @brief Write pin level with validation.
/// This is slower than unchecked write() due to the validation step, but safe for any pin
/// number.
/// @return true if the pin is a valid output GPIO, false otherwise.
inline bool checkedWrite(uint8_t pin, bool high)
{
        if (!detail::isValidOutputGpio(pin)) {
                return false;
        }
        gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), high ? 1 : 0);
        return true;
}

// ---- Interrupt / ISR support ----

enum class InterruptEdge : uint8_t { EDGE_RISING = 0, EDGE_FALLING = 1, EDGE_ANY = 2 };

/// @brief Pull resistor mode for interrupt-enabled inputs.
enum class PullMode : uint8_t { NONE = 0, UP = 1, DOWN = 2 };

enum class IsrServiceInstall : uint8_t { Installed = 0, AlreadyInstalled = 1, Failed = 2 };

using GpioIsrHandler = void (*)(void *);

/// @brief Configure a pin as input with interrupt on the specified edge.
/// @param pin GPIO number.
/// @param edge Which edge(s) trigger the interrupt.
/// @param pull Internal pull resistor selection (default: NONE).
/// @return true on success, false on invalid pin or driver error.
inline bool configInputInterrupt(uint8_t pin, InterruptEdge edge, PullMode pull = PullMode::NONE)
{
        if (pull == PullMode::UP) {
                if (!detail::supportsInternalPullup(pin)) {
                        return false;
                }
        } else if (pull == PullMode::DOWN) {
                if (!detail::supportsInternalPulldown(pin)) {
                        return false;
                }
        } else {
                if (!detail::isValidGpio(pin)) {
                        return false;
                }
        }

        gpio_int_type_t intrType = GPIO_INTR_DISABLE;
        switch (edge) {
        case InterruptEdge::EDGE_RISING:
                intrType = GPIO_INTR_POSEDGE;
                break;
        case InterruptEdge::EDGE_FALLING:
                intrType = GPIO_INTR_NEGEDGE;
                break;
        case InterruptEdge::EDGE_ANY:
                intrType = GPIO_INTR_ANYEDGE;
                break;
        }

        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = (pull == PullMode::UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = (pull == PullMode::DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = intrType;
        return gpio_config(&cfg) == ESP_OK;
}

inline IsrServiceInstall installIsrService()
{
        constexpr uint8_t kNotInstalled = 0;
        constexpr uint8_t kInstalling = 1;
        constexpr uint8_t kInstalled = 2;

        static std::atomic<uint8_t> state{ kNotInstalled };

        uint8_t s = state.load(std::memory_order_acquire);
        for (;;) {
                if (s == kInstalled) {
                        return IsrServiceInstall::AlreadyInstalled;
                }

                if (s == kInstalling) {
                        do {
                                s = state.load(std::memory_order_acquire);
                        } while (s == kInstalling);
                        continue;
                }

                if (state.compare_exchange_weak(s, kInstalling, std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                        const esp_err_t err = gpio_install_isr_service(0);
                        if (err == ESP_OK) {
                                state.store(kInstalled, std::memory_order_release);
                                return IsrServiceInstall::Installed;
                        }
                        if (err == ESP_ERR_INVALID_STATE) {
                                state.store(kInstalled, std::memory_order_release);
                                return IsrServiceInstall::AlreadyInstalled;
                        }

                        state.store(kNotInstalled, std::memory_order_release);
                        return IsrServiceInstall::Failed;
                }
        }
}

inline bool addIsrHandler(uint8_t pin, GpioIsrHandler handler, void *context)
{
        return gpio_isr_handler_add(static_cast<gpio_num_t>(pin), handler, context) == ESP_OK;
}

inline bool removeIsrHandler(uint8_t pin)
{
        return gpio_isr_handler_remove(static_cast<gpio_num_t>(pin)) == ESP_OK;
}

// ---- PWM output ----
// Implementation in platforms/gpio_pwm_esp32.cpp (single TU — shared state).
// configPwm() is single-assignment: a second call for the same pin returns false.
// Duty range: 0 to (2^resolutionBits - 1), capped to avoid ESP32 hardware overflow.

bool configPwm(uint8_t pin, uint32_t freqHz = 1000, uint8_t resolutionBits = 8);
bool writePwm(uint8_t pin, uint32_t duty);

// ADC input?? see <ungula/hal/adc/adc_manager.h>.
// Conceptually ADC is its own peripheral, not a GPIO function

} // namespace ungula::hal::gpio
