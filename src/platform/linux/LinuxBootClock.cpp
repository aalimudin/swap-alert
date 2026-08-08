#include "platform/linux/LinuxBootClock.hpp"

#include <ctime>

qint64 LinuxBootClock::nowMs() const
{
    timespec value {};
    if (clock_gettime(CLOCK_BOOTTIME, &value) != 0) {
        return 0;
    }
    return static_cast<qint64>(value.tv_sec) * 1000 + value.tv_nsec / 1'000'000;
}
