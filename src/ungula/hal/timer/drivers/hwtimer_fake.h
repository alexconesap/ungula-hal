// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <atomic>
#include "ungula/hal/timer/i_hwtimer.h"

namespace ungula::hal::timer::drivers
{

/// Header-only fake. Lets a host test drive the alarm callback manually
/// and inspect every state transition. Models true one-shot semantics
/// AND the trampoline's "drop fire if disarmed" gate, so tests can
/// reproduce the exact behaviour the production ESP32 backend exposes
/// — including the race window where `disarmFromIsr` lands between an
/// alarm being scheduled and being fired.
///
///   - `startOneShotTicks(N)`  → arms the timer.
///   - `fire()`                → if armed, clears `armed_` and invokes
///                               the callback. If the callback calls
///                               `rearmFromIsr()`, `armed_` becomes true
///                               again for the next `fire()`.
///   - `fire()` while disarmed → silently dropped; bumps the
///                               `firesDroppedWhileDisarmed` counter so
///                               tests can assert it didn't happen.
///   - `disarmFromIsr()` / `stop()` → clear armed. Subsequent `fire()`
///                                    calls are no-ops.
///
/// Typical use in a pulse-engine test:
///
/// ```cpp
/// HwTimerFake timer;
/// timer.begin({1'000'000u, 5u});
/// timer.setCallback(&onAlarm, &engine);
/// timer.startOneShotTicks(500);
/// EXPECT_TRUE(timer.isArmed());
/// timer.fire();                            // engine emits one edge,
///                                          // calls rearmFromIsr(500).
/// EXPECT_TRUE(timer.isArmed());            // still armed for next edge
/// EXPECT_EQ(timer.lastArmedTicks(), 500u);
/// timer.fireMany(2000);                    // run the whole move
/// EXPECT_FALSE(timer.isArmed());           // engine called disarmFromIsr at end
/// ```
class HwTimerFake final : public IHwTimer {
    public:
        HwTimerStatus begin(const HwTimerConfig &cfg) override
        {
                beginCallCount++;
                if (begun_)
                        return HwTimerStatus::AlreadyInitialized;
                if (cfg.resolutionHz == 0)
                        return HwTimerStatus::InvalidConfig;
                cfg_ = cfg;
                if (cfg_.minTicks == 0)
                        cfg_.minTicks = 1;
                begun_ = true;
                armed_.store(false, std::memory_order_release);
                return HwTimerStatus::Ok;
        }

        HwTimerStatus setCallback(IsrTimerCallback cb, void *ctx) override
        {
                setCallbackCallCount++;
                if (!begun_)
                        return HwTimerStatus::NotInitialized;
                cb_ = cb;
                ctx_ = ctx;
                return HwTimerStatus::Ok;
        }

        HwTimerStatus startOneShotTicks(uint32_t ticks) override
        {
                startCallCount++;
                if (!begun_)
                        return HwTimerStatus::NotInitialized;
                if (ticks < cfg_.minTicks)
                        return HwTimerStatus::InvalidTicks;
                lastArmedTicks_ = ticks;
                armed_.store(true, std::memory_order_release);
                return HwTimerStatus::Ok;
        }

        HwTimerStatus rearmFromIsr(uint32_t ticks) override
        {
                rearmCallCount++;
                if (!begun_)
                        return HwTimerStatus::NotInitialized;
                if (ticks < cfg_.minTicks)
                        return HwTimerStatus::InvalidTicks;
                lastArmedTicks_ = ticks;
                armed_.store(true, std::memory_order_release);
                return HwTimerStatus::Ok;
        }

        HwTimerStatus disarmFromIsr() override
        {
                disarmCallCount++;
                if (!begun_)
                        return HwTimerStatus::NotInitialized;
                armed_.store(false, std::memory_order_release);
                return HwTimerStatus::Ok;
        }

        HwTimerStatus stop() override
        {
                stopCallCount++;
                if (!begun_)
                        return HwTimerStatus::NotInitialized;
                armed_.store(false, std::memory_order_release);
                return HwTimerStatus::Ok;
        }

        uint32_t resolutionHz() const override
        {
                return cfg_.resolutionHz;
        }
        bool isArmed() const override
        {
                return armed_.load(std::memory_order_acquire);
        }

        // ---- Test knobs ------------------------------------------------

        /// Simulate the hardware alarm firing once. Mirrors the production
        /// trampoline: atomically exchange `armed_` for false; if it was
        /// already false (i.e. someone disarmed between scheduling and
        /// firing), drop the fire and bump the drop counter. Otherwise
        /// invoke the registered callback.
        void fire()
        {
                fireAttemptCount++;
                if (!armed_.exchange(false, std::memory_order_acq_rel)) {
                        firesDroppedWhileDisarmed++;
                        return;
                }
                if (cb_) {
                        cb_(ctx_);
                }
                fireCallCount++;
        }

        /// Fire repeatedly. Stops at `n` calls OR the moment the callback
        /// stops re-arming (whichever comes first). Useful for stepping
        /// through a whole planned move in one test line.
        uint32_t fireMany(uint32_t n)
        {
                uint32_t actual = 0;
                for (uint32_t i = 0; i < n; ++i) {
                        if (!armed_.load(std::memory_order_acquire))
                                break;
                        fire();
                        ++actual;
                }
                return actual;
        }

        // ---- Inspectors ------------------------------------------------

        uint32_t lastArmedTicks() const
        {
                return lastArmedTicks_;
        }
        bool isBegun() const
        {
                return begun_;
        }

        // ---- Counters --------------------------------------------------

        uint32_t beginCallCount = 0;
        uint32_t setCallbackCallCount = 0;
        uint32_t startCallCount = 0;
        uint32_t rearmCallCount = 0;
        uint32_t disarmCallCount = 0;
        uint32_t stopCallCount = 0;
        uint32_t fireAttemptCount = 0;
        uint32_t fireCallCount = 0;
        uint32_t firesDroppedWhileDisarmed = 0;

    private:
        HwTimerConfig cfg_{};
        IsrTimerCallback cb_ = nullptr;
        void *ctx_ = nullptr;
        bool begun_ = false;
        std::atomic<bool> armed_{ false };
        uint32_t lastArmedTicks_ = 0;
};

} // namespace ungula::hal::timer::drivers
