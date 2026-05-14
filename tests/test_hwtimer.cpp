// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <ungula/hal/timer/drivers/hwtimer.h>
#include <ungula/hal/timer/drivers/hwtimer_fake.h>

namespace
{

using ungula::hal::timer::HwTimerConfig;
using ungula::hal::timer::HwTimerStatus;
using ungula::hal::timer::drivers::HwTimer;
using ungula::hal::timer::drivers::HwTimerFake;

struct CallbackState {
        uint32_t count = 0;
};

void onTimer(void *ctx)
{
        auto *state = static_cast<CallbackState *>(ctx);
        state->count++;
}

// ---- Default (host) backend stub: structural transitions only ----

TEST(HwTimerDefaultBackendTest, BeginRejectsZeroResolution)
{
        HwTimer timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 0;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::InvalidConfig);
}

TEST(HwTimerDefaultBackendTest, BeginIsIdempotentlyRejected)
{
        HwTimer timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::AlreadyInitialized);
}

TEST(HwTimerDefaultBackendTest, LifecycleTransitions)
{
        HwTimer timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;
        cfg.minTicks = 5;

        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.resolutionHz(), 1'000'000U);
        EXPECT_EQ(timer.setCallback(&onTimer, nullptr), HwTimerStatus::Ok);

        EXPECT_EQ(timer.startOneShotTicks(100), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());

        EXPECT_EQ(timer.rearmFromIsr(200), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());

        EXPECT_EQ(timer.disarmFromIsr(), HwTimerStatus::Ok);
        EXPECT_FALSE(timer.isArmed());

        EXPECT_EQ(timer.stop(), HwTimerStatus::Ok);
        EXPECT_FALSE(timer.isArmed());
}

TEST(HwTimerDefaultBackendTest, OperationsFailBeforeBegin)
{
        HwTimer timer;
        EXPECT_EQ(timer.setCallback(&onTimer, nullptr), HwTimerStatus::NotInitialized);
        EXPECT_EQ(timer.startOneShotTicks(100), HwTimerStatus::NotInitialized);
        EXPECT_EQ(timer.rearmFromIsr(100), HwTimerStatus::NotInitialized);
        EXPECT_EQ(timer.disarmFromIsr(), HwTimerStatus::NotInitialized);
        EXPECT_EQ(timer.stop(), HwTimerStatus::NotInitialized);
}

TEST(HwTimerDefaultBackendTest, MinTicksValidation)
{
        HwTimer timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;
        cfg.minTicks = 10;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(5), HwTimerStatus::InvalidTicks);
        EXPECT_EQ(timer.rearmFromIsr(0), HwTimerStatus::InvalidTicks);
}

TEST(HwTimerDefaultBackendTest, StopKeepsNotInitializedStateAndCanRestart)
{
        HwTimer timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;

        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(50), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());

        EXPECT_EQ(timer.stop(), HwTimerStatus::Ok);
        EXPECT_FALSE(timer.isArmed());

        EXPECT_EQ(timer.startOneShotTicks(70), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());
}

// Regression for the ESP32 lifecycle bug: a second startOneShotTicks
// after a fire-without-rearm must succeed. The default backend doesn't
// drive a real timer FSM, but `isArmed()` must reflect re-arming and
// the return value must be Ok — the same call chain on ESP32 would
// previously have returned BackendError because gptimer_start was
// invoked against an already-running timer.
TEST(HwTimerDefaultBackendTest, RestartAfterFireSucceeds)
{
        HwTimer timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;
        cfg.minTicks = 5;

        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(100), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());

        // Simulate fire-without-rearm by calling disarmFromIsr (same end
        // state as the trampoline taking exchange(false) and the user
        // callback not re-arming).
        EXPECT_EQ(timer.disarmFromIsr(), HwTimerStatus::Ok);
        EXPECT_FALSE(timer.isArmed());

        // Critical: this call would have returned BackendError on ESP32
        // before the `started_` fix because the hardware counter was still
        // running. It must return Ok now.
        EXPECT_EQ(timer.startOneShotTicks(200), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());
}

// ---- Fake backend: drives the callback deterministically ----

TEST(HwTimerFakeTest, FiresOnceThenDisarmedByOneShotSemantics)
{
        HwTimerFake timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;

        CallbackState state;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.setCallback(&onTimer, &state), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(500), HwTimerStatus::Ok);

        EXPECT_TRUE(timer.isArmed());
        timer.fire();
        EXPECT_EQ(state.count, 1U);
        EXPECT_FALSE(timer.isArmed());

        // Subsequent fires must be dropped — true one-shot.
        timer.fire();
        timer.fire();
        EXPECT_EQ(state.count, 1U);
        EXPECT_EQ(timer.firesDroppedWhileDisarmed, 2U);
}

// Callback that re-arms the timer from inside itself; models how the
// pulse engine will run.
struct RearmingCallbackState {
        HwTimerFake *timer = nullptr;
        uint32_t count = 0;
        uint32_t maxFires = 3;
};
void onTimerRearm(void *ctx)
{
        auto *s = static_cast<RearmingCallbackState *>(ctx);
        s->count++;
        if (s->count < s->maxFires) {
                s->timer->rearmFromIsr(200);
        }
}

TEST(HwTimerFakeTest, RearmFromCallbackKeepsFiring)
{
        HwTimerFake timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;

        RearmingCallbackState s{ &timer, 0, 3 };

        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.setCallback(&onTimerRearm, &s), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(500), HwTimerStatus::Ok);

        // fireMany stops at the first dropped fire.
        const uint32_t actual = timer.fireMany(10);
        EXPECT_EQ(s.count, 3U);
        EXPECT_EQ(actual, 3U);
        EXPECT_FALSE(timer.isArmed()); // last callback chose not to re-arm
        EXPECT_EQ(timer.lastArmedTicks(), 200U);
}

TEST(HwTimerFakeTest, DisarmFromIsrStopsTheTrain)
{
        HwTimerFake timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(100), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());
        EXPECT_EQ(timer.disarmFromIsr(), HwTimerStatus::Ok);
        EXPECT_FALSE(timer.isArmed());
        timer.fire();
        EXPECT_EQ(timer.firesDroppedWhileDisarmed, 1U);
}

// Models the race window the production ISR gate is meant to close:
// task code calls `disarmFromIsr()` between an alarm being scheduled
// and the hardware firing it. The fire must NOT invoke the user callback.
TEST(HwTimerFakeTest, DisarmRaceDuringScheduledFireIsDropped)
{
        HwTimerFake timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;

        CallbackState state;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.setCallback(&onTimer, &state), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(500), HwTimerStatus::Ok);

        // Race: someone disarms between schedule and fire.
        EXPECT_EQ(timer.disarmFromIsr(), HwTimerStatus::Ok);
        timer.fire(); // hardware "fired" — but the gate must drop it

        EXPECT_EQ(state.count, 0U);
        EXPECT_EQ(timer.firesDroppedWhileDisarmed, 1U);
}

TEST(HwTimerFakeTest, MinTicksValidationOnFake)
{
        HwTimerFake timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;
        cfg.minTicks = 10;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(3), HwTimerStatus::InvalidTicks);
        EXPECT_FALSE(timer.isArmed());
}

TEST(HwTimerFakeTest, RearmBeforeStartArmsAndThenFiresOnce)
{
        HwTimerFake timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;

        CallbackState state;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.setCallback(&onTimer, &state), HwTimerStatus::Ok);

        EXPECT_EQ(timer.rearmFromIsr(120), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());

        timer.fire();
        EXPECT_EQ(state.count, 1U);
        EXPECT_FALSE(timer.isArmed());
}

TEST(HwTimerFakeTest, StopDisarmsAndAllowsRestart)
{
        HwTimerFake timer;
        HwTimerConfig cfg;
        cfg.resolutionHz = 1'000'000;

        CallbackState state;
        EXPECT_EQ(timer.begin(cfg), HwTimerStatus::Ok);
        EXPECT_EQ(timer.setCallback(&onTimer, &state), HwTimerStatus::Ok);
        EXPECT_EQ(timer.startOneShotTicks(80), HwTimerStatus::Ok);
        EXPECT_TRUE(timer.isArmed());

        EXPECT_EQ(timer.stop(), HwTimerStatus::Ok);
        EXPECT_FALSE(timer.isArmed());
        timer.fire();
        EXPECT_EQ(state.count, 0U);

        EXPECT_EQ(timer.startOneShotTicks(90), HwTimerStatus::Ok);
        timer.fire();
        EXPECT_EQ(state.count, 1U);
}

} // namespace
