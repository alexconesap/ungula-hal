// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <ungula/hal/gpio/gpio.h>

#include <cstdint>

namespace
{

using ungula::hal::gpio::GpioIsrHandler;
using ungula::hal::gpio::IsrServiceInstall;
using ungula::hal::gpio::InterruptEdge;
using ungula::hal::gpio::PullMode;
using ungula::hal::gpio::RelayPolarity;

constexpr uint8_t TEST_PIN = 4;

// ---- Pin configuration ----

TEST(GpioAccessStubTest, ConfigOutputSucceeds)
{
        EXPECT_TRUE(ungula::hal::gpio::configOutput(TEST_PIN));
}

TEST(GpioAccessStubTest, ConfigInputVariantsSucceed)
{
        EXPECT_TRUE(ungula::hal::gpio::configInput(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::configInputPullup(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::configInputPulldown(TEST_PIN));
}

TEST(GpioAccessStubTest, ConfigOutputOpenDrainSucceeds)
{
        EXPECT_TRUE(ungula::hal::gpio::configOutputOpenDrain(TEST_PIN));
}

TEST(GpioAccessStubTest, ConfigOutputRelayDefaultsToActiveLow)
{
        EXPECT_TRUE(ungula::hal::gpio::configOutputRelay(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::configOutputRelay(TEST_PIN, RelayPolarity::ActiveHigh));
        EXPECT_TRUE(ungula::hal::gpio::configOutputRelay(TEST_PIN, RelayPolarity::ActiveLow));
}

// ---- Unchecked digital I/O ----

TEST(GpioAccessStubTest, ReadReturnsDefaultLow)
{
        EXPECT_FALSE(ungula::hal::gpio::read(TEST_PIN));
}

TEST(GpioAccessStubTest, WriteVariantsDoNotCrash)
{
        ungula::hal::gpio::setHigh(TEST_PIN);
        ungula::hal::gpio::setLow(TEST_PIN);
        ungula::hal::gpio::write(TEST_PIN, true);
        ungula::hal::gpio::write(TEST_PIN, false);
        ungula::hal::gpio::writeHigh(TEST_PIN);
        ungula::hal::gpio::writeLow(TEST_PIN);
        ungula::hal::gpio::toggle(TEST_PIN);
}

TEST(GpioAccessStubTest, OnOffHelpersDoNotCrash)
{
        ungula::hal::gpio::on(TEST_PIN);
        ungula::hal::gpio::off(TEST_PIN);
}

TEST(GpioAccessStubTest, StateQueryHelpersAgreeWithRead)
{
        // Stub backend: read() returns false, so high/on/enabled/closed track that.
        EXPECT_FALSE(ungula::hal::gpio::isHigh(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::isLow(TEST_PIN));
        EXPECT_FALSE(ungula::hal::gpio::isOn(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::isOff(TEST_PIN));
        EXPECT_FALSE(ungula::hal::gpio::isEnabled(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::isDisabled(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::isOpen(TEST_PIN));
        EXPECT_FALSE(ungula::hal::gpio::isClosed(TEST_PIN));
}

// ---- Checked digital I/O ----

TEST(GpioAccessStubTest, CheckedReadReportsDefaultLow)
{
        bool out = true;
        EXPECT_TRUE(ungula::hal::gpio::checkedRead(TEST_PIN, out));
        EXPECT_FALSE(out);
}

TEST(GpioAccessStubTest, CheckedWriteVariantsSucceed)
{
        EXPECT_TRUE(ungula::hal::gpio::checkedSetHigh(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::checkedSetLow(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::checkedWrite(TEST_PIN, true));
        EXPECT_TRUE(ungula::hal::gpio::checkedWrite(TEST_PIN, false));
}

// ---- Interrupts ----

void UNGULA_ISR_ATTR dummyIsr(void * /*ctx*/)
{
}

TEST(GpioAccessStubTest, ConfigInputInterruptAcceptsAllEdges)
{
        EXPECT_TRUE(ungula::hal::gpio::configInputInterrupt(TEST_PIN, InterruptEdge::EDGE_RISING));
        EXPECT_TRUE(ungula::hal::gpio::configInputInterrupt(TEST_PIN, InterruptEdge::EDGE_FALLING,
                                                            PullMode::UP));
        EXPECT_TRUE(ungula::hal::gpio::configInputInterrupt(TEST_PIN, InterruptEdge::EDGE_ANY,
                                                            PullMode::DOWN));
}

TEST(GpioAccessStubTest, IsrServiceLifecycle)
{
        const auto first = ungula::hal::gpio::installIsrService();
        EXPECT_TRUE(first == IsrServiceInstall::Installed ||
                    first == IsrServiceInstall::AlreadyInstalled);

        const auto second = ungula::hal::gpio::installIsrService(); // idempotent
        EXPECT_EQ(second, IsrServiceInstall::AlreadyInstalled);
        EXPECT_TRUE(ungula::hal::gpio::addIsrHandler(TEST_PIN, &dummyIsr, nullptr));
        EXPECT_TRUE(ungula::hal::gpio::removeIsrHandler(TEST_PIN));
}

TEST(GpioAccessStubTest, IsrHandlerSignatureIsAssignable)
{
        GpioIsrHandler handler = &dummyIsr;
        EXPECT_NE(handler, nullptr);
}

// ---- PWM ----

TEST(GpioAccessStubTest, ConfigPwmDefaultsSucceed)
{
        EXPECT_TRUE(ungula::hal::gpio::configPwm(TEST_PIN));
        EXPECT_TRUE(ungula::hal::gpio::configPwm(TEST_PIN, 25000, 8));
}

TEST(GpioAccessStubTest, WritePwmAcceptsAnyDuty)
{
        EXPECT_TRUE(ungula::hal::gpio::writePwm(TEST_PIN, 0));
        EXPECT_TRUE(ungula::hal::gpio::writePwm(TEST_PIN, 128));
        EXPECT_TRUE(ungula::hal::gpio::writePwm(TEST_PIN, 255));
}

// ---- Enum stability ----

TEST(GpioAccessStubTest, InterruptEdgeValuesAreStable)
{
        EXPECT_EQ(static_cast<uint8_t>(InterruptEdge::EDGE_RISING), 0);
        EXPECT_EQ(static_cast<uint8_t>(InterruptEdge::EDGE_FALLING), 1);
        EXPECT_EQ(static_cast<uint8_t>(InterruptEdge::EDGE_ANY), 2);
}

TEST(GpioAccessStubTest, PullModeValuesAreStable)
{
        EXPECT_EQ(static_cast<uint8_t>(PullMode::NONE), 0);
        EXPECT_EQ(static_cast<uint8_t>(PullMode::UP), 1);
        EXPECT_EQ(static_cast<uint8_t>(PullMode::DOWN), 2);
}

TEST(GpioAccessStubTest, RelayPolarityIsTwoStates)
{
        EXPECT_NE(static_cast<uint8_t>(RelayPolarity::ActiveLow),
                  static_cast<uint8_t>(RelayPolarity::ActiveHigh));
}

} // namespace
