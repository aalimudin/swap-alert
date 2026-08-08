#pragma once

#include <QMetaType>
#include <QtGlobal>

struct SwapInfo {
    quint64 totalBytes = 0;
    quint64 usedBytes = 0;
    quint64 freeBytes = 0;
};

Q_DECLARE_METATYPE(SwapInfo)
