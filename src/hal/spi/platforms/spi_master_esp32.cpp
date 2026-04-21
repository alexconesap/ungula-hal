// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "../spi_master.h"
#include "driver/spi_master.h"

#include <cstring>

namespace ungula {
    namespace spi {

        SpiMaster::~SpiMaster() {
            if (installed_ && devHandle_ != nullptr) {
                spi_bus_remove_device(static_cast<spi_device_handle_t>(devHandle_));
            }
        }

        bool SpiMaster::begin(uint8_t sclkPin, uint8_t misoPin, uint8_t mosiPin, uint8_t csPin,
                              uint32_t freqHz, uint8_t mode, uint8_t host) {
            if (installed_) {
                return false;
            }

            spi_bus_config_t busCfg = {};
            busCfg.mosi_io_num = mosiPin;
            busCfg.miso_io_num = misoPin;
            busCfg.sclk_io_num = sclkPin;
            busCfg.quadwp_io_num = -1;
            busCfg.quadhd_io_num = -1;
            busCfg.max_transfer_sz = 32;

            auto spiHost = static_cast<spi_host_device_t>(host);

            // Initialize the bus (ignore ESP_ERR_INVALID_STATE = already initialized by another
            // device).
            esp_err_t err = spi_bus_initialize(spiHost, &busCfg, SPI_DMA_DISABLED);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                return false;
            }

            spi_device_interface_config_t devCfg = {};
            devCfg.clock_speed_hz = static_cast<int>(freqHz);
            devCfg.mode = mode;
            devCfg.spics_io_num = csPin;
            devCfg.queue_size = 1;

            spi_device_handle_t handle = nullptr;
            if (spi_bus_add_device(spiHost, &devCfg, &handle) != ESP_OK) {
                return false;
            }

            devHandle_ = handle;
            installed_ = true;
            return true;
        }

        bool SpiMaster::transfer(const uint8_t* txData, uint8_t* rxData, size_t length) {
            if (!installed_ || length == 0) {
                return false;
            }

            spi_transaction_t txn = {};
            txn.length = length * 8;
            txn.tx_buffer = txData;
            txn.rx_buffer = rxData;

            return spi_device_polling_transmit(static_cast<spi_device_handle_t>(devHandle_),
                                               &txn) == ESP_OK;
        }

        bool SpiMaster::write(const uint8_t* data, size_t length) {
            return transfer(data, nullptr, length);
        }

        bool SpiMaster::read(uint8_t* buffer, size_t length) {
            return transfer(nullptr, buffer, length);
        }

        bool SpiMaster::writeRead(const uint8_t* txData, size_t writeLen, uint8_t* rxBuf,
                                  size_t readLen) {
            if (!installed_) {
                return false;
            }

            // Combined: write command bytes then read response, all under one CS assertion.
            // Total length = writeLen + readLen. Send txData padded with zeros, receive into a
            // buffer where the first writeLen bytes are discarded (dummy from the command phase).
            const size_t totalLen = writeLen + readLen;
            if (totalLen == 0 || totalLen > 64) {
                return false;  // keep stack allocation bounded
            }

            uint8_t txBuf[64] = {};
            uint8_t rxAll[64] = {};
            memcpy(txBuf, txData, writeLen);

            spi_transaction_t txn = {};
            txn.length = totalLen * 8;
            txn.tx_buffer = txBuf;
            txn.rx_buffer = rxAll;

            if (spi_device_polling_transmit(static_cast<spi_device_handle_t>(devHandle_), &txn) !=
                ESP_OK) {
                return false;
            }

            memcpy(rxBuf, rxAll + writeLen, readLen);
            return true;
        }

    }  // namespace spi
}  // namespace ungula

#endif  // ESP_PLATFORM
