// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include "ungula/hal/core/compiler_attrs.h" // UNGULA_ISR_ATTR

namespace ungula::hal::sync
{

/// **Test-only no-op.** Host (non-ESP32) builds use this stub so the
/// motor library's logic compiles and runs under unit tests. There is
/// NO real mutual exclusion here.
///
/// This is deliberate: the project's host tests are single-threaded by
/// design. Embedding an `std::mutex` here would only hide concurrency
/// bugs — tests would silently serialise calls that on real hardware
/// would race. If a future test genuinely needs cross-thread safety,
/// add a separate `CriticalSectionStdMutex` rather than weakening this
/// stub.
///
/// Production code on ESP32 uses the `portMUX_TYPE`-backed version in
/// `critical_section_esp32.h`.
///
/// **STM32 (and any non-ESP32 MCU) port note:** the platform dispatch
/// in `critical_section.h` falls into this header on anything that is
/// not ESP32. That is correct for host unit tests, NOT for production
/// on an MCU. Before running motor code (or any code that uses
/// `CriticalSection` to guard ISR/task shared state) on STM32, add a
/// real `platforms/critical_section_stm32.h` (PRIMASK / BASEPRI save +
/// restore) and a corresponding branch in `critical_section.h`. The
/// no-op stub will silently allow races on real hardware.
class CriticalSection {
    public:
        CriticalSection() = default;

        CriticalSection(const CriticalSection &) = delete;
        CriticalSection &operator=(const CriticalSection &) = delete;

        void enter() UNGULA_ISR_ATTR
        {
        }
        void exit() UNGULA_ISR_ATTR
        {
        }
};

} // namespace ungula::hal::sync
