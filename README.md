# Swap Alert

Swap Alert is a lightweight macOS menu-bar utility that monitors system swap usage and escalates warnings through configurable alert tiers.

The macOS MVP currently includes:

- Native swap readings through `sysctl(vm.swapusage)`
- Three configurable alert thresholds
- Notification Center alerts
- A prominent Tier 2 warning window
- A Tier 3 application-review and cleanup window
- Normal quit and explicitly confirmed force quit
- Cooldown, stable tier hysteresis, and alert snoozing
- Sleep-aware timing through macOS continuous monotonic time
- Start-at-login support on macOS 13+
- A colored menu-bar status indicator
- Live dashboard with current tier, swap utilization, thresholds, and manual refresh
- Explicit paused and unavailable states in both the tray and dashboard

Linux support is planned but not implemented yet. See [PLAN.md](PLAN.md) for the full roadmap.

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

## Test

```bash
ctest --test-dir build --output-on-failure
```

The tests cover tier transitions, direct escalation, stable hysteresis, cooldown behavior, snooze expiration, pause/resume behavior, read failures, and a live macOS swap reading.

## Current development limitations

- Application memory is estimated from the primary GUI process. Multi-process applications such as browsers may display less than their full combined footprint.
- The development bundle links to the Homebrew Qt installation. A distributable build still needs `macdeployqt`, code signing, notarization, and DMG packaging.
- Start at login should be tested from a stable installed application location rather than a temporary build directory.
