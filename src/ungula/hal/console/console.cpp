// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alex Conesa
// See LICENSE file for details.

#include "console.h"

#include <cstring>

namespace ungula::hal::console
{

Console::Console(const ConsoleConfig &cfg)
        : cfg_(cfg)
        , uart_(cfg.port)
{
}

bool Console::begin()
{
        return uart_.begin(cfg_.baud, cfg_.txPin, cfg_.rxPin);
}

int Console::readChar()
{
        uint8_t b = 0U;
        const int32_t n = uart_.read(&b, 1U, 0U); // 0 ms = non-blocking
        return (n == 1) ? static_cast<int>(b) : -1;
}

void Console::echo(char c)
{
        const uint8_t b = static_cast<uint8_t>(c);
        (void)uart_.write(&b, 1U);
}

void Console::write(const char *str)
{
        if (str == nullptr)
                return;
        (void)uart_.write(reinterpret_cast<const uint8_t *>(str), std::strlen(str));
}

void Console::flushInput()
{
        uart_.flushInput();
}

const char *Console::readLine()
{
        for (;;) {
                const int c = readChar();
                if (c < 0)
                        return nullptr; // nothing pending; line still open

                if (c == '\r' || c == '\n') {
                        write("\r\n");
                        line_[lineLen_] = '\0';
                        lineLen_ = 0U;
                        return line_; // may be empty (bare Enter)
                }

                if (c == 0x08 || c == 0x7F) { // backspace / delete
                        if (lineLen_ > 0U) {
                                --lineLen_;
                                write("\b \b"); // erase the last echoed char
                        }
                        continue;
                }

                if (lineLen_ < DEFAULT_LINE_MAX) {
                        line_[lineLen_++] = static_cast<char>(c);
                        echo(static_cast<char>(c));
                }
                // Overflow: drop extra chars until Enter.
        }
}

} // namespace ungula::hal::console
