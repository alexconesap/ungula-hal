// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstdint>

#include "../uart/uart.h"

namespace ungula::hal::console
{

// Default console wiring: the standard ESP32 console UART — the port the
// bootloader and `idf.py monitor` already use (UART0, TX=GPIO1, RX=GPIO3,
// 115200 8N1). With these defaults a Console "just works" with no arguments.
constexpr uint8_t DEFAULT_PORT = 0U;
constexpr uint8_t DEFAULT_TX_PIN = 1U;
constexpr uint8_t DEFAULT_RX_PIN = 3U;
constexpr uint32_t DEFAULT_BAUD = 115200U;
constexpr uint16_t DEFAULT_LINE_MAX = 80U; // usable chars per readLine(), excl. NUL

/// Console wiring. Every field defaults to the standard ESP32 console UART,
/// so a default-constructed Console targets the same port the serial monitor
/// uses. Override only what you need for a secondary UART.
struct ConsoleConfig {
        uint8_t port = DEFAULT_PORT;
        uint8_t txPin = DEFAULT_TX_PIN;
        uint8_t rxPin = DEFAULT_RX_PIN;
        uint32_t baud = DEFAULT_BAUD;
};

/// Non-blocking serial console for interactive menus and hardware bring-up.
///
/// Reads keystrokes from a hardware UART without ever blocking the main loop,
/// and can assemble whole command lines. Program output keeps using the normal
/// stdout path (printf), so an attached serial monitor still shows logs while
/// the Console reads the same port's RX line and echoes typed characters
/// (terminals like `idf.py monitor` do not local-echo).
///
/// One owner per UART port — the underlying driver is non-copyable.
class Console {
    public:
        explicit Console(const ConsoleConfig &cfg = ConsoleConfig{});

        /// Install the UART RX driver. Returns false if it could not be
        /// installed (e.g. the port is already taken) — keystrokes won't be
        /// read in that case, so the caller should warn.
        bool begin();

        /// Next input byte, or -1 if none is pending. Never blocks.
        int readChar();

        /// Assemble a line without blocking. Returns the completed line
        /// (newline stripped, NUL-terminated) once Enter is seen, or nullptr
        /// while it is still being typed. Handles backspace and echoes input.
        /// The returned pointer stays valid until the next readLine() call.
        const char *readLine();

        /// Echo a single byte back to the terminal.
        void echo(char c);

        /// Write a NUL-terminated string to the console.
        void write(const char *str);

        /// Discard any pending RX bytes (e.g. to drop a half-typed line).
        void flushInput();

        const ConsoleConfig &config() const
        {
                return cfg_;
        }

    private:
        ConsoleConfig cfg_;
        ungula::hal::uart::Uart uart_;
        char line_[DEFAULT_LINE_MAX + 1U];
        uint16_t lineLen_ = 0U;
};

} // namespace ungula::hal::console
