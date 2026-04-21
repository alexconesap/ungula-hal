// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "../i2c_master.h"
#include "driver/i2c.h"

namespace ungula {
    namespace i2c {

        I2cMaster::I2cMaster(uint8_t portNumber) : port_(portNumber) {}

        I2cMaster::~I2cMaster() {
            if (installed_) {
                i2c_driver_delete(static_cast<i2c_port_t>(port_));
            }
        }

        bool I2cMaster::begin(uint8_t sdaPin, uint8_t sclPin, uint32_t freqHz) {
            if (installed_) {
                return false;
            }

            i2c_config_t conf = {};
            conf.mode = I2C_MODE_MASTER;
            conf.sda_io_num = static_cast<gpio_num_t>(sdaPin);
            conf.scl_io_num = static_cast<gpio_num_t>(sclPin);
            conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
            conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
            conf.master.clk_speed = freqHz;

            auto port = static_cast<i2c_port_t>(port_);

            if (i2c_param_config(port, &conf) != ESP_OK) {
                return false;
            }
            if (i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
                return false;
            }

            installed_ = true;
            return true;
        }

        bool I2cMaster::write(uint8_t addr, const uint8_t* data, size_t length,
                              uint32_t timeoutMs) {
            if (!installed_) {
                return false;
            }

            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1U) | I2C_MASTER_WRITE, true);
            if (length > 0 && data != nullptr) {
                i2c_master_write(cmd, data, length, true);
            }
            i2c_master_stop(cmd);

            esp_err_t err = i2c_master_cmd_begin(static_cast<i2c_port_t>(port_), cmd,
                                                 pdMS_TO_TICKS(timeoutMs));
            i2c_cmd_link_delete(cmd);
            return err == ESP_OK;
        }

        bool I2cMaster::read(uint8_t addr, uint8_t* buffer, size_t length, uint32_t timeoutMs) {
            if (!installed_ || length == 0 || buffer == nullptr) {
                return false;
            }

            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1U) | I2C_MASTER_READ, true);
            if (length > 1) {
                i2c_master_read(cmd, buffer, length - 1, I2C_MASTER_ACK);
            }
            i2c_master_read_byte(cmd, buffer + length - 1, I2C_MASTER_NACK);
            i2c_master_stop(cmd);

            esp_err_t err = i2c_master_cmd_begin(static_cast<i2c_port_t>(port_), cmd,
                                                 pdMS_TO_TICKS(timeoutMs));
            i2c_cmd_link_delete(cmd);
            return err == ESP_OK;
        }

        bool I2cMaster::writeRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                                  uint8_t* readBuf, size_t readLen, uint32_t timeoutMs) {
            if (!installed_ || readLen == 0 || readBuf == nullptr) {
                return false;
            }

            i2c_cmd_handle_t cmd = i2c_cmd_link_create();

            // Write phase.
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1U) | I2C_MASTER_WRITE, true);
            if (writeLen > 0 && writeData != nullptr) {
                i2c_master_write(cmd, writeData, writeLen, true);
            }

            // Repeated START + read phase.
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1U) | I2C_MASTER_READ, true);
            if (readLen > 1) {
                i2c_master_read(cmd, readBuf, readLen - 1, I2C_MASTER_ACK);
            }
            i2c_master_read_byte(cmd, readBuf + readLen - 1, I2C_MASTER_NACK);
            i2c_master_stop(cmd);

            esp_err_t err = i2c_master_cmd_begin(static_cast<i2c_port_t>(port_), cmd,
                                                 pdMS_TO_TICKS(timeoutMs));
            i2c_cmd_link_delete(cmd);
            return err == ESP_OK;
        }

    }  // namespace i2c
}  // namespace ungula

#endif  // ESP_PLATFORM
