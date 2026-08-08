# Swap Alert Code Walkthrough

This guide explains how Swap Alert works from application startup through swap monitoring, alert evaluation, notifications, and application cleanup. It is intended to make the repository useful as a learning project, not just as finished code.

## What this project demonstrates

Swap Alert combines several useful desktop-development concepts:

- Building a native macOS application with Qt 6 and CMake
- Using Qt's event loop, signals, slots, timers, dialogs, and system tray
- Mixing portable C++ with Objective-C++
- Calling native Apple frameworks from a Qt application
- Separating business logic from operating-system integrations
- Implementing an alert state machine with hysteresis and cooldowns
- Persisting user preferences
- Testing business logic without launching the GUI

## Project map

```text
swap-alert/
├── CMakeLists.txt
├── packaging/
│   └── macos/
│       └── Info.plist.in
├── src/
│   ├── app/
│   │   └── main.cpp
│   ├── core/
│   │   ├── AlertEngine.*
│   │   ├── AlertTier.hpp
│   │   ├── DiagnosticLogger.*
│   │   ├── IMonotonicClock.hpp
│   │   ├── ISwapReader.hpp
│   │   ├── Logging.*
│   │   ├── ProcessGrouping.*
│   │   ├── SettingsStore.*
│   │   ├── SwapInfo.hpp
│   │   └── SwapMonitor.*
│   ├── platform/
│   │   ├── IAutostartService.hpp
│   │   ├── INotificationService.hpp
│   │   ├── IProcessService.hpp
│   │   ├── ISystemEventMonitor.hpp
│   │   └── macos/
│   │       ├── MacAutostartService.*
│   │       ├── MacContinuousClock.*
│   │       ├── MacNotificationService.*
│   │       ├── MacProcessService.*
│   │       ├── MacSwapReader.*
│   │       └── MacSystemEventMonitor.*
│   └── ui/
│       ├── CleanupDialog.*
│       ├── Format.hpp
│       ├── SettingsDialog.*
│       ├── StatusDialog.*
│       ├── TrayController.*
│       └── WarningDialog.*
└── tests/
    ├── AlertEngineTests.cpp
    ├── ProcessGroupingTests.cpp
    ├── SwapMonitorTests.cpp
    └── UiSafetyTests.cpp
```

The folders have distinct responsibilities:

- `app` assembles the application.
- `core` contains portable behavior and data models.
- `platform` contains operating-system integrations.
- `ui` contains Qt widgets and presentation logic.
- `tests` verifies behavior independently from the visible application.

## Runtime data flow

The most important path through the application is:

```text
macOS vm.swapusage
        ↓
  MacSwapReader
        ↓
   SwapMonitor
        ↓
   AlertEngine
     ↙       ↘
Tray update   Alert triggered
               ↓
       Notification / Dialog
```

Every polling interval, Swap Alert reads the current swap values, evaluates them against the configured thresholds, updates the menu-bar display, and presents an alert only when a tier is newly triggered.

## 1. Build configuration

The project starts in [`CMakeLists.txt`](../CMakeLists.txt).

```cmake
project(SwapAlert VERSION 0.1.0 LANGUAGES CXX OBJCXX)
```

There are two source languages:

- C++ contains the portable application and Qt code.
- Objective-C++ allows C++ and Apple Objective-C APIs to be used in the same `.mm` file.

The project requires C++20 and targets macOS 13 or later:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
```

Qt's `AUTOMOC` support generates the extra C++ code required by classes that contain the `Q_OBJECT` macro:

```cmake
set(CMAKE_AUTOMOC ON)
```

The application uses Qt Core and Widgets:

```cmake
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)
```

Finding Qt Widgets also causes Qt to check some optional graphics integrations. That is why CMake may print:

```text
Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)
```

Swap Alert does not use Vulkan, so this message is harmless if CMake continues with `Configuring done` and `Generating done`.

The native Apple frameworks are linked here:

```cmake
target_link_libraries(SwapAlert PRIVATE
    Qt6::Core
    Qt6::Widgets
    "-framework AppKit"
    "-framework ServiceManagement"
    "-framework UserNotifications"
)
```

`qt_add_executable(... MACOSX_BUNDLE)` produces `Swap Alert.app` instead of a plain command-line executable.

## 2. Application startup and dependency assembly

The application entry point is [`src/app/main.cpp`](../src/app/main.cpp).

It creates `QApplication`, which owns the main Qt event loop:

```cpp
QApplication application(argc, argv);
QApplication::setQuitOnLastWindowClosed(false);
```

The second line is important for a tray application. Closing the Settings window must not terminate the monitor.

`main()` constructs the application's services and UI objects:

```cpp
SettingsStore settings;
MacAutostartService autostartService;
MacNotificationService notificationService;
MacProcessService processService;
SwapMonitor monitor(std::make_unique<MacSwapReader>(), settings,
    std::make_unique<MacContinuousClock>());
TrayController tray(settings);
SettingsDialog settingsDialog(settings, autostartService);
WarningDialog warningDialog;
CleanupDialog cleanupDialog(processService);
```

This is manual dependency injection. For example, `SwapMonitor` does not construct a `MacSwapReader` internally. It receives an object that implements `ISwapReader`. This makes the monitor easier to test and eventually allows a `LinuxSwapReader` to be supplied instead.

After creating the objects, `main()` connects their signals and slots. Finally, it starts the application:

```cpp
notificationService.requestAuthorization();
tray.show();
monitor.start();
return application.exec();
```

`application.exec()` starts Qt's event loop. Timer events, menu clicks, signals, and dialog actions all run through this loop.

## 3. Data models and interfaces

[`src/core/SwapInfo.hpp`](../src/core/SwapInfo.hpp) defines the swap reading shared by the application:

```cpp
struct SwapInfo {
    quint64 totalBytes = 0;
    quint64 usedBytes = 0;
    quint64 freeBytes = 0;
};
```

It contains only data and knows nothing about macOS, Qt widgets, or thresholds.

[`src/core/AlertTier.hpp`](../src/core/AlertTier.hpp) defines the possible states:

```cpp
enum class AlertTier : quint8 {
    Normal = 0,
    Tier1 = 1,
    Tier2 = 2,
    Tier3 = 3,
};
```

The platform interfaces describe what the core application needs without prescribing how an operating system provides it:

- `ISwapReader` reads swap usage.
- `INotificationService` sends notifications.
- `IProcessService` lists and terminates applications.
- `IAutostartService` manages launch at login.
- `ISystemEventMonitor` reports sleep, wake, session, shutdown, and desktop-restart events.

This pattern is sometimes called a port-and-adapter or dependency-inversion architecture.

## System lifecycle integration

`MacSystemEventMonitor.mm` subscribes to `NSWorkspace` notifications. It translates native notifications into the portable `SystemEvent` enum instead of allowing Objective-C types to leak into the rest of the application.

Before sleep or when the login session becomes inactive, `main.cpp` calls `SwapMonitor::suspendForSystemEvent()`. This stops the timer and rejects manual refreshes during suspension. After wake and session activation, `resumeAfterSystemEvent()` restarts the timer and immediately reads a fresh sample. The alert engine still uses `mach_continuous_time`, so cooldown and snooze durations account for time spent asleep without inventing a swap-growth sample.

SystemUIServer owns macOS menu-bar extras. If it or Finder restarts, Qt can temporarily lose the visible status item. The native event monitor detects those application launches and asks `TrayController` to show its existing icon again.

The lifecycle unit test injects a fake reader, starts the monitor, suspends it, verifies that refresh produces no sample, and verifies that resume produces exactly one fresh sample.

## Structured diagnostic logging

`Logging.hpp` declares Qt logging categories such as `swapalert.monitor`, `swapalert.alerts`, and `swapalert.system`. Categories make a mixed log easier to filter than unlabelled print statements.

`DiagnosticLogger::install()` installs a Qt message handler at startup. Each record contains a UTC timestamp, severity, category, and message. The file is stored under the application's `QStandardPaths::AppLocalDataLocation`, rotates at 1 MB, and retains two prior files. Messages are also forwarded to Qt's original handler, so they remain visible in a development terminal.

The tray action **Open Logs Folder…** resolves the path at runtime and opens it in Finder. Logging records numeric process IDs and operation results, but not application document contents.

## Packaging, signing, and notarization

`packaging/macos/package.sh` performs a Release configuration, builds and tests it, installs the app into a staging directory, and runs Qt's `macdeployqt`. That deployment step copies the Qt frameworks and plugins into the bundle and rewrites Mach-O library references so the release no longer depends on Homebrew paths.

Without a signing identity the script applies an ad-hoc signature for local testing. With `SIGN_IDENTITY`, `macdeployqt` signs nested components using the hardened runtime and timestamp options intended for notarization. The script then creates and signs a compressed DMG. If `NOTARIZE=1`, it submits that DMG through `notarytool`, waits for the result, and staples Apple's ticket.

`packaging/macos/verify-package.sh` mounts the finished image read-only, checks the bundle signature and property list, and scans every Mach-O file for development-only Homebrew dependencies. See [`docs/RELEASING.md`](RELEASING.md) for credential setup and the release checklist.

## 4. Reading swap usage from macOS

The native implementation is [`src/platform/macos/MacSwapReader.mm`](../src/platform/macos/MacSwapReader.mm).

It calls the macOS `sysctlbyname` API directly:

```cpp
xsw_usage usage {};
size_t size = sizeof(usage);

sysctlbyname("vm.swapusage", &usage, &size, nullptr, 0);
```

The returned `xsw_usage` structure contains:

- `xsu_total`: current total swap capacity
- `xsu_used`: swap currently in use
- `xsu_avail`: available swap

Those values are converted into the portable `SwapInfo` structure:

```cpp
return SwapInfo {
    static_cast<quint64>(usage.xsu_total),
    static_cast<quint64>(usage.xsu_used),
    static_cast<quint64>(usage.xsu_avail),
};
```

This avoids launching a shell command such as `sysctl vm.swapusage` every ten seconds. It is faster, produces structured values, and avoids parsing localized command output.

If the native call fails, the reader returns `std::nullopt` and fills an error message. The monitor then reports the failure without evaluating an invalid sample.

## 5. Polling with `SwapMonitor`

[`src/core/SwapMonitor.cpp`](../src/core/SwapMonitor.cpp) coordinates time, swap readings, and alert evaluation.

Its constructor connects a `QTimer` to `refreshNow()`:

```cpp
connect(&m_timer, &QTimer::timeout, this, &SwapMonitor::refreshNow);
```

When `start()` is called, the monitor starts the timer and immediately takes its first reading:

```cpp
m_timer.start();
refreshNow();
```

Each refresh follows these steps:

1. Check whether monitoring is enabled.
2. Ask `ISwapReader` for the current values.
3. Stop if the reading failed.
4. Pass used bytes to `AlertEngine`.
5. Emit a sample update for the tray.
6. Emit an alert only when a tier was newly triggered and alerts are not snoozed.

```cpp
const auto evaluation =
    m_alertEngine.evaluate(info->usedBytes, m_clock->nowMs());

emit sampleUpdated(*info, evaluation.currentTier);

if (evaluation.triggeredTier && !isSnoozed()) {
    emit alertTriggered(*evaluation.triggeredTier, *info);
}
```

The timer uses Qt's main event loop. A separate background thread is not currently needed because reading one sysctl value is a very small operation.

## 6. Qt signals and slots

Signals and slots allow components to communicate without tightly coupling them.

For example, `SwapMonitor` declares:

```cpp
void sampleUpdated(const SwapInfo& info, AlertTier tier);
void alertTriggered(AlertTier tier, const SwapInfo& info);
void readFailed(const QString& message);
```

`main.cpp` connects those signals to UI behavior:

```cpp
QObject::connect(&monitor, &SwapMonitor::sampleUpdated,
                 &tray, &TrayController::updateSample);
```

The monitor does not need to know that the UI uses `QSystemTrayIcon`. It only announces that a new sample exists.

This separation makes it possible to replace the tray UI, record samples, or add another observer without changing `SwapMonitor`.

## 7. Alert state machine

The central business logic lives in [`src/core/AlertEngine.cpp`](../src/core/AlertEngine.cpp).

The engine receives only two pieces of information:

- Current used swap in bytes
- Monotonic elapsed time in milliseconds

It first determines the current tier:

```cpp
if (usedBytes >= tier3Threshold)
    return AlertTier::Tier3;
if (usedBytes >= tier2Threshold)
    return AlertTier::Tier2;
if (usedBytes >= tier1Threshold)
    return AlertTier::Tier1;
return AlertTier::Normal;
```

The default thresholds are 2 GB, 4 GB, and 8 GB.

### Armed state

Each tier can be armed or disarmed. Once an alert fires, that tier is disarmed so the ten-second timer does not generate the same alert repeatedly.

### Direct escalation

If swap jumps directly from normal to Tier 3, the engine triggers Tier 3 and marks all lower tiers as handled:

```cpp
for (int index = 0; index <= currentIndex; ++index) {
    m_armed[index] = false;
    m_lastTriggeredMs[index] = nowMs;
}
```

This prevents three alerts from appearing at once.

### Hysteresis

Without hysteresis, usage fluctuating around a threshold could repeatedly arm and trigger an alert.

The engine rearms a tier only when usage falls below a reset point:

```cpp
resetPoint = threshold * (1.0 - resetMargin);
```

The default reset margin is 15%. A 4 GB tier therefore rearms only after usage falls below approximately 3.4 GB.

Hysteresis also stabilizes the displayed tier. If Tier 2 was reached, usage falling from 4.0 GB to 3.9 GB does not immediately turn the menu-bar indicator green. The current tier remains Tier 2 until usage crosses its reset point. This keeps the visible state consistent with the alert state.

### Cooldown

Even if a tier has been rearmed, the configured cooldown must have elapsed:

```cpp
nowMs - lastTriggeredMs >= cooldownMs
```

The default cooldown is 15 minutes.

### Why use monotonic time?

The core depends on `IMonotonicClock` instead of directly reading a clock. On macOS, `MacContinuousClock` uses `mach_continuous_time`, which is monotonic and advances while the computer sleeps. Changing the wall clock or moving between time zones therefore does not break cooldown calculations, and sleeping does not artificially extend a snooze.

Tests inject a fake clock so they can advance time instantly and deterministically.

## 8. Snoozing and pausing

Pausing monitoring stops the timer. Snoozing behaves differently: monitoring continues, and the tray still displays current usage, but new alert presentation is suppressed.

The monitor records the end of the snooze using elapsed time:

```cpp
m_snoozedUntilMs =
    m_clock->nowMs() + static_cast<qint64>(minutes) * 60 * 1000;
```

When snooze expires, the alert engine is reset so it can remind the user if swap remains above a threshold.

The tray also offers an Until Next Login option. This keeps alerts snoozed for the lifetime of the current Swap Alert process; the normal launch-at-login lifecycle clears it at the next login.

## 9. Menu-bar interface

[`src/ui/TrayController.cpp`](../src/ui/TrayController.cpp) owns `QSystemTrayIcon` and its menu.

The menu provides:

- Current swap usage
- Current alert tier
- Open Dashboard
- Refresh Now
- Review Applications
- Snooze for 15 minutes or one hour
- Monitoring toggle
- Settings
- Quit

The menu-bar icon is generated with `QPainter`. Its color represents the current tier:

- Green: normal
- Yellow: Tier 1
- Orange: Tier 2
- Red: Tier 3
- Gray: monitoring error

Every successful sample updates the menu and tooltip:

```cpp
const QString status = QStringLiteral("Swap: %1 of %2")
    .arg(formatBytes(info.usedBytes), formatBytes(info.totalBytes));
```

[`packaging/macos/Info.plist.in`](../packaging/macos/Info.plist.in) sets `LSUIElement` to `true`. That makes Swap Alert a menu-bar utility without a normal Dock icon.

When monitoring is paused, or a swap read fails, the tray changes to a gray icon and replaces stale status text with Paused or Unavailable. Resuming monitoring immediately takes a fresh sample.

## 10. Live dashboard

[`src/ui/StatusDialog.cpp`](../src/ui/StatusDialog.cpp) provides a larger live view without making the tray menu crowded. It shows:

- Current used and total swap
- A utilization progress bar
- The hysteresis-stabilized alert tier
- Monitoring, paused, or unavailable state
- All three configured thresholds
- Refresh, application review, and settings actions

The dashboard is constructed at startup and connected to the same `sampleUpdated` and `readFailed` signals as the tray. It therefore stays current even while its window is hidden.

## 11. Persistent settings

[`src/core/SettingsStore.cpp`](../src/core/SettingsStore.cpp) uses `QSettings` to save preferences for the current user.

Defaults are supplied when a value has not yet been stored:

```cpp
return m_settings.value(
    "thresholds/tier1Bytes",
    2ULL * gibibyte
).toULongLong();
```

The stored values include:

- Three thresholds
- Polling interval
- Alert cooldown
- Monitoring enabled state

When a setting changes, `SettingsStore` emits `changed()`. `SwapMonitor` receives that signal and updates its timer and alert configuration without restarting the application.

## 12. Settings dialog

[`src/ui/SettingsDialog.cpp`](../src/ui/SettingsDialog.cpp) builds a Qt form containing threshold, polling, cooldown, monitoring, and autostart controls.

Before saving, the dialog validates that:

```text
Tier 1 < Tier 2 < Tier 3
```

The Test Tier buttons emit simulated alert requests. They exercise notification and dialog presentation without changing real swap usage.

Saving uses one batched `setConfiguration()` call, so the monitor receives one coherent update and performs at most one immediate refresh. Restore Defaults only edits the form; it does not persist anything until Save is pressed, so Cancel behaves as expected.

## 13. Native notifications

[`src/platform/macos/MacNotificationService.mm`](../src/platform/macos/MacNotificationService.mm) uses Apple's UserNotifications framework.

At startup, it requests permission for alerts and sounds:

```objc
UNAuthorizationOptionAlert | UNAuthorizationOptionSound
```

Sending an alert creates `UNMutableNotificationContent` and submits a local notification request:

```objc
UNNotificationRequest* request =
    [UNNotificationRequest requestWithIdentifier:identifier
                                          content:content
                                          trigger:nil];
```

A notification delegate permits banners to appear while Swap Alert is active. Tier 1 is silent; Tier 2 and Tier 3 use the default notification sound.

Notification operations are asynchronous and return a `NotificationResult`. The result reports both authorization state and delivery errors:

- `NotDetermined`: macOS has not asked the user yet
- `Denied`: notifications are disabled for Swap Alert
- `Authorized`: alerts may be delivered
- `Unknown`: the service could not determine a usable state

Settings displays this state and offers either Request Permission or Open Settings. Sending a notification first checks authorization, requests it when necessary, and reports failures back on Qt's main thread.

Tier 1 normally has no application window, so a denied permission or delivery error opens `WarningDialog` as a fallback. Tier 2 already has a warning window and adds the failure explanation there. Tier 3 always opens guided cleanup, so the critical action remains visible even without Notification Center.

## 14. Alert presentation

`main.cpp` decides how each triggered tier is presented:

- Tier 1 sends a native notification.
- Tier 2 sends a notification and shows `WarningDialog`.
- Tier 3 sends a notification and opens `CleanupDialog`.

The core `AlertEngine` does not know about any of these windows. It only returns an alert tier. Presentation policy remains in the application layer.

## 15. Listing running applications

[`src/platform/macos/MacProcessService.mm`](../src/platform/macos/MacProcessService.mm) uses `NSWorkspace` to enumerate visible macOS applications and `libproc` to snapshot the current user's processes.

It excludes:

- Swap Alert itself
- Background-only applications
- Applications that have already terminated
- Core system applications such as Finder, Dock, loginwindow, and SystemUIServer
- Applications not owned by the current user

For each process, it calls `proc_pid_rusage` and reads `ri_phys_footprint` as an estimate of physical memory consumption.

[`src/core/ProcessGrouping.cpp`](../src/core/ProcessGrouping.cpp) groups process snapshots under each visible application. A process belongs to an application when it is:

- The visible application's main process
- A current-user descendant of that process
- A helper with the same bundle identifier or a bundle identifier beneath it
- A descendant of one of those matching helpers

Another visible application's root process is never absorbed into the parent group. Memory is summed for each group, process count is recorded, and the list is sorted from highest to lowest aggregate memory use.

This remains an estimate because macOS applications can use unrelated services that cannot be attributed reliably, but browsers and Electron applications are represented more accurately than a main-process-only reading.

## 16. Safe application cleanup

[`src/ui/CleanupDialog.cpp`](../src/ui/CleanupDialog.cpp) presents the application list with checkboxes.

There are two separate actions:

- `Quit Selected` requests normal termination.
- `Force Quit Selected` requires a prior normal quit request, requires the application to still be running, then asks for explicit confirmation.

The native calls are:

```objc
application.terminate
application.forceTerminate
```

The cleanup table preserves selection across refreshes, displays grouped process count and memory, and reports Quit requested, Still running, Force quit requested, or Failed. A delayed refresh gives macOS time to deliver the normal quit request.

Before every termination request, the macOS adapter rechecks the process owner, activation policy, protected-application list, and Swap Alert's own PID. This protects against stale rows and PID reuse between listing and action.

Swap Alert never automatically selects or terminates an application. Force quit is deliberately gated because it can destroy unsaved work.

The app also requires no root privileges and cannot terminate another user's processes through this UI.

## 17. Start at login

[`src/platform/macos/MacAutostartService.mm`](../src/platform/macos/MacAutostartService.mm) uses `SMAppService.mainAppService`, available on macOS 13 and later.

Enabling the preference registers the application. Disabling it unregisters the application.

Start at login should be tested after placing the application in a stable location such as `/Applications`. Registering a temporary build path is not appropriate for a final installation.

## 18. Automated tests

[`tests/AlertEngineTests.cpp`](../tests/AlertEngineTests.cpp) uses Qt Test.

The suite covers:

- Remaining normal below Tier 1
- Triggering each tier only once
- Jumping directly to a higher tier
- Rearming through hysteresis
- Preventing immediate repeats through cooldown
- Reading coherent swap values from macOS when the test environment permits it

[`tests/SwapMonitorTests.cpp`](../tests/SwapMonitorTests.cpp) injects a fake swap reader and fake clock. It verifies:

- Samples continue while alerts are snoozed
- A timed snooze reminds the user after it expires
- Until-next-login snooze does not expire with elapsed time
- Failed swap reads do not evaluate or emit a sample
- Pausing and resuming monitoring starts a fresh alert episode
- Batched settings updates persist all values and emit one change signal

[`tests/ProcessGroupingTests.cpp`](../tests/ProcessGroupingTests.cpp) verifies:

- Descendant and bundle-helper aggregation
- Exclusion of other users' processes
- Separation of multiple visible application roots
- Protection against similar but unrelated bundle identifiers
- Aggregate-memory and name sorting

[`tests/UiSafetyTests.cpp`](../tests/UiSafetyTests.cpp) uses fake platform services with real Qt dialogs. It verifies:

- Denied notification permission offers a System Settings action
- Requesting permission updates the Settings status
- All three test-alert buttons emit the correct tier
- Force Quit remains disabled until normal quit was requested and the application remains running

The core tests use small values such as 100, 200, and 300 bytes. The unit itself does not matter to the state machine, which makes the cases easy to read.

The macOS swap test may be skipped inside restricted execution sandboxes that deny access to `vm.swapusage`. Other read errors still fail the test.

Run the tests with:

```bash
ctest --test-dir build --output-on-failure
```

## 19. How Linux support fits later

The core alert logic and most Qt UI code do not depend on macOS. Linux support can be added by implementing the same interfaces:

```text
MacSwapReader          → LinuxSwapReader using /proc/meminfo
MacNotificationService → Linux D-Bus notification service
MacProcessService      → Linux /proc process service
MacAutostartService    → Linux XDG autostart service
```

The alert engine, timer behavior, settings, and dialogs can remain shared.

The CMake configuration will eventually select the correct platform files using conditions such as:

```cmake
if(APPLE)
    # macOS sources
elseif(UNIX)
    # Linux sources
endif()
```

## 20. Suggested learning exercises

These changes are small enough to attempt while learning the codebase:

1. Add a 30-minute snooze option to the tray menu.
2. Add the current alert tier as text next to the swap value.
3. Make the reset margin configurable in Settings.
4. Add tests for exact threshold boundaries.
5. Add a keyboard shortcut that opens the dashboard.
6. Record the last 60 readings and display a small history table.
7. Extend the fake-reader monitor tests to cover live threshold changes.
8. Add application icons to the guided cleanup table.
9. Add `LinuxSwapReader` while keeping all existing core tests unchanged.

## 21. Good next architectural improvements

As the project grows, consider:

- Structured logging with `QLoggingCategory` or Apple `OSLog`
- Notification categories with actions such as Open Dashboard and Snooze
- Process grouping for browsers and Electron applications
- A short swap-history model separated from the UI
- CMake presets for debug, release, and CI builds
- Packaging with `macdeployqt`, signing, notarization, and DMG creation

The current design intentionally keeps those concerns out of the first MVP while leaving clear places to add them.
