# Swap Alert

Swap Alert is a lightweight macOS menu-bar utility that monitors system swap usage and escalates warnings through configurable alert tiers.

The macOS MVP currently includes:

- Native swap readings through `sysctl(vm.swapusage)`
- Three configurable alert thresholds
- Notification Center alerts
- Notification permission status, Settings guidance, delivery errors, and in-app fallback
- A prominent Tier 2 warning window
- A Tier 3 application-review and cleanup window
- Normal quit and explicitly confirmed force quit
- Aggregated memory estimates for application helper and child processes
- Protected-process and current-user verification before every quit request
- Cooldown, stable tier hysteresis, and alert snoozing
- Sleep-aware timing through macOS continuous monotonic time
- Pause/resume handling for sleep, wake, and user-session changes
- Menu-bar recovery after SystemUIServer or Finder restarts
- Structured, rotating diagnostic logs with an in-app folder shortcut
- Start-at-login support on macOS 13+
- A colored menu-bar status indicator
- Live dashboard with current tier, swap utilization, thresholds, and manual refresh
- Explicit paused and unavailable states in both the tray and dashboard

Linux support is planned but not implemented yet. The release plan targets AppImage and Flatpak first, with optional RPM and DEB packages rather than assuming a Debian-based system. See [PLAN.md](PLAN.md) for the full roadmap.

## Learning documentation

See [docs/CODE_WALKTHROUGH.md](docs/CODE_WALKTHROUGH.md) for a guided explanation of the architecture, runtime flow, Qt concepts, native macOS integrations, tests, and suggested exercises.

## Safety

Swap Alert never quits applications automatically. The cleanup window only shows ordinary GUI applications owned by the current user. A normal quit is requested by default, and every force-quit operation requires confirmation because unsaved work may be lost.

## Requirements

- macOS 13 or later
- Apple Command Line Tools or Xcode
- Qt 6.5 or later
- CMake 3.24 or later
- Ninja

With Homebrew:

```bash
brew install qt cmake ninja pkgconf
```

## Configure and build

```bash
cmake -S . -B build \
  -G Ninja \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build --parallel
```

## Run

```bash
open "build/Swap Alert.app"
```

Swap Alert runs as a menu-bar-only application, so it does not show an icon in the Dock. macOS asks for notification permission on first launch.

## Create an installable DMG

For a local ad-hoc signed package:

```bash
./packaging/macos/package.sh
./packaging/macos/verify-package.sh dist/Swap-Alert-0.1.0-macOS.dmg
```

Open the DMG and drag **Swap Alert** to **Applications**. Developer ID signing and notarization are opt-in because they require Apple credentials; see [docs/RELEASING.md](docs/RELEASING.md).

## Test

```bash
ctest --test-dir build --output-on-failure
```

The tests cover tier transitions, direct escalation, stable hysteresis, cooldown behavior, snooze expiration, monitoring and system suspend/resume behavior, read failures, notification-permission UI, force-quit gating, process grouping and memory aggregation, and a live macOS swap reading.

## Permissions and troubleshooting

- **Notifications do not appear:** open Swap Alert Settings and use the notification status action, or enable Swap Alert under System Settings > Notifications.
- **Start at login does not persist:** install the app in `/Applications` first, then enable the setting. macOS 13 or later is required.
- **The menu-bar icon disappears:** the app listens for desktop and session restarts and re-adds it automatically. If it does not return, relaunch Swap Alert.
- **Swap usage is unavailable:** choose **Open Logs Folder…** from the tray menu and inspect `swap-alert.log`.
- **Gatekeeper blocks a local DMG:** ad-hoc packages are only for local testing. Public distribution needs Developer ID signing and Apple notarization.

Diagnostic logs are stored under the application's `~/Library/Application Support/…/logs/` directory; **Open Logs Folder…** resolves the exact path. The active log rotates at 1 MB and keeps two older files. Logs contain lifecycle state, swap byte counts, alert tiers, and operation results; they do not record document contents.

## Current development limitations

- Application memory aggregates identifiable child and helper processes, but unrelated XPC services may not be attributable to their originating application.
- The Debug bundle links to the Homebrew Qt installation. The packaging script deploys Qt into a self-contained DMG; public releases still require the maintainer's Developer ID and notarization credentials.
- Start at login should be tested from a stable installed application location rather than a temporary build directory.
