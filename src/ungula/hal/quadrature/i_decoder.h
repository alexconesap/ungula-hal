// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stdint.h>

/// @brief Abstract quadrature (A/B) decoder.
///
/// Wraps "I have an A/B pulse pair on these two pins — give me the
/// signed count of edges". The underlying mechanism (ESP32 PCNT, dual
/// GPIO interrupts, dedicated decoder IC) is a backend concern. ABI
/// encoders that also emit a Z index pulse can expose it through
/// `hasIndex()` / `latchedAtIndex()` — backends without a Z line keep
/// the safe defaults.
///
/// Decoders are 4× per A/B cycle by default (every edge counts), which
/// is the convention every ABI encoder we ship targets. Backends are
/// free to configure 1× / 2× internally if the wiring or noise floor
/// requires it; the count returned is whatever the backend produces.

namespace ungula::hal::quadrature
{

class IDecoder {
    public:
        virtual ~IDecoder() = default;

        IDecoder(const IDecoder &) = delete;
        IDecoder &operator=(const IDecoder &) = delete;

        /// @brief Install the decoder on `pinA` / `pinB` and seed
        ///        the count.
        virtual bool begin(uint8_t pinA, uint8_t pinB, int32_t initialCount = 0) = 0;

        /// @brief Tear down the decoder. Idempotent.
        virtual bool stop() = 0;

        /// @brief Current signed count.
        virtual int32_t count() const = 0;

        /// @brief Force the count to `value`. Useful for homing.
        virtual bool reset(int32_t value = 0) = 0;

        /// @brief True when the decoder also tracks a Z (index) line.
        virtual bool hasIndex() const
        {
                return false;
        }

        /// @brief True when the latest count was captured at the Z
        ///        pulse — meaningful only when `hasIndex()` is true.
        virtual bool latchedAtIndex() const
        {
                return false;
        }

        /// @brief Pin A / pin B as supplied to `begin()`. 0xFF means
        ///        not yet installed.
        virtual uint8_t pinA() const = 0;
        virtual uint8_t pinB() const = 0;

    protected:
        IDecoder() = default;
};

} // namespace ungula::hal::quadrature
