# Releasing Swap Alert

## Linux AppImage

The AppImage is the primary full-featured Linux artifact. Build it on the oldest supported Linux baseline so its glibc dependency remains compatible with newer distributions. The packaging workflow builds and tests Swap Alert, installs a complete AppDir, asks `linuxdeploy` and its Qt plugin to bundle runtime dependencies, and emits a versioned AppImage under `dist/`.

The build host also needs CMake, Ninja, a C++ compiler, Qt 6 development packages, `file`, AppStream CLI tools, and `desktop-file-validate` (usually provided by `desktop-file-utils`).

Download the official `linuxdeploy` and Qt plugin AppImages into a tools directory, make them executable, then pass their paths to the package script:

```bash
mkdir -p tools
curl -L -o tools/linuxdeploy-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -L -o tools/linuxdeploy-plugin-qt-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x tools/linuxdeploy*.AppImage

LINUXDEPLOY="$PWD/tools/linuxdeploy-x86_64.AppImage" \
LINUXDEPLOY_PLUGIN_QT="$PWD/tools/linuxdeploy-plugin-qt-x86_64.AppImage" \
  ./packaging/linux/package-appimage.sh
```

Verify the dependency bundle, required desktop resources, AppStream metadata, Qt platform plugin, and headless startup:

```bash
./packaging/linux/verify-appimage.sh \
  dist/Swap-Alert-0.1.0-x86_64.AppImage
```

Run the artifact directly after making it executable. AppImages are not installed system-wide:

```bash
chmod +x Swap-Alert-0.1.0-x86_64.AppImage
./Swap-Alert-0.1.0-x86_64.AppImage
```

Do not build the release AppImage on a newer distribution and assume it will run on Ubuntu 22.04. Test the exact artifact on supported Ubuntu and Fedora releases, including GNOME and KDE tray/notification behavior.

## Linux Flatpak

The Flatpak uses the `org.kde.Platform` and `org.kde.Sdk` 6.11 runtime, which provides Qt 6. Install `flatpak-builder`, configure Flathub, and run:

```bash
flatpak remote-add --user --if-not-exists flathub \
  https://dl.flathub.org/repo/flathub.flatpakrepo

./packaging/linux/package-flatpak.sh
./packaging/linux/verify-flatpak.sh dist/Swap-Alert-0.1.0.flatpak
```

The package script installs missing SDK/runtime dependencies for the current user by default. Use `INSTALL_DEPS=0` when CI or the build machine has already provisioned them.

Install and run a local bundle with:

```bash
flatpak install --user dist/Swap-Alert-0.1.0.flatpak
flatpak run com.swapalert.app
```

The Flatpak grants only display access, narrowly scoped notification/status-notifier D-Bus access, login1 event access, and permission to create its XDG autostart entry. Flatpak isolates host processes in a separate PID namespace, so guided host-application cleanup is unavailable. Swap Alert reports this limitation in the cleanup window. Use the AppImage when cleanup is required.

Before publishing, test the exact Flatpak bundle on GNOME and KDE. Confirm swap readings, notifications, tray recovery, suspend/resume handling, settings persistence, and Flatpak-aware start-at-login behavior.

## Native DEB and RPM packages

Native packages use the same `/usr` install layout and desktop metadata as the cross-distribution artifacts. Build each package on the oldest release of its target distribution so the executable and automatically generated shared-library dependencies match that distribution.

Install the packaging tools in addition to the normal build dependencies:

```bash
# Debian or Ubuntu
sudo apt install dpkg-dev file

# Fedora
sudo dnf install rpm-build
```

The script detects the host-native format by default, builds the application, runs all tests, and writes the package to `dist/`:

```bash
./packaging/linux/package-native.sh

# Explicit formats when the corresponding toolchains are installed:
./packaging/linux/package-native.sh deb
./packaging/linux/package-native.sh rpm
./packaging/linux/package-native.sh all
```

Inspect package identity, dependencies, and required payload files with:

```bash
./packaging/linux/verify-native-package.sh \
  dist/swap-alert_0.1.0_amd64.deb
./packaging/linux/verify-native-package.sh \
  dist/swap-alert-0.1.0-1.x86_64.rpm
```

DEB dependencies are derived by `dpkg-shlibdeps`; RPM dependencies use RPM's automatic ELF requirement generation. Do not build one distribution's package against another distribution's Qt libraries. Until the repository contains a project license, RPM metadata defaults to `LicenseRef-Proprietary`; release builds can override it with `PACKAGE_LICENSE=SPDX-ID` after the project license is chosen.

## Linux release checklist

1. Update the project version and AppStream release entry.
2. Build and test the AppImage on the oldest supported build baseline.
3. Run `verify-appimage.sh`, then test the same artifact on supported Ubuntu and Fedora releases.
4. Build and verify the Flatpak bundle.
5. Build DEB and RPM convenience packages on their target distribution baselines.
6. Test GNOME and KDE tray, notification, suspend/resume, and autostart behavior.
7. Confirm the Flatpak cleanup limitation is visible and the AppImage cleanup flow remains safe.
8. Generate checksums for published artifacts and tag the exact source revision.

## macOS DMG

This project includes a repeatable release pipeline that builds, tests, deploys Qt frameworks, creates a DMG, and optionally performs Developer ID signing and Apple notarization.

## Local package

Install the normal build prerequisites, then run:

```bash
./packaging/macos/package.sh
```

The script creates an ad-hoc signed DMG under `dist/`. Ad-hoc signing is suitable for local package testing, but it does not make the application trusted on another Mac.

Verify the result with:

```bash
./packaging/macos/verify-package.sh dist/Swap-Alert-0.1.0-macOS.dmg
```

The verifier mounts the image, validates the bundle signature and property list, and checks that no executable still links to a Homebrew Qt path.

If disk-image creation is unavailable in a restricted environment, the staged application can be checked directly:

```bash
./packaging/macos/verify-package.sh "dist/stage/Swap Alert.app"
```

## Developer ID signing

Distribution outside the Mac App Store requires a `Developer ID Application` certificate from the Apple Developer Program. List available identities with:

```bash
security find-identity -v -p codesigning
```

Then create a signed package:

```bash
SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
  ./packaging/macos/package.sh
```

`macdeployqt` copies the Qt frameworks and signs the nested bundle components with the hardened runtime and a secure timestamp. The script signs the DMG separately.

## Notarization

Store credentials once in the login keychain. Apple documents the issuer ID, key ID, and private API key in the App Store Connect API workflow.

```bash
xcrun notarytool store-credentials "swap-alert-notary" \
  --key "/path/to/AuthKey_KEYID.p8" \
  --key-id "KEYID" \
  --issuer "ISSUER_UUID"
```

Build, submit, wait for acceptance, and staple the ticket with:

```bash
SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
NOTARY_PROFILE="swap-alert-notary" \
NOTARIZE=1 \
  ./packaging/macos/package.sh
```

Real signing and notarization require an Apple Developer account, a Developer ID certificate, notarization credentials, and a working full Xcode installation. The local automated tests cannot substitute for Apple's notarization service.

For a signed and notarized build, also enable Gatekeeper verification:

```bash
VERIFY_GATEKEEPER=1 \
  ./packaging/macos/verify-package.sh dist/Swap-Alert-0.1.0-macOS.dmg
```

## Release checklist

1. Update the project version and bundle build number in `CMakeLists.txt`.
2. Run the unit and UI tests.
3. Build the signed, notarized DMG.
4. Run package verification on the DMG.
5. Install the DMG on a second Mac and confirm notifications and start at login.
6. Put the application in `/Applications` before enabling start at login.
7. Tag the exact source revision used for the package.
