#pragma once

#include <QMetaType>
#include <QtGlobal>

enum class AlertTier : quint8 {
    Normal = 0,
    Tier1 = 1,
    Tier2 = 2,
    Tier3 = 3,
};

Q_DECLARE_METATYPE(AlertTier)
