# Swap Alert — Project Plan

## Product summary

Swap Alert is a lightweight macOS and Linux tray utility that continuously monitors system swap usage and escalates warnings through three configurable alert tiers.

The first release prioritizes safety: it may help the user quit memory-heavy applications, but it will never terminate an application automatically. Normal quit is attempted first, and force quit always requires explicit confirmation.

## Supported platforms

- macOS 13 or later
- Ubuntu 22.04 or later
- Fedora and comparable modern Linux distributions
- GNOME, KDE Plasma, and Xfce where a system tray is available

Windows support is outside the initial scope.

## Technology stack

- C++20
- Qt 6 Widgets
- CMake
- Ninja
- Qt Test
- `QSettings` for persistent settings
- `QTimer` for periodic monitoring
- `QSystemTrayIcon` for the menu-bar/system-tray interface
- Native platform adapters for swap data, notifications, autostart, and process management

Qt must be dynamically linked unless the licensing implications of static linking are deliberately addressed.

## Default configuration

| Setting | Default |
| --- | ---: |
| Tier 1 threshold | 2 GB |
| Tier 2 threshold | 4 GB |
| Tier 3 threshold | 8 GB |
| Polling interval | 10 seconds |
| Repeat-alert cooldown | 15 minutes |
| Reset margin | 15% below threshold |

Thresholds use absolute swap usage in GB for the first release and must satisfy:

```text
Tier 1 < Tier 2 < Tier 3
```

Snooze choices are 15 minutes, 1 hour, and until the next login.

## Alert tiers

### Tier 1 — Notification

- Send a native desktop notification.
- Change the tray icon from green to yellow.
- Include current swap usage in the notification.
- Trigger only once during a swap-pressure episode.

### Tier 2 — Warning window

- Open a prominent Qt warning window.
- Change the tray icon to orange.
- Show current usage, thresholds, and recent swap growth.
- Provide actions to review applications, snooze, or dismiss.

### Tier 3 — Guided cleanup

- Open the application cleanup window.
- Change the tray icon to red.
- List eligible user applications or process groups by memory consumption.
- Allow selection of one or more applications.
- Attempt normal quit before offering force quit.
- Require explicit confirmation before every force-quit operation.

Automatic termination is deferred. A future version may support opt-in automatic normal quitting for an explicit user-managed allowlist.

## Alert-state behavior

```text
Normal
  -> Tier 1 crossed: notify once
  -> Tier 2 crossed: show warning once
  -> Tier 3 crossed: show cleanup window once
  -> Usage drops below reset margin: re-arm relevant tiers
```

Additional rules:

- A direct jump to a higher tier records lower tiers as triggered.
- Editing thresholds causes immediate reevaluation.
- Snooze suppresses alert presentation but does not stop monitoring.
- The tray status continues updating while snoozed.
- Invalid or unavailable readings never trigger an alert.
- Sleep and wake must not create false swap-growth spikes.
- Cooldowns and hysteresis prevent notification spam.

## Architecture

```text
Qt UI
├── TrayController
├── SettingsDialog
├── WarningDialog
└── CleanupDialog

Shared core
├── SwapMonitor
├── AlertEngine
├── AlertState
├── SwapHistory
└── SettingsStore

Platform layer
├── ISwapReader
├── INotificationService
├── IProcessService
└── IAutostartService
```

The shared core must contain no operating-system-specific code. CMake selects the appropriate platform implementations at build time.

## Platform integrations

### macOS

- Read swap data with `sysctl` and `vm.swapusage`.
- Deliver notifications with `UserNotifications`.
- Enumerate applications with `NSWorkspace` and `NSRunningApplication`.
- Manage launch at login with `SMAppService`.
- Expose native APIs to C++ through Objective-C++ `.mm` adapters.
- Request normal termination before confirmed force termination.
- Distribute using Developer ID signing, notarization, and a DMG.

### Linux

- Read `SwapTotal` and `SwapFree` from `/proc/meminfo`.
- Deliver notifications through `org.freedesktop.Notifications` over D-Bus.
- Fall back to `QSystemTrayIcon::showMessage()` when native notifications are unavailable.
- Read process information from `/proc`.
- Group related processes by executable, desktop entry, or application identity where possible.
- Manage launch at login with an XDG autostart `.desktop` entry.
- Use `SIGTERM` for normal termination and confirmed `SIGKILL` as the last resort.
- Never request root access to terminate processes.
- Publish AppImage and Flatpak packages as the primary cross-distribution formats.
- Optionally provide native `.rpm` and `.deb` packages for tighter Fedora/RHEL and Debian/Ubuntu integration; neither native format blocks the general Linux release.

Only current-user processes may be offered for termination. System services, protected processes, and Swap Alert itself must be excluded.

## User interface

### Tray menu

- Current swap usage
- Current alert tier
- Open dashboard
- Review applications
- Snooze
- Enable or pause monitoring
- Start at login
- Settings
- Quit

Tray icon states:

- Green: below Tier 1
- Yellow: Tier 1
- Orange: Tier 2
- Red: Tier 3
- Gray: paused or swap information unavailable

### Settings window

- Three ordered threshold controls
- Polling interval
- Alert cooldown
- Start-at-login setting
- Notification test button
- Popup test button
- Force-quit availability setting
- Restore defaults

### Cleanup window

- Application name and icon
- Process ID or grouped process count
- Estimated memory usage
- Selection checkbox
- Quit selected applications
- Force quit selected applications
- Refresh
- Cancel or snooze

## Proposed repository layout

```text
swap-alert/
├── CMakeLists.txt
├── PLAN.md
├── README.md
├── LICENSE
├── cmake/
├── resources/
├── src/
│   ├── app/
│   ├── core/
│   ├── ui/
│   └── platform/
│       ├── macos/
│       └── linux/
├── tests/
├── packaging/
│   ├── macos/
│   └── linux/
└── docs/
```

## Delivery milestones

### Milestone 1 — Monitoring core

**Status: complete for macOS and Linux.**

- Create the CMake and Qt project skeleton.
- Define platform service interfaces.
- Implement macOS and Linux swap readers.
- Add settings persistence and short in-memory swap history.
- Validate readings against Activity Monitor and `/proc/meminfo`.

### Milestone 2 — Alert engine

**Status: complete for macOS and Linux (shared core).**

- Implement three-tier threshold transitions.
- Add hysteresis, cooldown, snooze, and reset behavior.
- Add unit tests for transitions and edge cases.

### Milestone 3 — Tray application

**Status: implemented for macOS and Linux. Linux desktop validation remains.**

- Add tray icon and status menu.
- Add settings UI and live swap display.
- Support pausing and resuming monitoring.

### Milestone 4 — Alerts

**Status: complete for macOS; implemented for Linux pending GNOME/KDE validation.**

- Add native platform notifications.
- Add the Tier 2 warning window.
- Handle denied permissions and unavailable notification services.
- Add controls for testing each alert type.

### Milestone 5 — Guided cleanup

**Status: complete for macOS; implemented for Linux pending GNOME/KDE validation.**

- Enumerate eligible user applications and process groups.
- Estimate and display memory consumption.
- Implement normal quit.
- Implement explicit, confirmed force quit.
- Protect system processes and the monitor itself.

### Milestone 6 — System integration

**Status: complete for macOS; implemented for Linux pending desktop and suspend/resume validation.**

- Add start-at-login support on both platforms.
- Handle sleep, wake, login, and desktop restarts.
- Add structured diagnostic logging.

### Milestone 7 — Packaging and release

**Status: macOS, AppImage, Flatpak, DEB, and RPM packaging pipelines are implemented. AppImage is verified end to end, and native package generation is verified on Fedora 44 and Debian 13. macOS signing/notarization, an exact Flatpak bundle build, native package installation testing, and Linux cross-desktop artifact validation remain release-environment steps.**

- Sign, notarize, and package the macOS application as a DMG.
- Package Linux as an AppImage for a portable standalone download.
- Package Linux as a Flatpak for a managed, sandboxed installation across distributions.
- Add `.rpm` and `.deb` convenience packages after the cross-distribution artifacts are stable. *(Implemented with host-native dependency discovery.)*
- Verify installation and desktop integration on both Fedora and Ubuntu.
- Document installation, permissions, configuration, and troubleshooting.

## Testing strategy

- Unit-test the alert state machine independently of the OS.
- Inject fake swap readers and clocks for deterministic threshold, cooldown, and snooze tests.
- Test native adapters separately on macOS and Linux.
- Run CI builds and tests on macOS, Ubuntu, and Fedora.
- Manually test GNOME and KDE notification and tray behavior.
- Test sleep/wake, login startup, notification denial, desktop restart, and rapid threshold changes.
- Test applications that refuse to quit and applications with unsaved work.

## Release acceptance criteria

- Swap readings closely match operating-system values.
- Each alert tier fires once per pressure episode.
- Alerts re-arm only after crossing the configured reset margin.
- Settings survive application and system restarts.
- Snooze and cooldown work across sleep and wake.
- Notification denial does not stop monitoring.
- Only eligible, current-user applications appear in cleanup.
- Normal quit is always attempted before force quit.
- Force quit always requires explicit confirmation.
- Idle CPU usage is negligible and the app does not materially increase memory pressure.
- Installation and normal operation require no administrator or root access.
- Autostart can be enabled and disabled cleanly.

## Deferred features

- Windows support
- Automatic force quitting
- Root-owned process management
- Cloud synchronization
- Remote alerts
- Long-term historical charts
- Predictive or rate-based alert thresholds
- Percentage-based thresholds
- Distribution-specific package repositories

## Local development prerequisites

On macOS:

```bash
brew install qt cmake ninja pkgconf
```

Full Xcode is required for the macOS SDK, Objective-C++ adapters, native frameworks, signing, and notarization.

On Ubuntu or Debian:

```bash
sudo apt update
sudo apt install \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  qt6-base-dev \
  qt6-base-dev-tools \
  qt6-tools-dev \
  libdbus-1-dev
```

On Fedora:

```bash
sudo dnf install \
  gcc-c++ \
  cmake \
  ninja-build \
  pkgconf-pkg-config \
  qt6-qtbase-devel \
  qt6-qttools-devel \
  dbus-devel
```

Linux-specific behavior should be built and tested on Linux or in CI rather than cross-compiled from macOS.
