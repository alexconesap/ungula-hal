// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stdint.h>

#include "ungula/hal/i2c/i2c_master.h"
#include "ungula/hal/multiplexer/i_multiplexer.h"

/// @brief TCA9548A 8-channel I2C multiplexer driver.
///
/// One control byte selects up to 8 downstream channels (one bit per
/// channel). The driver uses `ungula::hal::i2c::I2cMaster` for the bus —
/// the host project owns the bus and hands a reference. Several
/// multiplexers can share a single bus.
///
/// Default address is `0x70`; the address pins A0/A1/A2 select among
/// `0x70..0x77`.

namespace ungula::hal::multiplexer::drivers {

    constexpr uint8_t TCA9548_DEFAULT_ADDRESS = 0x70;

    class MultiplexerTCA9548 final : public IMultiplexer {
        public:
            /// @param i2cAddress  7-bit address (0x70..0x77).
            /// @param bus         Borrowed I2C master. Caller must keep it
            ///                    alive and call `bus.begin(...)` before
            ///                    using this multiplexer.
            MultiplexerTCA9548(uint8_t i2cAddress, ungula::hal::i2c::I2cMaster& bus)
                    : IMultiplexer("TCA9548", i2cAddress), bus_(bus) {}

            bool begin() override;
            void restartBus() override;
            bool isResponding() override;

        protected:
            bool selectChannel_(uint8_t channel) override;

        private:
            ungula::hal::i2c::I2cMaster& bus_;
    };

}  // namespace ungula::hal::multiplexer::drivers
