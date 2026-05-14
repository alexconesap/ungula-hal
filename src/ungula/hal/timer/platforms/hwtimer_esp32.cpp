// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)

#include "ungula/hal/timer/drivers/hwtimer.h"

#include <driver/gptimer.h>
#include <esp_attr.h>

namespace ungula::hal::timer::drivers
{

/// ISR trampoline.
///
/// Static linkage so the symbol is unique to this TU. Forwards to the
/// C++ class through `HwTimer::fireFromIsr()`, passing the alarm value
/// from the event data so `rearmFromIsr` can compute the next alarm
/// without re-reading the counter.
static bool IRAM_ATTR onGpTimerAlarm(gptimer_handle_t /*timer*/,
                                     const gptimer_alarm_event_data_t *ev, void *userCtx)
{
        auto *self = static_cast<HwTimer *>(userCtx);
        self->fireFromIsr(ev != nullptr ? ev->alarm_value : 0);
        // `false` → do not yield from ISR. The pulse engine never wakes a
        // task from the alarm callback, so this is correct.
        return false;
}

void IRAM_ATTR HwTimer::fireFromIsr(uint64_t firedAlarmCount)
{
        // Cache the alarm count so rearmFromIsr() can compute the next
        // alarm as (lastAlarmCount_ + ticks). The full access invariant is
        // documented on the field itself in hwtimer.h — short version:
        // task-context writes happen only when the counter is stopped, so
        // they never race with this ISR-context write or with the read in
        // rearmFromIsr (which is always called from inside the user
        // callback this function invokes).
        lastAlarmCount_ = firedAlarmCount;

        // Software gate: even if the hardware fires a stale alarm right
        // after a task-context `disarmFromIsr()` (race window), we drop it
        // here. Acquire-load + cas semantics aren't needed for a single
        // bool — seq_cst default is fine and barely costs anything.
        if (!armed_.exchange(false, std::memory_order_acq_rel)) {
                return;
        }

        if (cb_) {
                cb_(ctx_);
        }
}

HwTimer::HwTimer() = default;

HwTimer::~HwTimer()
{
        if (handle_) {
                auto h = static_cast<gptimer_handle_t>(handle_);
                // Hardware FSM has to be in GPTIMER_FSM_ENABLE before disable.
                // Only call gptimer_stop if we actually started; calling it on
                // an already-stopped timer would return ESP_ERR_INVALID_STATE
                // (harmless but log-noisy).
                if (started_) {
                        gptimer_stop(h);
                        started_ = false;
                }
                armed_.store(false, std::memory_order_release);
                if (enabled_) {
                        gptimer_disable(h);
                        enabled_ = false;
                }
                gptimer_del_timer(h);
                handle_ = nullptr;
        }
}

HwTimerStatus HwTimer::begin(const HwTimerConfig &cfg)
{
        if (handle_) {
                return HwTimerStatus::AlreadyInitialized;
        }
        if (cfg.resolutionHz == 0) {
                return HwTimerStatus::InvalidConfig;
        }

        gptimer_config_t tcfg = {};
        tcfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        tcfg.direction = GPTIMER_COUNT_UP;
        tcfg.resolution_hz = cfg.resolutionHz;

        gptimer_handle_t handle = nullptr;
        if (gptimer_new_timer(&tcfg, &handle) != ESP_OK) {
                return HwTimerStatus::BackendError;
        }

        gptimer_event_callbacks_t cbs = {};
        cbs.on_alarm = &onGpTimerAlarm;
        if (gptimer_register_event_callbacks(handle, &cbs, this) != ESP_OK) {
                gptimer_del_timer(handle);
                return HwTimerStatus::BackendError;
        }

        if (gptimer_enable(handle) != ESP_OK) {
                gptimer_del_timer(handle);
                return HwTimerStatus::BackendError;
        }

        handle_ = handle;
        resolution_ = cfg.resolutionHz;
        minTicks_ = (cfg.minTicks == 0) ? 1u : cfg.minTicks;
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

        auto h = static_cast<gptimer_handle_t>(handle_);

        // CRITICAL: gate gptimer_stop on `started_`, NOT `armed_`. After a
        // one-shot alarm fires with auto_reload_on_alarm=false, the
        // trampoline atomically clears `armed_`, but the GPTimer FSM stays
        // in GPTIMER_FSM_RUN (the counter keeps counting; only the alarm
        // action is spent). The next startOneShotTicks therefore needs to
        // bring the FSM back to GPTIMER_FSM_ENABLE before gptimer_start
        // will accept it — `gptimer_start` returns ESP_ERR_INVALID_STATE
        // otherwise. Tracking `started_` separately from `armed_` is the
        // only correct way to drive this.
        if (started_) {
                gptimer_stop(h);
                started_ = false;
        }
        armed_.store(false, std::memory_order_release);

        if (gptimer_set_raw_count(h, 0) != ESP_OK) {
                return HwTimerStatus::BackendError;
        }
        lastAlarmCount_ = 0;

        // Auto-reload OFF gives true one-shot at the hardware level: alarm
        // fires once at count == ticks, then no further fires until we set
        // a new alarm_count beyond the counter.
        gptimer_alarm_config_t a = {};
        a.alarm_count = ticks;
        a.reload_count = 0;
        a.flags.auto_reload_on_alarm = false;
        if (gptimer_set_alarm_action(h, &a) != ESP_OK) {
                return HwTimerStatus::BackendError;
        }
        if (gptimer_start(h) != ESP_OK) {
                return HwTimerStatus::BackendError;
        }

        started_ = true;
        armed_.store(true, std::memory_order_release);
        return HwTimerStatus::Ok;
}

HwTimerStatus IRAM_ATTR HwTimer::rearmFromIsr(uint32_t ticks)
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;
        if (ticks < minTicks_)
                return HwTimerStatus::InvalidTicks;

        // Schedule the next alarm relative to the alarm that just fired.
        // `lastAlarmCount_` was cached by the trampoline from
        // `gptimer_alarm_event_data_t::alarm_value`, avoiding a counter
        // register read on every pulse.
        const uint64_t next = lastAlarmCount_ + ticks;

        gptimer_alarm_config_t a = {};
        a.alarm_count = next;
        a.reload_count = 0;
        a.flags.auto_reload_on_alarm = false;
        if (gptimer_set_alarm_action(static_cast<gptimer_handle_t>(handle_), &a) != ESP_OK) {
                return HwTimerStatus::BackendError;
        }

        armed_.store(true, std::memory_order_release);
        return HwTimerStatus::Ok;
}

HwTimerStatus IRAM_ATTR HwTimer::disarmFromIsr()
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;

        // Software-only disarm. With auto_reload_on_alarm = false at the
        // hardware level, the alarm has already fired once (we're inside
        // the callback) and will NOT fire again on its own. Setting
        // `armed_ = false` is sufficient — the trampoline's gate will drop
        // any stale fire if some weird path schedules one anyway.
        //
        // Deliberately NOT calling gptimer_stop() here. Even though current
        // ESP-IDF documents it as ISR-safe, the contract of this method is
        // "guarantee no more user callbacks", which the gate already
        // delivers. Task-context full stop is `stop()`.
        armed_.store(false, std::memory_order_release);
        return HwTimerStatus::Ok;
}

HwTimerStatus HwTimer::stop()
{
        if (!handle_)
                return HwTimerStatus::NotInitialized;
        auto h = static_cast<gptimer_handle_t>(handle_);

        // Task-context full halt: stop the hardware counter AND clear the
        // software gate. After this, `startOneShotTicks` is required to
        // resume. Only call gptimer_stop if we're actually running — calling
        // it on an idle timer returns ESP_ERR_INVALID_STATE.
        if (started_) {
                gptimer_stop(h);
                started_ = false;
        }
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

#endif // ESP_PLATFORM || ARDUINO_ARCH_ESP32
