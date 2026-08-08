#pragma once

#include <QtGlobal>

class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;
    [[nodiscard]] virtual qint64 nowMs() const = 0;
};

