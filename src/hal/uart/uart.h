// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Platform-abstracted UART driver.
///
/// Wraps the platform UART peripheral into a simple read/write interface.
/// On ESP32, uses ESP-IDF uart_driver_install / uart_write_bytes / uart_read_bytes.
/// On other platforms, provides a no-op stub for compilation.

namespace ungula {
    namespace uart {

        /// Default RX ring buffer size (bytes).
        constexpr uint16_t DEFAULT_RX_BUF = 256;

        /// Default TX ring buffer size (0 = blocking writes, no ring buffer).
        constexpr uint16_t DEFAULT_TX_BUF = 0;

        /// @brief UART port wrapper.
        ///
        /// Owns the driver for a single hardware UART port. Destructor uninstalls it.
        /// Non-copyable — one owner per physical port.
        class Uart {
            public:
                /// @param portNumber Hardware UART peripheral index (e.g. 0, 1, or 2 on ESP32).
                explicit Uart(uint8_t portNumber);
                ~Uart();

                Uart(const Uart&) = delete;
                Uart& operator=(const Uart&) = delete;

                /// @brief Install driver, configure baud rate and pin routing.
                /// @param baudRate  Bits per second (e.g. 115200).
                /// @param txPin     GPIO number for TX output.
                /// @param rxPin     GPIO number for RX input.
                /// @param rxBufSize RX ring buffer in bytes.
                /// @param txBufSize TX ring buffer in bytes (0 = blocking writes).
                /// @return true on success, false if already installed or driver error.
                bool begin(uint32_t baudRate, uint8_t txPin, uint8_t rxPin,
                           uint16_t rxBufSize = DEFAULT_RX_BUF,
                           uint16_t txBufSize = DEFAULT_TX_BUF);

                /// @brief Transmit bytes. Blocks until data is in the TX FIFO/ring buffer.
                /// @return Number of bytes written, or -1 on error.
                int32_t write(const uint8_t* data, size_t length);

                /// @brief Read bytes with timeout.
                /// @param buffer     Destination buffer.
                /// @param maxLength  Maximum bytes to read.
                /// @param timeoutMs  Timeout in milliseconds (0 = non-blocking).
                /// @return Number of bytes read, or -1 on error.
                int32_t read(uint8_t* buffer, size_t maxLength, uint32_t timeoutMs);

                /// @brief Block until all TX data has been physically sent.
                void flush();

                /// @brief Discard all pending data in the RX ring buffer.
                void flushInput();

                /// @brief Hardware peripheral index passed at construction.
                uint8_t port() const {
                    return port_;
                }

            private:
                uint8_t port_;
                bool installed_ = false;
        };

    }  // namespace uart
}  // namespace ungula
