#pragma once

#include "core/ISwapReader.hpp"

class MacSwapReader final : public ISwapReader {
public:
    std::optional<SwapInfo> read(QString& errorMessage) override;
};

