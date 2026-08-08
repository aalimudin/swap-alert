#pragma once

#include "core/SwapInfo.hpp"

#include <QString>
#include <optional>

class ISwapReader {
public:
    virtual ~ISwapReader() = default;
    virtual std::optional<SwapInfo> read(QString& errorMessage) = 0;
};

