// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if !defined(ESP_PLATFORM)

#include "ungula/hal/pwm_input/drivers/pwm_input.h"

namespace ungula::hal::pwm_input::drivers
{

// Host stub. Real PWM-input capture is meaningless off-target — host
// tests of consumer code should use `PwmInputFake` to script samples.
// This stub exists only so the same source compiles on host.

PwmInput::PwmInput() = default;
PwmInput::~PwmInput() = default;

bool PwmInput::begin(uint8_t pin)
{
        if (installed_) {
                return false;
        }
        pin_ = pin;
        installed_ = true;
        return true;
}

bool PwmInput::stop()
{
        installed_ = false;
        return true;
}

uint32_t PwmInput::lastHighTimeUs() const
{
        return 0U;
}

uint32_t PwmInput::lastPeriodUs() const
{
        return 0U;
}

bool PwmInput::hasSample() const
{
        return false;
}

uint32_t PwmInput::sampleAgeUs() const
{
        return 0U;
}

void PwmInput::setSampleCallback(SampleCallback /*cb*/, void * /*ctx*/)
{
        // Host stub never produces samples, so a callback could never
        // fire. Accept and drop the registration — this lets consumer
        // code that always installs an ISR callback compile and link
        // for host tests; the test should drive a `PwmInputFake`
        // instead to actually exercise the callback path.
}

} // namespace ungula::hal::pwm_input::drivers

#endif // !ESP_PLATFORM
