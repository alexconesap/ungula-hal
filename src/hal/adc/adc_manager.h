// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

/// @brief Platform-abstracted ADC manager — bridge header.
///
/// Dispatches to the platform-specific implementation. The class lives in
/// the ungula::adc namespace regardless of backend.
///
/// Usage:
///   #include <hal/adc/adc_manager.h>

#if defined(ESP_PLATFORM)
#include "platforms/adc_manager_esp32.h"
#else
#include "platforms/adc_manager_default.h"
#endif
