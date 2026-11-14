# Build It Yourself: Guided Projects

The [Code Walkthrough](CODE_WALKTHROUGH.md) teaches you to *read* Swap Alert. These projects teach you to *change* it. Each one adds a real feature that's listed as missing in [`PLAN.md`](../PLAN.md#known-gaps), so when you finish, the app is genuinely better.

Every project gives you:

- **The goal** and what you'll learn, with links back to the walkthrough lessons.
- **Steps** in order, small enough to build and test after each one.
- **Hints** hidden in folds. Try without them first; open one when you're stuck for more than 15 minutes.
- **"Done when"** — a checklist so you know you've finished.

The guides deliberately don't give you every line. Where they do show code, it's the parts that are about an OS API you couldn't guess, or a design decision worth copying exactly.

### Project map

| # | Project | Difficulty | Builds on | Platforms |
|---|---------|-----------|-----------|-----------|
| 1 | [Make the re-arm margin configurable](#project-1--make-the-re-arm-margin-configurable) | ★☆☆ | — | Both |
| 2 | [A tray icon you can read without colour](#project-2--a-tray-icon-you-can-read-without-colour) | ★☆☆ | — | Both |
| 3 | [Remember recent readings: `SwapHistory`](#project-3--remember-recent-readings-swaphistory) | ★★☆ | — | Both |
| 4 | [Show how fast swap is growing](#project-4--show-how-fast-swap-is-growing) | ★★☆ | Project 3 | Both |
| 5 | [Measure swap activity](#project-5--measure-swap-activity) | ★★★ | Projects 3 and 4 | Both (two implementations) |
| 6 | [Signal the right process, always: `pidfd`](#project-6--signal-the-right-process-always-pidfd) | ★★★ | — | 🐧 Linux only |

Do them in order if you can. Projects 1 and 2 are warm-ups that get you comfortable with the edit → build → test loop.

---

## Before you start

**1. Make sure the app builds and the tests pass** (see [Setup](CODE_WALKTHROUGH.md#setup--build-run-and-test-the-app)).

**2. Use one git branch per project**, so a half-finished project never gets in the way of the next one:

```bash
git switch main && git pull
git switch -c project/reset-margin        # a new name for each project
```

**3. Also build the way CI does.** CI has a job that turns warnings into errors and runs the tests under sanitizers (Lesson 14). Run the same thing locally before you push, or CI will catch it for you:

```bash
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror"
cmake --build build-ci && ctest --test-dir build-ci --output-on-failure
```

(On 🐧 drop the `CMAKE_PREFIX_PATH` line. On 🍎 the flags don't reach the `.mm` files; that's fine.)

**4. Format only what you touch.** The repo has a `.clang-format`. Run `git clang-format` after `git add` to format just your changes.

---

## Project 1 — Make the re-arm margin configurable

**Goal:** add a "Re-arm margin" setting (5–50%, default 15%) to the Settings window.

**You'll learn:** how a setting flows from a Qt widget into `QSettings` and on into the alert engine; letting the compiler find every place you need to change; writing a UI test. *(Lessons 8, 10)*

**Files:** `src/core/SettingsStore.hpp/.cpp`, `src/ui/SettingsDialog.hpp/.cpp`, `tests/SwapMonitorTests.cpp`, `tests/UiSafetyTests.cpp`

### Background

Remember hysteresis from Lesson 10: a tier re-arms only after usage falls 15% below its threshold. That 15% is the **reset margin**. Half of the plumbing already exists:

```text
QSettings "alerts/resetMargin" ──▶ SettingsStore::resetMargin() ──▶ alertConfiguration() ──▶ AlertEngine
              ▲
              └── nothing writes it yet, and there's no control in the Settings window
```

### Steps

**1. Let `setConfiguration()` save it.** `SettingsStore::setConfiguration(...)` saves all settings in one go and emits `changed()` once. Add a `double resetMargin` parameter **between** `cooldownMinutes` and `monitoringEnabled`, and save it under the key `"alerts/resetMargin"`.

Now build. **Don't go looking for callers — let the compiler find them.** Every place that calls `setConfiguration` with the old arguments is now a compile error. You should get exactly two: `SettingsDialog::save()` and a test. Fix the test now by passing `0.25` and adding a check that it was saved:

```cpp
QCOMPARE(settings.resetMargin(), 0.25);
QCOMPARE(settings.alertConfiguration().resetMargin, 0.25);
```

The second line proves the value really reaches the alert engine. (`QCOMPARE` on `double`s uses a fuzzy comparison, so `0.25` is safe.)

**2. Add the control.** In `SettingsDialog`, add a `QSpinBox* m_resetMargin;` member and create it in the constructor's initializer list, next to `m_cooldown`. Configure it like the others: range 5 to 50, suffix `" %"`. Then:

- Give it an object name, `"resetMarginSpinBox"`, so the test in step 4 can find it.
- Give it a tooltip explaining what it does. "Re-arm margin" means nothing to most users.
- Add a row to the `thresholds` form layout, under "Alert cooldown".

**3. Wire up the three places that move values in and out of the form:**

| Function | What to do |
|---|---|
| `reload()` | Settings → form. The setting is a fraction (`0.15`); the box shows a percentage (`15`). |
| `save()` | Form → settings. Percentage → fraction. Pass it to `setConfiguration`. |
| `restoreDefaults()` | Set the box to 15. (Only the form; nothing is saved until the user presses Save.) |

<details><summary>Hint: converting between 0.15 and 15</summary>

`qRound(m_settings.resetMargin() * 100)` going in, and `m_resetMargin->value() / 100.0` going out. Note the `100.0`: with `100` it would be integer division and every value would become 0.
</details>

**4. Write a UI test** in `tests/UiSafetyTests.cpp` that:

1. stores a margin of `0.30` with `setConfiguration`,
2. creates a `SettingsDialog` and checks that the spin box shows `30`,
3. changes the spin box to `40` and clicks **Save**,
4. checks that `settings.resetMargin()` is now `0.40`.

<details><summary>Hint: finding and clicking the Save button</summary>

The Save button lives in a `QDialogButtonBox`:

```cpp
auto* buttons = dialog.findChild<QDialogButtonBox*>();
buttons->button(QDialogButtonBox::Save)->click();
```
</details>

<details><summary>Hint: my test hangs forever!</summary>

Look at what thresholds you passed to `setConfiguration`. If you used small numbers like `1, 2, 3` (bytes), the dialog shows them in GB rounded to 0.1, so all three become `0.1`. `save()` then thinks Tier 1 < Tier 2 < Tier 3 is violated and opens a **modal** `QMessageBox`. Modal dialogs wait for a click that never comes in a test (Lesson 8). Use realistic values like `2 * gib, 4 * gib, 8 * gib` with `constexpr quint64 gib = 1024ULL * 1024 * 1024;`.

This is a real lesson: tests that touch UI must avoid code paths that open modal dialogs.
</details>

### Done when

- [ ] The Settings window has a "Re-arm margin" row with a tooltip.
- [ ] Changing it, pressing Save, closing and reopening Settings shows your value.
- [ ] `cat ~/.config/SwapAlert/SwapAlert.ini` shows `resetMargin=` under `[alerts]`.
- [ ] Restore Defaults sets it back to 15% (and Cancel leaves the saved value alone).
- [ ] All tests pass, including your new UI test, in both `build` and `build-ci`.

**Stretch:** Tier thresholds have a validation rule (Tier 1 < 2 < 3). Is there a margin value that makes the alert engine behave strangely? Look at `resetPointForTier()` in `AlertEngine.cpp` and the `qBound` there.

---

## Project 2 — A tray icon you can read without colour

**Goal:** draw the tier number inside the tray dot (1, 2, 3), and a dash when paused or unavailable, so the icon works for colour-blind users.

**You'll learn:** drawing with `QPainter`; colour contrast; checking accessibility for real. *(Lesson 8)*

**Files:** `src/ui/TrayController.cpp`

### Background

Right now `statusIcon()` and `unavailableIcon()` draw a 24-pixel dot in green, yellow, orange, red or grey. About 1 in 12 men have some form of colour blindness; for many of them yellow, orange and green look alike. The tooltip has the information, but you have to hover to see it.

### Steps

**1. Remove the duplication first.** `statusIcon()` and `unavailableIcon()` are nearly identical. Write one helper that both use:

```cpp
QIcon dotIcon(const QColor& fill, const QString& label, const QColor& labelColor);
```

Make the two existing functions one-liners that call it. Build and run: the app should look exactly the same. Refactoring first, then changing behaviour, keeps each step easy to check.

**2. Make room for a digit.** The pixmap is 36×36 and the dot is drawn at `(6, 6, 24, 24)`. A digit needs a bigger dot; try `(3, 3, 30, 30)`.

**3. Draw the label.** After the ellipse, if `label` isn't empty:

- get the painter's font, set a **pixel** size (around 20) and bold, and set it back on the painter,
- set the pen to `labelColor`,
- draw the text centred in the dot's rectangle.

<details><summary>Hint: centring text</summary>

`painter.drawText(QRect(3, 3, 30, 30), Qt::AlignCenter, label);` centres both ways inside the rectangle. Use `font.setPixelSize(...)` rather than `setPointSize(...)`: points depend on the screen's DPI, pixels map directly onto your 36-pixel image.
</details>

**4. Pick readable colours.** White text is unreadable on the light yellow Tier 1 dot. Use black text there and white on the darker dots. For the unavailable icon, use a dash (`"–"`) so it differs from the empty green "Normal" dot even in greyscale.

### Test it

There's no unit test here; you check it with your eyes, the way accessibility is really tested.

1. **See every state.** If your machine is using some swap, the quickest way is to lower the thresholds in Settings below the current usage (e.g. 0.1 / 0.2 / 0.3 GB) and watch the icon change; untick "Monitoring Enabled" to see the paused icon. Put your real thresholds back afterwards.

    If you have no swap in use, temporarily hard-code a tier instead: in `TrayController::updateSample`, change `rebuildIcon(tier);` to `rebuildIcon(AlertTier::Tier2);`, rebuild, look, and try the others. **Undo it before committing.**
2. **Look at it in greyscale**, the way many colour-blind users effectively see it:
    - 🍎 System Settings → Accessibility → Display → Color Filters → turn on, choose Greyscale.
    - 🐧 KDE Plasma 6: System Settings → Accessibility → Color Blindness Correction. GNOME has no built-in filter; temporarily add `pixmap.save(QStringLiteral("/tmp/tier-%1.png").arg(label));` in `dotIcon`, then open the PNGs in an image editor and desaturate them.

### Done when

- [ ] Tier 1–3 show their number; Normal shows a plain dot; paused/unavailable shows a dash.
- [ ] In greyscale you can tell every state apart.
- [ ] The digit is readable at real menu-bar size, not just zoomed in.
- [ ] Your throwaway debugging code is gone (`git diff` shows only the icon changes).

**Stretch:** macOS menu-bar icons are usually monochrome "template" images that adapt to light and dark menu bars. Read about `QIcon::setIsMask()` and decide whether it suits this app (hint: what happens to the colour?).

---

## Project 3 — Remember recent readings: `SwapHistory`

**Goal:** a new core class that keeps the last hour of readings in a fixed-size **ring buffer**, and can answer "how much did swap grow in the last N minutes?". `SwapMonitor` records every reading into it.

**You'll learn:** designing a small class and its tests before wiring it in; ring buffers and index arithmetic; signed vs unsigned maths; adding a file and a test to CMake. *(Lessons 1, 6, 7, 13)*

**Files:** new `src/core/SwapHistory.hpp/.cpp`, new `tests/SwapHistoryTests.cpp`, `src/core/SwapMonitor.hpp/.cpp`, `tests/SwapMonitorTests.cpp`, `CMakeLists.txt`

### Background: what's a ring buffer?

We want "the last 360 readings" (one hour at 10 s each). Appending to a growing list forever would leak memory; removing from the front of a `std::vector` shifts every element each time. A **ring buffer** is a fixed-size array where you write into the next slot and wrap around to the start when you reach the end, overwriting the oldest:

```text
capacity 4, after adding A B C D E F:

 index:   0   1   2   3
        [ E | F | C | D ]
                  ▲
                  m_next = 2  (next write goes here; it's also the oldest)
 oldest → newest:  C D E F
```

You track two numbers: `m_next` (where the next write goes) and `m_count` (how many slots hold real data, up to the capacity).

### Steps

**1. Create the header.** Use this API; it's what Projects 4 and 5 build on:

```cpp
// src/core/SwapHistory.hpp
#pragma once

#include <QtGlobal>
#include <cstddef>
#include <optional>
#include <vector>

struct SwapSample {
    qint64 timeMs = 0;       // from IMonotonicClock, like the alert engine uses
    quint64 usedBytes = 0;
};

class SwapHistory {
public:
    explicit SwapHistory(std::size_t capacity = 360);

    void add(const SwapSample& sample);
    void clear();
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::optional<SwapSample> latest() const;
    // Newest usage minus the oldest usage within the window. Negative if usage shrank.
    [[nodiscard]] std::optional<qint64> growthOverMs(qint64 windowMs) const;

private:
    [[nodiscard]] const SwapSample& at(std::size_t index) const;   // 0 = oldest

    std::vector<SwapSample> m_samples;
    std::size_t m_next = 0;
    std::size_t m_count = 0;
};
```

Notice it has **no Qt signals and no OS calls**. It's a plain C++ class, so it's trivial to test.

**2. Write the tests before the implementation.** Create `tests/SwapHistoryTests.cpp`, modelled on `tests/ProcessGroupingTests.cpp` (a `QObject` class with `private slots:`, then `QTEST_GUILESS_MAIN(SwapHistoryTests)` and `#include "SwapHistoryTests.moc"` at the end — Lesson 8 explains why that include is needed when the class lives in a `.cpp`).

Test at least:

| Test | Setup | Expect |
|---|---|---|
| Empty | nothing added | `latest()` empty, `growthOverMs(...)` empty |
| One sample | one `add` | growth still empty (you need two points) |
| Growth in window | samples at 0 s / 100, 10 s / 150, 20 s / 400 | `growthOverMs(20'000) == 300`, `growthOverMs(10'000) == 250`, `growthOverMs(5'000)` empty |
| Shrinking | 0 s / 500, then 10 s / 200 | growth is **−300** |
| Wrap-around | capacity 3, add 5 samples | `size() == 3`, `latest()` is the 5th, growth uses the 3rd as oldest |

<details><summary>Hint: comparing optionals in QCOMPARE</summary>

Compare with an `optional` of the same type: `QCOMPARE(history.growthOverMs(20'000), std::optional<qint64>(300));`
</details>

**3. Add both to CMake.** This is three edits in `CMakeLists.txt`:

1. `src/core/SwapHistory.cpp` in `SWAP_ALERT_CORE_SOURCES` (the app).
2. `src/core/SwapHistory.cpp` in the `SwapMonitorTests` sources (it'll need it in step 6).
3. A new test executable, copying the pattern of `ProcessGroupingTests`: `qt_add_executable`, `target_include_directories`, `target_link_libraries`, `add_test`.

Because you added files, re-run the configure step (`cmake -S . -B build`) before building. Create `SwapHistory.cpp` with **stub** functions that return nothing useful (`return std::nullopt;`, `return 0;`), so everything compiles and links. Your tests should then build and **fail**. That's correct: now you know they actually test something.

**4. Implement it.** Work through the functions in this order: constructor, `add`, `clear`, `size`, `at`, `latest`, `growthOverMs`.

<details><summary>Hint: the constructor</summary>

Size the vector once, up front: `m_samples(capacity)`. What happens in `add` if someone passes a capacity of 0? (Modulo by zero is undefined behaviour — Lesson 14.) Guard against it.
</details>

<details><summary>Hint: <code>add</code></summary>

Write at `m_next`, advance `m_next` with `% m_samples.size()`, and increase `m_count` only while it's below the capacity.
</details>

<details><summary>Hint: <code>at(index)</code> — the tricky one</summary>

The oldest sample is `m_count` slots behind `m_next`. To step backwards without going negative on an unsigned number, add the capacity first:

```cpp
const std::size_t oldest = (m_next + m_samples.size() - m_count) % m_samples.size();
return m_samples[(oldest + index) % m_samples.size()];
```

Check it against the diagram above: capacity 4, `m_next` 2, `m_count` 4 → oldest = (2 + 4 − 4) % 4 = 2. ✔
</details>

<details><summary>Hint: <code>growthOverMs</code></summary>

Take the newest sample. Walk from the oldest towards the newest and stop at the **first** sample whose `newest.timeMs - sample.timeMs <= windowMs`. Don't count the newest itself (that would be zero growth from one point).

The subtraction must be signed. `usedBytes` is `quint64`; if usage shrank, `newest.usedBytes - old.usedBytes` would wrap around to about 18 quintillion (Lesson 2). Convert each to `qint64` before subtracting.
</details>

**5. Make the tests pass.** If one fails, use the debugger on that single test (Lesson 14): `lldb ./build/SwapHistoryTests -- growthUsesOldestSampleInsideWindow`.

**6. Record every reading in `SwapMonitor`.**

- Add a `SwapHistory m_history;` member and a `const SwapHistory& history() const;` accessor.
- In `refreshNow()`, after a successful read, read the clock **once** into a local `nowMs`, add the sample, and pass the same `nowMs` to the alert engine. Add it *before* the `emit`s, so anything listening to `sampleUpdated` sees an up-to-date history.
- **Clear** the history in `resumeAfterSystemEvent()` and where monitoring is re-enabled (next to `m_alertEngine.reset()` in `applySettings()`). `PLAN.md` asks that sleep and wake don't create false growth spikes; starting fresh is the simplest way to guarantee that.

**7. Test the wiring** in `tests/SwapMonitorTests.cpp`: refresh twice with different fake values and fake times, check `monitor.history().size()` and the growth; then `start()`, suspend and resume, and check the history holds only the one fresh sample.

### Done when

- [ ] `SwapHistoryTests` has at least the five tests above, and they pass.
- [ ] `SwapMonitorTests` checks that readings are recorded and cleared on resume.
- [ ] `build-ci` (sanitizers + warnings as errors) passes.
- [ ] No Qt or OS headers in `SwapHistory.hpp` other than `<QtGlobal>`.

**Stretch:** the capacity is in *samples*, but the polling interval is configurable. At 2-second polling, 360 samples is only 12 minutes. How would you make it "always one hour"? Is it worth the complexity?

---

## Project 4 — Show how fast swap is growing

**Goal:** the Tier 2 warning window and the dashboard show "Change: +1.20 GB in the last 5 minutes". `PLAN.md` asks for exactly this in the Tier 2 window.

**You'll learn:** connecting a core feature to the UI through `main.cpp`; formatting signed values; testing a dialog's text. *(Lessons 8, 9)*

**Requires:** Project 3.

**Files:** `src/ui/Format.hpp`, `src/ui/WarningDialog.hpp/.cpp`, `src/ui/StatusDialog.hpp/.cpp`, `src/app/main.cpp`, `tests/UiSafetyTests.cpp`, `CMakeLists.txt`

### Steps

**1. A formatting helper.** In `src/ui/Format.hpp`, next to `formatBytes`, add:

```cpp
inline QString formatGrowth(std::optional<qint64> bytes, int minutes);
```

It should return:

| Input | Output |
|---|---|
| empty | `Not enough readings yet` |
| `0` | `No change in the last 5 minutes` |
| `+512 MiB` | `+512 MB in the last 5 minutes` |
| `−2 GiB` | `−2.00 GB in the last 5 minutes` |

<details><summary>Hint: reuse <code>formatBytes</code></summary>

`formatBytes` takes a `quint64`, so it can't take a negative number. Format the sign yourself and pass the *magnitude*:

```cpp
const auto magnitude = static_cast<quint64>(*bytes < 0 ? -*bytes : *bytes);
```

For a nice touch, use the real minus sign `−` (U+2212) rather than a hyphen.
</details>

**2. Add a label to `WarningDialog`.** Add a `QLabel* m_growthLabel;` under the usage label, give it the object name `"growthLabel"`, and add:

```cpp
void setRecentGrowth(std::optional<qint64> bytes, int minutes);
```

which sets the label to `"Change: " + formatGrowth(...)`.

**3. Do the same in `StatusDialog`** (the dashboard), placing it under the detail line.

**4. Wire it up in `main.cpp`.** The dialogs don't know about `SwapMonitor`, and they shouldn't (Lesson 6). `main.cpp` connects them:

```cpp
constexpr int growthWindowMinutes = 5;
const auto recentGrowth = [&] {
    return monitor.history().growthOverMs(growthWindowMinutes * 60 * 1000LL);
};
```

Then:

- at the start of `presentAlert`, call `warningDialog.setRecentGrowth(recentGrowth(), growthWindowMinutes);`
- connect `SwapMonitor::sampleUpdated` to a lambda that calls `statusDialog.setRecentGrowth(...)`.

<details><summary>Hint: why the <code>1000LL</code>?</summary>

`5 * 60 * 1000` fits in an `int`, but a habit of multiplying `int`s to get milliseconds overflows quickly (24 days of milliseconds don't fit in 32 bits). `LL` makes the whole calculation 64-bit.
</details>

<details><summary>Hint: is the history up to date when <code>sampleUpdated</code> fires?</summary>

Yes, if you followed step 6 of Project 3: the sample is added *before* the signal is emitted, and slots on the same thread run immediately inside `emit` (Lesson 9). Check the order in `refreshNow()`.
</details>

**5. Test the dialog.** `UiSafetyTests` doesn't compile `WarningDialog.cpp` yet: add it to that target's sources in `CMakeLists.txt`. Then write a test that calls `setRecentGrowth` with an empty value, a positive and a negative one, and checks the label text exactly (find it with `findChild<QLabel*>("growthLabel")`).

### Done when

- [ ] The dashboard shows the change line and updates with every reading.
- [ ] A Tier 2 alert (try **Settings → Test Tier 2**) shows the change line.
- [ ] Just after starting the app it says "Not enough readings yet", not "+0 MB".
- [ ] The UI test checks all three cases.

**Stretch:** colour the dashboard line red when growth is above, say, 500 MB, to draw attention. Then ask yourself if colour alone is enough (Project 2).

---

## Project 5 — Measure swap activity

**Goal:** read how many bytes were swapped in and out since boot, on both OSes, and show the current rate on the dashboard: "Activity: 1.2 MB/s in · 0 KB/s out".

**You'll learn:** a new kernel interface on each OS (`/proc/vmstat`, Mach `host_statistics64`); optional interface methods; why changing a struct breaks code far away. *(Lessons 0, 2, 3, 6)*

**Requires:** Projects 3 and 4.

**Files:** `src/core/SwapInfo.hpp`, `src/core/ISwapReader.hpp`, both swap readers, `src/core/SwapHistory.*`, `src/core/SwapMonitor.cpp`, `src/ui/Format.hpp`, `src/ui/StatusDialog.*`, `src/app/main.cpp`, tests

### Background

Lesson 0 explained that *total* swap used can stay high long after the pressure is gone, while swap **activity** (pages moving in and out right now) is what makes a computer feel slow. Both OSes keep running totals since boot. Read the total twice, divide the difference by the time between, and you have a rate.

### Steps

**1. A type for the counters.** In `src/core/SwapInfo.hpp`:

```cpp
// Totals since boot. The difference between two readings gives the swap activity.
struct SwapCounters {
    quint64 swappedInBytes = 0;
    quint64 swappedOutBytes = 0;
};
```

**2. An *optional* reader method.** In `ISwapReader`, add:

```cpp
// Optional: readers that can't report activity keep this default.
virtual std::optional<SwapCounters> readCounters() { return std::nullopt; }
```

Unlike `read()`, this isn't `= 0`. It's a virtual function **with a default implementation**. Every existing reader, including the fake readers in the tests, keeps compiling unchanged and simply reports "not available". Only the real readers override it. Compare with what would happen if you made it pure virtual.

**3. 🐧 Linux: read `/proc/vmstat`.**

> **Try it:** `grep pswp /proc/vmstat`. You'll see `pswpin` and `pswpout`: **pages** swapped in and out since boot.

In `LinuxSwapReader`:

- Add a second constructor parameter, `QString vmstatPath = QStringLiteral("/proc/vmstat")`, stored like `m_meminfoPath`, so tests can point it at a fake file.
- Override `readCounters()`: read the whole file (remember the `/proc` zero-size gotcha, Lesson 2), split into lines, and parse `pswpin N` and `pswpout N`. If either is missing, return `std::nullopt`.
- Convert pages to bytes with the system page size: `sysconf(_SC_PAGESIZE)` from `<unistd.h>`.

Test it in `tests/AlertEngineTests.cpp` (that's where the existing `LinuxSwapReader` tests live), with a temp file containing `pswpin 3` and `pswpout 5`; expect `3 * pageSize` and `5 * pageSize`.

**4. 🍎 macOS: ask Mach for VM statistics.**

> **Try it:** run `vm_stat`. Its first line shows the page size; `Swapins` and `Swapouts` are the counters you want, in pages.

`vm_stat` gets them from `host_statistics64()`. This is the code, because the Mach API is hard to guess; read every line and make sure you understand it:

```cpp
// src/platform/macos/MacSwapReader.mm
#include <mach/mach.h>

std::optional<SwapCounters> MacSwapReader::readCounters()
{
    // mach_host_self() hands out a new reference to the host port on every call; ask once.
    static const host_t host = mach_host_self();

    vm_statistics64_data_t stats {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;   // in: room we have; out: what was filled
    if (host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count)
        != KERN_SUCCESS) {
        return std::nullopt;
    }

    vm_size_t pageSize = 0;
    if (host_page_size(host, &pageSize) != KERN_SUCCESS) {
        return std::nullopt;
    }
    return SwapCounters {stats.swapins * pageSize, stats.swapouts * pageSize};
}
```

It's the same "caller provides a struct and its size" pattern as `sysctlbyname` (Lesson 3). Mach functions return a `kern_return_t` instead of setting `errno`; `KERN_SUCCESS` is 0. Declare the override in `MacSwapReader.hpp`.

> ⚠️ This macOS code wasn't compiled while writing this guide. If it doesn't build, the compiler errors will point at the exact type that differs — a good exercise in reading Apple's headers. Check your numbers against `vm_stat`.

**5. Store counters in the history.** Add `std::optional<SwapCounters> counters;` to `SwapSample` (and `#include "core/SwapInfo.hpp"` in `SwapHistory.hpp`), and in `SwapMonitor::refreshNow()` pass `m_reader->readCounters()` when adding the sample.

Now build with your `build-ci` folder. You'll get errors like:

```text
error: missing initializer for member 'SwapSample::counters' [-Werror=missing-field-initializers]
```

in your Project 3 tests, which wrote `history.add({0, 100})`. With `-Wextra`, the compiler warns when brace initialisation leaves out fields, because that's often a bug: someone added a field and forgot to update old code. That's exactly what just happened. Fix each one by spelling it out: `history.add({0, 100, std::nullopt})`.

(This is why CI builds with warnings as errors: the normal build compiled silently.)

**6. Compute the rate.** Add to `SwapHistory`:

```cpp
struct SwapActivity {
    double inBytesPerSecond = 0;
    double outBytesPerSecond = 0;
};

[[nodiscard]] std::optional<SwapActivity> activityOverMs(qint64 windowMs) const;
```

Same idea as `growthOverMs`: newest sample vs the oldest one in the window, but only samples that *have* counters, divided by elapsed seconds.

<details><summary>Hint: what can go wrong?</summary>

Return empty when:

- fewer than two samples, or the newest has no counters,
- the elapsed time is 0 (division by zero),
- a counter went *down*. Counters since boot only grow, so this means something odd happened, and unsigned subtraction would wrap around (Lesson 2).
</details>

Test it: two samples 10 s apart whose `swappedInBytes` differ by 20 000 → 2 000 bytes/s in. Also test that samples without counters give an empty result.

**7. Show it on the dashboard.** Add `formatRate(double bytesPerSecond)` to `Format.hpp` (KB/s below 1 MB/s, otherwise MB/s with one decimal), a `setSwapActivity(std::optional<SwapActivity>)` to `StatusDialog`, and call it from the same `sampleUpdated` lambda as Project 4, with a one-minute window.

### Test it for real

Watch the dashboard next to `vmstat 1` (🐧, columns `si`/`so`) or `vm_stat 1` (🍎) while creating memory pressure: open many browser tabs, or on 🐧 `stress-ng --vm 2 --vm-bytes 90% --timeout 60s` (careful: this makes your machine sluggish on purpose). The numbers should move together.

### Done when

- [ ] Both readers implement `readCounters()`, and the Linux one has a test.
- [ ] The fake readers in the tests didn't need changing.
- [ ] `SwapHistory` has activity tests, including "no counters".
- [ ] The dashboard shows activity, and "not available yet" until there are two readings.
- [ ] `build-ci` passes.

**Stretch:** should high swap *activity* trigger an alert on its own? `PLAN.md` defers rate-based alerts. Write down the pros and cons before deciding; this is a product decision, not just a code one.

---

## Project 6 — Signal the right process, always: `pidfd`

**Goal:** on Linux, quit applications through a **pidfd** instead of a raw PID, so a recycled PID can never receive the signal.

**You'll learn:** a modern Linux syscall with no glibc wrapper on older systems; RAII for file descriptors; why the order of checks matters. *(Lessons 5, 7, 14)*

**Platform:** 🐧 Linux only. macOS quits apps through `NSRunningApplication`, which refers to one specific app instance rather than a number, so it doesn't have this problem.

**Files:** new `src/platform/linux/PidFd.hpp/.cpp`, `src/platform/linux/LinuxProcessService.cpp`, `tests/LinuxProcessServiceTests.cpp`, `CMakeLists.txt`

### Background

Lesson 5 explained the race: between listing an app and sending it `SIGTERM`, the app could exit and its PID could be reused by a different process. `terminate()` re-checks everything just before acting, which makes the window tiny, but not zero.

Linux 5.3 added a fix. `pidfd_open(pid)` returns a **file descriptor that refers to that exact process**. If the process exits, the descriptor doesn't move on to whatever gets the PID next; signalling through it just fails with `ESRCH` ("no such process").

### Steps

**1. Experiment first, outside the app.** Save this as `pidfd_demo.cpp` somewhere outside the repo:

```cpp
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    const pid_t child = fork();
    if (child == 0) {
        pause();                                   // child: sleep until a signal arrives
        return 0;
    }

    const int fd = static_cast<int>(syscall(SYS_pidfd_open, child, 0));
    if (fd < 0) {
        std::printf("pidfd_open failed: %s\n", std::strerror(errno));
        return 1;
    }
    std::printf("child PID %d, pidfd %d\n", child, fd);

    syscall(SYS_pidfd_send_signal, fd, SIGTERM, nullptr, 0);
    waitpid(child, nullptr, 0);                    // reap it: the PID is now free to be reused
    std::printf("child exited and was reaped\n");

    if (syscall(SYS_pidfd_send_signal, fd, SIGTERM, nullptr, 0) != 0) {
        std::printf("second signal failed: %s\n", std::strerror(errno));
    }
    close(fd);
}
```

```bash
c++ -std=c++20 -Wall -Wextra pidfd_demo.cpp -o pidfd_demo && ./pidfd_demo
# child PID 5313, pidfd 3
# child exited and was reaped
# second signal failed: No such process
```

Three new things here:

- **`fork()`** splits your program into two processes. It returns 0 in the child and the child's PID in the parent.
- **`waitpid()`** "reaps" an exited child. Until a parent reaps it, a dead child is a *zombie* and its PID can't be reused.
- **`syscall(SYS_..., ...)`** calls a kernel function directly by number. glibc only added `pidfd_open()` wrappers in version 2.36, and Ubuntu 22.04 (a supported platform in `PLAN.md`) ships 2.35, so calling the syscalls directly works everywhere.

> **Try it:** run it under `strace -f ./pidfd_demo` and find the `pidfd_open` and `pidfd_send_signal` calls.

**2. Wrap the descriptor in a class (RAII).** A raw `int fd` must be `close()`d on every path out of a function, including early `return`s. That's easy to get wrong, and leaked descriptors pile up. Wrap it, like `QFile` wraps files (Lesson 7):

```cpp
// src/platform/linux/PidFd.hpp
#pragma once

#include <sys/types.h>

// A handle to one specific process. Unlike a PID, it can't be reused by a new process.
class PidFd {
public:
    explicit PidFd(pid_t processId);
    ~PidFd();
    PidFd(const PidFd&) = delete;
    PidFd& operator=(const PidFd&) = delete;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] int openError() const;   // errno from pidfd_open, if it failed
    bool sendSignal(int signal) const;     // false on failure; errno says why

private:
    int m_fd = -1;
    int m_openError = 0;
};
```

Why `= delete` the copy operations? If a `PidFd` were copied, both copies would `close()` the same number in their destructors. The second close might close a *different* file that has since been given that number. Deleting the copy makes that mistake a compile error.

Implement the `.cpp`: open in the constructor (record `errno` if it fails), close in the destructor if valid, and `syscall(SYS_pidfd_send_signal, m_fd, signal, nullptr, 0) == 0` in `sendSignal`.

**3. Add it to CMake** in two places: the Linux `SWAP_ALERT_PLATFORM_SOURCES`, and the `LinuxProcessServiceTests` sources.

**4. Test it**, in `tests/LinuxProcessServiceTests.cpp`, by doing what the demo did but with `QProcess`:

1. Start `sleep 30` with `QProcess` and wait for it to start.
2. Create a `PidFd` from `child.processId()`. If it's invalid with `ENOSYS` (kernel too old), call `QSKIP(...)`.
3. Send `SIGTERM`; wait for the process to finish. (`QProcess` reaps it for you.)
4. Sending again must fail, with `errno == ESRCH`.

**5. Use it in `LinuxProcessService::terminate()`.** The order is the whole point:

```text
 1. basic checks (pid > 1, not ourselves)        ← already there
 2. open the PidFd                                ← NEW: pin down *this* process
 3. re-read owner, protected list, eligibility    ← already there
 4. send the signal through the PidFd             ← NEW, instead of kill()
```

If you opened the handle *after* the checks, the PID could be recycled between the checks and the open, and you'd be back to the original race. Opening it first means: if the original process dies after step 2, step 4 fails safely with `ESRCH`, whatever the checks in step 3 saw.

Handle the failure cases:

- Open failed with `ESRCH`: the process is already gone; return a clear error.
- Open failed with `ENOSYS`: a kernel older than 5.3. Fall back to `::kill()` so the app still works there.

<details><summary>Hint: the signal-sending line</summary>

```cpp
const int signal = force ? SIGKILL : SIGTERM;
const bool sent = handle.isValid() ? handle.sendSignal(signal)
                                   : ::kill(static_cast<pid_t>(processId), signal) == 0;
```

The existing error message after it uses `errno`, which both paths set.
</details>

### Done when

- [ ] The demo program prints `No such process` for the second signal, and you can explain why.
- [ ] `PidFd` can't be copied, and always closes its descriptor.
- [ ] The new test passes (or is skipped on a very old kernel).
- [ ] In `terminate()`, the `PidFd` is opened before the ownership and eligibility checks.
- [ ] Quitting an app from the cleanup window still works (try it with a text editor).
- [ ] `build-ci` passes.
- [ ] No descriptors leak. Sanitizers don't track file descriptors, so check by hand: count them with `ls /proc/$(pgrep swap-alert)/fd | wc -l` before and after quitting a few apps from the cleanup window. The number should not keep growing.

**Stretch:** `pidfd` can also tell you *when* a process exits: `poll()` on it becomes readable. Could `CleanupDialog` use that instead of its fixed 1.5-second refresh delay? Look at `QSocketNotifier`, which watches a file descriptor from the Qt event loop.

---

## When you're done with a project

1. Run the full test suite in both `build` and `build-ci`.
2. Update `PLAN.md`: remove the item from **Known gaps**.
3. Commit with a message saying *why*, not just what. For example: "Show recent swap growth in the Tier 2 warning, as specified in PLAN.md".
4. Open a pull request and let CI check it on all three platforms.
5. Read your own diff once more as if someone else wrote it. Anything you'd ask them to change?
