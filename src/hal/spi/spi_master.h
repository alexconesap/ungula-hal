// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Platform-abstracted SPI master driver.
///
/// Wraps the platform SPI peripheral into a transaction-based interface.
/// On ESP32, uses ESP-IDF spi_master (SPI2_HOST / SPI3_HOST).
/// On other platforms, provides a no-op stub for compilation.
///
/// This is a single-device wrapper: one SpiMaster instance = one CS line.
/// For multiple devices on the same bus, create one SpiMaster per device
/// sharing the same host but with different CS pins.
///
/// Usage:
///   ungula::spi::SpiMaster spi;
///   spi.begin(18, 19, 23, 5, 1000000, 1);  // SCLK, MISO, MOSI, CS, 1MHz, mode 1
///   spi.writeRead(txBuf, rxBuf, 4);

namespace ungula {
    namespace spi {

        /// @brief SPI master device wrapper.
        ///
        /// Owns one SPI device handle on a bus. Destructor removes the device
        /// (but does not free the bus — other devices may share it).
        /// Non-copyable.
        class SpiMaster {
            public:
                SpiMaster() = default;
                ~SpiMaster();

                SpiMaster(const SpiMaster&) = delete;
                SpiMaster& operator=(const SpiMaster&) = delete;

                /// @brief Initialize the SPI bus and add this device.
                /// @param sclkPin   GPIO for SCLK.
                /// @param misoPin   GPIO for MISO (input).
                /// @param mosiPin   GPIO for MOSI (output).
                /// @param csPin     GPIO for chip select (active low).
                /// @param freqHz    Clock frequency in Hz (e.g. 1000000 for 1 MHz).
                /// @param mode      SPI mode 0-3 (CPOL | CPHA).
                /// @param host      SPI host index (1 = SPI2_HOST, 2 = SPI3_HOST on ESP32).
                /// @return true on success, false on error.
                bool begin(uint8_t sclkPin, uint8_t misoPin, uint8_t mosiPin, uint8_t csPin,
                           uint32_t freqHz = 1000000, uint8_t mode = 1, uint8_t host = 1);

                /// @brief Full-duplex transfer. Simultaneously sends txData and receives into
                /// rxData.
                /// @param txData Bytes to send (may be nullptr for read-only).
                /// @param rxData Receive buffer (may be nullptr for write-only).
                /// @param length Number of bytes to transfer.
                /// @return true on success.
                bool transfer(const uint8_t* txData, uint8_t* rxData, size_t length);

                /// @brief Write-only transfer.
                bool write(const uint8_t* data, size_t length);

                /// @brief Read-only transfer (sends zeros, receives data).
                bool read(uint8_t* buffer, size_t length);

                /// @brief Write followed by read in a single CS assertion.
                /// Sends writeLen bytes from txData, then clocks readLen bytes into rxBuf.
                bool writeRead(const uint8_t* txData, size_t writeLen, uint8_t* rxBuf,
                               size_t readLen);

            private:
                bool installed_ = false;
                void* devHandle_ = nullptr;
        };

    }  // namespace spi
}  // namespace ungula
