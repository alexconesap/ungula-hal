// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Platform-abstracted CAN 2.0 controller.
///
/// Wraps the platform CAN peripheral into a simple frame-based interface.
/// On ESP32, uses ESP-IDF's TWAI driver (`driver/twai.h`). On other
/// platforms, provides a no-op stub for compilation and host tests.
///
/// CAN-FD is intentionally out of scope; ESP32 classic does not support
/// it and v1 callers (servo motor controllers) use CAN 2.0.
///
/// Usage:
///
/// ```cpp
///   ungula::hal::can::Can bus(0);
///   bus.begin(/*tx=*/21, /*rx=*/22, ungula::hal::can::BITRATE_1M);
///
///   ungula::hal::can::CanFrame f{};
///   f.id = 0x123;
///   f.dlc = 2;
///   f.data[0] = 0xDE;
///   f.data[1] = 0xAD;
///   bus.send(f);
///
///   ungula::hal::can::CanFrame in{};
///   if (bus.receive(in, /*timeoutMs=*/100) == 1) {
///       // process in
///   }
/// ```

namespace ungula::hal::can {

    /// Standard CAN 2.0 frame. Up to 8 data bytes. Both 11-bit
    /// (standard) and 29-bit (extended) IDs are supported via the
    /// `extendedId` flag.
    struct CanFrame {
            uint32_t id;          // 11-bit (extendedId=false) or 29-bit (extendedId=true)
            bool extendedId;      // selects ID width
            bool remote;          // remote transmission request (rarely used)
            uint8_t dlc;          // 0..8
            uint8_t data[8];
    };

    /// Common bitrates expressed in Hz so call sites read clearly.
    /// Backends map these to their own internal presets; values not in
    /// the table here may still work — drivers reject what they cannot
    /// configure by returning `false` from `begin()`.
    constexpr uint32_t BITRATE_25K = 25'000;
    constexpr uint32_t BITRATE_50K = 50'000;
    constexpr uint32_t BITRATE_100K = 100'000;
    constexpr uint32_t BITRATE_125K = 125'000;
    constexpr uint32_t BITRATE_250K = 250'000;
    constexpr uint32_t BITRATE_500K = 500'000;
    constexpr uint32_t BITRATE_800K = 800'000;
    constexpr uint32_t BITRATE_1M = 1'000'000;

    /// @brief CAN controller wrapper.
    ///
    /// Owns the driver for a single hardware CAN/TWAI controller.
    /// Destructor uninstalls it. Non-copyable — one owner per
    /// physical controller.
    class Can {
        public:
            /// @param controllerNumber Hardware controller index (0 on
            ///                         ESP32, which has a single TWAI).
            explicit Can(uint8_t controllerNumber);
            ~Can();

            Can(const Can&) = delete;
            Can& operator=(const Can&) = delete;

            /// @brief Install the driver, configure pins + bitrate, and
            ///        start the controller.
            /// @param txPin     GPIO number for CAN TX.
            /// @param rxPin     GPIO number for CAN RX.
            /// @param bitrateBps One of the `BITRATE_*` constants (or
            ///                   another value the backend supports).
            /// @return true on success, false if already installed,
            ///         the bitrate is unsupported, or driver error.
            bool begin(uint8_t txPin, uint8_t rxPin, uint32_t bitrateBps);

            /// @brief Stop the controller and free the driver. Idempotent.
            bool stop();

            /// @brief Transmit one frame. Blocks up to `timeoutMs`
            ///        waiting for room in the TX queue.
            /// @return true on ACK / queued, false on timeout or error.
            bool send(const CanFrame& frame, uint32_t timeoutMs = 50);

            /// @brief Receive one frame.
            /// @param out        Destination frame.
            /// @param timeoutMs  0 = non-blocking, >0 = block.
            /// @return 1 on success, 0 on timeout, -1 on error.
            int32_t receive(CanFrame& out, uint32_t timeoutMs = 0);

            /// @brief Single hardware acceptance filter. After the call,
            ///        the controller drops every frame where
            ///        `(rxId & mask) != (id & mask)`.
            /// @return true on success.
            bool setAcceptanceFilter(uint32_t id, uint32_t mask, bool extendedId);

            /// @brief Reset the filter to "accept all" (default).
            bool clearAcceptanceFilter();

            /// @brief True when the controller is in bus-off (error
            ///        counter overflow, line stuck low, etc.).
            bool isBusOff() const;

            /// @brief Initiate bus-off recovery. Returns true on
            ///        success; the bus may take some milliseconds
            ///        to come back online.
            bool recoverFromBusOff();

            /// @brief Hardware controller index passed at construction.
            uint8_t controller() const {
                return port_;
            }

        private:
            uint8_t port_;
            bool installed_ = false;

            // Remembered for re-installs triggered by acceptance-filter
            // changes — TWAI only lets you set a filter at install time,
            // so changing it at runtime means stop + uninstall + install +
            // start with the new filter. The class hides that round-trip
            // from callers.
            uint8_t txPin_ = 0;
            uint8_t rxPin_ = 0;
            uint32_t bitrateBps_ = 0;
            uint32_t filterId_ = 0;
            uint32_t filterMask_ = 0;
            bool filterExtended_ = false;
            bool filterEnabled_ = false;
    };

}  // namespace ungula::hal::can
