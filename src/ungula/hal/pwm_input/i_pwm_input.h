// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stdint.h>

/// @brief Abstract single-channel PWM-input capture.
///
/// Wraps "this pin is carrying a PWM signal — give me the latest measured
/// high-pulse width and period". Used by encoders that report angle as a
/// duty cycle (AS5600 PWM mode, MT6701 PWM mode, etc.) and by anything
/// else that consumes a measured PWM input (servo feedback, RC input).
///
/// The interface is intentionally narrow: callers ask "what was the most
/// recent measurement", they do not subscribe to per-edge events. The
/// underlying capture mechanism (GPIO interrupt, MCPWM input, RMT, LEDC,
/// etc.) is a backend concern.
///
/// State after `begin()`:
///   - `hasSample()` is `false` until the first complete period has been
///     observed.
///   - `lastHighTimeUs()` / `lastPeriodUs()` return 0 until then.
///   - `sampleAgeUs()` reports microseconds since the last edge so callers
///     can flag a stalled signal even when `hasSample()` is `true`.
///
/// Logging is the host application's responsibility — this interface does
/// not log.
///
/// ## ISR-driven consumers
///
/// Polling `lastHighTimeUs()` / `lastPeriodUs()` from a task is the
/// default pattern. For consumers that need per-frame latency without
/// running a polling task, `setSampleCallback()` registers a function
/// the backend will invoke from ISR context immediately after each
/// complete period sample is captured. The callback runs in the same
/// constraints as any GPIO ISR: short, no float on hosts that lack
/// hardware FP, no logging, no I2C / SPI / blocking calls. See the
/// `As5600Pwm::enableIsrUpdates()` consumer for a concrete example.

namespace ungula::hal::pwm_input
{

    /// @brief ISR-context callback fired once per complete PWM period.
    ///
    /// The backend invokes it after the rising-edge handler has updated
    /// the high-time + period state, so the callee can read
    /// `lastHighTimeUs()` / `lastPeriodUs()` and see consistent values.
    /// `ctx` is the opaque pointer the caller supplied to
    /// `setSampleCallback()`.
    using SampleCallback = void (*)(void *ctx);

    class IPwmInput {
    public:
        virtual ~IPwmInput() = default;

        IPwmInput(const IPwmInput &) = delete;
        IPwmInput &operator=(const IPwmInput &) = delete;

        /// @brief Install the capture for `pin`. Idempotent: a second
        ///        call with the same pin is treated as already-installed
        ///        and returns `false`.
        virtual bool begin(uint8_t pin) = 0;

        /// @brief Tear down the capture. Safe to call when not installed.
        virtual bool stop() = 0;

        /// @brief Most recent high-pulse width in microseconds. Returns
        ///        0 before the first complete period.
        virtual uint32_t lastHighTimeUs() const = 0;

        /// @brief Most recent full period in microseconds. Returns 0
        ///        before the first complete period.
        virtual uint32_t lastPeriodUs() const = 0;

        /// @brief True once at least one complete period has been
        ///        captured.
        virtual bool hasSample() const = 0;

        /// @brief Microseconds since the most recent edge. Used to
        ///        detect a stalled signal even when `hasSample()` is
        ///        `true`.
        virtual uint32_t sampleAgeUs() const = 0;

        /// @brief Pin the capture is bound to (only valid after `begin()`).
        virtual uint8_t pin() const = 0;

        /// @brief Install a per-period ISR callback. Pass `nullptr`
        ///        as `cb` to disarm. At most one callback per
        ///        instance — re-arming replaces the previous one.
        ///        The callback fires from interrupt context after
        ///        every complete period, so it must respect the
        ///        usual ISR rules (short, IRAM-safe, no blocking
        ///        calls).
        virtual void setSampleCallback(SampleCallback cb, void *ctx) = 0;

    protected:
        IPwmInput() = default;
    };

} // namespace ungula::hal::pwm_input
