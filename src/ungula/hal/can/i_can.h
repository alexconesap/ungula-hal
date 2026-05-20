// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstdint>

#include "ungula/hal/can/can_frame.h"

namespace ungula::hal::can
{

/// Abstract interface for a classic CAN 2.0 controller.
///
/// Implemented by every concrete classic-CAN driver in the lib:
/// `Can` (ESP32 native TWAI today; on other MCUs the same class name
/// dispatches to the platform's default controller), and any future
/// external-controller class such as `Mcp2515Can`. Higher layers
/// (lib_canbus and the device protocols that live in it) consume
/// the interface so the same protocol code runs against any
/// controller without re-templating or recompiling.
///
/// CAN-FD has a different frame shape (up to 64 bytes, BRS / FDF
/// bits) and gets its own parallel interface when it lands - not
/// part of `ICan`.
///
/// Acceptance-filter and family-specific bring-up parameters
/// (TX/RX pins, SPI handles for external controllers, bitrate)
/// live on the concrete classes' `begin(...)` overloads, not here.
/// Hosts that need to swap controllers behind common protocol code
/// hold an `ICan&` after calling the controller-specific `begin()`.
///
/// Threading: TASK-context only. CAN ISR delivery is handled inside
/// the platform driver and surfaced through the polled `receive`.
class ICan {
    public:
        virtual ~ICan() = default;

        /// Transmit one frame. Blocks up to `timeoutMs` waiting for
        /// room in the TX queue. Returns true on ACK / queued.
        virtual bool send(const CanFrame &frame, uint32_t timeoutMs = 50) = 0;

        /// Receive one frame. `timeoutMs == 0` is non-blocking.
        /// Returns 1 on success, 0 on timeout, -1 on error.
        virtual int32_t receive(CanFrame &out, uint32_t timeoutMs = 0) = 0;

        /// True when the controller is in bus-off (error counter
        /// overflow, line stuck low, etc.).
        virtual bool isBusOff() const = 0;

        /// Initiate bus-off recovery. Returns true on success; the
        /// bus may take some milliseconds to come back online.
        virtual bool recoverFromBusOff() = 0;

        /// Stop the controller and free the driver. Idempotent.
        /// Concrete `begin(...)` reinstalls it.
        virtual bool stop() = 0;
};

} // namespace ungula::hal::can
