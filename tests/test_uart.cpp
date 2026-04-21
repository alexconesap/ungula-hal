// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <hal/uart/uart.h>

namespace {

    using ungula::uart::Uart;

    TEST(UartStubTest, ConstructorSetsPort) {
        Uart uart(2);
        EXPECT_EQ(uart.port(), 2);
    }

    TEST(UartStubTest, BeginSucceeds) {
        Uart uart(0);
        EXPECT_TRUE(uart.begin(115200, 17, 16));
    }

    TEST(UartStubTest, BeginTwiceFails) {
        Uart uart(0);
        EXPECT_TRUE(uart.begin(115200, 17, 16));
        EXPECT_FALSE(uart.begin(115200, 17, 16));
    }

    TEST(UartStubTest, WriteReturnsLengthAfterBegin) {
        Uart uart(0);
        uart.begin(115200, 17, 16);
        uint8_t data[] = {0x01, 0x02, 0x03};
        EXPECT_EQ(uart.write(data, sizeof(data)), 3);
    }

    TEST(UartStubTest, WriteReturnsErrorBeforeBegin) {
        Uart uart(0);
        uint8_t data[] = {0x01};
        EXPECT_EQ(uart.write(data, sizeof(data)), -1);
    }

    TEST(UartStubTest, ReadReturnsZeroBytesAfterBegin) {
        Uart uart(0);
        uart.begin(115200, 17, 16);
        uint8_t buf[8] = {};
        EXPECT_EQ(uart.read(buf, sizeof(buf), 100), 0);
    }

    TEST(UartStubTest, ReadReturnsErrorBeforeBegin) {
        Uart uart(0);
        uint8_t buf[8] = {};
        EXPECT_EQ(uart.read(buf, sizeof(buf), 100), -1);
    }

    TEST(UartStubTest, FlushDoesNotCrash) {
        Uart uart(0);
        uart.begin(115200, 17, 16);
        uart.flush();
        uart.flushInput();
    }

}  // namespace
