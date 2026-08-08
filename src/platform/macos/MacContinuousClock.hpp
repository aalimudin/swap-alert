#pragma once

#include "core/IMonotonicClock.hpp"

class MacContinuousClock final : public IMonotonicClock {
public:
    [[nodiscard]] qint64 nowMs() const override;
};

