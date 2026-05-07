// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stdint.h>

#include "ungula/hal/multiplexer/i_multiplexer.h"

/// @brief Header-only test fake for `IMultiplexer`.
///
/// Drop-in for any code that takes a `IMultiplexer*`. Records every
/// `selectChannel_()` call so tests can assert ordering, retry behaviour,
/// and caching. No I2C traffic. Lives in `drivers/` so it ships with the
/// library and can be reused by every test that needs to inject a
/// multiplexer.
///
/// ```cpp
///   ungula::hal::multiplexer::drivers::MultiplexerFake mux("fake", 0x70);
///   mux.begin();
///   mux.selectChannel(2);
///   EXPECT_EQ(mux.selectCallCount(), 1U);
///   EXPECT_EQ(mux.lastChannel(), 2U);
/// ```

namespace ungula::hal::multiplexer::drivers {

    class MultiplexerFake final : public IMultiplexer {
        public:
            MultiplexerFake(const char* name = "fake", uint8_t address = 0x70)
                    : IMultiplexer(name, address) {}

            bool begin() override {
                initiated_ = true;
                isFunctional_ = beginResult_;
                ++beginCallCount_;
                return beginResult_;
            }

            void restartBus() override {
                ++restartCallCount_;
                isFunctional_ = false;
                currentChannel_ = 0xFF;
            }

            bool isResponding() override {
                ++isRespondingCallCount_;
                isFunctional_ = isRespondingResult_;
                return isRespondingResult_;
            }

            // ---- Test knobs ----

            /// @brief Set the value `begin()` returns.
            void setBeginResult(bool ok) {
                beginResult_ = ok;
            }

            /// @brief Set the value `isResponding()` returns.
            void setIsRespondingResult(bool ok) {
                isRespondingResult_ = ok;
            }

            /// @brief Make the next `failureCount` calls to
            /// `selectChannel_()` fail. Useful for retry tests.
            void failNextSelects(uint8_t failureCount) {
                failuresRemaining_ = failureCount;
            }

            /// @brief Force every future `selectChannel_()` to fail until
            /// reset.
            void setSelectAlwaysFails(bool fail) {
                selectAlwaysFails_ = fail;
            }

            // ---- Test inspectors ----

            uint8_t lastChannel() const {
                return lastChannelRequested_;
            }
            uint32_t selectCallCount() const {
                return selectCallCount_;
            }
            uint32_t selectFailureCount() const {
                return selectFailureCount_;
            }
            uint32_t beginCallCount() const {
                return beginCallCount_;
            }
            uint32_t restartCallCount() const {
                return restartCallCount_;
            }
            uint32_t isRespondingCallCount() const {
                return isRespondingCallCount_;
            }

            void resetCounters() {
                selectCallCount_ = 0;
                selectFailureCount_ = 0;
                beginCallCount_ = 0;
                restartCallCount_ = 0;
                isRespondingCallCount_ = 0;
                failuresRemaining_ = 0;
                selectAlwaysFails_ = false;
            }

        protected:
            bool selectChannel_(uint8_t channel) override {
                ++selectCallCount_;
                lastChannelRequested_ = channel;
                if (selectAlwaysFails_ || failuresRemaining_ > 0) {
                    if (failuresRemaining_ > 0) {
                        --failuresRemaining_;
                    }
                    ++selectFailureCount_;
                    return false;
                }
                return true;
            }

        private:
            bool beginResult_ = true;
            bool isRespondingResult_ = true;
            bool selectAlwaysFails_ = false;
            uint8_t failuresRemaining_ = 0;

            uint8_t lastChannelRequested_ = 0xFF;
            uint32_t selectCallCount_ = 0;
            uint32_t selectFailureCount_ = 0;
            uint32_t beginCallCount_ = 0;
            uint32_t restartCallCount_ = 0;
            uint32_t isRespondingCallCount_ = 0;
    };

}  // namespace ungula::hal::multiplexer::drivers
