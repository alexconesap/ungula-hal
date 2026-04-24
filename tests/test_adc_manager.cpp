// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <hal/adc/adc_manager.h>

namespace {

    using ungula::adc::AdcManager;
    using ungula::adc::Attenuation;

    TEST(AdcManagerStubTest, ConfigureNewPinSucceeds) {
        AdcManager mgr;
        EXPECT_TRUE(mgr.configure(34));
        EXPECT_TRUE(mgr.configure(35, Attenuation::DB_6));
    }

    TEST(AdcManagerStubTest, ConfigureSamePinTwiceFails) {
        AdcManager mgr;
        EXPECT_TRUE(mgr.configure(34, Attenuation::DB_12));
        EXPECT_FALSE(mgr.configure(34, Attenuation::DB_0));
    }

    TEST(AdcManagerStubTest, ConfigureBeyondCapacityFails) {
        AdcManager mgr;
        for (size_t i = 0; i < AdcManager::MAX_CHANNELS; i++) {
            EXPECT_TRUE(mgr.configure(static_cast<uint8_t>(i + 1)));
        }
        EXPECT_FALSE(mgr.configure(static_cast<uint8_t>(AdcManager::MAX_CHANNELS + 1)));
    }

    TEST(AdcManagerStubTest, ReadMvReturnsFalseForUnknownPin) {
        AdcManager mgr;
        uint32_t mv = 0xDEAD;
        EXPECT_FALSE(mgr.readMv(10, mv));
        EXPECT_EQ(mv, 0U);
    }

    TEST(AdcManagerStubTest, ReadMvSucceedsAfterConfigure) {
        AdcManager mgr;
        mgr.configure(34);
        uint32_t mv = 0xDEAD;
        EXPECT_TRUE(mgr.readMv(34, mv));
        EXPECT_EQ(mv, 0U);
    }

    TEST(AdcManagerStubTest, ReadRawFollowsSameContract) {
        AdcManager mgr;
        int raw = 0xDEAD;
        EXPECT_FALSE(mgr.readRaw(34, raw));
        EXPECT_EQ(raw, 0);

        mgr.configure(34);
        raw = 0xDEAD;
        EXPECT_TRUE(mgr.readRaw(34, raw));
        EXPECT_EQ(raw, 0);
    }

    TEST(AdcManagerStubTest, DeinitResetsState) {
        AdcManager mgr;
        mgr.configure(34);
        mgr.deinit();

        uint32_t mv = 0;
        EXPECT_FALSE(mgr.readMv(34, mv));
        EXPECT_TRUE(mgr.configure(34));  // slot is free again after deinit.
    }

    TEST(AdcManagerStubTest, DeinitIsIdempotent) {
        AdcManager mgr;
        mgr.configure(34);
        mgr.deinit();
        mgr.deinit();  // must not explode.
    }

    TEST(AdcManagerStubTest, AttenuationEnumValuesAreStable) {
        EXPECT_EQ(static_cast<uint8_t>(Attenuation::DB_0), 0);
        EXPECT_EQ(static_cast<uint8_t>(Attenuation::DB_2_5), 1);
        EXPECT_EQ(static_cast<uint8_t>(Attenuation::DB_6), 2);
        EXPECT_EQ(static_cast<uint8_t>(Attenuation::DB_12), 3);
    }

    TEST(AdcManagerStubTest, InstancesAreIndependent) {
        AdcManager a;
        AdcManager b;
        EXPECT_TRUE(a.configure(34));
        EXPECT_TRUE(b.configure(34));  // different instance — separate slot space.
    }

}  // namespace
