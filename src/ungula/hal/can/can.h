// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ungula/hal/can/can_frame.h"
#include "ungula/hal/can/i_can.h"

/// @brief Platform-abstracted classic CAN 2.0 controller.
///
/// `Can` is the default classic-CAN class — the one most host code
/// constructs and calls `begin()` on. On ESP32 it dispatches to the
/// native TWAI controller (see `platforms/can_esp32_twai.cpp`); on
/// non-ESP platforms it falls back to the host stub
/// (`platforms/can_default.cpp`). Other CAN families live in their
/// own classes: a future `Mcp2515Can` (external SPI controller,
/// same `ICan` interface) and a future `CanFd` (CAN-FD on its own
/// `ICanFd` interface) join the hierarchy without changing this
/// class.
///
/// Acceptance-filter and pin / bitrate config live on this concrete
/// class because their shape varies between controller families
/// (TWAI's single 11/29-bit filter vs MCP2515's multi-mailbox
/// scheme, etc.). Higher layers that want to swap controller
/// families take an `ICan&` instead and skip the family-specific
/// API.
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

namespace ungula::hal::can
{

/// @brief CAN controller wrapper.
///
/// Owns the driver for a single hardware CAN/TWAI controller.
/// Destructor uninstalls it. Non-copyable — one owner per
/// physical controller. Implements `ICan` so callers that hold an
/// `ICan&` can talk to this controller, an `Mcp2515Can`, or any
/// future classic-CAN controller through the same surface.
class Can : public ICan {
    public:
        /// @param controllerNumber Hardware controller index (0 on
        ///                         ESP32, which has a single TWAI).
        explicit Can(uint8_t controllerNumber);
        ~Can() override;

        Can(const Can &) = delete;
        Can &operator=(const Can &) = delete;

        /// @brief Install the driver, configure pins + bitrate, and
        ///        start the controller.
        /// @param txPin     GPIO number for CAN TX.
        /// @param rxPin     GPIO number for CAN RX.
        /// @param bitrateBps One of the `BITRATE_*` constants (or
        ///                   another value the backend supports).
        /// @return true on success, false if already installed,
        ///         the bitrate is unsupported, or driver error.
        bool begin(uint8_t txPin, uint8_t rxPin, uint32_t bitrateBps);

        // ---- ICan -------------------------------------------------------
        bool    send(const CanFrame &frame, uint32_t timeoutMs = 50) override;
        int32_t receive(CanFrame &out, uint32_t timeoutMs = 0)        override;
        bool    isBusOff() const                                       override;
        bool    recoverFromBusOff()                                    override;
        bool    stop()                                                 override;

        // ---- Family-specific (not on ICan) ------------------------------
        /// @brief Single hardware acceptance filter. After the call,
        ///        the controller drops every frame where
        ///        `(rxId & mask) != (id & mask)`.
        /// @return true on success.
        bool setAcceptanceFilter(uint32_t id, uint32_t mask, bool extendedId);

        /// @brief Reset the filter to "accept all" (default).
        bool clearAcceptanceFilter();

        /// @brief Hardware controller index passed at construction.
        uint8_t controller() const
        {
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

} // namespace ungula::hal::can
