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
#include "ungula/hal/gpio/gpio.h"

// ADC (oneshot with per-unit/attenuation calibration)
#include "ungula/hal/adc/adc_manager.h"

// UART (serial communication)
#include "ungula/hal/uart/uart.h"

// I2C master
#include "ungula/hal/i2c/i2c_master.h"

// SPI master
#include "ungula/hal/spi/spi_master.h"

// CAN bus (CAN 2.0)
#include "ungula/hal/can/can.h"

// I2C multiplexer interface (drivers in ungula/hal/multiplexer/drivers/)
#include "ungula/hal/multiplexer/i_multiplexer.h"

// Hardware timer (one-shot / variable-period alarm with ISR re-arm)
#include "ungula/hal/timer/i_hwtimer.h"
#include "ungula/hal/timer/drivers/hwtimer.h"

// Critical section (dual-core spinlock on ESP32, no-op on host)
#include "ungula/hal/sync/critical_section.h"
