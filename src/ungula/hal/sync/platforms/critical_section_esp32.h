// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include "ungula/hal/core/compiler_attrs.h" // UNGULA_ISR_ATTR

namespace ungula::hal::sync
{

/// Dual-core-safe spinlock backed by ESP-IDF `portMUX_TYPE`. Use this to
/// guard small bursts of shared state between a task (or two tasks on
/// different cores) and an ISR. Both enter() and exit() are ISR-safe via
/// portENTER_CRITICAL_SAFE / portEXIT_CRITICAL_SAFE.
///
/// Keep the locked region tiny — no logging, no allocation, no UART, no
/// blocking. Reads/writes of small POD state only.
class CriticalSection {
    public:
        CriticalSection()
                : mux_(portMUX_INITIALIZER_UNLOCKED)
        {
        }

        CriticalSection(const CriticalSection &) = delete;
        CriticalSection &operator=(const CriticalSection &) = delete;

        void enter() UNGULA_ISR_ATTR
        {
                portENTER_CRITICAL_SAFE(&mux_);
        }
        void exit() UNGULA_ISR_ATTR
        {
                portEXIT_CRITICAL_SAFE(&mux_);
        }

    private:
        portMUX_TYPE mux_;
};

} // namespace ungula::hal::sync
