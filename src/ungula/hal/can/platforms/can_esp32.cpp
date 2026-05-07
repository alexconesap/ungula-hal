// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "../can.h"

#include "driver/twai.h"

namespace ungula::hal::can {

    namespace {

        // Map a numeric bitrate to the matching TWAI timing preset.
        // Anything outside the table makes begin() reject — caller must
        // pick a supported value.
        bool resolveTiming(uint32_t bitrateBps, twai_timing_config_t& out) {
            switch (bitrateBps) {
                case BITRATE_25K:  out = TWAI_TIMING_CONFIG_25KBITS();  return true;
                case BITRATE_50K:  out = TWAI_TIMING_CONFIG_50KBITS();  return true;
                case BITRATE_100K: out = TWAI_TIMING_CONFIG_100KBITS(); return true;
                case BITRATE_125K: out = TWAI_TIMING_CONFIG_125KBITS(); return true;
                case BITRATE_250K: out = TWAI_TIMING_CONFIG_250KBITS(); return true;
                case BITRATE_500K: out = TWAI_TIMING_CONFIG_500KBITS(); return true;
                case BITRATE_800K: out = TWAI_TIMING_CONFIG_800KBITS(); return true;
                case BITRATE_1M:   out = TWAI_TIMING_CONFIG_1MBITS();   return true;
                default:           return false;
            }
        }

        // Build the SJA1000-compatible code/mask register pair from a
        // user-facing (id, mask, extendedId) triple. TWAI single-filter
        // mode left-justifies the ID into the 32-bit register: standard
        // IDs occupy bits 31..21, extended IDs occupy bits 31..3. The
        // mask follows the same alignment, with 1 = "don't care".
        twai_filter_config_t makeFilter(uint32_t id, uint32_t mask, bool extendedId) {
            twai_filter_config_t f{};
            if (extendedId) {
                f.acceptance_code = (id & 0x1FFFFFFFU) << 3;
                f.acceptance_mask = ~((mask & 0x1FFFFFFFU) << 3);
            } else {
                f.acceptance_code = (id & 0x7FFU) << 21;
                f.acceptance_mask = ~((mask & 0x7FFU) << 21);
            }
            f.single_filter = true;
            return f;
        }

        twai_filter_config_t acceptAllFilter() {
            return TWAI_FILTER_CONFIG_ACCEPT_ALL();
        }

        void frameToMessage(const CanFrame& src, twai_message_t& dst) {
            dst = {};
            dst.identifier = src.id;
            dst.extd = src.extendedId ? 1U : 0U;
            dst.rtr = src.remote ? 1U : 0U;
            dst.data_length_code = src.dlc > 8U ? 8U : src.dlc;
            for (uint8_t i = 0; i < dst.data_length_code; ++i) {
                dst.data[i] = src.data[i];
            }
        }

        void messageToFrame(const twai_message_t& src, CanFrame& dst) {
            dst = {};
            dst.id = src.identifier;
            dst.extendedId = src.extd != 0U;
            dst.remote = src.rtr != 0U;
            dst.dlc = src.data_length_code > 8U ? 8U : src.data_length_code;
            for (uint8_t i = 0; i < dst.dlc; ++i) {
                dst.data[i] = src.data[i];
            }
        }

        // Install + start the driver with the supplied parameters.
        // Caller (the public methods of `Can`) owns the in-memory state.
        bool installAndStart(uint8_t txPin, uint8_t rxPin, uint32_t bitrateBps,
                             const twai_filter_config_t& filter) {
            twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
                    static_cast<gpio_num_t>(txPin),
                    static_cast<gpio_num_t>(rxPin),
                    TWAI_MODE_NORMAL);
            twai_timing_config_t timing{};
            if (!resolveTiming(bitrateBps, timing)) {
                return false;
            }
            if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
                return false;
            }
            if (twai_start() != ESP_OK) {
                twai_driver_uninstall();
                return false;
            }
            return true;
        }

    }  // namespace

    Can::Can(uint8_t controllerNumber) : port_(controllerNumber) {}

    Can::~Can() {
        if (installed_) {
            twai_stop();
            twai_driver_uninstall();
        }
    }

    bool Can::begin(uint8_t txPin, uint8_t rxPin, uint32_t bitrateBps) {
        if (installed_) {
            return false;
        }
        if (!installAndStart(txPin, rxPin, bitrateBps, acceptAllFilter())) {
            return false;
        }

        txPin_ = txPin;
        rxPin_ = rxPin;
        bitrateBps_ = bitrateBps;
        filterEnabled_ = false;
        installed_ = true;
        return true;
    }

    bool Can::stop() {
        if (!installed_) {
            return true;
        }
        twai_stop();
        twai_driver_uninstall();
        installed_ = false;
        return true;
    }

    bool Can::send(const CanFrame& frame, uint32_t timeoutMs) {
        if (!installed_) {
            return false;
        }
        twai_message_t msg{};
        frameToMessage(frame, msg);
        return twai_transmit(&msg, pdMS_TO_TICKS(timeoutMs)) == ESP_OK;
    }

    int32_t Can::receive(CanFrame& out, uint32_t timeoutMs) {
        if (!installed_) {
            return -1;
        }
        twai_message_t msg{};
        const esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(timeoutMs));
        if (err == ESP_OK) {
            messageToFrame(msg, out);
            return 1;
        }
        if (err == ESP_ERR_TIMEOUT) {
            return 0;
        }
        return -1;
    }

    bool Can::setAcceptanceFilter(uint32_t id, uint32_t mask, bool extendedId) {
        if (!installed_) {
            return false;
        }
        // TWAI only accepts the filter at install time. Tear down and
        // bring the driver back with the new filter so callers see
        // a single atomic "filter is now active" transition.
        twai_stop();
        twai_driver_uninstall();
        installed_ = false;

        if (!installAndStart(txPin_, rxPin_, bitrateBps_,
                             makeFilter(id, mask, extendedId))) {
            return false;
        }
        filterId_ = id;
        filterMask_ = mask;
        filterExtended_ = extendedId;
        filterEnabled_ = true;
        installed_ = true;
        return true;
    }

    bool Can::clearAcceptanceFilter() {
        if (!installed_) {
            return false;
        }
        twai_stop();
        twai_driver_uninstall();
        installed_ = false;

        if (!installAndStart(txPin_, rxPin_, bitrateBps_, acceptAllFilter())) {
            return false;
        }
        filterEnabled_ = false;
        installed_ = true;
        return true;
    }

    bool Can::isBusOff() const {
        if (!installed_) {
            return false;
        }
        twai_status_info_t status{};
        if (twai_get_status_info(&status) != ESP_OK) {
            return false;
        }
        return status.state == TWAI_STATE_BUS_OFF;
    }

    bool Can::recoverFromBusOff() {
        if (!installed_) {
            return false;
        }
        return twai_initiate_recovery() == ESP_OK;
    }

}  // namespace ungula::hal::can

#endif  // ESP_PLATFORM
