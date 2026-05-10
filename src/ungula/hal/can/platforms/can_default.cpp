// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if !defined(ESP_PLATFORM)

#include "../can.h"

namespace ungula::hal::can
{

    Can::Can(uint8_t controllerNumber)
            : port_(controllerNumber)
    {
    }

    Can::~Can()
    {
    }

    bool Can::begin(uint8_t /*txPin*/, uint8_t /*rxPin*/, uint32_t /*bitrateBps*/)
    {
        if (installed_) {
            return false;
        }
        installed_ = true;
        return true;
    }

    bool Can::stop()
    {
        installed_ = false;
        return true;
    }

    bool Can::send(const CanFrame & /*frame*/, uint32_t /*timeoutMs*/)
    {
        return installed_;
    }

    int32_t Can::receive(CanFrame & /*out*/, uint32_t /*timeoutMs*/)
    {
        // Stub never produces frames; tests that exercise reception
        // belong on real hardware (or with an injected fake).
        return installed_ ? 0 : -1;
    }

    bool Can::setAcceptanceFilter(uint32_t /*id*/, uint32_t /*mask*/, bool /*extendedId*/)
    {
        return installed_;
    }

    bool Can::clearAcceptanceFilter()
    {
        return installed_;
    }

    bool Can::isBusOff() const
    {
        return false;
    }

    bool Can::recoverFromBusOff()
    {
        return installed_;
    }

} // namespace ungula::hal::can

#endif // !ESP_PLATFORM
