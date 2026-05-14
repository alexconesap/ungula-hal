// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include <gtest/gtest.h>

#include <ungula/hal/sync/critical_section.h>

namespace
{

using ungula::hal::sync::CriticalSection;
using ungula::hal::sync::ScopedLock;

TEST(CriticalSectionTest, EnterExitCompilesAndRuns)
{
        CriticalSection critical;
        critical.enter();
        critical.exit();
}

TEST(CriticalSectionTest, ScopedLockAcquiresAndReleases)
{
        CriticalSection critical;
        {
                ScopedLock guard(critical);
                (void)guard;
        }
}

} // namespace
