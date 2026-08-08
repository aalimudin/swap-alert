#include "platform/macos/MacSwapReader.hpp"

#include <cerrno>
#include <cstring>
#include <sys/sysctl.h>

std::optional<SwapInfo> MacSwapReader::read(QString& errorMessage)
{
    xsw_usage usage {};
    size_t size = sizeof(usage);
    if (sysctlbyname("vm.swapusage", &usage, &size, nullptr, 0) != 0) {
        errorMessage = QStringLiteral("Unable to read vm.swapusage: %1")
                           .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return std::nullopt;
    }

    return SwapInfo {
        static_cast<quint64>(usage.xsu_total),
        static_cast<quint64>(usage.xsu_used),
        static_cast<quint64>(usage.xsu_avail),
    };
}

