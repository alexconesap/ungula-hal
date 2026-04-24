// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once
#ifndef __cplusplus
#error UngulaHal requires a C++ compiler
#endif

// Ungula HAL Library - hardware abstraction for embedded projects
// Include this header to activate the library in Arduino

// GPIO (digital I/O, PWM, interrupts)
#include "hal/gpio/gpio_access.h"

// ADC (oneshot with per-unit/attenuation calibration)
#include "hal/adc/adc_manager.h"

// UART (serial communication)
#include "hal/uart/uart.h"

// I2C master
#include "hal/i2c/i2c_master.h"

// SPI master
#include "hal/spi/spi_master.h"
