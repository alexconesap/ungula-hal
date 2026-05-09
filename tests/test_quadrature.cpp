// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <ungula/hal/quadrature/drivers/decoder.h>
#include <ungula/hal/quadrature/drivers/decoder_fake.h>
#include <ungula/hal/quadrature/i_decoder.h>

namespace {

    using ungula::hal::quadrature::IDecoder;
    using ungula::hal::quadrature::drivers::Decoder;
    using ungula::hal::quadrature::drivers::DecoderFake;

    // ---- Concrete host stub ----

    TEST(DecoderHostStub, BeginIsAcceptedOnce) {
        Decoder dec;
        EXPECT_TRUE(dec.begin(34, 35, /*initial=*/100));
        EXPECT_FALSE(dec.begin(34, 35));
        EXPECT_EQ(dec.pinA(), 34U);
        EXPECT_EQ(dec.pinB(), 35U);
        EXPECT_EQ(dec.count(), 100);
    }

    TEST(DecoderHostStub, ResetMovesTheCount) {
        Decoder dec;
        dec.begin(34, 35);
        EXPECT_TRUE(dec.reset(-42));
        EXPECT_EQ(dec.count(), -42);
    }

    TEST(DecoderHostStub, HasNoIndexByDefault) {
        Decoder dec;
        EXPECT_FALSE(dec.hasIndex());
    }

    TEST(DecoderHostStub, IsAValidIDecoder) {
        Decoder dec;
        IDecoder* api = static_cast<IDecoder*>(&dec);
        EXPECT_NE(api, nullptr);
    }

    // ---- Fake ----

    TEST(DecoderFake, TicksAdvanceTheCount) {
        DecoderFake fake;
        fake.begin(1, 2);
        fake.tick(+5);
        fake.tick(-2);
        EXPECT_EQ(fake.count(), 3);
    }

    TEST(DecoderFake, IndexLatchSurfacesThroughInterface) {
        DecoderFake fake;
        fake.setHasIndex(true);
        fake.markIndex(true);
        IDecoder& api = fake;
        EXPECT_TRUE(api.hasIndex());
        EXPECT_TRUE(api.latchedAtIndex());
    }

}  // namespace
