// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <hal/gpio/gpio_access.h>

#include <cstdint>

namespace {

    using ungula::gpio::InterruptEdge;
    using ungula::gpio::PullMode;
    using ungula::gpio::RelayPolarity;
    using ungula::gpio::GpioIsrHandler;

    constexpr uint8_t TEST_PIN = 4;

    // ---- Pin configuration ----

    TEST(GpioAccessStubTest, ConfigOutputSucceeds) {
        EXPECT_TRUE(ungula::gpio::configOutput(TEST_PIN));
    }

    TEST(GpioAccessStubTest, ConfigInputVariantsSucceed) {
        EXPECT_TRUE(ungula::gpio::configInput(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::configInputPullup(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::configInputPulldown(TEST_PIN));
    }

    TEST(GpioAccessStubTest, ConfigOutputOpenDrainSucceeds) {
        EXPECT_TRUE(ungula::gpio::configOutputOpenDrain(TEST_PIN));
    }

    TEST(GpioAccessStubTest, ConfigOutputRelayDefaultsToActiveLow) {
        EXPECT_TRUE(ungula::gpio::configOutputRelay(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::configOutputRelay(TEST_PIN, RelayPolarity::ActiveHigh));
        EXPECT_TRUE(ungula::gpio::configOutputRelay(TEST_PIN, RelayPolarity::ActiveLow));
    }

    // ---- Unchecked digital I/O ----

    TEST(GpioAccessStubTest, ReadReturnsDefaultLow) {
        EXPECT_FALSE(ungula::gpio::read(TEST_PIN));
    }

    TEST(GpioAccessStubTest, WriteVariantsDoNotCrash) {
        ungula::gpio::setHigh(TEST_PIN);
        ungula::gpio::setLow(TEST_PIN);
        ungula::gpio::write(TEST_PIN, true);
        ungula::gpio::write(TEST_PIN, false);
        ungula::gpio::writeHigh(TEST_PIN);
        ungula::gpio::writeLow(TEST_PIN);
        ungula::gpio::toggle(TEST_PIN);
    }

    TEST(GpioAccessStubTest, OnOffHelpersDoNotCrash) {
        ungula::gpio::on(TEST_PIN);
        ungula::gpio::off(TEST_PIN);
    }

    TEST(GpioAccessStubTest, StateQueryHelpersAgreeWithRead) {
        // Stub backend: read() returns false, so high/on/enabled/closed track that.
        EXPECT_FALSE(ungula::gpio::isHigh(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::isLow(TEST_PIN));
        EXPECT_FALSE(ungula::gpio::isOn(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::isOff(TEST_PIN));
        EXPECT_FALSE(ungula::gpio::isEnabled(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::isDisabled(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::isOpen(TEST_PIN));
        EXPECT_FALSE(ungula::gpio::isClosed(TEST_PIN));
    }

    // ---- Checked digital I/O ----

    TEST(GpioAccessStubTest, CheckedReadReportsDefaultLow) {
        bool out = true;
        EXPECT_TRUE(ungula::gpio::checkedRead(TEST_PIN, out));
        EXPECT_FALSE(out);
    }

    TEST(GpioAccessStubTest, CheckedWriteVariantsSucceed) {
        EXPECT_TRUE(ungula::gpio::checkedSetHigh(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::checkedSetLow(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::checkedWrite(TEST_PIN, true));
        EXPECT_TRUE(ungula::gpio::checkedWrite(TEST_PIN, false));
    }

    // ---- Interrupts ----

    void UNGULA_ISR_ATTR dummyIsr(void* /*ctx*/) {}

    TEST(GpioAccessStubTest, ConfigInputInterruptAcceptsAllEdges) {
        EXPECT_TRUE(ungula::gpio::configInputInterrupt(TEST_PIN, InterruptEdge::EDGE_RISING));
        EXPECT_TRUE(ungula::gpio::configInputInterrupt(TEST_PIN, InterruptEdge::EDGE_FALLING,
                                                      PullMode::UP));
        EXPECT_TRUE(ungula::gpio::configInputInterrupt(TEST_PIN, InterruptEdge::EDGE_ANY,
                                                      PullMode::DOWN));
    }

    TEST(GpioAccessStubTest, IsrServiceLifecycle) {
        EXPECT_TRUE(ungula::gpio::installIsrService());
        EXPECT_TRUE(ungula::gpio::installIsrService());  // idempotent
        EXPECT_TRUE(ungula::gpio::addIsrHandler(TEST_PIN, &dummyIsr, nullptr));
        EXPECT_TRUE(ungula::gpio::removeIsrHandler(TEST_PIN));
    }

    TEST(GpioAccessStubTest, IsrHandlerSignatureIsAssignable) {
        GpioIsrHandler handler = &dummyIsr;
        EXPECT_NE(handler, nullptr);
    }

    // ---- PWM ----

    TEST(GpioAccessStubTest, ConfigPwmDefaultsSucceed) {
        EXPECT_TRUE(ungula::gpio::configPwm(TEST_PIN));
        EXPECT_TRUE(ungula::gpio::configPwm(TEST_PIN, 25000, 8));
    }

    TEST(GpioAccessStubTest, WritePwmAcceptsAnyDuty) {
        EXPECT_TRUE(ungula::gpio::writePwm(TEST_PIN, 0));
        EXPECT_TRUE(ungula::gpio::writePwm(TEST_PIN, 128));
        EXPECT_TRUE(ungula::gpio::writePwm(TEST_PIN, 255));
    }

    // ---- Enum stability ----

    TEST(GpioAccessStubTest, InterruptEdgeValuesAreStable) {
        EXPECT_EQ(static_cast<uint8_t>(InterruptEdge::EDGE_RISING), 0);
        EXPECT_EQ(static_cast<uint8_t>(InterruptEdge::EDGE_FALLING), 1);
        EXPECT_EQ(static_cast<uint8_t>(InterruptEdge::EDGE_ANY), 2);
    }

    TEST(GpioAccessStubTest, PullModeValuesAreStable) {
        EXPECT_EQ(static_cast<uint8_t>(PullMode::NONE), 0);
        EXPECT_EQ(static_cast<uint8_t>(PullMode::UP), 1);
        EXPECT_EQ(static_cast<uint8_t>(PullMode::DOWN), 2);
    }

    TEST(GpioAccessStubTest, RelayPolarityIsTwoStates) {
        EXPECT_NE(static_cast<uint8_t>(RelayPolarity::ActiveLow),
                  static_cast<uint8_t>(RelayPolarity::ActiveHigh));
    }

}  // namespace
