#pragma once

#include "core/ISwapReader.hpp"

#include <QString>

class LinuxSwapReader final : public ISwapReader {
public:
    explicit LinuxSwapReader(QString meminfoPath = QStringLiteral("/proc/meminfo"));
    std::optional<SwapInfo> read(QString& errorMessage) override;

private:
    QString m_meminfoPath;
};
