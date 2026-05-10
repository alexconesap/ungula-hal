// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "ungula/hal/pwm_input/drivers/pwm_input.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"

namespace ungula::hal::pwm_input::drivers
{

    namespace
    {

        // One slot per active capture. `gpio_isr_handler_add()` only
        // takes a single `void*` payload, so the ISR routes through this
        // file-static array indexed by the GPIO number to find its slot.
        // 64 covers every GPIO any ESP32 variant exposes today.
        struct CaptureState {
            volatile uint64_t lastRiseUs = 0;
            volatile uint64_t lastFallUs = 0;
            volatile uint64_t lastEdgeUs = 0;
            volatile uint32_t highTimeUs = 0;
            volatile uint32_t periodUs = 0;
            volatile bool hasSample = false;
            bool inUse = false;

            // Per-period ISR callback. `cb` is read once per edge;
            // the host writes it from `setSampleCallback()`. Single
            // 32-bit aligned pointer write is atomic on Xtensa /
            // RISC-V — no extra locking needed.
            SampleCallback cb = nullptr;
            void *cbCtx = nullptr;
        };

        constexpr size_t kMaxGpio = 64;
        CaptureState g_state[kMaxGpio]{};
        bool g_isrServiceInstalled = false;

        IRAM_ATTR void onEdge(void *arg)
        {
            const uintptr_t pin = reinterpret_cast<uintptr_t>(arg);
            if (pin >= kMaxGpio) {
                return;
            }
            CaptureState &s = g_state[pin];
            const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
            const int level = gpio_get_level(static_cast<gpio_num_t>(pin));
            s.lastEdgeUs = nowUs;
            if (level != 0) {
                // Rising edge → period = rise-to-rise.
                bool fireCallback = false;
                if (s.lastRiseUs != 0U) {
                    s.periodUs = static_cast<uint32_t>(nowUs - s.lastRiseUs);
                    s.hasSample = true;
                    fireCallback = true;
                }
                s.lastRiseUs = nowUs;
                // Fire after state is consistent. Read the pointer
                // through a local so a concurrent setSampleCallback()
                // on the host cannot see it half-updated.
                if (fireCallback) {
                    SampleCallback cb = s.cb;
                    if (cb != nullptr) {
                        cb(s.cbCtx);
                    }
                }
            } else {
                if (s.lastRiseUs != 0U) {
                    s.highTimeUs = static_cast<uint32_t>(nowUs - s.lastRiseUs);
                }
                s.lastFallUs = nowUs;
            }
        }

    } // namespace

    PwmInput::PwmInput() = default;

    PwmInput::~PwmInput()
    {
        if (installed_) {
            (void)stop();
        }
    }

    bool PwmInput::begin(uint8_t pin)
    {
        if (installed_ || pin >= kMaxGpio) {
            return false;
        }
        gpio_config_t cfg{};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_ANYEDGE;
        if (gpio_config(&cfg) != ESP_OK) {
            return false;
        }
        if (!g_isrServiceInstalled) {
            const esp_err_t e = gpio_install_isr_service(0);
            if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
                return false;
            }
            g_isrServiceInstalled = true;
        }
        g_state[pin] = CaptureState{};
        g_state[pin].inUse = true;
        if (gpio_isr_handler_add(static_cast<gpio_num_t>(pin), &onEdge,
                                 reinterpret_cast<void *>(static_cast<uintptr_t>(pin))) != ESP_OK) {
            g_state[pin].inUse = false;
            return false;
        }
        pin_ = pin;
        installed_ = true;
        return true;
    }

    bool PwmInput::stop()
    {
        if (!installed_) {
            return true;
        }
        gpio_isr_handler_remove(static_cast<gpio_num_t>(pin_));
        g_state[pin_].inUse = false;
        installed_ = false;
        return true;
    }

    uint32_t PwmInput::lastHighTimeUs() const
    {
        if (!installed_) {
            return 0U;
        }
        return g_state[pin_].highTimeUs;
    }

    uint32_t PwmInput::lastPeriodUs() const
    {
        if (!installed_) {
            return 0U;
        }
        return g_state[pin_].periodUs;
    }

    bool PwmInput::hasSample() const
    {
        if (!installed_) {
            return false;
        }
        return g_state[pin_].hasSample;
    }

    uint32_t PwmInput::sampleAgeUs() const
    {
        if (!installed_) {
            return 0U;
        }
        const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
        const uint64_t edge = g_state[pin_].lastEdgeUs;
        if (edge == 0U || nowUs < edge) {
            return 0U;
        }
        return static_cast<uint32_t>(nowUs - edge);
    }

    void PwmInput::setSampleCallback(SampleCallback cb, void *ctx)
    {
        if (!installed_) {
            return; // No-op until begin() — slot belongs to no one.
        }
        // Order matters: store ctx before cb so the ISR never reads a
        // new function pointer paired with a stale context.
        CaptureState &s = g_state[pin_];
        s.cbCtx = ctx;
        s.cb = cb;
    }

} // namespace ungula::hal::pwm_input::drivers

#endif // ESP_PLATFORM
