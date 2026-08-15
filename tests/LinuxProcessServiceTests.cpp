#include "platform/linux/LinuxProcessService.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

class LinuxProcessServiceTests final : public QObject {
    Q_OBJECT

private slots:
    void discoversCurrentUserProcessFromDesktopEntry();
};

void LinuxProcessServiceTests::discoversCurrentUserProcessFromDesktopEntry()
{
    QTemporaryDir dataHome;
    QVERIFY(dataHome.isValid());
    QVERIFY(QDir().mkpath(dataHome.filePath(QStringLiteral("applications"))));

    QFile desktopFile(dataHome.filePath(
        QStringLiteral("applications/com.swapalert.ProcessFixture.desktop")));
    QVERIFY(desktopFile.open(QIODevice::WriteOnly | QIODevice::Text));
    desktopFile.write("[Desktop Entry]\n"
                      "Type=Application\n"
                      "Name=Swap Alert Process Fixture\n"
                      "Exec=sleep 30\n");
    desktopFile.close();

    const QByteArray previousDataHome = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());

    QProcess fixture;
    fixture.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    QVERIFY(fixture.waitForStarted());
    const QString processId = QString::number(fixture.processId());
    QVERIFY2(QDir(QStringLiteral("/proc")).entryList(
                 QDir::Dirs | QDir::NoDotAndDotDot).contains(processId),
        "QDir did not classify the procfs PID entry as a directory");
    QFile statusFile(QStringLiteral("/proc/%1/status").arg(processId));
    QVERIFY(statusFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(statusFile.readAll().contains("Uid:"));
    QVERIFY2(!QFileInfo(QStringLiteral("/proc/%1/exe").arg(processId)).symLinkTarget().isEmpty(),
        "Qt could not resolve the procfs executable symlink");

    LinuxProcessService service;
    const auto applications = service.runningApplications();
    const auto match = std::find_if(applications.cbegin(), applications.cend(),
        [&fixture](const ApplicationProcess& application) {
            return application.processId == fixture.processId();
        });

    fixture.terminate();
    fixture.waitForFinished();
    if (previousDataHome.isNull()) {
        qunsetenv("XDG_DATA_HOME");
    } else {
        qputenv("XDG_DATA_HOME", previousDataHome);
    }

    QVERIFY2(match != applications.cend(),
        "A current-user process with a visible desktop entry was not discovered");
    QCOMPARE(match->name, QStringLiteral("Swap Alert Process Fixture"));
    QVERIFY(match->memoryBytes > 0);
}

QTEST_APPLESS_MAIN(LinuxProcessServiceTests)

#include "LinuxProcessServiceTests.moc"
