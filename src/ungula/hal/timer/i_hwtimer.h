// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstdint>

namespace ungula::hal::timer
{

/// ISR-context callback invoked when the timer's alarm fires.
///
/// The callback runs in true interrupt context on ESP32 — same rules as
/// a GPIO ISR:
///   - Must be placed in IRAM. Mark with `UNGULA_ISR_ATTR`.
///   - No logging, UART, I2C, SPI, file I/O.
///   - No allocation, no exceptions.
///   - No FreeRTOS API that takes locks (use the *FromISR variants only).
///   - Keep it tight — do work, optionally call `rearmFromIsr()` or
///     `disarmFromIsr()` on the timer that fired, return.
///
/// The type cannot enforce these rules; documentation has to.
using IsrTimerCallback = void (*)(void *ctx);

enum class HwTimerStatus : uint8_t {
        Ok = 0,
        NotInitialized,
        AlreadyInitialized,
        InvalidConfig,
        InvalidTicks,
        BackendError,
        Unsupported,
        Busy,
};

inline const char *hwTimerStatusToString(HwTimerStatus s)
{
        switch (s) {
        case HwTimerStatus::Ok:
                return "Ok";
        case HwTimerStatus::NotInitialized:
                return "NotInitialized";
        case HwTimerStatus::AlreadyInitialized:
                return "AlreadyInitialized";
        case HwTimerStatus::InvalidConfig:
                return "InvalidConfig";
        case HwTimerStatus::InvalidTicks:
                return "InvalidTicks";
        case HwTimerStatus::BackendError:
                return "BackendError";
        case HwTimerStatus::Unsupported:
                return "Unsupported";
        case HwTimerStatus::Busy:
                return "Busy";
        }
        return "Unknown";
}

struct HwTimerConfig {
        /// Tick rate of the underlying counter. 1 MHz → 1 µs per tick is the
        /// project default for step-pulse generation. Higher rates buy
        /// sub-microsecond resolution but shrink the maximum representable
        /// 32-bit interval.
        uint32_t resolutionHz = 1'000'000;

        /// Lower bound enforced by `startOneShotTicks` / `rearmFromIsr` to
        /// guarantee the alarm has time to settle past the current counter
        /// value before the hardware compares. 5 ticks at 1 MHz = 5 µs which
        /// comfortably covers the ISR's set-alarm path; the pulse engine's
        /// minimum-pulse-width constraint already keeps real workloads well
        /// above this. Set higher if a backend has higher overhead.
        uint32_t minTicks = 5;
};

/// Abstract one-shot hardware timer for deterministic motion control.
///
/// Semantics — true one-shot:
///   - `startOneShotTicks(N)`: fires the callback exactly once, N ticks
///     in the future. After firing, the alarm is spent. No further
///     callbacks until `rearmFromIsr` or another `startOneShotTicks`.
///   - `rearmFromIsr(N)`: ISR-safe. Schedules the next callback N ticks
///     after the alarm that just fired. Designed to be invoked from
///     inside the alarm callback to produce a variable-period pulse
///     train without leaving ISR context.
///   - `disarmFromIsr()`: ISR-safe. Guarantees no further callback will
///     fire until `startOneShotTicks` is called again from task context.
///     Implemented as a software gate at the trampoline boundary — the
///     hardware counter keeps running but the user callback is no longer
///     invoked. Use this at the last pulse of a move so the engine
///     never overruns its segment queue.
///
/// All non-ISR methods (`begin`, `setCallback`, `startOneShotTicks`,
/// `stop`) must be called from task context. `rearmFromIsr` and
/// `disarmFromIsr` are the only ISR-safe operations.
///
/// State model (kept precise on purpose):
///   - "begun"  — `begin()` succeeded; timer resources allocated.
///   - "armed"  — an alarm is scheduled and the trampoline will invoke
///                the user callback when it fires. Cleared at fire time
///                and re-set by `rearmFromIsr`; cleared by
///                `disarmFromIsr` / `stop`.
///   - The hardware counter is a separate axis; `stop()` halts it from
///     task context, but inside the ISR we never touch it.
class IHwTimer {
    public:
        virtual ~IHwTimer() = default;

        /// Allocate and configure the timer. Returns `AlreadyInitialized` if
        /// called twice without an intervening `stop()`. `InvalidConfig` if
        /// `resolutionHz == 0`.
        virtual HwTimerStatus begin(const HwTimerConfig &cfg) = 0;

        /// Register the ISR-context callback. Pass `cb = nullptr` to disarm
        /// the callback slot (the timer hardware is unaffected — use
        /// `stop()` / `disarmFromIsr()` to halt alarms). Returns
        /// `NotInitialized` if `begin()` hasn't run.
        virtual HwTimerStatus setCallback(IsrTimerCallback cb, void *ctx) = 0;

        /// Reset the counter and schedule the next (and only) alarm `ticks`
        /// from now. Returns `InvalidTicks` if `ticks < minTicks`,
        /// `NotInitialized` if `begin()` hasn't run, `BackendError` on
        /// driver failure. Idempotent if already armed — re-arms cleanly.
        virtual HwTimerStatus startOneShotTicks(uint32_t ticks) = 0;

        /// ISR-safe re-arm. Sets the next alarm to fire `ticks` ticks after
        /// the alarm that just fired (no current-count read needed — the
        /// implementation tracks the last alarm count from the event data).
        /// `InvalidTicks` if `ticks < minTicks`.
        ///
        /// **ISR-context.** Concrete implementations must place the
        /// function body in IRAM by tagging the DEFINITION (not this
        /// declaration) with `UNGULA_ISR_ATTR` / `IRAM_ATTR`. The attribute
        /// is intentionally absent from the declaration here: `IRAM_ATTR`
        /// expands to `__attribute__((section(".iram1." __COUNTER__)))`,
        /// and a second expansion at the definition site requests a
        /// *different* section name — GCC reports it as a conflict.
        virtual HwTimerStatus rearmFromIsr(uint32_t ticks) = 0;

        /// ISR-safe disarm. Guarantees no further user callback will be
        /// invoked until the next `startOneShotTicks` from task context.
        /// Does not stop the hardware counter (that's reserved for `stop()`).
        ///
        /// **ISR-context.** Implementations must mark the definition with
        /// `UNGULA_ISR_ATTR`. See `rearmFromIsr` for the attribute-placement
        /// rationale.
        virtual HwTimerStatus disarmFromIsr() = 0;

        /// Task-context full stop. Halts the hardware counter AND clears the
        /// armed state. Safe to call after `begin()`; returns `NotInitialized`
        /// if `begin()` has not run yet.
        virtual HwTimerStatus stop() = 0;

        /// Tick rate selected by `begin()`. Returns 0 before `begin()`.
        virtual uint32_t resolutionHz() const = 0;

        /// True between any successful arm/re-arm and the next firing /
        /// `disarmFromIsr` / `stop`. Replaces the older `isRunning()` —
        /// renamed because the hardware counter may still be ticking when
        /// no alarm is armed; what callers actually want to know is
        /// "will my callback fire?".
        virtual bool isArmed() const = 0;
};

} // namespace ungula::hal::timer
