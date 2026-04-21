// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "../uart.h"
#include "driver/uart.h"

namespace ungula {
    namespace uart {

        static constexpr uint32_t TX_FLUSH_TIMEOUT_MS = 100;
        static constexpr int UART_PIN_UNUSED = -1;

        Uart::Uart(uint8_t portNumber) : port_(portNumber) {}

        Uart::~Uart() {
            if (installed_) {
                uart_driver_delete(static_cast<uart_port_t>(port_));
            }
        }

        bool Uart::begin(uint32_t baudRate, uint8_t txPin, uint8_t rxPin, uint16_t rxBufSize,
                         uint16_t txBufSize) {
            if (installed_) {
                return false;
            }

            uart_config_t config = {};
            config.baud_rate = static_cast<int>(baudRate);
            config.data_bits = UART_DATA_8_BITS;
            config.parity = UART_PARITY_DISABLE;
            config.stop_bits = UART_STOP_BITS_1;
            config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
            config.source_clk = UART_SCLK_APB;

            auto port = static_cast<uart_port_t>(port_);

            if (uart_param_config(port, &config) != ESP_OK) {
                return false;
            }
            if (uart_set_pin(port, txPin, rxPin, UART_PIN_UNUSED, UART_PIN_UNUSED) != ESP_OK) {
                return false;
            }
            if (uart_driver_install(port, rxBufSize, txBufSize, 0, nullptr, 0) != ESP_OK) {
                return false;
            }

            installed_ = true;
            return true;
        }

        int32_t Uart::write(const uint8_t* data, size_t length) {
            if (!installed_) {
                return -1;
            }
            return uart_write_bytes(static_cast<uart_port_t>(port_), data, length);
        }

        int32_t Uart::read(uint8_t* buffer, size_t maxLength, uint32_t timeoutMs) {
            if (!installed_) {
                return -1;
            }
            TickType_t ticks = (timeoutMs == 0) ? 0 : pdMS_TO_TICKS(timeoutMs);
            return uart_read_bytes(static_cast<uart_port_t>(port_), buffer, maxLength, ticks);
        }

        void Uart::flush() {
            if (installed_) {
                uart_wait_tx_done(static_cast<uart_port_t>(port_),
                                  pdMS_TO_TICKS(TX_FLUSH_TIMEOUT_MS));
            }
        }

        void Uart::flushInput() {
            if (installed_) {
                uart_flush_input(static_cast<uart_port_t>(port_));
            }
        }

    }  // namespace uart
}  // namespace ungula

#endif  // ESP_PLATFORM
