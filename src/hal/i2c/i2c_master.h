// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Platform-abstracted I2C master driver.
///
/// Wraps the platform I2C peripheral into a simple transaction-based interface.
/// On ESP32, uses ESP-IDF i2c_master (new driver API, ESP-IDF >= 5.1).
/// On other platforms, provides a no-op stub for compilation.
///
/// Usage:
///   ungula::i2c::I2cMaster bus(0);
///   bus.begin(21, 22, 400000);            // SDA, SCL, 400 kHz
///   bus.writeRead(0x2A, cmd, 1, buf, 3);  // typical register read

namespace ungula {
    namespace i2c {

        /// @brief I2C master port wrapper.
        ///
        /// Owns a single I2C master bus. Destructor releases the driver.
        /// Non-copyable — one owner per physical port.
        class I2cMaster {
            public:
                /// @param portNumber Hardware I2C peripheral index (0 or 1 on ESP32).
                explicit I2cMaster(uint8_t portNumber);
                ~I2cMaster();

                I2cMaster(const I2cMaster&) = delete;
                I2cMaster& operator=(const I2cMaster&) = delete;

                /// @brief Install driver, configure pins and clock speed.
                /// @param sdaPin   GPIO number for SDA.
                /// @param sclPin   GPIO number for SCL.
                /// @param freqHz   Clock frequency in Hz (e.g. 100000 or 400000).
                /// @return true on success, false if already installed or driver error.
                bool begin(uint8_t sdaPin, uint8_t sclPin, uint32_t freqHz = 400000);

                /// @brief Write bytes to a device.
                /// @param addr     7-bit I2C device address.
                /// @param data     Bytes to write.
                /// @param length   Number of bytes.
                /// @param timeoutMs Timeout in milliseconds.
                /// @return true on ACK, false on NACK or timeout.
                bool write(uint8_t addr, const uint8_t* data, size_t length,
                           uint32_t timeoutMs = 50);

                /// @brief Read bytes from a device.
                /// @param addr     7-bit I2C device address.
                /// @param buffer   Destination buffer.
                /// @param length   Number of bytes to read.
                /// @param timeoutMs Timeout in milliseconds.
                /// @return true on success, false on NACK or timeout.
                bool read(uint8_t addr, uint8_t* buffer, size_t length, uint32_t timeoutMs = 50);

                /// @brief Combined write-then-read (repeated START).
                /// Typical register read: write register address, then read N bytes.
                /// @param addr       7-bit I2C device address.
                /// @param writeData  Bytes to write (register address, etc.).
                /// @param writeLen   Number of bytes to write.
                /// @param readBuf    Destination buffer for the read phase.
                /// @param readLen    Number of bytes to read.
                /// @param timeoutMs  Timeout in milliseconds.
                /// @return true on success, false on NACK or timeout.
                bool writeRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                               uint8_t* readBuf, size_t readLen, uint32_t timeoutMs = 50);

                /// @brief Hardware peripheral index passed at construction.
                uint8_t port() const {
                    return port_;
                }

            private:
                uint8_t port_;
                bool installed_ = false;
        };

    }  // namespace i2c
}  // namespace ungula
