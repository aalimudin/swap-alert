#pragma once

#include <QString>
#include <QtGlobal>

inline QString formatBytes(quint64 bytes)
{
    constexpr double gibibyte = 1024.0 * 1024.0 * 1024.0;
    constexpr double mebibyte = 1024.0 * 1024.0;
    if (bytes >= static_cast<quint64>(gibibyte)) {
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / gibibyte, 0, 'f', 2);
    }
    return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / mebibyte, 0, 'f', 0);
}

