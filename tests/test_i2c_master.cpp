// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <hal/i2c/i2c_master.h>

#include <cstring>

namespace {

    using ungula::i2c::I2cMaster;

    TEST(I2cMasterStubTest, ConstructorSetsPort) {
        I2cMaster bus(0);
        EXPECT_EQ(bus.port(), 0);

        I2cMaster bus1(1);
        EXPECT_EQ(bus1.port(), 1);
    }

    TEST(I2cMasterStubTest, BeginSucceeds) {
        I2cMaster bus(0);
        EXPECT_TRUE(bus.begin(21, 22, 400000));
    }

    TEST(I2cMasterStubTest, BeginTwiceFails) {
        I2cMaster bus(0);
        EXPECT_TRUE(bus.begin(21, 22));
        EXPECT_FALSE(bus.begin(21, 22));
    }

    TEST(I2cMasterStubTest, WriteFailsBeforeBegin) {
        I2cMaster bus(0);
        uint8_t data[] = {0x00, 0x01};
        EXPECT_FALSE(bus.write(0x2A, data, sizeof(data)));
    }

    TEST(I2cMasterStubTest, WriteSucceedsAfterBegin) {
        I2cMaster bus(0);
        bus.begin(21, 22);
        uint8_t data[] = {0x00, 0x01};
        EXPECT_TRUE(bus.write(0x2A, data, sizeof(data)));
    }

    TEST(I2cMasterStubTest, ReadFailsBeforeBegin) {
        I2cMaster bus(0);
        uint8_t buf[4] = {};
        EXPECT_FALSE(bus.read(0x2A, buf, sizeof(buf)));
    }

    TEST(I2cMasterStubTest, ReadSucceedsAfterBegin) {
        I2cMaster bus(0);
        bus.begin(21, 22);
        uint8_t buf[4] = {};
        EXPECT_TRUE(bus.read(0x2A, buf, sizeof(buf)));
    }

    TEST(I2cMasterStubTest, WriteReadFailsBeforeBegin) {
        I2cMaster bus(0);
        uint8_t cmd[] = {0x12};
        uint8_t buf[3] = {};
        EXPECT_FALSE(bus.writeRead(0x2A, cmd, 1, buf, 3));
    }

    TEST(I2cMasterStubTest, WriteReadSucceedsAfterBegin) {
        I2cMaster bus(0);
        bus.begin(21, 22);
        uint8_t cmd[] = {0x12};
        uint8_t buf[3] = {};
        EXPECT_TRUE(bus.writeRead(0x2A, cmd, 1, buf, 3));
    }

}  // namespace
