// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if !defined(ESP_PLATFORM)

#include "../uart.h"

namespace ungula {
    namespace uart {

        Uart::Uart(uint8_t portNumber) : port_(portNumber) {}

        Uart::~Uart() {}

        bool Uart::begin(uint32_t /*baudRate*/, uint8_t /*txPin*/, uint8_t /*rxPin*/,
                         uint16_t /*rxBufSize*/, uint16_t /*txBufSize*/) {
            if (installed_) {
                return false;
            }
            installed_ = true;
            return true;
        }

        int32_t Uart::write(const uint8_t* /*data*/, size_t length) {
            if (!installed_) {
                return -1;
            }
            return static_cast<int32_t>(length);
        }

        int32_t Uart::read(uint8_t* buffer, size_t maxLength, uint32_t /*timeoutMs*/) {
            if (!installed_) {
                return -1;
            }
            if (buffer != nullptr && maxLength > 0) {
                buffer[0] = 0;
            }
            return 0;
        }

        void Uart::flush() {}

        void Uart::flushInput() {}

    }  // namespace uart
}  // namespace ungula

#endif  // !ESP_PLATFORM
