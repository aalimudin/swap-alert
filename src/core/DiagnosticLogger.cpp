#include "core/DiagnosticLogger.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <cstdio>
#include <memory>

namespace {
constexpr qint64 maximumLogSize = 1024 * 1024;
QMutex loggerMutex;
std::unique_ptr<QFile> loggerFile;
QtMessageHandler previousHandler = nullptr;
QString configuredLogPath;

const char* severityName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "UNKNOWN";
}

void rotateLogs(const QString& path)
{
    const QFileInfo current(path);
    if (!current.exists() || current.size() < maximumLogSize) {
        return;
    }

    QFile::remove(path + QStringLiteral(".2"));
    if (QFile::exists(path + QStringLiteral(".1"))) {
        QFile::rename(path + QStringLiteral(".1"), path + QStringLiteral(".2"));
    }
    QFile::rename(path, path + QStringLiteral(".1"));
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString sanitized = QString(message).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    const QByteArray line = QStringLiteral("%1 [%2] [%3] %4\n")
                                .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                    QString::fromLatin1(severityName(type)),
                                    QString::fromUtf8(context.category ? context.category : "default"),
                                    sanitized)
                                .toUtf8();
    {
        const QMutexLocker locker(&loggerMutex);
        if (loggerFile && loggerFile->isOpen()) {
            if (loggerFile->size() + line.size() >= maximumLogSize) {
                const QString path = loggerFile->fileName();
                loggerFile->close();
                rotateLogs(path);
                loggerFile->setFileName(path);
                if (!loggerFile->open(
                        QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                    loggerFile.reset();
                }
            }
            if (loggerFile && loggerFile->isOpen()) {
                loggerFile->write(line);
                loggerFile->flush();
            }
        }
    }

    if (previousHandler) {
        previousHandler(type, context, message);
    } else {
        fprintf(stderr, "%s", line.constData());
        fflush(stderr);
    }
}
}

bool DiagnosticLogger::install()
{
    if (loggerFile) {
        return true;
    }

    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    const QString logsDirectory = QDir(dataDirectory).filePath(QStringLiteral("logs"));
    if (!QDir().mkpath(logsDirectory)) {
        return false;
    }

    configuredLogPath = QDir(logsDirectory).filePath(QStringLiteral("swap-alert.log"));
    rotateLogs(configuredLogPath);
    auto file = std::make_unique<QFile>(configuredLogPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    loggerFile = std::move(file);
    previousHandler = qInstallMessageHandler(messageHandler);
    return true;
}

QString DiagnosticLogger::logFilePath()
{
    if (!configuredLogPath.isEmpty()) {
        return configuredLogPath;
    }
    const QString dataDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return QDir(dataDirectory).filePath(QStringLiteral("logs/swap-alert.log"));
}
