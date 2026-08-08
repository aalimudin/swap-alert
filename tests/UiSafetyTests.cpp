#include "platform/IAutostartService.hpp"
#include "platform/INotificationService.hpp"
#include "platform/IProcessService.hpp"
#include "ui/CleanupDialog.hpp"
#include "ui/SettingsDialog.hpp"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableWidget>
#include <QtTest>

class FakeAutostartService final : public IAutostartService {
public:
    [[nodiscard]] bool isEnabled() const override { return enabled; }
    bool setEnabled(bool requested, QString&) override
    {
        enabled = requested;
        return true;
    }

    bool enabled = false;
};

class FakeNotificationService final : public INotificationService {
public:
    void authorizationStatus(NotificationCallback callback) override
    {
        callback({status == NotificationAuthorizationStatus::Authorized, status, {}});
    }

    void requestAuthorization(NotificationCallback callback) override
    {
        ++requestCount;
        status = NotificationAuthorizationStatus::Authorized;
        callback({true, status, {}});
    }

    void send(const QString&, const QString&, AlertTier, NotificationCallback callback) override
    {
        callback({true, status, {}});
    }

    bool openNotificationSettings(QString&) override
    {
        ++openSettingsCount;
        return true;
    }

    NotificationAuthorizationStatus status = NotificationAuthorizationStatus::NotDetermined;
    int requestCount = 0;
    int openSettingsCount = 0;
};

class FakeProcessService final : public IProcessService {
public:
    [[nodiscard]] QVector<ApplicationProcess> runningApplications() const override
    {
        return {{42, QStringLiteral("Example App"), QStringLiteral("com.example.app"),
            512ULL * 1024 * 1024, 3}};
    }

    bool terminate(qint64 processId, bool force, QString&) override
    {
        calls.push_back({processId, force});
        return true;
    }

    QVector<QPair<qint64, bool>> calls;
};

class UiSafetyTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void deniedNotificationsOfferSystemSettings();
    void requestingPermissionUpdatesStatus();
    void testButtonsEmitEveryTier();
    void forceQuitRequiresNormalQuitFirst();
};

void UiSafetyTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void UiSafetyTests::cleanup()
{
    SettingsStore settings;
    settings.restoreDefaults();
}

void UiSafetyTests::deniedNotificationsOfferSystemSettings()
{
    SettingsStore settings;
    FakeAutostartService autostart;
    FakeNotificationService notifications;
    notifications.status = NotificationAuthorizationStatus::Denied;
    SettingsDialog dialog(settings, autostart, notifications);

    auto* label = dialog.findChild<QLabel*>(QStringLiteral("notificationStatusLabel"));
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("notificationActionButton"));
    QVERIFY(label);
    QVERIFY(button);
    QVERIFY(label->text().contains(QStringLiteral("Disabled")));
    QCOMPARE(button->text(), QStringLiteral("Open Settings…"));

    button->click();
    QCOMPARE(notifications.openSettingsCount, 1);
}

void UiSafetyTests::requestingPermissionUpdatesStatus()
{
    SettingsStore settings;
    FakeAutostartService autostart;
    FakeNotificationService notifications;
    SettingsDialog dialog(settings, autostart, notifications);
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("notificationStatusLabel"));
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("notificationActionButton"));

    button->click();

    QCOMPARE(notifications.requestCount, 1);
    QVERIFY(label->text().contains(QStringLiteral("Allowed")));
    QCOMPARE(button->text(), QStringLiteral("Open Settings…"));
}

void UiSafetyTests::testButtonsEmitEveryTier()
{
    SettingsStore settings;
    FakeAutostartService autostart;
    FakeNotificationService notifications;
    SettingsDialog dialog(settings, autostart, notifications);
    QSignalSpy spy(&dialog, &SettingsDialog::testAlertRequested);

    dialog.findChild<QPushButton*>(QStringLiteral("testTier1Button"))->click();
    dialog.findChild<QPushButton*>(QStringLiteral("testTier2Button"))->click();
    dialog.findChild<QPushButton*>(QStringLiteral("testTier3Button"))->click();

    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QCOMPARE(spy.at(1).at(0).toInt(), 2);
    QCOMPARE(spy.at(2).at(0).toInt(), 3);
}

void UiSafetyTests::forceQuitRequiresNormalQuitFirst()
{
    FakeProcessService processes;
    CleanupDialog dialog(processes);
    dialog.refresh();
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("applicationTable"));
    auto* quitButton = dialog.findChild<QPushButton*>(QStringLiteral("quitSelectedButton"));
    auto* forceButton = dialog.findChild<QPushButton*>(QStringLiteral("forceQuitSelectedButton"));

    QCOMPARE(table->rowCount(), 1);
    table->item(0, 0)->setCheckState(Qt::Checked);
    QVERIFY(quitButton->isEnabled());
    QVERIFY(!forceButton->isEnabled());

    quitButton->click();
    QCOMPARE(processes.calls.size(), 1);
    QCOMPARE(processes.calls.first().first, 42);
    QCOMPARE(processes.calls.first().second, false);
    QVERIFY(!forceButton->isEnabled());

    QTRY_VERIFY_WITH_TIMEOUT(forceButton->isEnabled(), 2500);
    QCOMPARE(table->item(0, 4)->text(), QStringLiteral("Still running"));
}

QTEST_MAIN(UiSafetyTests)

#include "UiSafetyTests.moc"
