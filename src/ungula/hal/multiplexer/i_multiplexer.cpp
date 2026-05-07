// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include "i_multiplexer.h"

#include <stdarg.h>
#include <stdio.h>

#include <emblogx/logger.h>

namespace ungula::hal::multiplexer {

    namespace {
        // Body buffer for the caller-supplied portion of the log line.
        // Sized so that `prefix + body` fits comfortably under EmblogX's
        // own line buffer; truncation is silent (vsnprintf-style).
        constexpr size_t LOG_BODY_CAPACITY = 96;
        constexpr size_t LOG_PREFIX_CAPACITY = 48;
    }  // namespace

    size_t IMultiplexer::formatLogPrefix(char* buf, size_t bufSize) const {
        if (buf == nullptr || bufSize == 0) {
            return 0;
        }
        const int n = snprintf(buf, bufSize, "[%s @0x%02X]",
                               name_ != nullptr ? name_ : "?", i2cAddress_);
        return (n < 0) ? 0 : static_cast<size_t>(n);
    }

#define UNGULA_MUX_DEFINE_LOG_HELPER(NAME, EMIT)                            \
    void IMultiplexer::NAME(const char* fmt, ...) const {                   \
        if (!loggingEnabled_) {                                             \
            return;                                                         \
        }                                                                   \
        char prefix[LOG_PREFIX_CAPACITY];                                   \
        formatLogPrefix(prefix, sizeof(prefix));                            \
        char body[LOG_BODY_CAPACITY];                                       \
        va_list ap;                                                         \
        va_start(ap, fmt);                                                  \
        vsnprintf(body, sizeof(body), fmt, ap);                             \
        va_end(ap);                                                         \
        EMIT(LOG_MODULE, "%s %s", prefix, body);                            \
    }

    UNGULA_MUX_DEFINE_LOG_HELPER(logInfof, log_info_m)
    UNGULA_MUX_DEFINE_LOG_HELPER(logWarnf, log_warn_m)
    UNGULA_MUX_DEFINE_LOG_HELPER(logErrorf, log_error_m)
    UNGULA_MUX_DEFINE_LOG_HELPER(logDebugf, log_debug_m)

#undef UNGULA_MUX_DEFINE_LOG_HELPER

    bool IMultiplexer::selectChannel(uint8_t channel) {
        // Cache: if we are already on this channel and the device is
        // healthy, skip the I2C write entirely. Big win for hot paths
        // that read several registers from the same downstream device.
        if (initiated_ && isFunctional_ && currentChannel_ == channel) {
            return true;
        }

        if (!initiated_) {
            logErrorf("not initialised, call begin() first");
            return false;
        }

        logDebugf("selecting channel %u", channel);

        for (uint8_t attempt = 0; attempt < SELECT_RETRY_COUNT; ++attempt) {
            if (selectChannel_(channel)) {
                currentChannel_ = channel;
                isFunctional_ = true;
                return true;
            }
        }

        // All retries failed — mark unhealthy so the next call re-tries
        // even if the caller asks for the same channel.
        isFunctional_ = false;
        logErrorf("select channel %u failed after %u retries", channel,
                  SELECT_RETRY_COUNT);
        return false;
    }

}  // namespace ungula::hal::multiplexer
