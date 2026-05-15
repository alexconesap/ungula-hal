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
/// on destruction.
///
/// ISR-safety note: the ctor/dtor are intentionally NOT tagged with
/// `UNGULA_ISR_ATTR`. On ESP-IDF that macro expands to `IRAM_ATTR`
/// which uses `__COUNTER__` to generate a unique section name —
/// each translation unit that instantiates the inline body would
/// claim a different section, and the linker rejects the resulting
/// weak-symbol section mismatch. The ctor/dtor inline anyway; if the
/// CALLER is in IRAM the inlined body lives in IRAM too, which is
/// what callers actually need. The underlying `cs_.enter()` /
/// `cs_.exit()` carry the attribute on their definitions where it
/// matters.
class ScopedLock {
    public:
        explicit ScopedLock(CriticalSection &cs) : cs_(cs)
        {
                cs_.enter();
        }
        ~ScopedLock()
        {
                cs_.exit();
        }

        ScopedLock(const ScopedLock &) = delete;
        ScopedLock &operator=(const ScopedLock &) = delete;

    private:
        CriticalSection &cs_;
};

} // namespace ungula::hal::sync
