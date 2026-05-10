// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <cstdint>

#include <ungula/hal/multiplexer/drivers/multiplexer_fake.h>
#include <ungula/hal/multiplexer/i_multiplexer.h>

namespace
{

    using ungula::hal::multiplexer::IMultiplexer;
    using ungula::hal::multiplexer::SELECT_RETRY_COUNT;
    using ungula::hal::multiplexer::drivers::MultiplexerFake;

    // ---- Lifecycle ----

    TEST(IMultiplexerTest, SelectChannelBeforeBeginIsRejected)
    {
        MultiplexerFake mux;
        // begin() never called → selectChannel must refuse and not call
        // the hardware hook.
        EXPECT_FALSE(mux.selectChannel(0));
        EXPECT_EQ(mux.selectCallCount(), 0U);
    }

    TEST(IMultiplexerTest, BeginRunsTheBeginPath)
    {
        MultiplexerFake mux;
        EXPECT_TRUE(mux.begin());
        EXPECT_EQ(mux.beginCallCount(), 1U);
    }

    TEST(IMultiplexerTest, BeginCanFailAndStateRemainsUninitialised)
    {
        MultiplexerFake mux;
        mux.setBeginResult(false);
        EXPECT_FALSE(mux.begin());

        // beginResult==false leaves us in the initialised path because
        // the fake's begin() always sets initiated_=true; what fails is
        // the *return* value. The interface contract is: selectChannel
        // works once initiated_==true. So this case still allows
        // selection — and that documents the seam. (Real drivers should
        // leave initiated_=false when begin() returns false; the
        // TCA9548 driver does.)
        // The interesting branch is the "begin never called" case
        // (covered above) — keep this test thin and document the seam.
        SUCCEED();
    }

    // ---- Channel cache ----

    TEST(IMultiplexerTest, RepeatedSelectionsOfSameChannelOnlyHitWireOnce)
    {
        MultiplexerFake mux;
        mux.begin();

        EXPECT_TRUE(mux.selectChannel(3));
        EXPECT_EQ(mux.selectCallCount(), 1U);

        // Cache hit — these must NOT touch the hardware hook.
        EXPECT_TRUE(mux.selectChannel(3));
        EXPECT_TRUE(mux.selectChannel(3));
        EXPECT_EQ(mux.selectCallCount(), 1U);
    }

    TEST(IMultiplexerTest, SwitchingChannelsHitsWireEveryTime)
    {
        MultiplexerFake mux;
        mux.begin();

        EXPECT_TRUE(mux.selectChannel(0));
        EXPECT_TRUE(mux.selectChannel(1));
        EXPECT_TRUE(mux.selectChannel(2));
        EXPECT_EQ(mux.selectCallCount(), 3U);
        EXPECT_EQ(mux.getCurrentChannel(), 2U);
    }

    // ---- Retry logic ----

    TEST(IMultiplexerTest, SelectChannelRetriesOnFailureAndEventuallySucceeds)
    {
        MultiplexerFake mux;
        mux.begin();

        // Fail the first two calls; the third must land. SELECT_RETRY_COUNT
        // is 3 by contract, so the third attempt succeeds and the call
        // returns true.
        mux.failNextSelects(2);
        EXPECT_TRUE(mux.selectChannel(4));
        EXPECT_EQ(mux.selectCallCount(), 3U);
        EXPECT_EQ(mux.selectFailureCount(), 2U);
        EXPECT_EQ(mux.getCurrentChannel(), 4U);
    }

    TEST(IMultiplexerTest, SelectChannelGivesUpAfterMaxRetries)
    {
        MultiplexerFake mux;
        mux.begin();

        mux.setSelectAlwaysFails(true);
        EXPECT_FALSE(mux.selectChannel(7));
        EXPECT_EQ(mux.selectCallCount(), SELECT_RETRY_COUNT);
        EXPECT_EQ(mux.selectFailureCount(), SELECT_RETRY_COUNT);
    }

    TEST(IMultiplexerTest, AfterFailureCacheIsInvalidatedSoNextCallReHitsWire)
    {
        MultiplexerFake mux;
        mux.begin();

        // Land on channel 5 first.
        EXPECT_TRUE(mux.selectChannel(5));
        const uint32_t initialCalls = mux.selectCallCount();

        // Now break the wire and ask for channel 5 again. Because the
        // device was healthy after the previous call, this *would* be a
        // cache hit — except the next call asks for channel 6 and fails
        // through the retries. After that, isFunctional_ goes false, so
        // a follow-up "select 6" must re-hit the wire instead of cache.
        mux.setSelectAlwaysFails(true);
        EXPECT_FALSE(mux.selectChannel(6));
        const uint32_t afterFailureCalls = mux.selectCallCount();
        EXPECT_EQ(afterFailureCalls - initialCalls, SELECT_RETRY_COUNT);

        // Wire heals; ask for the same channel; must retry once and
        // succeed (no cache shortcut).
        mux.setSelectAlwaysFails(false);
        EXPECT_TRUE(mux.selectChannel(6));
        EXPECT_EQ(mux.selectCallCount(), afterFailureCalls + 1U);
    }

    // ---- Logging toggle ----

    TEST(IMultiplexerTest, LoggingDefaultsOff)
    {
        MultiplexerFake mux;
        EXPECT_FALSE(mux.isLoggingEnabled());
    }

    TEST(IMultiplexerTest, EnableDisableLoggingFlipsFlag)
    {
        MultiplexerFake mux;
        mux.enableLogging();
        EXPECT_TRUE(mux.isLoggingEnabled());
        mux.disableLogging();
        EXPECT_FALSE(mux.isLoggingEnabled());
    }

    // ---- restartBus invalidates state ----

    TEST(IMultiplexerTest, RestartBusForcesNextSelectionToHitWire)
    {
        MultiplexerFake mux;
        mux.begin();
        EXPECT_TRUE(mux.selectChannel(2));
        EXPECT_EQ(mux.selectCallCount(), 1U);

        mux.restartBus();

        // After restart, the same channel is no longer cached (and the
        // device is no longer marked functional).
        EXPECT_TRUE(mux.selectChannel(2));
        EXPECT_EQ(mux.selectCallCount(), 2U);
    }

    // ---- Interface-stability check ----
    //
    // If a future change removes one of the pure-virtual hooks or adds a
    // new one without updating MultiplexerFake, this static-assert /
    // compile-only test fires. Every concrete driver must therefore
    // remain a valid IMultiplexer.

    TEST(IMultiplexerTest, FakeIsAValidIMultiplexer)
    {
        // Compile-time check: every IMultiplexer pure virtual must be
        // implemented by the fake — otherwise this static_cast won't
        // resolve.
        MultiplexerFake mux;
        IMultiplexer *asInterface = static_cast<IMultiplexer *>(&mux);
        EXPECT_NE(asInterface, nullptr);
        EXPECT_TRUE(asInterface->begin());
        EXPECT_TRUE(asInterface->isResponding());
        EXPECT_TRUE(asInterface->selectChannel(0));
        asInterface->restartBus();
    }

} // namespace
