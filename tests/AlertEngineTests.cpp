#include "core/AlertEngine.hpp"
#include "platform/macos/MacSwapReader.hpp"

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
    void macSwapReaderReturnsCoherentValues();
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

void AlertEngineTests::macSwapReaderReturnsCoherentValues()
{
    MacSwapReader reader;
    QString error;
    const auto info = reader.read(error);
    if (!info && error.contains(QStringLiteral("Operation not permitted"))) {
        QSKIP("The test runner sandbox does not permit reading vm.swapusage.");
    }
    QVERIFY2(info.has_value(), qPrintable(error));
    QCOMPARE(info->usedBytes + info->freeBytes, info->totalBytes);
    QVERIFY(info->usedBytes <= info->totalBytes);
}

QTEST_APPLESS_MAIN(AlertEngineTests)

#include "AlertEngineTests.moc"
