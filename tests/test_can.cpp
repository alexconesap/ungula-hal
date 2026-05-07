// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <type_traits>

#include <ungula/hal/can/can.h>

namespace {

    using ungula::hal::can::BITRATE_125K;
    using ungula::hal::can::BITRATE_1M;
    using ungula::hal::can::BITRATE_500K;
    using ungula::hal::can::Can;
    using ungula::hal::can::CanFrame;

    // Compile-time guarantee: copying/moving the controller would
    // double-uninstall the underlying driver. Catch that intent here —
    // both copy AND move must be off the table because the class
    // owns a hardware driver handle.
    static_assert(!std::is_copy_constructible<Can>::value, "Can must not copy");
    static_assert(!std::is_copy_assignable<Can>::value, "Can must not copy-assign");
    static_assert(!std::is_move_constructible<Can>::value, "Can must not move");
    static_assert(!std::is_move_assignable<Can>::value, "Can must not move-assign");

    TEST(Can, ConstructorRecordsControllerNumber) {
        Can bus(0);
        EXPECT_EQ(bus.controller(), 0U);

        Can bus2(7);
        EXPECT_EQ(bus2.controller(), 7U);
    }

    TEST(Can, BeginSucceedsOnceAndIsRejectedOnSecondCall) {
        Can bus(0);
        EXPECT_TRUE(bus.begin(21, 22, BITRATE_500K));
        EXPECT_FALSE(bus.begin(21, 22, BITRATE_500K));
    }

    TEST(Can, SendFailsBeforeBegin) {
        Can bus(0);
        CanFrame f{};
        EXPECT_FALSE(bus.send(f));
    }

    TEST(Can, SendSucceedsAfterBegin) {
        Can bus(0);
        bus.begin(21, 22, BITRATE_1M);
        CanFrame f{};
        f.id = 0x123;
        f.dlc = 2;
        f.data[0] = 0xDE;
        f.data[1] = 0xAD;
        EXPECT_TRUE(bus.send(f));
    }

    TEST(Can, ReceiveBeforeBeginReturnsMinusOne) {
        Can bus(0);
        CanFrame in{};
        EXPECT_EQ(bus.receive(in), -1);
    }

    TEST(Can, ReceiveOnEmptyStubReturnsZero) {
        Can bus(0);
        bus.begin(21, 22, BITRATE_500K);
        CanFrame in{};
        // The host stub never produces frames; receive must report
        // "no frame" (0), not "error" (-1).
        EXPECT_EQ(bus.receive(in, /*timeoutMs=*/0), 0);
    }

    TEST(Can, StopThenSendFailsAgain) {
        Can bus(0);
        bus.begin(21, 22, BITRATE_500K);
        EXPECT_TRUE(bus.stop());
        CanFrame f{};
        EXPECT_FALSE(bus.send(f));
    }

    TEST(Can, StopBeforeBeginIsIdempotent) {
        Can bus(0);
        EXPECT_TRUE(bus.stop());  // never began — must not blow up
        EXPECT_TRUE(bus.stop());
    }

    TEST(Can, StopAfterBeginIsIdempotent) {
        Can bus(0);
        bus.begin(21, 22, BITRATE_500K);
        EXPECT_TRUE(bus.stop());
        EXPECT_TRUE(bus.stop());  // second stop after begin must also no-op
    }

    TEST(Can, BeginAfterStopWorks) {
        // Catches a stop() that forgets to clear installed_ — the
        // second begin() would otherwise hit the "already installed"
        // guard and fail. This is the bring-up path you want after a
        // controller hiccup.
        Can bus(0);
        EXPECT_TRUE(bus.begin(21, 22, BITRATE_500K));
        EXPECT_TRUE(bus.stop());
        EXPECT_TRUE(bus.begin(21, 22, BITRATE_500K));

        CanFrame f{};
        f.id = 0x10;
        f.dlc = 1;
        f.data[0] = 0x55;
        EXPECT_TRUE(bus.send(f));
    }

    TEST(Can, ReceiveAfterStopReturnsMinusOne) {
        // Symmetric to StopThenSendFailsAgain: the controller is no
        // longer up so receive must fail hard, not silently report
        // "no frame".
        Can bus(0);
        bus.begin(21, 22, BITRATE_500K);
        bus.stop();
        CanFrame in{};
        EXPECT_EQ(bus.receive(in, /*timeoutMs=*/0), -1);
    }

    TEST(Can, BusOffStubReportsClean) {
        Can bus(0);
        bus.begin(21, 22, BITRATE_500K);
        EXPECT_FALSE(bus.isBusOff());
        EXPECT_TRUE(bus.recoverFromBusOff());
    }

    TEST(Can, FilterCallsSucceedAfterBegin) {
        Can bus(0);
        bus.begin(21, 22, BITRATE_500K);
        EXPECT_TRUE(bus.setAcceptanceFilter(0x100, 0x7F0, /*extendedId=*/false));
        EXPECT_TRUE(bus.clearAcceptanceFilter());
    }

    TEST(Can, SetAcceptanceFilterFailsBeforeBegin) {
        // Filter operations only make sense on a running controller.
        // The contract is "no driver, no filter changes".
        Can bus(0);
        EXPECT_FALSE(bus.setAcceptanceFilter(0x100, 0x7F0, /*extendedId=*/false));
    }

    TEST(Can, ClearAcceptanceFilterFailsBeforeBegin) {
        Can bus(0);
        EXPECT_FALSE(bus.clearAcceptanceFilter());
    }

    TEST(Can, RecoverFromBusOffFailsBeforeBegin) {
        Can bus(0);
        EXPECT_FALSE(bus.recoverFromBusOff());
    }

    TEST(Can, IsBusOffIsFalseBeforeBegin) {
        // Before installation there is no bus to be in any state — the
        // safe answer is "not bus-off" so loops that gate recovery on
        // isBusOff() don't spin.
        Can bus(0);
        EXPECT_FALSE(bus.isBusOff());
    }

    // ---- Public contract: bitrate constants are the documented values.
    // If anyone ever bumps these by accident the host project's wiring
    // breaks silently. Lock them.
    TEST(Can, BitrateConstantsAreLockedToTheirValues) {
        EXPECT_EQ(static_cast<uint32_t>(BITRATE_125K), 125'000U);
        EXPECT_EQ(static_cast<uint32_t>(BITRATE_500K), 500'000U);
        EXPECT_EQ(static_cast<uint32_t>(BITRATE_1M), 1'000'000U);
    }

    TEST(Can, CanFrameValueInitialisesToZero) {
        // `CanFrame f{};` is the standard "empty frame" idiom across
        // the codebase — make sure every field starts clean so a partial
        // population leaves no garbage bytes on the wire.
        CanFrame f{};
        EXPECT_EQ(f.id, 0U);
        EXPECT_FALSE(f.extendedId);
        EXPECT_FALSE(f.remote);
        EXPECT_EQ(f.dlc, 0U);
        for (uint8_t i = 0; i < 8; ++i) {
            EXPECT_EQ(f.data[i], 0U);
        }
    }

    TEST(Can, SendWithDlcZeroSucceeds) {
        // DLC=0 is a real CAN frame shape — used as a heartbeat / wake
        // pulse and by some servo protocols for "ping". Driver must
        // accept it without inspecting `data`.
        Can bus(0);
        bus.begin(21, 22, BITRATE_500K);
        CanFrame ping{};
        ping.id = 0x7FF;
        ping.dlc = 0;
        EXPECT_TRUE(bus.send(ping));
    }

    // ---- CanFrame field round-trip — catches an accidental narrower
    // type for `id` (must hold 29 bits) or for `dlc` (0..8).
    TEST(Can, CanFrameRoundTripsExtendedIdAndFullPayload) {
        CanFrame in{};
        in.id = 0x1ABCDEFU;     // 25 bits — well into the 29-bit range
        in.extendedId = true;
        in.remote = false;
        in.dlc = 8;
        for (uint8_t i = 0; i < 8; ++i) {
            in.data[i] = static_cast<uint8_t>(0xA0 | i);
        }

        CanFrame out = in;
        EXPECT_EQ(out.id, 0x1ABCDEFU);
        EXPECT_TRUE(out.extendedId);
        EXPECT_FALSE(out.remote);
        EXPECT_EQ(out.dlc, 8U);
        for (uint8_t i = 0; i < 8; ++i) {
            EXPECT_EQ(out.data[i], static_cast<uint8_t>(0xA0 | i));
        }
    }

}  // namespace
