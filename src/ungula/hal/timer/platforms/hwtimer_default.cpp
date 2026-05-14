// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if !(defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32))

#include "ungula/hal/timer/drivers/hwtimer.h"

/// Host-side stub. Stores configuration and accepts every operation that
/// makes structural sense, but never fires the callback on its own.
/// Tests of consumer code that need the callback to fire deterministically
/// must use `HwTimerFake`.
///
/// **STM32 (and any non-ESP32 MCU) port note:** the platform dispatch
/// falls into this `.cpp` on anything that is not ESP32 (see the macro
/// guard below). That is correct for host unit tests, NOT for
/// production on an MCU. Before running motor code on STM32, add a
/// real `platforms/hwtimer_stm32.cpp` (the interface is portable —
/// `startOneShotTicks` maps onto TIM CCR + one-pulse mode, `rearmFromIsr`
/// reuses the last compare value, `disarmFromIsr` disables the compare
/// interrupt) and exclude STM32 from this stub's macro guard.

namespace ungula::hal::timer::drivers
{

HwTimer::HwTimer() = default;
HwTimer::~HwTimer()
{
        stop();
}

void HwTimer::fireFromIsr(uint64_t firedAlarmCount)
{
        lastAlarmCount_ = firedAlarmCount;
        if (!armed_.exchange(false, std::memory_order_acq_rel)) {
                return;
        }
        if (cb_)
                cb_(ctx_);
}

HwTimerStatus HwTimer::begin(const HwTimerConfig &cfg)
{
        if (handle_)
                return HwTimerStatus::AlreadyInitialized;
        if (cfg.resolutionHz == 0)
                return HwTimerStatus::InvalidConfig;
        resolution_ = cfg.resolutionHz;
        minTicks_ = (cfg.minTicks == 0) ? 1u : cfg.minTicks;
        handle_ = reinterpret_cast<void *>(0x1);
        enabled_ = true;
        started_ = false;
        armed_.store(false, std::memory_order_release);
        lastAlarmCount_ = 0;
        return HwTimerStatus::Ok;
}

HwTimerStatus HwTimer::setCallback(IsrTimerCallback cb, void *ctx)
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;
        cb_ = cb;
        ctx_ = ctx;
        return HwTimerStatus::Ok;
}

HwTimerStatus HwTimer::startOneShotTicks(uint32_t ticks)
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;
        if (ticks < minTicks_)
                return HwTimerStatus::InvalidTicks;
        started_ = true;
        lastAlarmCount_ = 0;
        armed_.store(true, std::memory_order_release);
        return HwTimerStatus::Ok;
}

HwTimerStatus HwTimer::rearmFromIsr(uint32_t ticks)
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;
        if (ticks < minTicks_)
                return HwTimerStatus::InvalidTicks;
        armed_.store(true, std::memory_order_release);
        return HwTimerStatus::Ok;
}

HwTimerStatus HwTimer::disarmFromIsr()
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;
        armed_.store(false, std::memory_order_release);
        return HwTimerStatus::Ok;
}

HwTimerStatus HwTimer::stop()
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;
        started_ = false;
        armed_.store(false, std::memory_order_release);
        return HwTimerStatus::Ok;
}

uint32_t HwTimer::resolutionHz() const
{
        return resolution_;
}
bool HwTimer::isArmed() const
{
        return armed_.load(std::memory_order_acquire);
}

} // namespace ungula::hal::timer::drivers

#endif
