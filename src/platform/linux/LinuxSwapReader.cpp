#include "platform/linux/LinuxSwapReader.hpp"

#include <QFile>
#include <limits>

LinuxSwapReader::LinuxSwapReader(QString meminfoPath)
    : m_meminfoPath(std::move(meminfoPath))
{
}

std::optional<SwapInfo> LinuxSwapReader::read(QString& errorMessage)
{
    QFile file(m_meminfoPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage = QStringLiteral("Unable to read %1: %2").arg(m_meminfoPath, file.errorString());
        return std::nullopt;
    }

    std::optional<quint64> totalKiB;
    std::optional<quint64> freeKiB;
    // procfs reports a zero file size, so QFile::atEnd() may be true before the first read.
    while (!totalKiB || !freeKiB) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isEmpty() && file.atEnd()) {
            break;
        }
        const QByteArray line = rawLine.simplified();
        const bool isTotal = line.startsWith("SwapTotal:");
        const bool isFree = line.startsWith("SwapFree:");
        if (!isTotal && !isFree) {
            continue;
        }

        const auto fields = line.split(' ');
        bool valid = false;
        const quint64 value = fields.value(1).toULongLong(&valid);
        if (fields.size() != 3 || fields.value(2) != "kB" || !valid
            || value > std::numeric_limits<quint64>::max() / 1024) {
            errorMessage = QStringLiteral("Invalid swap value in %1.").arg(m_meminfoPath);
            return std::nullopt;
        }
        if (isTotal) {
            totalKiB = value;
        } else {
            freeKiB = value;
        }
    }

    if (!totalKiB || !freeKiB) {
        errorMessage = QStringLiteral("%1 is missing SwapTotal or SwapFree.").arg(m_meminfoPath);
        return std::nullopt;
    }
    if (*freeKiB > *totalKiB) {
        errorMessage = QStringLiteral("%1 reports SwapFree (%2 kB) above SwapTotal (%3 kB).")
                           .arg(m_meminfoPath)
                           .arg(*freeKiB)
                           .arg(*totalKiB);
        return std::nullopt;
    }

    const quint64 totalBytes = *totalKiB * 1024;
    const quint64 freeBytes = *freeKiB * 1024;
    return SwapInfo {totalBytes, totalBytes - freeBytes, freeBytes};
}
