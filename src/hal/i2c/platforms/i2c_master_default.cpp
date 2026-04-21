// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if !defined(ESP_PLATFORM)

#include "../i2c_master.h"

namespace ungula {
    namespace i2c {

        I2cMaster::I2cMaster(uint8_t portNumber) : port_(portNumber) {}

        I2cMaster::~I2cMaster() {}

        bool I2cMaster::begin(uint8_t /*sdaPin*/, uint8_t /*sclPin*/, uint32_t /*freqHz*/) {
            if (installed_) {
                return false;
            }
            installed_ = true;
            return true;
        }

        bool I2cMaster::write(uint8_t /*addr*/, const uint8_t* /*data*/, size_t /*length*/,
                              uint32_t /*timeoutMs*/) {
            return installed_;
        }

        bool I2cMaster::read(uint8_t /*addr*/, uint8_t* /*buffer*/, size_t /*length*/,
                             uint32_t /*timeoutMs*/) {
            return installed_;
        }

        bool I2cMaster::writeRead(uint8_t /*addr*/, const uint8_t* /*writeData*/,
                                  size_t /*writeLen*/, uint8_t* /*readBuf*/, size_t /*readLen*/,
                                  uint32_t /*timeoutMs*/) {
            return installed_;
        }

    }  // namespace i2c
}  // namespace ungula

#endif  // !ESP_PLATFORM
