// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

/// @brief Shared compiler / platform attributes for lib_hal.
///
/// This header exists so any subsystem (gpio, timer, sync, future ones) can
/// pick up `UNGULA_ISR_ATTR` without dragging in a peripheral header. Keep
/// this file headers-free of vendor SDK headers — only platform-detection
/// macros + the bare attribute defines belong here.

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
/// ISR placement attribute. On ESP32 this expands to `IRAM_ATTR`, which
/// forces the function into IRAM so it remains callable while flash is
/// busy (cache disabled during flash erase/program). Apply to every
/// function reachable from an ISR — including non-virtual callbacks,
/// trampolines, helpers called from ISR context, and inline helpers.
///
/// We forward-declare `IRAM_ATTR` here as the macro from `esp_attr.h`
/// to avoid pulling the full ESP-IDF header into every consumer. The
/// vendor header defines it identically; the function lands in the
/// `.iram1` section either way.
#include <esp_attr.h>
#define UNGULA_ISR_ATTR IRAM_ATTR
#else
/// Host / non-ESP32 builds: no special placement needed. The macro
/// expands to nothing so the same source compiles cleanly.
#define UNGULA_ISR_ATTR
#endif
