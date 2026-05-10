// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include "multiplexer_tca9548.h"

#include "ungula/core/time/time_control.h"

namespace ungula::hal::multiplexer::drivers
{

    namespace
    {
        // Channel-select propagation time on the chip. Datasheet quotes
        // <1 µs but a tiny pause helps when the downstream device is
        // about to be addressed immediately after.
        constexpr int64_t SELECT_SETTLE_US = 25;
    } // namespace

    bool MultiplexerTCA9548::begin()
    {
        // The bus is owned by the host project. We only probe the chip
        // here and flag ourselves ready.
        if (isResponding()) {
            initiated_ = true;
            logInfof("ready");
            return true;
        }
        initiated_ = false;
        logErrorf("not responding on begin()");
        return false;
    }

    void MultiplexerTCA9548::restartBus()
    {
        // The shared I2cMaster is owned by the host project; the host
        // decides whether to tear it down. Nothing to do here beyond
        // forgetting our cached state so the next selectChannel() will
        // hit the wire.
        isFunctional_ = false;
        currentChannel_ = 0xFF;
    }

    bool MultiplexerTCA9548::isResponding()
    {
        // Zero-length write probes the device without changing channel
        // state — most bus implementations treat it as an address+ACK
        // round-trip.
        const bool ok = bus_.write(i2cAddress_, nullptr, 0);
        isFunctional_ = ok;
        if (!ok) {
            logWarnf("not responding");
        }
        return ok;
    }

    bool MultiplexerTCA9548::selectChannel_(uint8_t channel)
    {
        if (channel >= 8U) {
            // TCA9548 has 8 channels (0..7). Out-of-range request.
            return false;
        }
        const uint8_t controlByte = static_cast<uint8_t>(1U << channel);
        if (!bus_.write(i2cAddress_, &controlByte, 1)) {
            return false;
        }
        ungula::core::time::delayUs(SELECT_SETTLE_US);
        return true;
    }

} // namespace ungula::hal::multiplexer::drivers
