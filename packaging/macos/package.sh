#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build-release}"
DIST_DIR="${DIST_DIR:-$PROJECT_DIR/dist}"
STAGE_DIR="$DIST_DIR/stage"
DMG_ROOT="$DIST_DIR/dmg-root"
QT_PREFIX="${QT_PREFIX:-$(brew --prefix qt)}"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"

if [[ ! -x "$MACDEPLOYQT" ]]; then
    echo "macdeployqt was not found at: $MACDEPLOYQT" >&2
    echo "Set QT_PREFIX to the Qt installation prefix and try again." >&2
    exit 1
fi

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

cmake -E remove_directory "$STAGE_DIR"
cmake -E remove_directory "$DMG_ROOT"
cmake -E make_directory "$STAGE_DIR" "$DMG_ROOT"
cmake --install "$BUILD_DIR" --prefix "$STAGE_DIR"

APP_PATH="$STAGE_DIR/Swap Alert.app"
if [[ ! -d "$APP_PATH" ]]; then
    echo "Installed application was not found at: $APP_PATH" >&2
    exit 1
fi

if [[ -n "${SIGN_IDENTITY:-}" ]]; then
    "$MACDEPLOYQT" "$APP_PATH" -always-overwrite -verbose=1 \
        "-libpath=$QT_PREFIX/lib" \
        "-sign-for-notarization=$SIGN_IDENTITY"
else
    echo "SIGN_IDENTITY is unset; creating an ad-hoc signed local package."
    "$MACDEPLOYQT" "$APP_PATH" -always-overwrite -verbose=1 \
        "-libpath=$QT_PREFIX/lib" -codesign=-
fi

# Swap Alert generates its icons in code and does not load external image files,
# SVG icons, or virtual keyboards. Removing those optional plugin families avoids
# pulling unrelated Qt modules into the release while retaining Cocoa and macOS style.
cmake -E remove_directory "$APP_PATH/Contents/PlugIns/iconengines"
cmake -E remove_directory "$APP_PATH/Contents/PlugIns/imageformats"
cmake -E remove_directory "$APP_PATH/Contents/PlugIns/platforminputcontexts"
if [[ -n "${SIGN_IDENTITY:-}" ]]; then
    codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" "$APP_PATH"
else
    codesign --force --sign - "$APP_PATH"
fi

codesign --verify --deep --strict --verbose=2 "$APP_PATH"
ditto "$APP_PATH" "$DMG_ROOT/Swap Alert.app"
ln -s /Applications "$DMG_ROOT/Applications"

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$APP_PATH/Contents/Info.plist")"
DMG_PATH="$DIST_DIR/Swap-Alert-$VERSION-macOS.dmg"
cmake -E rm -f "$DMG_PATH"
hdiutil create -volname "Swap Alert" -srcfolder "$DMG_ROOT" \
    -ov -format UDZO "$DMG_PATH"

if [[ -n "${SIGN_IDENTITY:-}" ]]; then
    codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG_PATH"
    codesign --verify --verbose=2 "$DMG_PATH"
fi

if [[ "${NOTARIZE:-0}" == "1" ]]; then
    if [[ -z "${SIGN_IDENTITY:-}" || -z "${NOTARY_PROFILE:-}" ]]; then
        echo "NOTARIZE=1 requires SIGN_IDENTITY and NOTARY_PROFILE." >&2
        exit 1
    fi
    xcrun notarytool submit "$DMG_PATH" \
        --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG_PATH"
    xcrun stapler validate "$DMG_PATH"
fi

echo "Created: $DMG_PATH"
