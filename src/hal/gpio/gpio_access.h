// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

/// @brief Platform-abstracted GPIO and PWM access — bridge header.
///
/// Dispatches to the platform-specific implementation based on build
/// environment. The platform header provides all functions in the
/// ungula::gpio namespace and defines the UNGULA_ISR_ATTR macro.
///
/// Usage:
///   #include <hal/gpio/gpio_access.h>   // cross-library
///
/// Two API layers for digital I/O:
///
///   **Unchecked (hot-path)**: read(), setHigh(), setLow(), write()
///   No pin validation. Use in ISRs, timers, PID loops — anywhere
///   the pin was already validated via a config*() call at boot.
///   A few helpers are available to make the code more readable:
///   isHigh(), isLow(), isEnabled(), isDisabled(), isOpen(), isClosed()
///
///   Use these only if your compiler optimizes inlined calls — they are just wrappers around read() or !read().
///
///   **Checked**: checkedRead(), checkedSetHigh(), checkedSetLow(), checkedWrite()
///   Validate pin against the SoC bitmask on every call. Return false
///   if invalid. Use when the pin number is from configuration or user input.
///
/// Config functions (configOutput, configInput, etc.) always validate.
///
/// On ESP32, unchecked read/write uses the HAL low-level API (gpio_ll)
/// which compiles to direct register writes (W1TS/W1TC) — single-cycle
/// GPIO access safe for timer ISRs.

#if defined(ESP_PLATFORM)
#include "platforms/gpio_access_esp32.h"
#else
#include "platforms/gpio_access_default.h"
#endif
