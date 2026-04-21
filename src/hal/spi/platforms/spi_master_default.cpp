// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if !defined(ESP_PLATFORM)

#include "../spi_master.h"

#include <cstring>

namespace ungula {
    namespace spi {

        SpiMaster::~SpiMaster() {}

        bool SpiMaster::begin(uint8_t /*sclkPin*/, uint8_t /*misoPin*/, uint8_t /*mosiPin*/,
                              uint8_t /*csPin*/, uint32_t /*freqHz*/, uint8_t /*mode*/,
                              uint8_t /*host*/) {
            installed_ = true;
            return true;
        }

        bool SpiMaster::transfer(const uint8_t* /*txData*/, uint8_t* rxData, size_t length) {
            if (rxData != nullptr) {
                memset(rxData, 0, length);
            }
            return installed_;
        }

        bool SpiMaster::write(const uint8_t* /*data*/, size_t /*length*/) {
            return installed_;
        }

        bool SpiMaster::read(uint8_t* buffer, size_t length) {
            if (buffer != nullptr) {
                memset(buffer, 0, length);
            }
            return installed_;
        }

        bool SpiMaster::writeRead(const uint8_t* /*txData*/, size_t /*writeLen*/, uint8_t* rxBuf,
                                  size_t readLen) {
            if (rxBuf != nullptr) {
                memset(rxBuf, 0, readLen);
            }
            return installed_;
        }

    }  // namespace spi
}  // namespace ungula

#endif  // !ESP_PLATFORM
