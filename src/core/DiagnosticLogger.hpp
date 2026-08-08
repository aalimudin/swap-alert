#pragma once

#include <QString>

class DiagnosticLogger {
public:
    static bool install();
    [[nodiscard]] static QString logFilePath();
};

