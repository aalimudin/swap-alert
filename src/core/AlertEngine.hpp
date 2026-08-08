#pragma once

#include <array>
#include <optional>

#include "core/AlertTier.hpp"

struct AlertEvaluation {
    AlertTier currentTier = AlertTier::Normal;
    std::optional<AlertTier> triggeredTier;
};

class AlertEngine {
public:
    struct Configuration {
        std::array<quint64, 3> thresholdsBytes {};
        qint64 cooldownMs = 15 * 60 * 1000;
        double resetMargin = 0.15;
    };

    explicit AlertEngine(Configuration configuration);

    void configure(Configuration configuration);
    AlertEvaluation evaluate(quint64 usedBytes, qint64 nowMs);
    [[nodiscard]] AlertTier currentTier() const;
    void reset();

private:
    [[nodiscard]] AlertTier tierForUsage(quint64 usedBytes) const;

    Configuration m_configuration;
    AlertTier m_currentTier = AlertTier::Normal;
    std::array<bool, 3> m_armed {true, true, true};
    std::array<qint64, 3> m_lastTriggeredMs {-1, -1, -1};
};
