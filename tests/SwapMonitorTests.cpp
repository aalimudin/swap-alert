#include "core/IMonotonicClock.hpp"
#include "core/ISwapReader.hpp"
#include "core/SettingsStore.hpp"
#include "core/SwapMonitor.hpp"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>
#include <memory>

class FakeClock final : public IMonotonicClock {
public:
    [[nodiscard]] qint64 nowMs() const override { return currentMs; }
    qint64 currentMs = 0;
};

class FakeSwapReader final : public ISwapReader {
public:
    std::optional<SwapInfo> read(QString& errorMessage) override
    {
        if (shouldFail) {
            errorMessage = QStringLiteral("Synthetic read failure");
            return std::nullopt;
        }
        return info;
    }

    SwapInfo info {400, 0, 400};
    bool shouldFail = false;
};

class SwapMonitorTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void snoozeSuppressesAlertsButKeepsSampling();
    void snoozeUntilRestartDoesNotExpire();
    void readFailureDoesNotEvaluateSample();
    void pauseAndResumeResetTheAlertEpisode();
    void batchedSettingsUpdateEmitsOnce();

private:
    std::unique_ptr<SettingsStore> makeSettings();
};

void SwapMonitorTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    qRegisterMetaType<AlertTier>();
    qRegisterMetaType<SwapInfo>();
}

void SwapMonitorTests::init()
{
    SettingsStore settings;
    settings.restoreDefaults();
}

void SwapMonitorTests::cleanup()
{
    SettingsStore settings;
    settings.restoreDefaults();
}

std::unique_ptr<SettingsStore> SwapMonitorTests::makeSettings()
{
    auto settings = std::make_unique<SettingsStore>();
    settings->setThresholds(100, 200, 300);
    settings->setPollingSeconds(300);
    settings->setCooldownMinutes(1);
    settings->setMonitoringEnabled(true);
    return settings;
}

void SwapMonitorTests::snoozeSuppressesAlertsButKeepsSampling()
{
    auto settings = makeSettings();
    auto reader = std::make_unique<FakeSwapReader>();
    auto clock = std::make_unique<FakeClock>();
    auto* readerControl = reader.get();
    auto* clockControl = clock.get();
    SwapMonitor monitor(std::move(reader), *settings, std::move(clock));
    QSignalSpy sampleSpy(&monitor, &SwapMonitor::sampleUpdated);
    QSignalSpy alertSpy(&monitor, &SwapMonitor::alertTriggered);

    readerControl->info = {400, 100, 300};
    monitor.refreshNow();
    QCOMPARE(sampleSpy.count(), 1);
    QCOMPARE(alertSpy.count(), 1);

    monitor.snoozeForMinutes(1);
    readerControl->info = {400, 80, 320};
    clockControl->currentMs = 10'000;
    monitor.refreshNow();
    readerControl->info = {400, 100, 300};
    clockControl->currentMs = 20'000;
    monitor.refreshNow();
    QCOMPARE(sampleSpy.count(), 3);
    QCOMPARE(alertSpy.count(), 1);
    QVERIFY(monitor.isSnoozed());

    clockControl->currentMs = 60'001;
    monitor.refreshNow();
    QCOMPARE(sampleSpy.count(), 4);
    QCOMPARE(alertSpy.count(), 2);
    QVERIFY(!monitor.isSnoozed());
}

void SwapMonitorTests::snoozeUntilRestartDoesNotExpire()
{
    auto settings = makeSettings();
    auto reader = std::make_unique<FakeSwapReader>();
    auto clock = std::make_unique<FakeClock>();
    auto* readerControl = reader.get();
    auto* clockControl = clock.get();
    SwapMonitor monitor(std::move(reader), *settings, std::move(clock));
    QSignalSpy sampleSpy(&monitor, &SwapMonitor::sampleUpdated);
    QSignalSpy alertSpy(&monitor, &SwapMonitor::alertTriggered);

    monitor.snoozeUntilRestart();
    readerControl->info = {400, 100, 300};
    monitor.refreshNow();
    clockControl->currentMs = 30LL * 24 * 60 * 60 * 1000;
    readerControl->info = {400, 80, 320};
    monitor.refreshNow();
    readerControl->info = {400, 100, 300};
    monitor.refreshNow();

    QCOMPARE(sampleSpy.count(), 3);
    QCOMPARE(alertSpy.count(), 0);
    QVERIFY(monitor.isSnoozed());
}

void SwapMonitorTests::readFailureDoesNotEvaluateSample()
{
    auto settings = makeSettings();
    auto reader = std::make_unique<FakeSwapReader>();
    auto clock = std::make_unique<FakeClock>();
    auto* readerControl = reader.get();
    SwapMonitor monitor(std::move(reader), *settings, std::move(clock));
    QSignalSpy sampleSpy(&monitor, &SwapMonitor::sampleUpdated);
    QSignalSpy alertSpy(&monitor, &SwapMonitor::alertTriggered);
    QSignalSpy failureSpy(&monitor, &SwapMonitor::readFailed);

    readerControl->shouldFail = true;
    monitor.refreshNow();

    QCOMPARE(sampleSpy.count(), 0);
    QCOMPARE(alertSpy.count(), 0);
    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(failureSpy.first().first().toString(), QStringLiteral("Synthetic read failure"));
}

void SwapMonitorTests::pauseAndResumeResetTheAlertEpisode()
{
    auto settings = makeSettings();
    auto reader = std::make_unique<FakeSwapReader>();
    auto clock = std::make_unique<FakeClock>();
    auto* readerControl = reader.get();
    SwapMonitor monitor(std::move(reader), *settings, std::move(clock));
    QSignalSpy sampleSpy(&monitor, &SwapMonitor::sampleUpdated);
    QSignalSpy alertSpy(&monitor, &SwapMonitor::alertTriggered);

    readerControl->info = {400, 100, 300};
    monitor.start();
    QCOMPARE(alertSpy.count(), 1);

    settings->setMonitoringEnabled(false);
    monitor.refreshNow();
    QCOMPARE(sampleSpy.count(), 1);

    readerControl->info = {400, 100, 300};
    settings->setMonitoringEnabled(true);
    QCOMPARE(sampleSpy.count(), 2);
    QCOMPARE(alertSpy.count(), 2);
}

void SwapMonitorTests::batchedSettingsUpdateEmitsOnce()
{
    SettingsStore settings;
    QSignalSpy changedSpy(&settings, &SettingsStore::changed);

    settings.setConfiguration(10, 20, 30, 25, 40, false);

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(settings.tier1Bytes(), 10ULL);
    QCOMPARE(settings.tier2Bytes(), 20ULL);
    QCOMPARE(settings.tier3Bytes(), 30ULL);
    QCOMPARE(settings.pollingSeconds(), 25);
    QCOMPARE(settings.cooldownMinutes(), 40);
    QVERIFY(!settings.monitoringEnabled());
}

QTEST_GUILESS_MAIN(SwapMonitorTests)

#include "SwapMonitorTests.moc"
