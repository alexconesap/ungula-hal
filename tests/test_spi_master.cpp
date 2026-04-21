// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <hal/spi/spi_master.h>

#include <cstring>

namespace {

    using ungula::spi::SpiMaster;

    TEST(SpiMasterStubTest, BeginSucceeds) {
        SpiMaster spi;
        EXPECT_TRUE(spi.begin(18, 19, 23, 5, 1000000, 1));
    }

    TEST(SpiMasterStubTest, TransferFailsBeforeBegin) {
        SpiMaster spi;
        uint8_t tx[] = {0xAA};
        uint8_t rx[1] = {};
        EXPECT_FALSE(spi.transfer(tx, rx, 1));
    }

    TEST(SpiMasterStubTest, TransferZerosRxBuffer) {
        SpiMaster spi;
        spi.begin(18, 19, 23, 5);
        uint8_t tx[] = {0xAA, 0xBB};
        uint8_t rx[2] = {0xFF, 0xFF};
        EXPECT_TRUE(spi.transfer(tx, rx, 2));
        EXPECT_EQ(rx[0], 0);
        EXPECT_EQ(rx[1], 0);
    }

    TEST(SpiMasterStubTest, WriteSucceedsAfterBegin) {
        SpiMaster spi;
        spi.begin(18, 19, 23, 5);
        uint8_t data[] = {0x06, 0x08};
        EXPECT_TRUE(spi.write(data, sizeof(data)));
    }

    TEST(SpiMasterStubTest, ReadZerosBuffer) {
        SpiMaster spi;
        spi.begin(18, 19, 23, 5);
        uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        EXPECT_TRUE(spi.read(buf, sizeof(buf)));
        for (auto b : buf) {
            EXPECT_EQ(b, 0);
        }
    }

    TEST(SpiMasterStubTest, WriteReadZerosRxPortion) {
        SpiMaster spi;
        spi.begin(18, 19, 23, 5);
        uint8_t cmd[] = {0x10};
        uint8_t rx[3] = {0xFF, 0xFF, 0xFF};
        EXPECT_TRUE(spi.writeRead(cmd, 1, rx, 3));
        for (auto b : rx) {
            EXPECT_EQ(b, 0);
        }
    }

    TEST(SpiMasterStubTest, NullRxDoesNotCrash) {
        SpiMaster spi;
        spi.begin(18, 19, 23, 5);
        uint8_t tx[] = {0x01};
        EXPECT_TRUE(spi.transfer(tx, nullptr, 1));
    }

}  // namespace
