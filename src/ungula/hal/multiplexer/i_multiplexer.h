// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Abstract multiplexer interface.
///
/// Common interface for I2C bus multiplexers (TCA9548A and similar). The
/// host application creates a driver instance, calls `begin()` once, and
/// then uses `selectChannel(n)` before talking to the device wired to
/// channel `n`.
///
/// State is cached: a `selectChannel(n)` that asks for the already-active
/// channel is a no-op (no I2C traffic). Descendant drivers must update
/// `currentChannel_` only after a successful hardware write.
///
/// Logging is optional and routed through EmblogX (`log_*_m("mux", ...)`).
/// It is OFF by default. Toggle at runtime via `enableLogging()` /
/// `disableLogging()`.
///
/// Usage:
/// ```cpp
///   namespace mux = ungula::hal::multiplexer;
///   mux::drivers::MultiplexerTCA9548 mux(0x70, bus);
///   mux.begin();
///   if (mux.selectChannel(2)) {
///       // talk to the device on channel 2
///   }
/// ```

namespace ungula::hal::multiplexer {

    /// @brief Number of bus-restart retries inside `selectChannel()`.
    /// Drivers may raise this; the base method retries this many times
    /// before giving up.
    constexpr uint8_t SELECT_RETRY_COUNT = 3;

    /// @brief Abstract multiplexer. Drivers implement the platform hooks.
    class IMultiplexer {
        public:
            IMultiplexer() = delete;

            /// @param name  Short label for log lines (e.g. "TCA9548").
            ///              Borrowed pointer; the caller must keep it alive.
            /// @param i2cAddress  7-bit I2C address of the multiplexer.
            IMultiplexer(const char* name, uint8_t i2cAddress)
                : name_(name), i2cAddress_(i2cAddress) {}

            virtual ~IMultiplexer() = default;

            IMultiplexer(const IMultiplexer&) = delete;
            IMultiplexer& operator=(const IMultiplexer&) = delete;

            /// @brief Initialise the underlying bus. Must be called once
            /// before any `selectChannel()`.
            virtual bool begin() = 0;

            /// @brief Restart the I2C bus (re-init pins + clock).
            virtual void restartBus() = 0;

            /// @brief Probe the multiplexer (send empty transmission).
            virtual bool isResponding() = 0;

            /// @brief Select a channel on the multiplexer. Cached: no-op
            /// when the requested channel matches the one already active
            /// AND the device is reported functional.
            ///
            /// Retries up to `SELECT_RETRY_COUNT` times on hardware
            /// failure before returning false.
            ///
            /// @return true on success, false on persistent failure.
            bool selectChannel(uint8_t channel);

            /// @brief Last channel successfully selected. Undefined until
            /// the first successful `selectChannel()`.
            uint8_t getCurrentChannel() const {
                return currentChannel_;
            }

            /// @brief Short name passed at construction. Useful in logs.
            const char* getName() const {
                return name_;
            }

            /// @brief 7-bit address passed at construction.
            uint8_t getAddress() const {
                return i2cAddress_;
            }

            // ---- Optional logging (off by default) ----

            void enableLogging() {
                loggingEnabled_ = true;
            }
            void disableLogging() {
                loggingEnabled_ = false;
            }
            bool isLoggingEnabled() const {
                return loggingEnabled_;
            }

        protected:
            /// @brief Hardware-level channel select. Called by
            /// `selectChannel()` after the cache check. Must do a single
            /// I2C write to the multiplexer's control register (or
            /// equivalent) and return true on ACK.
            virtual bool selectChannel_(uint8_t channel) = 0;

            // Used by derived drivers to gate their own non-log work on
            // the shared toggle without re-implementing the check.
            bool shouldLog() const {
                return loggingEnabled_;
            }

            /// @brief Per-instance log helpers. Each prepends the prefix
            /// produced by `formatLogPrefix()` so drivers never repeat
            /// the `[<name> @0x<addr>]` boilerplate. No-op when logging
            /// is disabled. Format-checked by the compiler.
            void logInfof(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
            void logWarnf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
            void logErrorf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
            void logDebugf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));

            /// @brief Build the per-instance log prefix into `buf`.
            /// Default shape: `[<name> @0x<addr>]`. Drivers may override
            /// to add per-class fields (e.g. selected channel).
            virtual size_t formatLogPrefix(char* buf, size_t bufSize) const;

            /// @brief EmblogX module tag used by every log line emitted
            /// from this class hierarchy. Concentrating it here keeps
            /// filters consistent across drivers.
            static constexpr const char* LOG_MODULE = "mux";

            const char* name_;
            const uint8_t i2cAddress_;

            bool initiated_ = false;
            bool isFunctional_ = false;
            uint8_t currentChannel_ = 0xFF;  // 0xFF = "no channel selected yet"

        private:
            bool loggingEnabled_ = false;
    };

}  // namespace ungula::hal::multiplexer
