#include "core/AlertEngine.hpp"
#if defined(Q_OS_MACOS)
#include "platform/macos/MacSwapReader.hpp"
#elif defined(Q_OS_LINUX)
#include "platform/linux/LinuxSwapReader.hpp"
#endif

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class AlertEngineTests final : public QObject {
    Q_OBJECT

private slots:
    void reportsNormalBelowFirstThreshold();
    void triggersEachTierOnce();
    void directJumpTriggersHighestTier();
    void hysteresisRearmsTier();
    void hysteresisKeepsDisplayedTierStable();
    void descendingMultipleTiersUsesEachResetPoint();
    void cooldownPreventsImmediateRetrigger();
    void resetRearmsEveryTierAndClearsCooldown();
    void platformSwapReaderReturnsCoherentValues();
#if defined(Q_OS_LINUX)
    void linuxSwapReaderParsesMeminfo();
    void linuxSwapReaderRejectsInvalidMeminfo();
#endif
};

namespace {
AlertEngine makeEngine(qint64 cooldownMs = 1000)
{
    return AlertEngine({{100, 200, 300}, cooldownMs, 0.10});
}
}

void AlertEngineTests::reportsNormalBelowFirstThreshold()
{
    auto engine = makeEngine();
    const auto result = engine.evaluate(99, 0);
    QCOMPARE(result.currentTier, AlertTier::Normal);
    QVERIFY(!result.triggeredTier.has_value());
}

void AlertEngineTests::triggersEachTierOnce()
{
    auto engine = makeEngine();
    auto first = engine.evaluate(100, 0);
    QCOMPARE(first.triggeredTier, std::optional(AlertTier::Tier1));
    QVERIFY(!engine.evaluate(150, 100).triggeredTier.has_value());

    auto second = engine.evaluate(200, 200);
    QCOMPARE(second.triggeredTier, std::optional(AlertTier::Tier2));
    auto third = engine.evaluate(300, 300);
    QCOMPARE(third.triggeredTier, std::optional(AlertTier::Tier3));
}

void AlertEngineTests::directJumpTriggersHighestTier()
{
    auto engine = makeEngine();
    const auto result = engine.evaluate(350, 0);
    QCOMPARE(result.currentTier, AlertTier::Tier3);
    QCOMPARE(result.triggeredTier, std::optional(AlertTier::Tier3));

    QVERIFY(!engine.evaluate(250, 2000).triggeredTier.has_value());
    QVERIFY(!engine.evaluate(150, 3000).triggeredTier.has_value());
}

void AlertEngineTests::hysteresisRearmsTier()
{
    auto engine = makeEngine(0);
    QVERIFY(engine.evaluate(100, 0).triggeredTier.has_value());
    QVERIFY(!engine.evaluate(95, 10).triggeredTier.has_value());
    QVERIFY(!engine.evaluate(90, 20).triggeredTier.has_value());
    QVERIFY(engine.evaluate(89, 30).currentTier == AlertTier::Normal);
    QCOMPARE(engine.evaluate(100, 40).triggeredTier, std::optional(AlertTier::Tier1));
}

void AlertEngineTests::hysteresisKeepsDisplayedTierStable()
{
    auto engine = makeEngine(0);
    QCOMPARE(engine.evaluate(100, 0).currentTier, AlertTier::Tier1);
    QCOMPARE(engine.evaluate(99, 10).currentTier, AlertTier::Tier1);
    QCOMPARE(engine.evaluate(90, 20).currentTier, AlertTier::Tier1);
    QCOMPARE(engine.evaluate(89, 30).currentTier, AlertTier::Normal);
}

void AlertEngineTests::descendingMultipleTiersUsesEachResetPoint()
{
    auto engine = makeEngine(0);
    QCOMPARE(engine.evaluate(300, 0).currentTier, AlertTier::Tier3);
    QCOMPARE(engine.evaluate(280, 10).currentTier, AlertTier::Tier3);
    QCOMPARE(engine.evaluate(269, 20).currentTier, AlertTier::Tier2);
    QCOMPARE(engine.evaluate(179, 30).currentTier, AlertTier::Tier1);
    QCOMPARE(engine.evaluate(89, 40).currentTier, AlertTier::Normal);
}

void AlertEngineTests::cooldownPreventsImmediateRetrigger()
{
    auto engine = makeEngine(1000);
    QVERIFY(engine.evaluate(100, 0).triggeredTier.has_value());
    engine.evaluate(80, 100);
    QVERIFY(!engine.evaluate(100, 500).triggeredTier.has_value());
    engine.evaluate(80, 1100);
    QVERIFY(engine.evaluate(100, 1200).triggeredTier.has_value());
}

void AlertEngineTests::resetRearmsEveryTierAndClearsCooldown()
{
    auto engine = makeEngine(60'000);
    QCOMPARE(engine.evaluate(350, 0).triggeredTier, std::optional(AlertTier::Tier3));
    engine.reset();
    QCOMPARE(engine.currentTier(), AlertTier::Normal);
    QCOMPARE(engine.evaluate(350, 1).triggeredTier, std::optional(AlertTier::Tier3));
}

void AlertEngineTests::platformSwapReaderReturnsCoherentValues()
{
#if defined(Q_OS_MACOS)
    MacSwapReader reader;
#elif defined(Q_OS_LINUX)
    LinuxSwapReader reader;
#endif
    QString error;
    const auto info = reader.read(error);
#if defined(Q_OS_MACOS)
    if (!info && error.contains(QStringLiteral("Operation not permitted"))) {
        QSKIP("The test runner sandbox does not permit reading vm.swapusage.");
    }
#endif
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->usedBytes + info->freeBytes, info->totalBytes);
    QVERIFY(info->usedBytes <= info->totalBytes);
}

#if defined(Q_OS_LINUX)
void AlertEngineTests::linuxSwapReaderParsesMeminfo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("meminfo"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray contents("MemTotal: 999 kB\nSwapTotal: 4096 kB\nSwapFree: 1024 kB\n");
    QCOMPARE(file.write(contents), contents.size());
    file.close();

    LinuxSwapReader reader(path);
    QString error;
    const auto info = reader.read(error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->totalBytes, 4096ULL * 1024);
    QCOMPARE(info->freeBytes, 1024ULL * 1024);
    QCOMPARE(info->usedBytes, 3072ULL * 1024);
}

void AlertEngineTests::linuxSwapReaderRejectsInvalidMeminfo()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("meminfo"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray contents("SwapTotal: 100 kB\nSwapFree: 101 kB\n");
    QCOMPARE(file.write(contents), contents.size());
    file.close();

    LinuxSwapReader reader(path);
    QString error;
    QVERIFY(!reader.read(error).has_value());
    QVERIFY2(error.contains(QStringLiteral("SwapFree")), qPrintable(error));
}
#endif

QTEST_APPLESS_MAIN(AlertEngineTests)

#include "AlertEngineTests.moc"
