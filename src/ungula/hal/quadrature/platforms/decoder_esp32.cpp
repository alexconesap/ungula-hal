// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "ungula/hal/quadrature/drivers/decoder.h"

#include "driver/pulse_cnt.h"

namespace ungula::hal::quadrature::drivers
{

namespace
{

        struct EspState {
                pcnt_unit_handle_t unit = nullptr;
                pcnt_channel_handle_t chA = nullptr;
                pcnt_channel_handle_t chB = nullptr;
        };

} // namespace

Decoder::Decoder() = default;

Decoder::~Decoder()
{
        if (installed_) {
                (void)stop();
        }
}

bool Decoder::begin(uint8_t pinA, uint8_t pinB, int32_t initialCount)
{
        if (installed_) {
                return false;
        }
        EspState *st = new EspState();
        pcnt_unit_config_t unitCfg{};
        unitCfg.high_limit = 32'767;
        unitCfg.low_limit = -32'768;
        unitCfg.flags.accum_count = 1; // virtual 32-bit accumulator
        if (pcnt_new_unit(&unitCfg, &st->unit) != ESP_OK) {
                delete st;
                return false;
        }

        pcnt_chan_config_t aCfg{};
        aCfg.edge_gpio_num = static_cast<int>(pinA);
        aCfg.level_gpio_num = static_cast<int>(pinB);
        if (pcnt_new_channel(st->unit, &aCfg, &st->chA) != ESP_OK) {
                pcnt_del_unit(st->unit);
                delete st;
                return false;
        }
        pcnt_chan_config_t bCfg{};
        bCfg.edge_gpio_num = static_cast<int>(pinB);
        bCfg.level_gpio_num = static_cast<int>(pinA);
        if (pcnt_new_channel(st->unit, &bCfg, &st->chB) != ESP_OK) {
                pcnt_del_channel(st->chA);
                pcnt_del_unit(st->unit);
                delete st;
                return false;
        }

        // Quadrature: count A on both edges (decode = increase if B is
        // low, decrease if B is high), and B mirrored. Standard 4× decode.
        pcnt_channel_set_edge_action(st->chA, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        pcnt_channel_set_level_action(st->chA, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
        pcnt_channel_set_edge_action(st->chB, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE);
        pcnt_channel_set_level_action(st->chB, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

        pcnt_unit_enable(st->unit);
        pcnt_unit_clear_count(st->unit);
        pcnt_unit_start(st->unit);

        unit_ = st;
        pinA_ = pinA;
        pinB_ = pinB;
        count_ = initialCount;
        installed_ = true;
        return true;
}

bool Decoder::stop()
{
        if (!installed_) {
                return true;
        }
        EspState *st = static_cast<EspState *>(unit_);
        if (st != nullptr) {
                pcnt_unit_stop(st->unit);
                pcnt_unit_disable(st->unit);
                pcnt_del_channel(st->chA);
                pcnt_del_channel(st->chB);
                pcnt_del_unit(st->unit);
                delete st;
        }
        unit_ = nullptr;
        installed_ = false;
        return true;
}

int32_t Decoder::count() const
{
        if (!installed_ || unit_ == nullptr) {
                return count_;
        }
        EspState *st = static_cast<EspState *>(unit_);
        int hwCount = 0;
        pcnt_unit_get_count(st->unit, &hwCount);
        // Combine the seed value the host gave at begin/reset time
        // with the hardware delta. The PCNT accumulator is reset to 0
        // on each `reset()`, so the public count is `seed + hw`.
        return count_ + hwCount;
}

bool Decoder::reset(int32_t value)
{
        count_ = value;
        if (installed_ && unit_ != nullptr) {
                EspState *st = static_cast<EspState *>(unit_);
                pcnt_unit_clear_count(st->unit);
        }
        return true;
}

} // namespace ungula::hal::quadrature::drivers

#endif // ESP_PLATFORM
