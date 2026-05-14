// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <atomic>
#include <cstdint>
#include "ungula/hal/timer/i_hwtimer.h"

/// @brief Platform-dispatched hardware timer.
///
/// On ESP32 the backend wraps GPTimer (one GPTimer instance per `HwTimer`).
/// On host (and other unsupported platforms) the backend is a stub that
/// stores state but never fires. Tests of consumer code should use
/// `HwTimerFake` for deterministic alarm firing.
///
/// The public header deliberately exposes only the abstract interface
/// plus a `void*` handle and a 64-bit "last alarm count" — no ESP-IDF
/// GPTimer types leak. The ISR trampoline that ESP-IDF needs is defined
/// inside the ESP32 `.cpp` and only forwards to `fireFromIsr()`.

namespace ungula::hal::timer::drivers
{

class HwTimer final : public IHwTimer {
    public:
        HwTimer();
        ~HwTimer() override;

        HwTimer(const HwTimer &) = delete;
        HwTimer &operator=(const HwTimer &) = delete;

        HwTimerStatus begin(const HwTimerConfig &cfg) override;
        HwTimerStatus setCallback(IsrTimerCallback cb, void *ctx) override;
        HwTimerStatus startOneShotTicks(uint32_t ticks) override;

        /// ISR-context. The platform implementation places its body in IRAM
        /// via `IRAM_ATTR` on the function DEFINITION (see `.cpp`). We do not
        /// repeat the attribute on this declaration: `IRAM_ATTR` expands to
        /// `__attribute__((section(".iram1." __COUNTER__)))`, and a second
        /// expansion at the definition site would request a *different*
        /// section name — GCC reports it as a conflict.
        HwTimerStatus rearmFromIsr(uint32_t ticks) override;

        /// ISR-context. Placement-in-IRAM decided at the definition site.
        /// See `rearmFromIsr` for the rationale.
        HwTimerStatus disarmFromIsr() override;

        HwTimerStatus stop() override;
        uint32_t resolutionHz() const override;
        bool isArmed() const override;

        /// ISR-only entry point invoked by the ESP32 trampoline. Not part of
        /// the user-facing API — declared public so the free trampoline in
        /// the ESP32 backend can dispatch without a friend declaration. Do
        /// not call from application code.
        ///
        /// `firedAlarmCount` is the absolute counter value at which the
        /// hardware alarm fired (`gptimer_alarm_event_data_t::alarm_value`).
        /// `rearmFromIsr` consults it to schedule the next alarm without an
        /// extra `gptimer_get_raw_count` call.
        ///
        /// Body placed in IRAM by the platform `.cpp` definition. See the
        /// IRAM_ATTR placement note above.
        void fireFromIsr(uint64_t firedAlarmCount);

    private:
        void *handle_ = nullptr; // opaque (ESP32: gptimer_handle_t)
        IsrTimerCallback cb_ = nullptr;
        void *ctx_ = nullptr;
        uint32_t resolution_ = 0;
        uint32_t minTicks_ = 5;
        bool enabled_ = false; // gptimer_enable() succeeded

        /// Hardware counter is running (`gptimer_start` succeeded, no
        /// matching `gptimer_stop` yet). Task-context only — `startOneShotTicks`
        /// / `stop` / destructor write it; the ISR never touches it.
        ///
        /// Separate from `armed_` because the two lifecycles diverge: with
        /// `auto_reload_on_alarm = false`, the counter keeps running after a
        /// one-shot alarm fires (which atomically clears `armed_`), but the
        /// GPTimer FSM stays in `GPTIMER_FSM_RUN`. A second `startOneShotTicks`
        /// must call `gptimer_stop` first to return the FSM to
        /// `GPTIMER_FSM_ENABLE` before `gptimer_start` will accept it; gating
        /// that stop on `armed_` would miss this case.
        bool started_ = false;

        /// True between any successful arm/re-arm and the next firing or
        /// disarm. Drives both `isArmed()` and the ISR's "should I call the
        /// user callback?" gate, so the type must be safe for concurrent
        /// access from task and ISR contexts on dual-core ESP32.
        std::atomic<bool> armed_{ false };

        /// Last counter value at which the hardware alarm fired. Cached
        /// inside the trampoline so `rearmFromIsr` can schedule the next
        /// alarm as `lastAlarmCount_ + ticks` without re-reading the counter
        /// register on every pulse.
        ///
        /// **Invariant — ISR-only access path:**
        ///   - Written inside `fireFromIsr()` (ISR context).
        ///   - Read inside `rearmFromIsr()` (ISR context, always called from
        ///     within the user callback that `fireFromIsr` invokes — same
        ///     core, same ISR chain).
        ///   - Reset to 0 inside `begin()` and `startOneShotTicks()` (task
        ///     context) but only when the hardware counter is stopped, so
        ///     no ISR can be racing.
        ///
        /// 64-bit reads/writes are NOT atomic on Xtensa (two 32-bit ops).
        /// Do not add a task-context read path without first switching to
        /// a critical-section-protected access pattern or a 32-bit cached
        /// shadow. `volatile` is intentionally NOT used: it does not
        /// provide synchronisation, and the invariant above already
        /// guarantees there is no concurrent access window.
        uint64_t lastAlarmCount_ = 0;
};

} // namespace ungula::hal::timer::drivers
