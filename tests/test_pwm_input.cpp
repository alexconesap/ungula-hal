// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <ungula/hal/pwm_input/drivers/pwm_input.h>
#include <ungula/hal/pwm_input/drivers/pwm_input_fake.h>
#include <ungula/hal/pwm_input/i_pwm_input.h>

namespace {

    using ungula::hal::pwm_input::IPwmInput;
    using ungula::hal::pwm_input::drivers::PwmInput;
    using ungula::hal::pwm_input::drivers::PwmInputFake;

    // --- Concrete host stub ---------------------------------------------------
    //
    // Real PWM-input capture is meaningless off-target. The host stub only
    // has to compile, accept begin()/stop() and report "no sample". These
    // tests guard those invariants.

    TEST(PwmInputHostStub, BeginIsAcceptedOnce) {
        PwmInput cap;
        EXPECT_TRUE(cap.begin(34));
        EXPECT_FALSE(cap.begin(34));
        EXPECT_EQ(cap.pin(), 34U);
        EXPECT_FALSE(cap.hasSample());
        EXPECT_EQ(cap.lastHighTimeUs(), 0U);
        EXPECT_EQ(cap.lastPeriodUs(), 0U);
    }

    TEST(PwmInputHostStub, StopIsIdempotent) {
        PwmInput cap;
        cap.begin(34);
        EXPECT_TRUE(cap.stop());
        EXPECT_TRUE(cap.stop());
    }

    TEST(PwmInputHostStub, IsAValidIPwmInput) {
        PwmInput cap;
        IPwmInput* api = static_cast<IPwmInput*>(&cap);
        EXPECT_NE(api, nullptr);
    }

    // --- Fake -----------------------------------------------------------------

    TEST(PwmInputFake, InjectSampleSurfacesThroughInterface) {
        PwmInputFake fake;
        IPwmInput& api = fake;
        EXPECT_TRUE(api.begin(7));
        EXPECT_FALSE(api.hasSample());

        fake.injectSample(/*highUs=*/2'500, /*periodUs=*/8'700);
        EXPECT_TRUE(api.hasSample());
        EXPECT_EQ(api.lastHighTimeUs(), 2'500U);
        EXPECT_EQ(api.lastPeriodUs(), 8'700U);
        EXPECT_EQ(api.sampleAgeUs(), 0U);
    }

    TEST(PwmInputFake, SecondBeginRejected) {
        PwmInputFake fake;
        EXPECT_TRUE(fake.begin(7));
        EXPECT_FALSE(fake.begin(7));
    }

    TEST(PwmInputFake, SampleAgeIsCallerControlled) {
        PwmInputFake fake;
        fake.begin(7);
        fake.injectSample(1'000, 8'700);
        fake.setSampleAgeUs(50'000);
        EXPECT_EQ(fake.sampleAgeUs(), 50'000U);
    }

}  // namespace
