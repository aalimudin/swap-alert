#include "core/AlertEngine.hpp"

#include <algorithm>

namespace {
int tierIndex(AlertTier tier)
{
    return static_cast<int>(tier) - 1;
}
}

AlertEngine::AlertEngine(Configuration configuration)
    : m_configuration(std::move(configuration))
{
}

void AlertEngine::configure(Configuration configuration)
{
    m_configuration = std::move(configuration);
}

AlertEvaluation AlertEngine::evaluate(quint64 usedBytes, qint64 nowMs)
{
    for (int index = 0; index < 3; ++index) {
        const auto resetPoint = static_cast<quint64>(
            static_cast<double>(m_configuration.thresholdsBytes[index])
            * (1.0 - m_configuration.resetMargin));
        if (usedBytes < resetPoint) {
            m_armed[index] = true;
        }
    }

    m_currentTier = tierForUsage(usedBytes);
    const int currentIndex = tierIndex(m_currentTier);
    if (currentIndex < 0) {
        return {m_currentTier, std::nullopt};
    }

    const bool cooldownElapsed = m_lastTriggeredMs[currentIndex] < 0
        || nowMs - m_lastTriggeredMs[currentIndex] >= m_configuration.cooldownMs;
    if (!m_armed[currentIndex] || !cooldownElapsed) {
        return {m_currentTier, std::nullopt};
    }

    for (int index = 0; index <= currentIndex; ++index) {
        m_armed[index] = false;
        m_lastTriggeredMs[index] = nowMs;
    }

    return {m_currentTier, m_currentTier};
}

AlertTier AlertEngine::currentTier() const
{
    return m_currentTier;
}

void AlertEngine::reset()
{
    m_currentTier = AlertTier::Normal;
    m_armed = {true, true, true};
    m_lastTriggeredMs = {-1, -1, -1};
}

AlertTier AlertEngine::tierForUsage(quint64 usedBytes) const
{
    if (usedBytes >= m_configuration.thresholdsBytes[2]) {
        return AlertTier::Tier3;
    }
    if (usedBytes >= m_configuration.thresholdsBytes[1]) {
        return AlertTier::Tier2;
    }
    if (usedBytes >= m_configuration.thresholdsBytes[0]) {
        return AlertTier::Tier1;
    }
    return AlertTier::Normal;
}

