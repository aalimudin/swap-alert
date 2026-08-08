#include "core/ProcessGrouping.hpp"

#include <QtTest>
#include <algorithm>

class ProcessGroupingTests final : public QObject {
    Q_OBJECT

private slots:
    void groupsDescendantsAndBundleHelpers();
    void excludesOtherUsersAndOtherApplicationRoots();
    void sortsByAggregateMemoryThenName();
};

void ProcessGroupingTests::groupsDescendantsAndBundleHelpers()
{
    const QVector<ApplicationRoot> roots {
        {10, QStringLiteral("Browser"), QStringLiteral("com.example.browser")},
    };
    const QVector<ProcessSnapshot> snapshots {
        {10, 1, 501, 100, QStringLiteral("com.example.browser")},
        {11, 10, 501, 50, {}},
        {12, 1, 501, 75, QStringLiteral("com.example.browser.helper")},
        {13, 12, 501, 25, {}},
        {14, 1, 501, 500, QStringLiteral("com.example.browserish")},
    };

    const auto result = groupApplicationProcesses(roots, snapshots, 501);

    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first().memoryBytes, 250ULL);
    QCOMPARE(result.first().processCount, 4);
    QCOMPARE(result.first().bundleIdentifier, QStringLiteral("com.example.browser"));
}

void ProcessGroupingTests::excludesOtherUsersAndOtherApplicationRoots()
{
    const QVector<ApplicationRoot> roots {
        {10, QStringLiteral("Launcher"), QStringLiteral("com.example.launcher")},
        {20, QStringLiteral("Editor"), QStringLiteral("com.example.editor")},
    };
    const QVector<ProcessSnapshot> snapshots {
        {10, 1, 501, 100, QStringLiteral("com.example.launcher")},
        {20, 10, 501, 200, QStringLiteral("com.example.editor")},
        {21, 20, 501, 30, {}},
        {22, 20, 502, 900, {}},
    };

    const auto result = groupApplicationProcesses(roots, snapshots, 501);

    QCOMPARE(result.size(), 2);
    const auto launcher = std::find_if(result.cbegin(), result.cend(), [](const auto& app) {
        return app.processId == 10;
    });
    const auto editor = std::find_if(result.cbegin(), result.cend(), [](const auto& app) {
        return app.processId == 20;
    });
    QVERIFY(launcher != result.cend());
    QVERIFY(editor != result.cend());
    QCOMPARE(launcher->memoryBytes, 100ULL);
    QCOMPARE(launcher->processCount, 1);
    QCOMPARE(editor->memoryBytes, 230ULL);
    QCOMPARE(editor->processCount, 2);
}

void ProcessGroupingTests::sortsByAggregateMemoryThenName()
{
    const QVector<ApplicationRoot> roots {
        {10, QStringLiteral("Zulu"), QStringLiteral("zulu")},
        {20, QStringLiteral("Alpha"), QStringLiteral("alpha")},
        {30, QStringLiteral("Largest"), QStringLiteral("largest")},
    };
    const QVector<ProcessSnapshot> snapshots {
        {10, 1, 501, 100, QStringLiteral("zulu")},
        {20, 1, 501, 100, QStringLiteral("alpha")},
        {30, 1, 501, 300, QStringLiteral("largest")},
    };

    const auto result = groupApplicationProcesses(roots, snapshots, 501);

    QCOMPARE(result.at(0).name, QStringLiteral("Largest"));
    QCOMPARE(result.at(1).name, QStringLiteral("Alpha"));
    QCOMPARE(result.at(2).name, QStringLiteral("Zulu"));
}

QTEST_APPLESS_MAIN(ProcessGroupingTests)

#include "ProcessGroupingTests.moc"
