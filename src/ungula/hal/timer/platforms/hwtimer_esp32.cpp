// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)

#include "ungula/hal/timer/drivers/hwtimer.h"

#include <driver/gptimer.h>
#include <esp_attr.h>
#include <hal/timer_ll.h>
#include <soc/timer_group_struct.h>

namespace ungula::hal::timer::drivers
{

namespace
{

/// Layout mirror of the prefix of ESP-IDF's internal `gptimer_t`
/// (`components/driver/gptimer/gptimer_priv.h`). We only touch the
/// first two fields — the group pointer and the timer index inside
/// that group — both of which have been at offsets 0 and 4 of
/// `gptimer_t` since the driver was introduced in ESP-IDF 5.0 and
/// remain stable across the 5.x line. We never depend on anything
/// past `timer_id`; future ESP-IDF releases that append fields stay
/// compatible.
///
/// Why we can't just include the real header: `gptimer_priv.h` lives
/// inside the driver component's source directory, not its public
/// `include/` directory, so it isn't on the consumer's include path.
/// Replicating the prefix here is the price of staying off the
/// spinlock-protected public alarm API. The runtime validity check
/// in `HwTimer::begin()` catches a layout mismatch by checking the
/// recovered `(group_id, timer_id)` falls in the expected range.
struct GptimerLayoutPrefix {
        void *group;
        int timer_id;
};

/// Layout mirror of the prefix of ESP-IDF's internal `gptimer_group_t`
/// (same header). `group_id` is the first field and is the only one
/// we touch.
struct GptimerGroupLayoutPrefix {
        int group_id;
};

} // namespace

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

void IRAM_ATTR HwTimer::fireFromIsr(uint64_t /*firedAlarmCount*/)
{
        // `firedAlarmCount` is intentionally ignored. The gptimer driver
        // fills `gptimer_alarm_event_data_t::alarm_value` from its own
        // cached C-struct field `timer->alarm_count`, which is only
        // updated when someone calls the public `gptimer_set_alarm_action`
        // — and our `rearmFromIsr` deliberately bypasses that to avoid
        // the per-pulse critical section. So the event-data value is
        // stale (frozen at whatever `startOneShotTicks` set initially).
        // `lastAlarmCount_` is maintained internally instead:
        // `startOneShotTicks` seeds it, `rearmFromIsr` advances it. This
        // function leaves it alone.

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

        // Recover (group_id, timer_id) from the gptimer handle so
        // `rearmFromIsr` can write the alarm registers via the lockless
        // `timer_ll_*` primitives. See the comment on
        // `GptimerLayoutPrefix` for why this peek is structured the way
        // it is.
        auto *gp = reinterpret_cast<const GptimerLayoutPrefix *>(handle);
        const int timerId = gp->timer_id;
        auto *grp = reinterpret_cast<const GptimerGroupLayoutPrefix *>(gp->group);
        const int groupId = (grp != nullptr) ? grp->group_id : -1;

        // ESP32 / S2 / S3 all have exactly 2 timer groups (0 and 1), each
        // with up to 2 timers (0 and 1). Anything outside that range means
        // our layout mirror is stale relative to the installed ESP-IDF —
        // bail out instead of writing to a wild pointer.
        if (groupId < 0 || groupId > 1 || timerId < 0 || timerId > 1) {
                gptimer_disable(handle);
                gptimer_del_timer(handle);
                return HwTimerStatus::BackendError;
        }

        dev_ = (groupId == 0) ? static_cast<void *>(&TIMERG0)
                              : static_cast<void *>(&TIMERG1);
        timerId_ = static_cast<uint32_t>(timerId);

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

        // Seed the relative-scheduling cursor. Counter starts at 0 and
        // the alarm will fire at counter == ticks, so the "last alarm
        // count" we propagate into `rearmFromIsr` is `ticks`. From here
        // on `rearmFromIsr` maintains the running value itself.
        lastAlarmCount_ = ticks;

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
        // `lastAlarmCount_` is maintained internally: seeded by
        // `startOneShotTicks` (counter starts at 0, first alarm fires at
        // `ticks`) and advanced here every rearm. We do NOT trust the
        // value reported via `gptimer_alarm_event_data_t::alarm_value`
        // because the gptimer driver fills that from its own cached
        // struct field, which our LL-bypass deliberately does not update.
        const uint64_t next = lastAlarmCount_ + ticks;

        // Lockless ISR-safe rearm. The public `gptimer_set_alarm_action`
        // takes a per-timer portMUX critical section and does flag
        // bookkeeping per pulse; at ~21 µs pulse intervals (≈46 kHz STEP)
        // the cumulative ISR work overran the inter-pulse period, pinned
        // CPU0 inside the alarm ISR, and tripped the IDLE watchdog. The
        // lockless `timer_ll_*` primitives are pure register writes
        // (alarm-value HI/LO + alarm_en + level_int_en), `always_inline`
        // and IRAM-safe — no critical section, no driver state.
        //
        // The natural one-shot behaviour (auto_reload_on_alarm = false,
        // set once at `startOneShotTicks`) means hardware self-disables
        // `tx_alarm_en` after firing — we re-enable it here on every
        // pulse, same as the previous `gptimer_set_alarm_action` path
        // did internally.
        auto *hw = static_cast<timg_dev_t *>(dev_);
        timer_ll_set_alarm_value(hw, timerId_, next);
        timer_ll_enable_alarm(hw, timerId_, true);

        lastAlarmCount_ = next;
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
