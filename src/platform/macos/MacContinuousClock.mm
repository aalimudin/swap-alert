#include "platform/macos/MacContinuousClock.hpp"

#include <mach/mach_time.h>

qint64 MacContinuousClock::nowMs() const
{
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t result {};
        mach_timebase_info(&result);
        return result;
    }();

    const auto nanoseconds = static_cast<__uint128_t>(mach_continuous_time())
        * timebase.numer / timebase.denom;
    return static_cast<qint64>(nanoseconds / 1'000'000);
}

