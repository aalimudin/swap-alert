# Learning C++ and Systems Programming with Swap Alert

This guide uses Swap Alert as a textbook. Every lesson takes one real piece of the app, explains the idea behind it in plain language, walks through the code that implements it, and then gives you something to **run in your terminal** so you can see the operating system do it for yourself.

You do not need to know C++ already. You should be comfortable with *some* programming language (Python, JavaScript, Go…) and with opening a terminal.

---

## How to use this guide

- **Read the lessons in order.** Each one builds on the previous one. Lesson 1 gives you just enough C++ to read the code, 2–5 are about the operating system, 6–7 go deeper into C++ design, 8–9 are about Qt, and 10–13 put it all together. Lesson 14 teaches you to debug. Do the Setup section before Lesson 1.
- **Keep the source open next to this guide.** File paths are given as `src/core/AlertEngine.cpp → evaluate()`. Read the real file, not just the excerpt here.
- **Do the "Try it" boxes.** They take a minute each and are where most of the learning happens.
- **Answer the "Check yourself" questions before moving on.** Answers are hidden under a fold.
- Commands marked 🐧 are Linux-only, 🍎 are macOS-only.

### Lesson map

| # | Lesson | You will learn |
|---|--------|----------------|
| 0 | [The big picture](#lesson-0--the-big-picture) | Virtual memory, what swap is (and how it differs per OS), how data flows through the app |
| — | [Setup](#setup--build-run-and-test-the-app) | Build, run and test the app; the edit → build → run loop |
| 1 | [C++ building blocks](#lesson-1--c-building-blocks-in-this-codebase) | Headers, `struct`, `enum class`, `const`, references, `std::optional` |
| 2 | [Reading swap on Linux](#lesson-2--reading-swap-on-linux-proc) | `/proc`, "everything is a file", defensive parsing |
| 3 | [Reading swap on macOS](#lesson-3--reading-swap-on-macos-sysctl) | System calls, `sysctl`, `errno` |
| 4 | [Telling time correctly](#lesson-4--telling-time-correctly-clocks) | Wall clock vs monotonic clocks, sleep-aware time |
| 5 | [Processes and signals](#lesson-5--processes-and-signals) | PIDs, UIDs, the process tree, `kill`, `SIGTERM` vs `SIGKILL` |
| 6 | [Interfaces and polymorphism](#lesson-6--interfaces-virtual-functions-and-dependency-injection) | Virtual functions, abstract classes, dependency injection, fakes |
| 7 | [Ownership and lifetime](#lesson-7--ownership-and-object-lifetime) | Stack vs heap, RAII, `std::unique_ptr`, `std::move`, pimpl |
| 8 | [Meet Qt](#lesson-8--meet-qt) | What Qt is, `QObject` and moc, parent/child ownership, widgets, layouts, the tray, `QSettings`, Qt Test |
| 9 | [The event loop](#lesson-9--the-event-loop-timers-signals-and-slots) | Qt event loop, timers, signals/slots, lambdas, threads |
| 10 | [The alert state machine](#lesson-10--the-alert-state-machine) | State machines, hysteresis, cooldowns — traced step by step |
| 11 | [Talking to other programs](#lesson-11--talking-to-other-programs-d-bus-and-apple-frameworks) | D-Bus on Linux, Objective-C++ on macOS, callbacks across threads |
| 12 | [Writing files safely](#lesson-12--writing-files-safely) | Atomic writes, log rotation, mutexes |
| 13 | [Building and testing](#lesson-13--building-and-testing) | Compiling vs linking, CMake, the preprocessor, unit tests |
| 14 | [Debugging and catching bugs](#lesson-14--debugging-and-catching-bugs) | Logging rules, lldb/gdb, sanitizers, compiler warnings, clang-tidy |
| — | [Exercises](#exercises) · [Glossary](#glossary) · [Further reading](#further-reading) | |

---

## Lesson 0 — The big picture

### What is swap?

Your computer's RAM is fast but limited. When programs ask for more memory than physically exists, the operating system (OS) takes chunks of memory that haven't been used recently and writes them to disk. That disk area is **swap**. When a program needs that memory again, the OS reads it back.

Each chunk is a **page** (usually 4 KiB on Linux, 16 KiB on Apple Silicon). Moving pages to and from disk is thousands of times slower than RAM, so a computer that is swapping heavily feels sluggish. A little swap is normal. A lot of swap means you are running out of memory.

Swap Alert watches how much swap is in use and warns you before things get bad.

### How memory really works: virtual memory in 3 minutes

That description is the simple version. Here's what's actually going on.

**Every process gets its own fake address space.** When a program reads address `0x7ff3a000`, that isn't a location in a RAM chip. It's a **virtual address**. The CPU and the OS keep a **page table** for each process that maps its virtual pages to real **physical** pages of RAM. Two programs can both use address `0x7ff3a000` and see completely different data.

**A page doesn't have to be in RAM.** A page-table entry can say "this page is not present right now". When the program touches it, the CPU stops and asks the kernel to deal with it. That's a **page fault**. The kernel finds the data, puts it in a free physical page, fixes up the page table, and lets the program continue as if nothing happened. The program never knows.

**Not all memory goes to swap.** There are two kinds of pages:

| Kind | Examples | When RAM is tight, the kernel… |
|---|---|---|
| **File-backed** | The program's own code, shared libraries, memory-mapped files | Just drops the page. It can re-read it from the original file later. |
| **Anonymous** | The heap (`new`, `malloc`), the stack | Has nowhere to re-read it from, so it must **write it to swap** first. |

So "swap used" measures anonymous memory that was pushed out of RAM. It's also why the cleanup window measures **RSS** / **footprint** (pages currently in RAM, Lesson 5) rather than the much bigger "virtual size" you see in `top`. Virtual size counts every page a process *could* touch, including ones that were never used.

> **Try it** — watch swap traffic live (both OSes):
> ```bash
> vmstat 1          # 🐧: the si / so columns are pages swapped in / out per second
> vm_stat 1         # 🍎: watch the Swapins / Swapouts columns
> ```
> Then open lots of browser tabs. If the numbers climb, you're watching page faults being served from swap.

### Swap isn't the same on every OS

Swap Alert reads one number, "swap used", on both OSes. But that number means different things depending on how the OS handles memory pressure.

**🍎 macOS compresses first.** Before writing anything to disk, macOS squeezes inactive pages and keeps the compressed copies in RAM (the **memory compressor**). Only when that isn't enough does it write to swap files, which it creates and grows on demand. So on a Mac:

- "Total swap" isn't fixed. It grows (and shrinks) as macOS adds swap files. That's why `xsu_total` changes over time.
- By the time swap is in use, the compressor is already working hard. Swap on a Mac is a later warning sign than on a typical Linux machine.

> **Try it 🍎**
> ```bash
> ls -lh /System/Volumes/VM/             # the swap files themselves
> vm_stat | grep -E "compressor|Swap"    # compressed pages, swap-ins, swap-outs
> memory_pressure                        # the OS's own summary of how tight memory is
> ```

**🐧 Linux depends on the setup.** Swap is a fixed-size partition or file that an administrator enabled. But some systems use RAM-based swap:

- **zram** — a compressed RAM disk used as swap. Fedora enables it by default. Here, "4 GB of swap used" may mean about 1–2 GB of RAM holding compressed data, with no disk involved, which is much less painful than disk swap.
- **zswap** — a compressed cache in RAM sitting in front of a normal disk swap.

> **Try it 🐧**
> ```bash
> swapon --show                          # what swap devices exist? /dev/zram0 means zram
> cat /proc/sys/vm/swappiness            # how eagerly the kernel swaps (0–200, default 60)
> cat /sys/module/zswap/parameters/enabled   # Y if zswap is on
> cat /proc/pressure/memory              # % of time tasks were stalled waiting for memory
> ```

**What this means for the app.** "Swap used" is a reasonable, portable signal, but not a perfect one:

- **Swap usage stays high after the pressure is gone.** The kernel doesn't proactively read pages back in. A machine can show 3 GB of swap used and be perfectly snappy, because nothing is touching those pages.
- **Swap *activity* (pages in/out per second) is a better sign of slowness** than the total. That's Exercise 10.
- Linux has a direct measure of "are programs waiting on memory": **PSI** (`/proc/pressure/memory`). macOS has its memory-pressure level. A future version of the app could use these instead of or alongside swap totals.

Keep this in mind when you read the alert engine in Lesson 10. Its thresholds are in bytes of swap, and the defaults (2 / 4 / 8 GB) are a judgement call, not a law.

### What the app does, in one paragraph

Every few seconds the app asks the OS "how much swap is used?". It feeds that number into a small decision-making component (the **alert engine**) that decides which warning level (**tier**) we're at and whether a *new* alert should fire. The tray icon changes colour, and if an alert fires the app shows a notification, a warning window, or a window that helps you quit memory-hungry applications.

### The three layers

The code is split so that **things that depend on the OS** are kept away from **things that don't**:

```text
src/
├── app/       main.cpp — creates everything and wires it together
├── core/      pure logic: alert engine, monitor, settings. No OS-specific code.
├── platform/  one folder per OS. Talks to the kernel and system services.
│   ├── linux/
│   └── macos/
├── ui/        Qt windows and the tray icon
tests/         automated tests that run the core without any real OS data
```

### How data flows

```text
          ┌──────────── platform/ (OS-specific) ────────────┐
Kernel ──▶│ LinuxSwapReader  (/proc/meminfo)                │
          │ MacSwapReader    (sysctl vm.swapusage)          │
          └───────────────────────┬─────────────────────────┘
                                  │  SwapInfo { total, used, free }
                                  ▼
                       core/SwapMonitor  ◀── QTimer fires every N seconds
                                  │
                                  ▼
                       core/AlertEngine  "which tier? should we alert?"
                          │                   │
             sampleUpdated│                   │alertTriggered
                          ▼                   ▼
               ui/TrayController     app/main.cpp → notification / dialog
```

> **Key idea:** `core/` never includes anything from `platform/linux` or `platform/macos`. That is what lets the same alert logic run on two OSes and inside tests.

### Qt in 2 minutes

You'll see a lot of names starting with `Q` before Qt gets its own lesson (Lesson 8). They all come from **Qt**, a cross-platform C++ library for desktop apps. Until then, this is all you need:

| You see | Read it as |
|---|---|
| `QString` | Text (like `std::string` or a Python `str`) |
| `QByteArray` | Raw bytes |
| `QFile`, `QDir` | A file, a folder |
| `QVector`, `QHash`, `QSet` | A list, a dictionary/map, a set |
| `quint64`, `qint64` | Unsigned / signed 64-bit integer |
| `QStringLiteral("...")` | A string literal, built at compile time |
| `"%1 of %2".arg(a, b)` | String formatting: fills in `%1`, `%2` |
| `QTimer`, `QDialog`, `emit`, `connect(...)` | Timers, windows, signals — covered in Lessons 8–9 |
| `qCInfo(logMonitor) << ...` | Writes a log line |

---

## Setup — Build, run and test the app

Do this once before Lesson 1. Several "Try it" boxes need a built copy of the app, and seeing it run makes the code much easier to follow.

### 1. Install the tools

| Tool | Job |
|---|---|
| A C++ compiler | Turns `.cpp` into machine code. Clang on macOS, GCC or Clang on Linux. |
| CMake | Reads `CMakeLists.txt` and generates build instructions (Lesson 13). |
| Ninja | Runs those build instructions quickly. |
| Qt 6.5+ | The library the app is built on (Lesson 8). |

```bash
# 🍎 (installs Apple's compiler first if needed)
xcode-select --install
brew install qt cmake ninja

# 🐧 Fedora
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qttools-devel
# 🐧 Ubuntu/Debian: see the package list in PLAN.md
```

### 2. Configure and build

```bash
cd swap-alert

# 🍎
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

# 🐧 (Qt is found automatically)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# both
cmake --build build
```

What the options mean:

- `-S .` — the source is here. `-B build` — put all generated files in `build/`, so the source tree stays clean.
- `-DCMAKE_BUILD_TYPE=Debug` — no optimisation, plus debug symbols. You need this for the debugger in Lesson 14.
- `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` — writes `build/compile_commands.json`, which editors such as VS Code and CLion use for autocomplete and go-to-definition.

The first configure may print `Could NOT find WrapVulkanHeaders`. That's harmless; the app doesn't use Vulkan.

### 3. Run it

```bash
open "build/Swap Alert.app"                     # 🍎: look in the menu bar
./build/swap-alert                              # 🐧: look in the system tray
```

Running it **from a terminal** is more educational, because you see the log lines as they happen:

```bash
"build/Swap Alert.app/Contents/MacOS/Swap Alert"   # 🍎
./build/swap-alert                                 # 🐧
```

You'll see `swapalert.monitor` lines every 10 seconds. Open **Settings…** and press **Test Tier 2** to see an alert without needing real memory pressure. Press `Ctrl+C` in the terminal to quit.

> **No tray icon on 🐧 GNOME?** GNOME hides tray icons by default. Install the "AppIndicator and KStatusNotifierItem Support" extension.

### 4. Run the tests

```bash
ctest --test-dir build --output-on-failure
```

Every test should pass. You can also run one test executable directly, or even one test function, which is handy later:

```bash
./build/SwapAlertTests                         # all alert-engine tests
./build/SwapAlertTests hysteresisRearmsTier    # just one
```

### The edit → build → run loop

From now on, your loop is:

```bash
cmake --build build && ctest --test-dir build      # after every change
```

You only need to re-run the long `cmake -S . -B build ...` command if you add or remove source files or change `CMakeLists.txt`.

> **Try it** — make your first change. In `src/ui/TrayController.cpp`, change `"15 Minutes"` to `"15 Minutes (quick)"`. Rebuild, run, open the tray menu. Then change it back.

---

## Lesson 1 — C++ building blocks in this codebase

Before reading real code, here are the C++ basics you'll meet in every file. Every example is taken from the repo, so you'll recognise these lines when they come back in later lessons. Don't try to memorise it all; skim it now and come back when something looks unfamiliar.

### Headers (`.hpp`) and source files (`.cpp` / `.mm`)

C++ compiles each `.cpp` file separately. For `SwapMonitor.cpp` to call `AlertEngine::evaluate()`, it must first see a **declaration** saying that function exists. Declarations go in headers; `#include "core/AlertEngine.hpp"` literally pastes the header's text in.

- `.hpp` — *what exists* (class layouts, function signatures).
- `.cpp` — *how it works* (function bodies).
- `.mm` — Objective-C++ source, used on macOS so C++ and Apple's Objective-C APIs can be mixed (Lesson 11).

`#pragma once` at the top of every header means "if this file was already included, skip it", preventing duplicate definitions.

### `struct` for plain data

```cpp
// src/core/SwapInfo.hpp
struct SwapInfo {
    quint64 totalBytes = 0;
    quint64 usedBytes = 0;
    quint64 freeBytes = 0;
};
```

A `struct` bundles values. `= 0` gives a default, so a `SwapInfo{}` is never full of garbage. `quint64` is Qt's name for an unsigned 64-bit integer (same as `std::uint64_t`).

(`struct` and `class` are almost the same in C++; the only difference is that `struct` members are public by default. By convention, `struct` is used for plain data and `class` for things with behaviour.)

### `enum class` for a fixed set of states

```cpp
// src/core/AlertTier.hpp
enum class AlertTier : quint8 { Normal = 0, Tier1 = 1, Tier2 = 2, Tier3 = 3 };
```

An `enum class` is a type with a closed list of named values. Unlike old C `enum`s, it won't silently convert to `int`. You must write `static_cast<int>(tier)`, which makes conversions visible. `: quint8` sets the storage to one byte.

### `const` and `[[nodiscard]]`

```cpp
[[nodiscard]] AlertTier currentTier() const;
```

- The trailing `const` promises this method doesn't modify the object. The compiler enforces it.
- `[[nodiscard]]` makes the compiler warn if a caller ignores the return value — calling `currentTier()` and throwing away the answer is surely a bug.
- `const auto info = ...;` on a local variable means it can't be reassigned. This codebase uses `const` wherever possible; it makes code easier to reason about.

### References (`&`) vs pointers (`*`)

```cpp
std::optional<SwapInfo> read(QString& errorMessage);
```

`QString& errorMessage` is a **reference**: an alias for the caller's variable. Assigning to `errorMessage` inside `read()` changes the caller's string. This is an **out-parameter** — a second way to return information.

A **pointer** (`T*`) is a variable holding an address. It can be `nullptr` and can be re-pointed. A reference can't be null and always refers to the same thing. Rule of thumb: use a reference when "nothing" isn't a valid option.

The symbols are confusing at first because `&` and `*` each mean two things depending on where they appear:

| Code | Where | Meaning |
|---|---|---|
| `QString& name` | In a type | `name` is a reference |
| `&usage` | In an expression | "The address of `usage`" — gives you a pointer |
| `xsw_usage* p` | In a type | `p` is a pointer |
| `*p` | In an expression | "The thing `p` points to" |
| `p->field` | In an expression | Shorthand for `(*p).field` |

You'll need the middle two rows in Lesson 3, where the code hands the kernel `&usage` so it can fill that struct in. C system APIs (the ones the OS provides) use pointers everywhere, because C has no references.

### `std::optional` — "a value, or nothing"

```cpp
const auto info = m_reader->read(error);
if (!info) {                 // no value: the read failed
    emit readFailed(error);
    return;
}
m_alertEngine.evaluate(info->usedBytes, ...);   // -> reaches inside
emit sampleUpdated(*info, ...);                 // * gets the value out
```

Instead of returning a magic value like `-1` or an all-zero struct to mean "failed", the function returns `std::optional<SwapInfo>`. The type itself says "this might be empty", and you can't accidentally use a failed reading as if it were real. Inside the function, `return std::nullopt;` returns "nothing", and `return SwapInfo{...};` returns a value.

(`emit` is a Qt keyword that announces an event to whoever is listening. Lesson 9 explains it.)

### `static_cast` — explicit conversions

```cpp
return static_cast<qint64>(value.tv_sec) * 1000 + ...;
```

`static_cast<T>(x)` converts `x` to type `T`, and the compiler checks the conversion makes sense. You'll see it wherever numbers move between the OS's types and the app's own (`quint64`, `qint64`), and when converting an `enum class` to `int`. Writing it out makes every conversion easy to spot.

### `auto`

`auto x = expression;` lets the compiler infer the type. Used here mainly when the type is long or obvious from the right-hand side.

### Anonymous namespaces

```cpp
namespace {
int tierIndex(AlertTier tier) { return static_cast<int>(tier) - 1; }
}
```

Functions inside `namespace { ... }` are private to that one `.cpp` file. It's the C++ way to say "helper, not part of the public API".

---

## Lesson 2 — Reading swap on Linux (`/proc`)

**File:** `src/platform/linux/LinuxSwapReader.cpp → read()`

### Concept: "everything is a file"

Unix systems expose a surprising amount of kernel information as files. `/proc` is a **virtual filesystem**: nothing in it lives on disk. When you open `/proc/meminfo`, the kernel generates the text *at the moment you read it*.

This means a program can learn about memory using nothing more than "open a file and read lines" — no special API needed.

> **Try it 🐧**
> ```bash
> cat /proc/meminfo | grep -i swap
> ls -l /proc/meminfo          # size is 0! (see below)
> ```
> You'll see lines like `SwapTotal:  8388604 kB` and `SwapFree: 8100000 kB`.

### Concept: what is a system call?

Your program cannot touch hardware or kernel memory directly. To do anything privileged — open a file, read it, get the time — it asks the kernel through a **system call** (syscall). `QFile::open()` eventually becomes the `openat` syscall, and `readLine()` becomes `read`.

> **Try it 🐧** — watch the syscalls happen:
> ```bash
> strace -e trace=openat,read cat /proc/meminfo 2>&1 | grep -A1 meminfo
> ```
> `strace` prints every syscall a program makes. You'll see `openat(..., "/proc/meminfo", O_RDONLY)` and then `read(3, "MemTotal: ...", ...)`. The `3` is a **file descriptor** — a small integer the kernel hands you to refer to the open file.

### Walking through the code

```cpp
std::optional<SwapInfo> LinuxSwapReader::read(QString& errorMessage)
{
    QFile file(m_meminfoPath);                       // usually "/proc/meminfo"
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage = ...;
        return std::nullopt;                         // "no value" — see Lesson 1
    }
```

The path is stored in a member variable instead of hard-coded. Why? So tests can point the reader at a fake file with broken content (`tests/AlertEngineTests.cpp → linuxSwapReaderRejectsInvalidMeminfo`).

```cpp
    std::optional<quint64> totalKiB;
    std::optional<quint64> freeKiB;
    // procfs reports a zero file size, so QFile::atEnd() may be true before the first read.
    while (!totalKiB || !freeKiB) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isEmpty() && file.atEnd()) break;
```

**The zero-size gotcha.** `ls -l /proc/meminfo` showed size 0 because the content doesn't exist until you read it. Code that checks "am I at the end?" *before* reading would stop immediately. This loop reads first, then checks. This is a classic `/proc` bug, and the comment is there to stop someone "simplifying" it back.

```cpp
        const auto fields = line.split(' ');         // ["SwapTotal:", "8388604", "kB"]
        bool valid = false;
        const quint64 value = fields.value(1).toULongLong(&valid);
        if (fields.size() != 3 || fields.value(2) != "kB" || !valid
            || value > std::numeric_limits<quint64>::max() / 1024) {
            errorMessage = ...; return std::nullopt;
        }
```

**Defensive parsing.** Data that comes from outside your program is untrusted, even from the kernel. Each check guards against something:

| Check | Guards against |
|---|---|
| `fields.size() != 3` | A line with an unexpected format |
| `!= "kB"` | A unit we don't understand |
| `!valid` | Text that isn't a number |
| `value > max / 1024` | **Integer overflow** — we're about to multiply by 1024 |

The last one is subtle. `quint64` can hold at most ~1.8 × 10¹⁹. If `value × 1024` were bigger than that, the result would silently **wrap around** to a small number. Checking `value > max / 1024` *before* multiplying avoids that without needing a bigger type.

```cpp
    if (*freeKiB > *totalKiB) { errorMessage = ...; return std::nullopt; }
    const quint64 totalBytes = *totalKiB * 1024;
    const quint64 freeBytes  = *freeKiB  * 1024;
    return SwapInfo {totalBytes, totalBytes - freeBytes, freeBytes};
```

**Unsigned subtraction.** `quint64` is *unsigned* — it can't be negative. If `free > total`, then `total - free` wouldn't be negative; it would wrap to an enormous number like 18 446 744 073 709 551 000. The check above makes the subtraction safe.

> **Check yourself**
> 1. Why does `/proc/meminfo` show a size of 0?
> 2. What would happen if we removed the `free > total` check and the kernel reported nonsense?
>
> <details><summary>Answers</summary>
>
> 1. It's generated on demand by the kernel when read; there is no stored content to measure.
> 2. `totalBytes - freeBytes` would wrap around to a huge number, and the app would report an absurd amount of used swap and likely fire a Tier 3 alert.
> </details>

---

## Lesson 3 — Reading swap on macOS (`sysctl`)

**File:** `src/platform/macos/MacSwapReader.mm → read()`

### Concept: asking the kernel for a struct

macOS has no `/proc`. Instead it has `sysctl`: a kernel interface where you ask for a value by name and the kernel copies it straight into memory you provide. No text, no parsing.

> **Try it 🍎**
> ```bash
> sysctl vm.swapusage
> # vm.swapusage: total = 2048.00M  used = 1024.50M  free = 1023.50M  (encrypted)
> ```
> The command-line tool prints text, but the program gets a binary struct.

### Walking through the code

```cpp
xsw_usage usage {};                 // an empty struct; {} zero-fills every field
size_t size = sizeof(usage);        // how many bytes we have room for
if (sysctlbyname("vm.swapusage", &usage, &size, nullptr, 0) != 0) {
    errorMessage = QStringLiteral("Unable to read vm.swapusage: %1")
                       .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return std::nullopt;
}
```

This is the **classic C API pattern**, and you'll see it everywhere in systems code:

1. You allocate the memory (`usage` on the stack).
2. You pass a **pointer** to it (`&usage` = "the address of usage") and its size.
3. The function fills it in and returns `0` on success, `-1` on failure.
4. On failure, the reason is in a global variable called **`errno`**, and `strerror(errno)` turns it into a message like "Operation not permitted".

`&size` is also a pointer: the kernel may update it to say how many bytes it actually wrote. The two `nullptr, 0` arguments are for *setting* a value, which we don't do.

```cpp
return SwapInfo {
    static_cast<quint64>(usage.xsu_total),
    static_cast<quint64>(usage.xsu_used),
    static_cast<quint64>(usage.xsu_avail),
};
```

The `static_cast`s (Lesson 1) turn the macOS-specific field types into the portable `quint64` used everywhere else. After this line, nothing above the platform layer knows macOS was involved.

> **Try it 🍎** — write your own 10-line systems program:
> ```cpp
> // swap.cpp
> #include <sys/sysctl.h>
> #include <cerrno>
> #include <cstdio>
> #include <cstring>
>
> int main() {
>     xsw_usage u {};
>     size_t size = sizeof u;
>     if (sysctlbyname("vm.swapusage", &u, &size, nullptr, 0) != 0) {
>         std::printf("error: %s\n", std::strerror(errno));
>         return 1;
>     }
>     std::printf("used %llu of %llu bytes\n",
>         (unsigned long long)u.xsu_used, (unsigned long long)u.xsu_total);
> }
> ```
> ```bash
> c++ -std=c++20 swap.cpp -o swap && ./swap
> ```
> Now change `"vm.swapusage"` to `"vm.nope"` and see what `errno` says.

### Why not just run `sysctl` as a command?

You *could* launch `sysctl vm.swapusage` and parse its output. But that creates a whole new process every few seconds (expensive), and the text format can change between OS versions or languages. Calling the API directly is faster and returns typed data.

> **Check yourself**
> 1. In `sysctlbyname(..., &usage, &size, ...)`, who allocates the memory for `usage`?
> 2. Where do you look to find out *why* a C system function failed?
>
> <details><summary>Answers</summary>
>
> 1. The caller (our code). The kernel only writes into it.
> 2. `errno`, converted to text with `strerror(errno)`. Read it immediately — the next library call may overwrite it.
> </details>

---

## Lesson 4 — Telling time correctly (clocks)

**Files:** `src/core/IMonotonicClock.hpp`, `src/platform/linux/LinuxBootClock.cpp`, `src/platform/macos/MacContinuousClock.mm`

### Concept: there is more than one "now"

The app needs to measure durations: "has the 15-minute cooldown passed?", "is the snooze over?". The obvious approach — compare the current date/time — is wrong:

| Clock | What it measures | Problem for us |
|---|---|---|
| **Wall clock** (`time()`, `QDateTime`) | Calendar date and time | Jumps when the user changes time zone, NTP corrects it, or daylight saving starts |
| **Monotonic** (`CLOCK_MONOTONIC`, `mach_absolute_time`) | Time since some fixed point; never goes backwards | **Stops while the computer is asleep** on Linux and macOS |
| **Monotonic incl. sleep** (`CLOCK_BOOTTIME`, `mach_continuous_time`) | Time since boot, including suspended time | ✅ What we want |

If you close your laptop lid during a 1-hour snooze and open it 2 hours later, the snooze should be over. With plain monotonic time it wouldn't be.

### Linux: `clock_gettime`

```cpp
qint64 LinuxBootClock::nowMs() const
{
    timespec value {};
    if (clock_gettime(CLOCK_BOOTTIME, &value) != 0) return 0;
    return static_cast<qint64>(value.tv_sec) * 1000 + value.tv_nsec / 1'000'000;
}
```

Same pattern as Lesson 3: we own a struct, pass its address, the kernel fills it. `timespec` has two fields: whole seconds and the leftover nanoseconds. We convert both to milliseconds. (`1'000'000` — the `'` is just a digit separator for readability, allowed since C++14.)

> **Try it 🐧** — see how long your machine has been asleep:
> ```cpp
> // clocks.cpp
> #include <cstdio>
> #include <ctime>
> int main() {
>     timespec mono {}, boot {};
>     clock_gettime(CLOCK_MONOTONIC, &mono);
>     clock_gettime(CLOCK_BOOTTIME, &boot);
>     std::printf("monotonic: %ld s\nboottime:  %ld s\nslept:     %ld s\n",
>         (long)mono.tv_sec, (long)boot.tv_sec, (long)(boot.tv_sec - mono.tv_sec));
> }
> ```
> ```bash
> c++ -std=c++20 clocks.cpp -o clocks && ./clocks
> ```
> Suspend your laptop for a minute, wake it, run again. The `slept` number grows.

### macOS: Mach time and the timebase

```cpp
qint64 MacContinuousClock::nowMs() const
{
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t result {};
        mach_timebase_info(&result);
        return result;
    }();

    const auto nanoseconds = static_cast<__uint128_t>(mach_continuous_time())
        * timebase.numer / timebase.denom;
    return static_cast<qint64>(nanoseconds / 1'000'000);
}
```

Three things to learn here:

1. **Ticks, not nanoseconds.** `mach_continuous_time()` returns hardware **ticks**. To convert to nanoseconds you multiply by `numer / denom` from `mach_timebase_info`. On Intel Macs that's 1/1; on Apple Silicon it's 125/3 (one tick ≈ 41.67 ns). Code that assumes ticks are nanoseconds works on Intel and is silently wrong on M-series chips.
2. **Overflow again.** `ticks × 125` can exceed 64 bits after a long uptime, so the multiplication is done in a 128-bit integer (`__uint128_t`, a compiler extension).
3. **`static` local + lambda.** `[] { ... }()` defines a function with no name (a **lambda**) and immediately calls it. Marking the result `static` means it runs **once**, the first time `nowMs()` is called; every later call reuses the stored value. C++ guarantees that first-time initialisation is thread-safe.

### Why an interface?

`SwapMonitor` doesn't call either function directly. It asks an `IMonotonicClock`. In tests, a `FakeClock` whose time you set by hand is passed instead, so a test can "wait 15 minutes" instantly. See Lesson 6.

---

## Lesson 5 — Processes and signals

**Files:** `src/platform/linux/LinuxProcessService.cpp`, `src/platform/macos/MacProcessService.mm`, `src/core/ProcessGrouping.cpp`

When swap is critical, the app shows which applications use the most memory and lets you quit them. That requires understanding processes.

### Concept: what a process is

A **process** is a running program. The kernel tracks, for each one:

- **PID** — process ID, a number.
- **PPID** — parent PID. Every process is started by another one, so processes form a **tree**.
- **UID** — the user who owns it. You may only signal your own processes (unless you are root).
- Its memory, open files, environment variables, and more.

> **Try it** (both OSes)
> ```bash
> ps -o pid,ppid,uid,rss,comm -u "$USER" | head -20
> echo "my shell is PID $$, my parent is $PPID"
> ```
> `rss` is **resident set size**: how much of the process's memory is currently in RAM, in KiB.

> **Try it 🐧** — every process has a folder in `/proc`:
> ```bash
> ls /proc/$$                          # $$ = your shell's PID
> grep -E 'Uid|PPid|VmRSS' /proc/$$/status
> ls -l /proc/$$/exe                   # a symlink to the program's binary
> tr '\0' '\n' < /proc/$$/environ | head   # environment variables, NUL-separated
> ```

### Listing processes on Linux

`LinuxProcessService.cpp → inventory()` lists `/proc`, keeps only the entries whose names are numbers (those are PIDs), and calls `readProcess()` for each. `readProcess()` reads exactly the files you just looked at:

| Source | What we take | Why |
|---|---|---|
| `/proc/<pid>/status` → `Uid:` | Owner | Skip other users' processes |
| `/proc/<pid>/status` → `PPid:` | Parent | Build the tree |
| `/proc/<pid>/status` → `VmRSS:` | Memory in RAM | Rank by memory use |
| `/proc/<pid>/exe` (symlink) | Program name | Match to an installed app, protect system processes |
| `/proc/<pid>/environ` | `GIO_LAUNCHED_DESKTOP_FILE`, `FLATPAK_ID`… | Figure out which app launched it |

Notice it calls `status.readAll()` first and then splits into lines — the same zero-size `/proc` gotcha from Lesson 2.

Many reads will fail (processes exit between listing and reading, or belong to other users). The code simply `continue`s past them. Processes appearing and disappearing while you look is normal.

### Listing processes on macOS

macOS uses two APIs:

- `NSWorkspace.runningApplications` — Apple's list of *GUI* apps (the ones in the Dock).
- `libproc` — lower level: `proc_listallpids()` to get every PID and `proc_pidinfo(..., PROC_PIDTBSDINFO, ...)` to read owner and parent. `proc_pid_rusage()` gives `ri_phys_footprint`, the same "physical memory" number Activity Monitor shows.

`proc_listallpids` is called twice: first with `nullptr` to ask "how many are there?", then with a buffer of that size (+32 spare, because processes may start in between). This **ask-size-then-fill** pattern is common in C APIs.

### Grouping: walking a tree

Chrome, VS Code, Slack and friends run as one main process plus many helpers. To show "Chrome uses 3 GB" instead of 40 separate rows, `core/ProcessGrouping.cpp → groupApplicationProcesses()` sums each app's process **subtree**.

It's a **breadth-first search** (BFS):

```cpp
QQueue<qint64> pending;
pending.enqueue(root.processId);                 // start at the app's main PID
// (also enqueue helpers whose bundle ID is "com.google.Chrome.*")

while (!pending.isEmpty()) {
    const qint64 processId = pending.dequeue();
    if (included.contains(processId) || /* another app's root */) continue;
    included.insert(processId);
    memoryBytes += snapshot->memoryBytes;
    for (qint64 child : childrenByParent.values(processId))
        pending.enqueue(child);                  // visit children next
}
```

`childrenByParent` is a `QMultiHash` — a map where one key (a parent PID) can have many values (child PIDs). It's built once up front so finding children is fast. `included` prevents visiting the same PID twice.

This file is in `core/`, not `platform/`: it takes plain data (`ProcessSnapshot`) and has no OS calls, so it is tested directly in `tests/ProcessGroupingTests.cpp`.

### Concept: signals — asking a process to stop

On Unix you stop a process by sending it a **signal**, a small numbered notification delivered by the kernel. The two that matter here:

| Signal | Number | What happens |
|---|---|---|
| `SIGTERM` | 15 | "Please exit." The program can catch it, save work, clean up, then exit. Or ignore it. |
| `SIGKILL` | 9 | The kernel destroys the process immediately. It can't be caught. Unsaved work is lost. |

> **Try it** (both OSes) — see the difference yourself:
> ```bash
> bash -c 'trap "echo got SIGTERM, ignoring it" TERM; while true; do sleep 1; done' &
> PID=$!
> kill -TERM $PID      # prints the message; the process keeps running
> kill -KILL $PID      # gone, no message — it never got a chance
> ```

This is exactly why the cleanup window only enables **Force Quit** after a normal **Quit** was tried, and asks for confirmation (`src/ui/CleanupDialog.cpp`).

On Linux the app calls the `kill` syscall directly:

```cpp
if (::kill(static_cast<pid_t>(processId), force ? SIGKILL : SIGTERM) != 0) {
    errorMessage = ... std::strerror(errno) ...;
```

(`::kill` with leading `::` means "the global `kill` from the C library", not some other `kill` in scope.) On macOS, `NSRunningApplication.terminate` sends the app a polite "quit" Apple Event — the same as choosing *Quit* from its menu — and `forceTerminate` is the `SIGKILL` equivalent.

### Concept: check-then-act races (and PID reuse)

PIDs are recycled. Imagine:

1. The window lists "Firefox, PID 4242".
2. You go make coffee. Firefox exits. A new process starts and happens to get PID 4242.
3. You click *Quit*. We'd signal the wrong process.

This class of bug is called **TOCTOU** (time-of-check to time-of-use). `terminate()` defends against it by **re-checking everything immediately before acting**: re-reads the owner, re-checks the protected list, and on Linux re-runs the full inventory to confirm PID 4242 is still an eligible app root. A tiny window remains between the last check and `kill()`; modern Linux offers `pidfd_open` / `pidfd_send_signal` to close it completely (see Exercises).

> **Check yourself**
> 1. Why are the protected lists (`systemd`, `gnome-shell`, `Finder`, `Dock`…) needed if we already filter to the current user?
> 2. Why is grouping in `core/` but listing in `platform/`?
>
> <details><summary>Answers</summary>
>
> 1. Your desktop shell, window manager, and Finder run as *you*. Killing them would wreck your session, so ownership alone isn't a sufficient safety check.
> 2. Listing needs OS APIs; grouping is pure logic on plain data. Keeping it pure means it's shared by both OSes and testable without real processes.
> </details>

---

## Lesson 6 — Interfaces, virtual functions, and dependency injection

**Files:** `src/core/ISwapReader.hpp`, `src/platform/*.hpp`, `tests/SwapMonitorTests.cpp`

### The problem

`SwapMonitor` needs to read swap. But we have two very different readers (Linux and macOS), and in tests we want a third that returns whatever we tell it. How can `SwapMonitor` work with all three without knowing which one it has?

### An interface in C++

C++ has no `interface` keyword. You write a class with only **pure virtual** functions:

```cpp
// src/core/ISwapReader.hpp
class ISwapReader {
public:
    virtual ~ISwapReader() = default;
    virtual std::optional<SwapInfo> read(QString& errorMessage) = 0;
};
```

- `virtual` — the actual function called is chosen **at runtime**, based on the real type of the object.
- `= 0` — "no implementation here; subclasses must provide one". A class with any `= 0` function is **abstract** and can't be created directly.
- `virtual ~ISwapReader()` — a virtual destructor. Without it, destroying a `MacSwapReader` through an `ISwapReader*` would only run the base destructor. Any class meant to be used through a base pointer needs one.

An implementation:

```cpp
// src/platform/macos/MacSwapReader.hpp
class MacSwapReader final : public ISwapReader {
public:
    std::optional<SwapInfo> read(QString& errorMessage) override;
};
```

- `: public ISwapReader` — inherits from (implements) the interface.
- `override` — asks the compiler to check this really overrides a base function. A typo in the signature becomes a compile error instead of a silent bug.
- `final` — nothing may inherit from this class further.

> **How it works underneath:** every object of a class with virtual functions carries a hidden pointer to a **vtable** — a table of function addresses for its real type. Calling `reader->read()` looks up the address in that table. That's one extra memory lookup per call — negligible here.

### Dependency injection

`SwapMonitor` doesn't create its reader. It's **handed** one:

```cpp
// src/core/SwapMonitor.hpp
SwapMonitor(std::unique_ptr<ISwapReader> reader, SettingsStore& settings,
    std::unique_ptr<IMonotonicClock> clock, QObject* parent = nullptr);
```

`main.cpp` chooses the real implementation for the current OS:

```cpp
SwapMonitor monitor(std::make_unique<PlatformSwapReader>(), settings,
    std::make_unique<PlatformClock>());
```

…and `PlatformSwapReader` is just a type alias picked at compile time:

```cpp
#if defined(Q_OS_MACOS)
using PlatformSwapReader = MacSwapReader;
#elif defined(Q_OS_LINUX)
using PlatformSwapReader = LinuxSwapReader;
#endif
```

### The payoff: fakes in tests

```cpp
// tests/SwapMonitorTests.cpp
class FakeSwapReader final : public ISwapReader {
public:
    std::optional<SwapInfo> read(QString& errorMessage) override {
        if (shouldFail) { errorMessage = "Synthetic read failure"; return std::nullopt; }
        return info;
    }
    SwapInfo info {400, 0, 400};
    bool shouldFail = false;
};
```

The test can now say "pretend 100 bytes of swap are used, then pretend 60 seconds have passed" — no real memory pressure and no waiting.

All the other OS features use the same pattern: `INotificationService`, `IProcessService`, `IAutostartService`, `ISystemEventMonitor` (in `src/platform/`). This architecture is often called **ports and adapters**: `core/` defines the ports (interfaces), `platform/` provides adapters.

> **Check yourself** — What would go wrong if `SwapMonitor` did `m_reader = std::make_unique<MacSwapReader>()` itself?
>
> <details><summary>Answer</summary>
>
> `core/` would depend on macOS code, so it wouldn't compile on Linux, and tests couldn't substitute a fake.
> </details>

---

## Lesson 7 — Ownership and object lifetime

C++ has no garbage collector. Every object has an exact moment it is created and an exact moment it is destroyed, and you need to know both.

### Stack objects: destroyed at the closing brace

Look at `main()`:

```cpp
SettingsStore settings;
PlatformAutostartService autostartService;
...
SwapMonitor monitor(..., settings, ...);
TrayController tray(settings);
...
return application.exec();
}   // ← everything above is destroyed here, in REVERSE order
```

These are **stack** (automatic) objects. They live until `main` returns, then are destroyed last-created-first. That order matters: `monitor` holds a reference to `settings`, so `settings` must outlive `monitor`. Declaring `settings` first guarantees it.

### RAII: cleanup tied to lifetime

**RAII** ("Resource Acquisition Is Initialization") means: a resource (a file, a lock, a timer, an OS observer) is owned by an object, and the object's **destructor** releases it. You can't forget to clean up, because destruction is automatic.

Examples in the repo:

- `QFile file(path);` in `LinuxSwapReader::read()` — the file is closed when `file` goes out of scope, even on the early `return std::nullopt` paths.
- `const QMutexLocker locker(&loggerMutex);` in `DiagnosticLogger.cpp` — the lock is released at the closing `}`.
- `MacSystemEventMonitor::~MacSystemEventMonitor() { stop(); }` — unsubscribes from macOS notifications when destroyed.

### `std::unique_ptr` and `std::move`: single ownership on the heap

Sometimes an object must live on the **heap** (dynamic memory) — for example because we only know its concrete type at runtime, as with `ISwapReader`. `std::unique_ptr<T>` is a pointer that **owns** the object and deletes it automatically in its own destructor.

"Unique" means only one `unique_ptr` can own it. You can't copy it, only **move** it:

```cpp
SwapMonitor::SwapMonitor(std::unique_ptr<ISwapReader> reader, ...)
    : m_reader(std::move(reader))    // ownership transfers into the member
```

After `std::move`, the parameter `reader` is empty and `m_reader` owns the object. When `SwapMonitor` is destroyed, `m_reader` deletes the reader. No `delete` appears anywhere in this codebase.

The `: m_reader(...), m_clock(...)` part is a **member initializer list** — it constructs members directly rather than default-constructing and then assigning.

The tests show one more trick:

```cpp
auto reader = std::make_unique<FakeSwapReader>();
auto* readerControl = reader.get();          // a non-owning "borrowed" pointer
SwapMonitor monitor(std::move(reader), ...); // monitor now owns it
readerControl->info = {400, 100, 300};       // test still pokes it via the borrowed pointer
```

That works because `monitor` (and thus the reader) outlives every use of `readerControl`.

### References as members

`SettingsStore& m_settings;` in `SwapMonitor` is a reference member: the monitor *uses* the settings but doesn't own them. This is safe only because `main()` guarantees `settings` outlives `monitor`.

### The pimpl idiom

`LinuxSystemEventMonitor.hpp` declares only `class LinuxSystemEventMonitorImpl;` (no body) and holds a `std::unique_ptr<LinuxSystemEventMonitorImpl>`. The real class, with all its D-Bus types, lives in the `.cpp`. This **pointer-to-implementation** ("pimpl") keeps heavy or platform-specific types out of the header, so code that includes the header doesn't need to know about D-Bus at all. `MacSystemEventMonitor` does the same with `struct Impl` to keep Objective-C types out of a C++ header.

---

## Lesson 8 — Meet Qt

**Files:** `src/ui/WarningDialog.cpp`, `src/ui/TrayController.cpp`, `src/core/SettingsStore.cpp`, `CMakeLists.txt`

You've been reading Qt code since Lesson 0's cheat sheet. Every name with a `Q` prefix (`QString`, `QFile`, `QTimer`, `QDialog`) comes from **Qt** (pronounced "cute"). This lesson explains what Qt is, how it's organised, and the handful of ideas you need to read any Qt code.

### What Qt is

C++ itself has no windows, buttons, tray icons, or settings files. The standard library gives you strings, containers and files, but nothing for a desktop app. Qt is a large C++ library that fills that gap, and it does so **across platforms**: the same `QSystemTrayIcon` code puts an icon in the macOS menu bar and in a Linux system tray.

That's why most of this app is shared between macOS and Linux. Only the `platform/` folder needed per-OS code, for things Qt doesn't cover: reading swap, listing processes, native notifications, sleep events.

Qt is split into **modules**. You only link the ones you use (see `find_package(Qt6 ... COMPONENTS ...)` in `CMakeLists.txt`):

| Module | What it gives this app | Examples |
|---|---|---|
| **Qt Core** | Non-GUI foundations | `QObject`, `QString`, `QTimer`, `QFile`, `QSettings`, `QHash` |
| **Qt Gui** | Drawing and images (pulled in by Widgets) | `QPainter`, `QPixmap`, `QIcon`, `QColor` |
| **Qt Widgets** | Classic desktop UI controls | `QDialog`, `QPushButton`, `QTableWidget`, `QSystemTrayIcon` |
| **Qt DBus** | D-Bus client (Linux only here) | `QDBusConnection`, `QDBusInterface` |
| **Qt Test** | Unit-testing framework | `QCOMPARE`, `QSignalSpy` |

> **Try it** — your first Qt program, outside this repo. Make an empty folder with two files:
>
> `CMakeLists.txt`
> ```cmake
> cmake_minimum_required(VERSION 3.24)
> project(HelloQt LANGUAGES CXX)
> set(CMAKE_CXX_STANDARD 20)
> find_package(Qt6 REQUIRED COMPONENTS Widgets)
> qt_standard_project_setup()
> qt_add_executable(hello main.cpp)
> target_link_libraries(hello PRIVATE Qt6::Widgets)
> ```
>
> `main.cpp`
> ```cpp
> #include <QApplication>
> #include <QLabel>
> #include <QPushButton>
> #include <QVBoxLayout>
> #include <QWidget>
>
> int main(int argc, char* argv[])
> {
>     QApplication app(argc, argv);
>
>     QWidget window;                                    // top-level window, on the stack
>     auto* label = new QLabel("Clicked 0 times", &window);
>     auto* button = new QPushButton("Click me", &window);
>     auto* layout = new QVBoxLayout(&window);
>     layout->addWidget(label);
>     layout->addWidget(button);
>
>     int clicks = 0;
>     QObject::connect(button, &QPushButton::clicked, label, [&] {
>         label->setText(QString("Clicked %1 times").arg(++clicks));
>     });
>
>     window.show();
>     return app.exec();
> }
> ```
> ```bash
> cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"   # 🍎; on 🐧 drop the -D part
> cmake --build build && ./build/hello
> ```
> These 20 lines use every idea in this lesson: `QApplication`, widgets, parent/child ownership, layouts, a signal connected to a lambda, and the event loop. Keep this project for experiments; it builds in seconds, unlike the real app.

### `QObject`: the base of (almost) everything

Most Qt classes inherit from `QObject`. Being a `QObject` gives a class three abilities:

1. **Signals and slots** (Lesson 9).
2. **A parent and children** (next section).
3. **Runtime type information** — Qt can look up a class's name, its signals, and its properties while the program runs.

To get these, a class inherits from `QObject` *and* puts the `Q_OBJECT` macro at the top:

```cpp
// src/core/SettingsStore.hpp
class SettingsStore final : public QObject {
    Q_OBJECT
public:
    ...
signals:
    void changed();
};
```

`signals:` isn't standard C++. Plain C++ can't produce the signal plumbing or the type information by itself, so Qt ships a code generator called **moc** (meta-object compiler). Before compiling, moc reads every header containing `Q_OBJECT` and writes an extra `.cpp` file implementing the missing parts. `set(CMAKE_AUTOMOC ON)` in `CMakeLists.txt` makes CMake run moc for you.

> **Try it** — look at what moc generates:
> ```bash
> ls build/SwapAlert_autogen/*/moc_SwapMonitor.cpp
> grep -n "sampleUpdated" build/SwapAlert_autogen/*/moc_SwapMonitor.cpp
> ```
> You'll find a real C++ function body for `SwapMonitor::sampleUpdated`. You never wrote it; moc did. Signals are ordinary member functions that moc implements for you.

Two more things to know about `QObject`s:

- **They can't be copied.** A `QObject` has an identity (connections, a parent, children), and copying it wouldn't make sense. That's why `main.cpp` passes them around by reference (`SettingsStore&`) or pointer, never by value.
- **`Q_DECLARE_METATYPE(SwapInfo)`** (in `SwapInfo.hpp`) is for *your own* non-`QObject` types. It lets Qt store a `SwapInfo` inside a `QVariant`, which is needed when a signal carrying it is queued between threads or recorded by `QSignalSpy` in tests.

### Parent/child ownership: Qt's answer to "who deletes this?"

In Lesson 7 you learned that `std::unique_ptr` deletes what it owns. Qt has a second ownership system, and you'll see it on almost every UI line:

```cpp
// src/ui/WarningDialog.cpp
auto* reviewButton = new QPushButton(QStringLiteral("Review Applications"), this);
```

That's a raw `new` with no matching `delete` anywhere. Isn't that a leak? No. The last argument, `this`, makes the dialog the button's **parent**. Every `QObject` keeps a list of its children, and **when a parent is destroyed, it deletes all its children**. The objects form a tree:

```text
WarningDialog  (owned by main(), on the stack)
├── QLabel m_titleLabel
├── QLabel m_usageLabel
├── QLabel m_explanationLabel
├── QPushButton "Review Applications"
├── QPushButton "Snooze 15 Minutes"
├── QPushButton "Dismiss"
└── QVBoxLayout
    └── QHBoxLayout buttons
```

When `main()` returns, `warningDialog` is destroyed and takes the whole tree with it. The tree is also searchable: the UI tests call `dialog.findChild<QPushButton*>("testTier1Button")` to find a button by its `objectName`, without the dialog exposing it.

Rules of thumb for reading Qt code:

| You see | Who deletes it |
|---|---|
| `new QThing(..., this)` / `new QThing(parent)` | The parent, automatically |
| `QThing thing;` on the stack | Its scope, as usual (Lesson 7) |
| `std::unique_ptr<Thing>` | The `unique_ptr` (used here for non-`QObject` types like `ISwapReader`) |
| `new QThing` with **no parent** | **You must delete it yourself** |

The last row really happens in this codebase, and it's a good puzzle:

```cpp
// src/ui/TrayController.cpp
TrayController::TrayController(SettingsStore& settings, QObject* parent)
    : QObject(parent)
    , m_trayIcon(new QSystemTrayIcon(this))   // parent = this ✔
    , m_menu(new QMenu)                       // no parent! ✘
...
TrayController::~TrayController()
{
    m_trayIcon->setContextMenu(nullptr);
    delete m_menu;                            // so we delete it by hand
}
```

Why not `new QMenu(this)`? `QMenu` is a **widget**, and a widget's parent must also be a widget, because a child widget is drawn *inside* its parent. `TrayController` is a plain `QObject`, not a widget, so it can't be a widget's parent. The menu is created without one and deleted explicitly. Detaching it from the tray icon first means the icon never holds a pointer to a deleted menu.

> **Common bug:** giving a *stack* object a parent that is destroyed before it. The parent deletes it, then its scope ends and it's destroyed a second time → crash. Either allocate children with `new`, or make sure the parent outlives the stack object.

### Widgets and layouts

A **widget** is anything visible: a window, a label, a button, a table. A widget with no parent becomes a **top-level window**. Child widgets live inside it.

You almost never position widgets with pixel coordinates. Instead you put them in a **layout**, which sizes and arranges them automatically and adapts when the window is resized or the font is bigger:

```cpp
// src/ui/WarningDialog.cpp
auto* buttons = new QHBoxLayout;          // left-to-right row
buttons->addWidget(snoozeButton);
buttons->addStretch();                    // flexible empty space: pushes the rest to the right
buttons->addWidget(dismissButton);
buttons->addWidget(reviewButton);

auto* layout = new QVBoxLayout(this);     // top-to-bottom; "this" = install on the dialog
layout->addWidget(m_titleLabel);
layout->addWidget(m_usageLabel);
layout->addWidget(m_explanationLabel);
layout->addSpacing(8);                    // fixed 8 px gap
layout->addLayout(buttons);               // nest the row inside the column
```

Result:

```text
┌──────────────────────────────────────────────┐
│ High swap usage                              │
│ Currently using 4.20 GB of 8.00 GB swap.     │
│ High swap usage can make the system feel ... │
│                                              │
│ [Snooze 15 Minutes]     [Dismiss] [Review…]  │
└──────────────────────────────────────────────┘
```

Other layouts in the repo: `QFormLayout` in `SettingsDialog.cpp` (two-column "Label: control" rows) and `QDialogButtonBox`, which places Save/Cancel in the order each OS expects (macOS and Linux desktops order them differently).

This app builds its UI **in code**. Qt also has a visual designer (Qt Designer, `.ui` files) and a separate declarative UI language (QML / Qt Quick). Both are worth knowing about; neither is used here.

### Dialogs: `show()` vs modal

Look at how the app opens its windows:

```cpp
// main.cpp → showSettings
settingsDialog.show();
settingsDialog.raise();           // bring to front
settingsDialog.activateWindow();  // give it keyboard focus
```

`show()` is **non-modal**: it displays the window and returns immediately. The event loop keeps running, so the monitor keeps sampling while Settings is open. The dialogs are created once in `main()` and only shown and hidden; closing a window just hides it.

By contrast, `QMessageBox::warning(...)` in `CleanupDialog.cpp` is **modal**: it blocks the parent window and doesn't return until the user clicks a button. Internally it runs a *nested* event loop, so timers still fire meanwhile. That's right for "Force quit these applications?", where you want an explicit yes or no before continuing.

### The tray icon: `QSystemTrayIcon`, `QMenu`, `QAction`

`TrayController` is the app's main UI. Its parts:

- **`QSystemTrayIcon`** — the icon itself, in the macOS menu bar or the Linux tray.
- **`QMenu`** — the dropdown that appears when you click it.
- **`QAction`** — one menu item. Actions emit `triggered` when clicked. A **checkable** action (`setCheckable(true)`, used for "Monitoring Enabled") has an on/off state and emits `toggled(bool)`.

The coloured dot is **drawn at runtime** instead of loaded from an image file:

```cpp
QIcon statusIcon(AlertTier tier)
{
    QPixmap pixmap(36, 36);                         // an empty 36×36 image
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);                      // draws onto the pixmap
    painter.setRenderHint(QPainter::Antialiasing);  // smooth edges
    painter.setPen(Qt::NoPen);                      // no outline
    painter.setBrush(colorForTier(tier));           // fill colour
    painter.drawEllipse(6, 6, 24, 24);
    return QIcon(pixmap);
}
```

`QPainter` is Qt's 2D drawing API. The same calls draw onto widgets, images or PDFs.

### Breaking feedback loops with `QSignalBlocker`

There are two ways to toggle monitoring: the tray's checkbox and the Settings dialog, and both must stay in sync. When settings change, the tray updates its checkbox:

```cpp
connect(&m_settings, &SettingsStore::changed, this, [this] {
    const QSignalBlocker blocker(m_monitoringAction);
    m_monitoringAction->setChecked(m_settings.monitoringEnabled());
    ...
});
```

Without the blocker, here's what happens when you untick "Enable monitoring" in Settings:

```text
Settings saves → changed() → tray calls setChecked(false)
  → QAction emits toggled(false) → main.cpp: setMonitoringEnabled(false)
    → writes the same value again → changed() a second time
```

It stops there (`toggled` only fires when the value actually changes), but the setting is written twice and every listener runs twice. `QSignalBlocker` mutes the action's signals until `blocker` goes out of scope — RAII again. Use it whenever you update a widget *from* your data and don't want the widget to echo the change back.

### Qt's own types vs the standard library

The code mixes `QString` with `std::optional`, and `QVector` with `std::array`. Qt is older than much of modern C++, so it has its own versions of many things:

| Qt | Standard C++ | Notes |
|---|---|---|
| `QString` | `std::string` | **Unicode text** (stored as UTF-16). Has `.arg()` for formatting. |
| `QByteArray` | `std::string` / `std::vector<char>` | **Raw bytes**. Used when parsing `/proc` files, which are bytes, not user-facing text. |
| `QVector` / `QList` | `std::vector` | In Qt 6 these are the same class. |
| `QHash`, `QMultiHash`, `QSet` | `std::unordered_map`, `unordered_multimap`, `unordered_set` | |
| `quint64`, `qint64` | `std::uint64_t`, `std::int64_t` | |

The rule this codebase follows: **use Qt types for data you hand to Qt APIs** (text for labels, lists for tables), and **standard types where Qt has no equivalent or where it's about ownership** (`std::optional`, `std::unique_ptr`, `std::function`, `std::array`).

Two string habits you'll see everywhere:

```cpp
QStringLiteral("Swap: %1 of %2").arg(formatBytes(info.usedBytes), formatBytes(info.totalBytes))
```

- `QStringLiteral("...")` builds the string at **compile time**, avoiding a conversion every time the line runs.
- `.arg()` replaces `%1`, `%2`… in order. It's safer than gluing strings with `+`, and translators can reorder the placeholders.

**Implicit sharing.** Copying a `QString`, `QByteArray` or `QVector` is cheap: the copy shares the same buffer until one of them is *modified*, and only then makes its own copy. This is called **copy-on-write**, and it's why Qt code often passes containers by value without worrying about cost.

### Cross-platform helpers used in this app

Much of Qt's value is small things that differ per OS, handled once for you:

| Class | Used in | What it hides |
|---|---|---|
| `QSettings` | `SettingsStore.cpp` | Where and how to store preferences |
| `QStandardPaths` | `DiagnosticLogger.cpp` | "Where do app data and config files go on this OS?" |
| `QFile`, `QSaveFile`, `QDir` | Linux platform code, logger | File I/O and paths (Lesson 12) |
| `QDesktopServices::openUrl` | `main.cpp` (Open Logs Folder) | Opens Finder or the Linux file manager |
| `QTimer::singleShot` | `CleanupDialog.cpp` | "Run this once in 1.5 s" without a timer member |
| `QApplication::alert` | `WarningDialog.cpp` | Bounces the Dock icon / flashes the taskbar entry |

`SettingsStore` is a thin wrapper around `QSettings`:

```cpp
m_settings(QSettings::IniFormat, QSettings::UserScope, "SwapAlert", "SwapAlert")
...
return m_settings.value("alerts/cooldownMinutes", 15).toInt();   // 15 if never saved
```

`value(key, default)` returns a `QVariant`, a box that can hold any type, and `.toInt()` unboxes it. `IniFormat` means a plain text file you can read.

> **Try it** — open the app's settings file (after saving something in Settings):
> ```bash
> cat ~/.config/SwapAlert/SwapAlert.ini
> ```
> You'll see `[alerts]`, `[monitoring]` and `[thresholds]` sections: the `/` in `"alerts/cooldownMinutes"` became an INI section. Quit the app, edit a value, start it again, and watch the dashboard pick it up.

### Two ways to write `connect`

Most of the code uses the modern form:

```cpp
connect(&m_timer, &QTimer::timeout, this, &SwapMonitor::refreshNow);
```

The arguments are real C++ function pointers, so a typo or a mismatched argument type is a **compile error**.

`LinuxSystemEventMonitor.cpp` uses the older, string-based form:

```cpp
systemBus.connect(..., QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)));
```

`SLOT(...)` turns the function signature into a *string* that is looked up at runtime. A typo compiles fine and only prints a warning when the program runs. It's used here because the signal belongs to *another process* on D-Bus, which C++ can't know about at compile time. For that string lookup to work, the class declaring `prepareForSleep` needs `Q_OBJECT` and a `private slots:` section. And because that class lives in a `.cpp` file instead of a header, the file ends with `#include "LinuxSystemEventMonitor.moc"` so moc's generated code gets compiled in.

### Qt Test in one minute

The tests in `tests/` use Qt's own test framework:

```cpp
class UiSafetyTests final : public QObject {
    Q_OBJECT
private slots:                       // each private slot is one test case
    void testButtonsEmitEveryTier();
    ...
};

QSignalSpy spy(&dialog, &SettingsDialog::testAlertRequested);  // records every emission
QCOMPARE(spy.count(), 3);                                      // prints both values on failure
QTRY_VERIFY_WITH_TIMEOUT(forceButton->isEnabled(), 2500);      // runs the event loop until true or timeout
QTEST_MAIN(UiSafetyTests)                                      // generates main()
```

`QStandardPaths::setTestModeEnabled(true)` (in `initTestCase()`) redirects settings and data paths to a test location, so running the tests never overwrites your real preferences.

> **Check yourself**
> 1. In `WarningDialog`'s constructor, several objects are created with `new` and none are deleted. Why isn't that a leak?
> 2. Why is `m_menu` deleted by hand in `~TrayController()`?
> 3. What's the difference between what a `QString` and a `QByteArray` hold?
>
> <details><summary>Answers</summary>
>
> 1. They're all created with the dialog (`this`) as their parent, so the dialog deletes them when it is destroyed.
> 2. `QMenu` is a widget and can only have a widget parent. `TrayController` isn't a widget, so the menu has no parent and nothing else would delete it.
> 3. `QString` holds Unicode text; `QByteArray` holds raw bytes with no encoding assumed.
> </details>

---

## Lesson 9 — The event loop, timers, signals and slots

**Files:** `src/app/main.cpp`, `src/core/SwapMonitor.cpp`

### Concept: the event loop

A GUI program spends most of its life waiting. The last line of `main()` is:

```cpp
return application.exec();
```

`exec()` runs the **event loop**, which is essentially:

```text
while (app is running) {
    wait for the next event (mouse click, timer expired, OS notification...)
    run the code that handles it
}
```

Everything after startup — timer ticks, menu clicks, alerts — happens *inside* this loop, one event at a time, on the **main thread**. Because handlers run one at a time, most of this app needs no locks.

`setQuitOnLastWindowClosed(false)` is crucial for a tray app: by default Qt leaves the loop when the last window closes, which would kill the monitor when you close Settings.

### Timers

```cpp
// SwapMonitor constructor
connect(&m_timer, &QTimer::timeout, this, &SwapMonitor::refreshNow);
```

A `QTimer` doesn't create a thread. It asks the event loop "wake me in N ms". When that time comes, the loop emits `timeout`, which calls `refreshNow()`. Reading one sysctl value or one small file takes microseconds, so doing it on the main thread doesn't freeze the UI. If it took seconds, we'd need a background thread.

### Signals and slots

A **signal** is an announcement ("a new sample arrived"). A **slot** is any function that should run when it's announced. `connect` links them:

```cpp
// SwapMonitor.hpp
signals:
    void sampleUpdated(const SwapInfo& info, AlertTier tier);

// SwapMonitor.cpp
emit sampleUpdated(*info, evaluation.currentTier);

// main.cpp
QObject::connect(&monitor, &SwapMonitor::sampleUpdated, &tray, &TrayController::updateSample);
QObject::connect(&monitor, &SwapMonitor::sampleUpdated, &statusDialog, &StatusDialog::updateSample);
```

The monitor doesn't know the tray or dashboard exist. It just emits. `main.cpp` decides who listens. This is the **observer pattern**, and it's why you could add a new listener (say, a history graph) without touching `SwapMonitor`.

Remember from Lesson 8 that `emit sampleUpdated(...)` calls an ordinary function generated by moc. That function walks the list of connected slots and calls each one. When sender and receiver are on the same thread, the slots run immediately, before `emit` returns.

### Lambdas as slots

`main.cpp` connects many signals to **lambdas** — inline unnamed functions:

```cpp
const auto showDashboard = [&] {
    statusDialog.show();
    statusDialog.raise();
    statusDialog.activateWindow();
};
QObject::connect(&tray, &TrayController::dashboardRequested, &statusDialog, showDashboard);
```

The `[&]` is the **capture list**: "this lambda may use local variables of the enclosing function by reference". It's safe here only because all those locals live until `main()` returns. Capturing locals by reference in a lambda that outlives them is one of the most common C++ crashes.

In `presentAlert`, the inner lambda uses `[&, tier, info]`: everything by reference, *except* `tier` and `info`, which are **copied**. They're copied because that callback may run later, after the outer lambda has returned and its parameters are gone.

### When threads do appear

On macOS, Apple's notification APIs call you back **on a background thread**. Touching Qt widgets from there is not allowed. `MacNotificationService.mm → completeOnMain()` fixes this:

```cpp
QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(completion),
    Qt::QueuedConnection);
```

`Qt::QueuedConnection` means "put this function on the main thread's event queue and run it there later". This is the standard way to move work back to the UI thread.

---

## Lesson 10 — The alert state machine

**File:** `src/core/AlertEngine.cpp → evaluate()`

This is the "brain" of the app, and it contains no OS code at all. It takes two numbers — used bytes and current time — and decides what to do.

### Why not just `if (used > threshold) alert();`?

Because the timer fires every 10 seconds. If swap sits above 4 GB for an hour, you'd get 360 identical alerts. And if usage hovers around 4.0 GB — 4.01, 3.99, 4.02 — you'd get an alert every time it crosses. The engine solves this with three ideas:

| Idea | Rule | Solves |
|---|---|---|
| **Armed** | Each tier fires once, then is disarmed until re-armed | Repeating every tick |
| **Hysteresis** | A tier re-arms (and the displayed tier drops) only after usage falls **15% below** its threshold | Flapping around a threshold |
| **Cooldown** | Even if re-armed, a tier can't fire again within 15 minutes of its last alert | Rapid up-down-up cycles |

It also has one more rule: if usage jumps straight from Normal to Tier 3, it fires **only Tier 3**, and marks Tiers 1 and 2 as already handled.

### The state

```cpp
AlertTier m_currentTier = AlertTier::Normal;
std::array<bool, 3>   m_armed {true, true, true};
std::array<qint64, 3> m_lastTriggeredMs {-1, -1, -1};   // -1 = never fired
```

That's all the memory the engine has. A **state machine** is just: some state + a function that takes an input and the current state and produces an output and a new state.

### Trace it by hand

Let's use small numbers (the tests do this too): thresholds **100 / 200 / 300**, reset margin **15%** (so reset points are **85 / 170 / 255**), cooldown **60 s**.

| Time | Used | What happens | Tier shown | Alert? | Armed [T1,T2,T3] |
|---|---|---|---|---|---|
| 0 s | 50 | Below everything | Normal | – | ✔ ✔ ✔ |
| 10 s | 120 | Crossed 100, T1 armed, never fired | **T1** | 🔔 **T1** | ✘ ✔ ✔ |
| 20 s | 95 | Below 100 but not below 85: hysteresis keeps T1 | T1 | – | ✘ ✔ ✔ |
| 30 s | 80 | Below 85: T1 re-armed, display drops | Normal | – | ✔ ✔ ✔ |
| 40 s | 120 | T1 armed, but only 30 s since last T1 alert | T1 | – (cooldown) | ✔ ✔ ✔ |
| 80 s | 120 | 70 s since last T1 ≥ 60 s | T1 | 🔔 **T1** | ✘ ✔ ✔ |
| 90 s | 350 | Jumped to T3. Fire T3, mark T1–T3 handled | **T3** | 🔔 **T3** | ✘ ✘ ✘ |
| 100 s | 180 | Below 255 → T3 re-armed; not below 170 → display stays T2 | T2 | – | ✘ ✘ ✔ |
| 110 s | 160 | Below 170 → T2 re-armed, display T1; T1 still disarmed | T1 | – | ✘ ✔ ✔ |

Get a pen and verify a few rows against the code below. Being able to "run" code in your head like this is a core skill.

### The code, piece by piece

**Step 1 — re-arm any tier we've fallen far enough below:**

```cpp
for (int index = 0; index < 3; ++index) {
    const auto tier = static_cast<AlertTier>(index + 1);
    if (usedBytes < resetPointForTier(tier)) m_armed[index] = true;
}
```

**Step 2 — decide the displayed tier, with hysteresis:**

```cpp
AlertTier AlertEngine::tierWithHysteresis(quint64 usedBytes) const
{
    const AlertTier rawTier = tierForUsage(usedBytes);    // plain threshold comparison
    if (rawTier >= m_currentTier) return rawTier;         // going up: move immediately

    AlertTier result = m_currentTier;                     // going down: step down one tier
    while (result > rawTier && usedBytes < resetPointForTier(result))  // at a time, only
        result = previous tier;                           // while below that tier's reset point
    return result;
}
```

(Simplified — the real code compares and steps down via `static_cast<int>`, since you can't do arithmetic like "tier − 1" on an `enum class`.) Going **up** is instant; going **down** is "sticky".

**Step 3 — should we fire?**

```cpp
const bool cooldownElapsed = m_lastTriggeredMs[currentIndex] < 0
    || nowMs - m_lastTriggeredMs[currentIndex] >= m_configuration.cooldownMs;
if (!m_armed[currentIndex] || !cooldownElapsed)
    return {m_currentTier, std::nullopt};                 // show tier, no alert
```

**Step 4 — fire, and mark this tier and all lower ones as handled:**

```cpp
for (int index = 0; index <= currentIndex; ++index) {
    m_armed[index] = false;
    m_lastTriggeredMs[index] = nowMs;
}
return {m_currentTier, m_currentTier};                    // show tier, alert this tier
```

The function returns an `AlertEvaluation` struct with two parts: the tier to **display** and an `optional` tier to **alert**. Separating "what to show" from "whether to interrupt the user" keeps the tray accurate even when alerts are suppressed.

### Where snooze fits

Snooze lives in `SwapMonitor`, not the engine. While snoozed, the engine still runs (so the tray stays accurate), but `SwapMonitor` simply doesn't `emit alertTriggered`. When the snooze ends, `SwapMonitor` calls `m_alertEngine.reset()` so you get reminded if swap is still high.

> **Check yourself**
> 1. In the table at 20 s, usage is 95 (below the T1 threshold of 100). Why does the tray still show T1?
> 2. Why does the engine receive the time as a parameter instead of reading a clock itself?
>
> <details><summary>Answers</summary>
>
> 1. Hysteresis: the displayed tier only drops once usage is below the reset point (85).
> 2. So it is a pure function of its inputs: tests can pass any time they like, and the engine has no OS dependency.
> </details>

---

## Lesson 11 — Talking to other programs: D-Bus and Apple frameworks

The app needs to show notifications and learn when the computer sleeps. Neither is done by the kernel directly; they're provided by *other programs* (a notification daemon, the login manager). So the app needs **IPC** — inter-process communication.

### Linux: D-Bus

**D-Bus** is the standard message bus on Linux desktops. Programs register names on a bus; others call their methods and subscribe to their signals. There are two buses:

- **System bus** — one per machine. Services like `org.freedesktop.login1` (systemd-logind: sleep, shutdown, sessions).
- **Session bus** — one per logged-in user. Services like `org.freedesktop.Notifications` (your desktop's notification popups).

> **Try it 🐧** — send a notification from the terminal, exactly like the app does:
> ```bash
> gdbus call --session \
>   --dest org.freedesktop.Notifications \
>   --object-path /org/freedesktop/Notifications \
>   --method org.freedesktop.Notifications.Notify \
>   "Swap Alert" 0 "" "Hello" "Sent over D-Bus" "[]" "{}" 5000
> ```
> Then compare with `LinuxNotificationService.cpp → send()`: same service, same path, same method, same eight arguments (app name, replaces-id, icon, title, body, actions, hints, timeout).

> **Try it 🐧** — watch the sleep signal the app listens for:
> ```bash
> gdbus monitor --system --dest org.freedesktop.login1
> # now suspend and wake your machine; look for PrepareForSleep (true) then (false)
> ```

In `LinuxSystemEventMonitor.cpp → start()`, the app subscribes to exactly that:

```cpp
systemBus.connect("org.freedesktop.login1", "/org/freedesktop/login1",
    "org.freedesktop.login1.Manager", "PrepareForSleep",
    this, SLOT(prepareForSleep(bool)));
```

`prepareForSleep(true)` becomes `SystemEvent::WillSleep`; `false` becomes `DidWake`. Linux-specific D-Bus names never leave this file — the rest of the app only sees the portable `SystemEvent` enum.

Note that `notifications.call(...)` is **synchronous**: the main thread waits for a reply. The `setTimeout(3000)` caps that wait at 3 seconds so a hung notification daemon can't freeze the app forever.

### macOS: Objective-C++ and Apple frameworks

Apple's APIs (AppKit, UserNotifications, ServiceManagement) are written in **Objective-C**. A `.mm` file is **Objective-C++**: it can contain both C++ and Objective-C in the same function.

A quick syntax decoder:

| Objective-C | Meaning |
|---|---|
| `[object method]` | Call a method (a "message send") |
| `[object method:arg1 other:arg2]` | Call with named arguments |
| `object.property` | Read a property |
| `@"text"` | An `NSString` literal |
| `^(BOOL granted, NSError* error) { ... }` | A **block** — Objective-C's lambda |
| `@interface Foo : NSObject <Protocol>` | Declare a class implementing a protocol (≈ interface) |

Memory is managed by **ARC** (Automatic Reference Counting), enabled by `-fobjc-arc` in `CMakeLists.txt` — the compiler inserts retain/release calls for you, conceptually like `std::shared_ptr`.

Reading `MacNotificationService.mm → send()` is a good exercise. The flow is:

1. Ask for the current permission status (async; answer arrives in a block on a background thread).
2. If never asked → request permission, then call `send()` again.
3. If denied → report failure via the callback.
4. If allowed → build a `UNMutableNotificationContent`, submit it, report the result.

Every result goes through `completeOnMain()` (Lesson 9) so the C++ callback runs on Qt's main thread.

`MacSystemEventMonitor.mm` subscribes to `NSWorkspaceWillSleepNotification` and friends, specifying `queue:[NSOperationQueue mainQueue]` so those callbacks already arrive on the main thread.

### Converting strings across the boundary

Each world has its own string type: `QString` (Qt), `NSString` (Apple), `const char*` (C). The helpers `toNSString()` / `fromNSString()` convert through UTF-8. Every boundary between libraries tends to need little adapters like these.

---

## Lesson 12 — Writing files safely

### Atomic writes

**File:** `src/platform/linux/LinuxAutostartService.cpp → setEnabled()`

To start at login on Linux, the app writes a `.desktop` file into `~/.config/autostart/`. If the app crashed or the disk filled up halfway through writing, you'd be left with a corrupt file. So it uses `QSaveFile`:

```cpp
QSaveFile file(path);
if (!file.open(...) || file.write(desktopEntry) != desktopEntry.size() || !file.commit()) { ...error... }
```

`QSaveFile` writes to a **temporary file** next to the target, and `commit()` **renames** it over the real file. On POSIX systems, `rename()` within one filesystem is **atomic**: other programs see either the whole old file or the whole new file, never a half-written one. This "write temp + rename" trick is the standard way to update files safely.

Then `QFile::setPermissions(path, ReadOwner | WriteOwner)` sets mode `0600` (only you can read/write it), because an autostart file is something that runs code at login.

> **Try it** — see permissions:
> ```bash
> ls -l ~/.config/autostart/ 2>/dev/null    # 🐧, after enabling Start at Login
> # -rw------- = owner read/write only (0600)
> ```

Also note `desktopExecQuote()`: it escapes `\ " \` $` in the executable path. The `Exec=` line is interpreted by a launcher, so an unescaped path containing `$` or a backtick could be misread. Any time you build text that another program will parse, escape it.

### Log rotation and a mutex

**File:** `src/core/DiagnosticLogger.cpp`

`DiagnosticLogger::install()` calls `qInstallMessageHandler(messageHandler)`, so every `qCInfo(...)` / `qCWarning(...)` in the app goes through one function that writes a line to a log file.

- **Rotation:** when the file would exceed 1 MB, `rotateLogs()` renames `log.1 → log.2`, `log → log.1`, and starts fresh. Old logs can't grow forever.
- **Mutex:** Qt may log from any thread (e.g. the Apple notification callbacks). Two threads writing the same file at once could interleave bytes. `QMutexLocker locker(&loggerMutex);` makes sure only one thread at a time runs that block — RAII again (Lesson 7).
- **Sanitising:** newlines in messages are replaced with `\n` so one log record is always one line.

**Logging categories** (`src/core/Logging.hpp`, e.g. `logMonitor`, `logProcesses`) tag each line so you can filter:

```bash
# 🐧 Logs live under ~/.local/share/SwapAlert/Swap Alert/logs/  (use "Open Logs Folder…" in the tray)
grep '\[swapalert.processes\]' swap-alert.log
```

---

## Lesson 13 — Building and testing

### From source to running program

```text
 .cpp / .mm ──compile──▶ .o (object files) ──link──▶ executable
   ▲                                          ▲
   │ #include headers                         │ libraries: Qt6Core, AppKit.framework, ...
   │ (preprocessor pastes text in)
```

- **Preprocessing** handles `#include`, `#define`, `#if`. In `main.cpp`, `#if defined(Q_OS_MACOS)` removes the Linux includes before the compiler even sees them.
- **Compiling** turns each source file into machine code (`.o`), separately.
- **Linking** stitches the `.o` files and libraries together and resolves "you called `sysctlbyname`, here's where it lives". "Undefined symbol" errors come from this step.

### CMake

`CMakeLists.txt` is a recipe that generates the actual build commands. Things worth reading in it:

```cmake
set(CMAKE_CXX_STANDARD 20)                     # use C++20

if(APPLE)
    set(SWAP_ALERT_PLATFORM_SOURCES src/platform/macos/MacSwapReader.mm ...)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SWAP_ALERT_PLATFORM_SOURCES src/platform/linux/LinuxSwapReader.cpp ...)
endif()
```

Only one OS's files are compiled. Together with the `#if` in `main.cpp`, this is how one codebase builds on two platforms.

```cmake
target_link_libraries(SwapAlert PRIVATE "-framework AppKit" ...)   # macOS
target_link_libraries(SwapAlert PRIVATE Qt6::DBus)                 # Linux
```

These tell the linker which libraries to pull in.

> **Try it** — see the real compiler commands:
> ```bash
> cmake --build build --verbose 2>&1 | grep -m1 MacSwapReader   # or LinuxSwapReader
> ```
> Also open `build/compile_commands.json` (created by the `CMAKE_EXPORT_COMPILE_COMMANDS` option from Setup): it records the exact command for every file. Editors use it for autocomplete.

> **Try it** — see which libraries the binary depends on:
> ```bash
> ldd build/swap-alert | grep -i qt                          # 🐧
> otool -L "build/Swap Alert.app/Contents/MacOS/Swap Alert"  # 🍎
> ```

### Tests

Tests are separate executables built from the same `core/` sources plus fakes:

| Test file | What it checks | Uses |
|---|---|---|
| `AlertEngineTests.cpp` | Every rule from Lesson 10; `/proc/meminfo` parsing | Plain numbers, temp files |
| `SwapMonitorTests.cpp` | Snooze, pause/resume, read failures, sleep/wake | `FakeSwapReader`, `FakeClock` |
| `ProcessGroupingTests.cpp` | The BFS grouping from Lesson 5 | Hand-built process lists |
| `LinuxProcessServiceTests.cpp` | Linux process inventory | 🐧 only |
| `UiSafetyTests.cpp` | Force Quit stays disabled until it should be; permission UI | Fake platform services + real dialogs |

```bash
ctest --test-dir build --output-on-failure
```

Read `tests/SwapMonitorTests.cpp → snoozeSuppressesAlertsButKeepsSampling()` alongside Lesson 10. It sets fake swap values, advances a fake clock, and uses `QSignalSpy` to count how many times each signal fired. Notice how readable the test is *because* of the interfaces from Lesson 6.

### Packaging (brief)

`packaging/` turns the build into something installable: a signed `.dmg` on macOS, AppImage / Flatpak / `.deb` / `.rpm` on Linux. See [`RELEASING.md`](RELEASING.md). One interesting detail: inside Flatpak, the app runs in its own **PID namespace** — it literally can't see host processes in `/proc` — so `LinuxProcessService` disables cleanup when `FLATPAK_ID` is set.

---

## Lesson 14 — Debugging and catching bugs

So far you've read working code. This lesson is about what to do when code *doesn't* work: how to look inside a running program, and how to make the compiler and runtime catch bugs for you.

### Tool 1: log lines

The cheapest debugger is the log you already have. Every `qCInfo(logMonitor) << ...` line in the code prints when you run the app from a terminal (see Setup). Qt lets you turn categories on and off **without recompiling**, using an environment variable:

```bash
# hide the per-sample monitor spam, keep everything else
QT_LOGGING_RULES="swapalert.monitor.debug=false" ./build/swap-alert

# only show process-related lines
QT_LOGGING_RULES="*=false;swapalert.processes.*=true" ./build/swap-alert
```

(On 🍎 use `"build/Swap Alert.app/Contents/MacOS/Swap Alert"` instead of `./build/swap-alert`.)

Adding a temporary `qCDebug(logMonitor) << "here" << someValue;` is a perfectly good way to debug. Just remove it afterwards.

### Tool 2: a debugger

A **debugger** runs your program under its control. You can pause it at any line, look at every variable, and step through one line at a time. macOS uses **lldb**; Linux usually uses **gdb** (lldb also works there). Both need a **Debug** build (Setup step 2), because optimised builds rearrange and remove code, so the lines and variables no longer match what you wrote.

The easiest place to start is a **test**, not the app: it runs the same code every time and finishes on its own. Let's watch the alert engine from Lesson 10 make its decisions:

```bash
lldb ./build/SwapAlertTests -- hysteresisRearmsTier         # 🍎 (or 🐧 if you prefer lldb)
gdb --args ./build/SwapAlertTests hysteresisRearmsTier      # 🐧
```

Then type these commands at the `(lldb)` or `(gdb)` prompt:

| What you want | lldb | gdb |
|---|---|---|
| Stop whenever a function is called | `b AlertEngine::evaluate` | `break AlertEngine::evaluate` |
| Start the program | `run` | `run` |
| Show the arguments and local variables | `frame variable` | `info args` then `info locals` |
| Print any expression | `p usedBytes`, `p m_armed` | `p usedBytes`, `p m_armed` |
| Who called this? (the call stack) | `bt` | `bt` |
| Run one line, stepping *over* calls | `n` | `next` |
| Run one line, stepping *into* calls | `s` | `step` |
| Run until this function returns | `finish` | `finish` |
| Keep going until the next breakpoint | `c` | `continue` |
| Stop when a variable changes (while stopped inside `evaluate`) | `watchpoint set expression -- &m_currentTier` | `watch m_currentTier` |
| Quit | `q` | `q` |

> **Try it** — trace the hysteresis test by hand, then check yourself with the debugger.
> 1. Open `tests/AlertEngineTests.cpp → hysteresisRearmsTier()`. Write down the `usedBytes` of each `evaluate(...)` call and what you expect `m_currentTier` and `m_armed` to be *after* each one.
> 2. Run the test under the debugger with a breakpoint on `AlertEngine::evaluate`.
> 3. At each stop, `p usedBytes` and `p m_armed`. Then type `finish` to return to the test function and `p engine.m_armed` to see the state *after* the call (the debugger can read private members). Did each call do what you predicted?
>
> Being able to predict what code will do, then check the prediction, is the most useful debugging skill there is.

> **Try it** — see the event loop from Lesson 9 in a real call stack. Debug the full app and break on the timer slot:
> ```bash
> lldb "build/Swap Alert.app/Contents/MacOS/Swap Alert"   # 🍎
> gdb ./build/swap-alert                                  # 🐧
> ```
> Set a breakpoint on `SwapMonitor::refreshNow`, `run`, and wait up to 10 seconds. When it stops, type `bt`. Reading from the bottom up you'll see `main` → `QCoreApplication::exec` → … timer event … → `QTimer::timeout` → `SwapMonitor::refreshNow`. That's the event loop, the timer and the signal/slot connection, all in one picture.

Debuggers show C++ standard types (`std::array`, `std::optional`) nicely. Qt types like `QString` may show raw internals instead of the text. Qt Creator and the VS Code Qt extensions add "pretty printers" that fix this.

### Tool 3: sanitizers — let the computer find memory bugs

C++ has **undefined behaviour** (UB): things like reading freed memory, writing past the end of an array, or overflowing a signed integer. The language doesn't define what happens. The program might crash, might print garbage, or, worst of all, might *seem* to work. That's why C++ bugs can hide for years.

**Sanitizers** are compiler options that add checks to your program, so UB is reported the moment it happens, with the exact line:

- **AddressSanitizer (ASan)** — use-after-free, out-of-bounds reads and writes, double delete, memory leaks (leak detection is Linux-only by default).
- **UndefinedBehaviorSanitizer (UBSan)** — signed overflow, invalid casts, misaligned pointers, and more.

> **Try it** — meet both on purpose. Save this as `bug.cpp` outside the repo:
> ```cpp
> #include <climits>
> #include <cstdio>
> #include <memory>
>
> int main(int argc, char**)
> {
>     int big = INT_MAX;
>     big += argc;                        // signed overflow: undefined behaviour
>     std::printf("big = %d\n", big);
>
>     auto owner = std::make_unique<int>(42);
>     int* borrowed = owner.get();        // non-owning pointer (Lesson 7)
>     owner.reset();                      // the int is deleted here
>     std::printf("%d\n", *borrowed);     // use-after-free
> }
> ```
> ```bash
> c++ -std=c++20 -g bug.cpp -o bug && ./bug          # no checks: probably "works"
> c++ -std=c++20 -g -fsanitize=address,undefined bug.cpp -o bug && ./bug
> ```
> Without sanitizers it likely prints a negative number and then `42` or some garbage number, and exits as if nothing were wrong. With them you get `runtime error: signed integer overflow` and then a `heap-use-after-free` report showing **where the memory was freed** and **where it was used afterwards**. This is exactly the kind of bug the "borrowed pointer" pattern in Lesson 7 can cause if the owner dies first.

Now build the whole project with sanitizers, in a separate build folder so your normal build stays fast:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_OBJCXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

(On 🐧 drop the `CMAKE_PREFIX_PATH` line; the `OBJCXX` line is only used on macOS and is harmless elsewhere.)

The tests should still pass. If ASan reports leaks that come from inside Qt or system libraries on Linux rather than from `src/`, that's noise from libraries that intentionally keep memory until exit. Silence leak checking with `ASAN_OPTIONS=detect_leaks=0` and focus on reports that point into this repo.

Sanitized programs run about 2× slower and use more memory, so they're for development and tests, not for releases.

### Tool 4: compiler warnings and static analysis

The compiler can spot many bugs without running anything, if you ask it to. Try a build with extra warnings:

```bash
cmake -S . -B build-warn -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wshadow"
cmake --build build-warn 2>&1 | grep warning
```

| Flag | Catches, for example |
|---|---|
| `-Wall -Wextra` | Unused variables, comparing signed with unsigned, a `switch` missing an `enum` value |
| `-Wpedantic` | Non-standard compiler extensions |
| `-Wshadow` | A local variable that hides a member or another variable with the same name |

These flags only apply to `.cpp` files; on 🍎 add the same flags as `-DCMAKE_OBJCXX_FLAGS=...` to check the `.mm` files too.

Read each warning and decide: real bug, or harmless? Many projects add `-Werror` in CI so warnings can't pile up.

**clang-tidy** goes further: it knows hundreds of C++ bug patterns and style rules. It uses the `compile_commands.json` from Setup:

```bash
clang-tidy -p build src/core/AlertEngine.cpp
# 🍎: brew install llvm, then run "$(brew --prefix llvm)/bin/clang-tidy"
# 🐧: sudo dnf install clang-tools-extra   (Ubuntu: sudo apt install clang-tidy)
```

### A debugging checklist

When something is wrong:

1. **Make it happen reliably.** Ideally as a test with a fake reader and fake clock (Lesson 6). A bug you can trigger on demand is half fixed.
2. **Predict, then observe.** Write down what you think each step should do, then check with logs or the debugger. The bug is where prediction and reality disagree.
3. **Crash or weird value?** Rebuild with sanitizers first. They often point straight at the line.
4. **Fix it and keep the test**, so it can't come back.

> **Check yourself**
> 1. Why debug a test instead of the whole app when you can?
> 2. Your program prints the right answer on your machine and garbage on a friend's. What tool would you reach for first?
>
> <details><summary>Answers</summary>
>
> 1. A test runs the same inputs every time, uses fake time and fake swap readings, and finishes by itself. The app depends on real memory, real time and a GUI.
> 2. Sanitizers (ASan + UBSan). "Works here, breaks there" is a classic sign of undefined behaviour, such as reading uninitialised or freed memory.
> </details>

---

## Exercises

Want bigger, guided projects? **[Build It Yourself](PROJECTS.md)** walks you step by step through six real features the app is missing. Exercises 6, 7, 10 and 11 below are covered there in detail.

Ordered from easiest to hardest. Each says which lessons it practises.

### Warm-up

1. **Add a 30-minute snooze** to the tray menu. *(L8, L9)* Find where the 15-minute action is created in `src/ui/TrayController.cpp` and follow the `snoozeRequested` signal to `main.cpp`.
2. **Add a boundary test.** In `tests/AlertEngineTests.cpp`, test usage of *exactly* 100 and exactly 85 with thresholds 100/200/300. Predict the result first. *(L1, L10)*
3. **Extend the Hello Qt program** from Lesson 8: add a "Reset" button that sets the counter back to 0, and a `QSpinBox` whose value sets how much each click adds. *(L8)*
4. **Add a "Copy Status" item** to the tray menu that copies the current "Swap: X of Y" text to the clipboard (`QGuiApplication::clipboard()->setText(...)`). *(L8, L9)*
5. **Print your own `/proc` reading.** Write a standalone C++ program (no Qt) that prints `SwapTotal` and `SwapFree` using `std::ifstream` and `std::getline`. *(L2)*

### Core

6. **Make the reset margin configurable** in the Settings dialog. Follow how `cooldownMinutes` flows from `SettingsDialog` → `SettingsStore` → `AlertEngine::Configuration`. *(L6, L9, L10)* → [Guided: Project 1](PROJECTS.md#project-1--make-the-re-arm-margin-configurable)
7. **Keep a history.** Create a `SwapHistory` class in `core/` that stores the last 60 `SwapInfo` samples (use a fixed-size `std::array` as a ring buffer). Connect it to `sampleUpdated`. Write a test for it. *(L1, L7, L9)* → [Guided: Project 3](PROJECTS.md#project-3--remember-recent-readings-swaphistory)
8. **Write a test** that changes thresholds while monitoring is running and checks that the next sample uses the new ones. *(L6, L13)*

### Systems

9. **`strace` the app.** 🐧 Run `strace -f -e trace=openat,kill,clock_gettime ./build/swap-alert` and identify each syscall from this guide. Why does `clock_gettime` probably *not* show up even though it's called? (Hint: look up **vDSO**.) *(L2, L4, L5)*
10. **Show swap-in/swap-out rate**, not just total. 🐧 `/proc/vmstat` has `pswpin` / `pswpout` counters; 🍎 run `vm_stat` to see `Swapins` / `Swapouts`, then read them in code with `host_statistics64(..., HOST_VM_INFO64, ...)` (fields `swapins` / `swapouts` of `vm_statistics64`). You'll need to compute the *difference* between two samples. Add it to `SwapInfo` and both readers. *(L2, L3, L6)* → [Guided: Project 5](PROJECTS.md#project-5--measure-swap-activity)
11. **Close the PID-reuse window.** 🐧 Replace `kill()` in `LinuxProcessService::terminate()` with `pidfd_open()` + `pidfd_send_signal()` (Linux ≥ 5.3). Read `man 2 pidfd_open` and explain in a comment why this is safer. *(L5)* → [Guided: Project 6](PROJECTS.md#project-6--signal-the-right-process-always-pidfd)
12. **Find a bug with a sanitizer.** On a scratch branch, add a `std::unique_ptr<SwapInfo> m_last;` member to `SwapMonitor`. In `refreshNow()`, keep `const SwapInfo* previous = m_last.get();`, then replace it with `m_last = std::make_unique<SwapInfo>(*info);`, then log `previous->usedBytes` when `previous` isn't null. Run `SwapMonitorTests` in both `build` and `build-asan` and compare. Explain in your own words why the normal build stayed quiet. *(L1, L7, L14)*

### Stretch

13. **Add a third platform stub.** Create `platform/freebsd/` with a `FreeBsdSwapReader` that uses `sysctl` (FreeBSD has `vm.swap_info`). You don't need FreeBSD to get the structure right — the goal is to see how few files must change. *(L6, L13)*

---

## Glossary

| Term | Meaning |
|---|---|
| **ABI / API** | API: the functions you call in source code. ABI: how compiled code calls them (binary layout). |
| **Anonymous memory** | Memory not backed by a file (heap, stack). The only kind that goes to swap. |
| **Atomic** | Happens all at once or not at all; nobody can observe a half-done state. |
| **D-Bus** | Linux message bus for inter-process communication. |
| **Debug build** | A build without optimisation and with debug symbols, so a debugger can map machine code back to your source lines. |
| **errno** | Global variable where C system functions store the reason for their last failure. |
| **Event loop** | The loop that waits for events and dispatches them to handlers. |
| **File descriptor** | Small integer the kernel gives you to refer to an open file/socket. |
| **Hysteresis** | Using different thresholds for going up vs coming down, to prevent flapping. |
| **Implicit sharing** | Qt's copy-on-write: copies of `QString`, `QVector` etc. share one buffer until one of them is modified. |
| **Layout** | A Qt object that sizes and positions widgets automatically. |
| **Memory compressor** | macOS feature that compresses inactive pages in RAM before resorting to swap. |
| **moc** | Qt's code generator that implements signals/slots. |
| **Modal** | A dialog that blocks its parent window until it's closed. |
| **Monotonic clock** | A clock that never goes backwards. |
| **Page fault** | The CPU trap that happens when a program touches a page that isn't in RAM; the kernel then loads it. |
| **Page** | Fixed-size chunk of memory the OS moves between RAM and swap. |
| **Parent / child (Qt)** | Ownership tree of `QObject`s: destroying a parent deletes its children. |
| **PID / PPID / UID** | Process ID / parent's process ID / owning user's ID. |
| **PSI** | Linux *pressure stall information*: how much time tasks spent waiting for memory, CPU or I/O. |
| **QObject** | Base class of most Qt classes; provides signals/slots, parent/child ownership and runtime type info. |
| **QVariant** | A Qt box that can hold a value of almost any type. |
| **RAII** | Tying resource cleanup to an object's destructor. |
| **RSS / footprint** | Memory of a process currently in physical RAM. |
| **Sanitizer** | Compiler option (ASan, UBSan…) that adds runtime checks to catch memory bugs and undefined behaviour. |
| **Signal (Qt)** | An event a `QObject` emits, delivered to connected slots. Unrelated to Unix signals despite the name. |
| **Signal (Unix)** | Kernel notification sent to a process, e.g. `SIGTERM`, `SIGKILL`. |
| **Syscall** | A request from a program to the kernel. |
| **TOCTOU** | Time-of-check to time-of-use race: the world changes between checking and acting. |
| **Undefined behaviour** | Code the C++ standard gives no meaning to (e.g. use-after-free, signed overflow). Anything can happen. |
| **Virtual memory** | Each process sees its own address space; the OS maps it onto physical RAM page by page. |
| **vtable** | Hidden table of function pointers that makes `virtual` calls work. |
| **Widget** | A visible UI element in Qt Widgets: window, button, label, table… |
| **zram / zswap** | Linux features that keep swapped pages compressed in RAM instead of (or before) writing them to disk. |

---

## Further reading

**Man pages** (run `man <section> <name>`, e.g. `man 5 proc`):

- 🐧 `proc(5)` — everything in `/proc`
- 🐧 `clock_gettime(2)` — all the Linux clocks
- `kill(2)`, 🐧 `signal(7)` / 🍎 `signal(3)` — signals
- 🐧 `credentials(7)` — PIDs, UIDs, process groups
- 🍎 `sysctl(3)`, `sysctl(8)` — macOS kernel values
- `rename(2)` — why rename is atomic

**Books and sites**

- *The Linux Programming Interface* — Michael Kerrisk. The reference for Linux/Unix system calls; chapters on processes, signals, `/proc`, and timers map directly onto Lessons 2–5.
- *A Tour of C++* — Bjarne Stroustrup. Short, modern overview of the C++ in Lessons 1, 6 and 7.
- [cppreference.com](https://en.cppreference.com) — look up any standard library type (`std::optional`, `std::unique_ptr`…).
- Qt documentation, in this order: [Object Trees & Ownership](https://doc.qt.io/qt-6/objecttrees.html), [Signals & Slots](https://doc.qt.io/qt-6/signalsandslots.html), [The Meta-Object System](https://doc.qt.io/qt-6/metaobjects.html), [Layout Management](https://doc.qt.io/qt-6/layout.html), [Implicit Sharing](https://doc.qt.io/qt-6/implicit-sharing.html), [The Event System](https://doc.qt.io/qt-6/eventsandfilters.html), and the [Qt Test tutorial](https://doc.qt.io/qt-6/qttestlib-tutorial1-example.html).
- [LLDB tutorial](https://lldb.llvm.org/use/tutorial.html) and the [GDB documentation](https://sourceware.org/gdb/current/onlinedocs/gdb.html/) — Lesson 14.
- [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) and [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — what each sanitizer catches.
- Linux kernel docs on [zram](https://docs.kernel.org/admin-guide/blockdev/zram.html), [zswap](https://docs.kernel.org/admin-guide/mm/zswap.html) and [PSI](https://docs.kernel.org/accounting/psi.html) — Lesson 0.
- [freedesktop.org Desktop Notifications spec](https://specifications.freedesktop.org/notification-spec/latest/) — the D-Bus API used in Lesson 11.
- *Mac OS X Internals* — Amit Singh. Older but still the best explanation of Mach, the XNU kernel and `sysctl`.
