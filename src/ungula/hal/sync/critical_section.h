// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include "ungula/hal/core/compiler_attrs.h" // UNGULA_ISR_ATTR

// Unsupported MCU targets intentionally fall back only for tests.
// Add a platform backend before production use.

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include "ungula/hal/sync/platforms/critical_section_esp32.h"
#else
#include "ungula/hal/sync/platforms/critical_section_default.h"
#endif

namespace ungula::hal::sync
{

/// RAII guard around a CriticalSection. Locks on construction, unlocks
/// on destruction. Marked UNGULA_ISR_ATTR so it can be used inside ISR
/// handlers on ESP32.
class ScopedLock {
    public:
        explicit ScopedLock(CriticalSection &cs) UNGULA_ISR_ATTR : cs_(cs)
        {
                cs_.enter();
        }
        ~ScopedLock() UNGULA_ISR_ATTR
        {
                cs_.exit();
        }

        ScopedLock(const ScopedLock &) = delete;
        ScopedLock &operator=(const ScopedLock &) = delete;

    private:
        CriticalSection &cs_;
};

} // namespace ungula::hal::sync
