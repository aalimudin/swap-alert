#pragma once

#include "core/IMonotonicClock.hpp"

class LinuxBootClock final : public IMonotonicClock {
public:
    [[nodiscard]] qint64 nowMs() const override;
};
