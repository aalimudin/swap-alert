#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ( ! -f "$1" && ! -d "$1" ) ]]; then
    echo "Usage: $0 /path/to/Swap-Alert.AppImage-or-AppDir" >&2
    exit 1
fi

INPUT_PATH="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/swap-alert-appimage-verify.XXXXXX")"
cleanup() {
    rm -rf -- "$TEMP_DIR"
}
trap cleanup EXIT

if [[ -d "$INPUT_PATH" ]]; then
    APPDIR="$INPUT_PATH"
else
    if [[ ! -x "$INPUT_PATH" ]]; then
        echo "AppImage is not executable; run: chmod +x '$INPUT_PATH'" >&2
        exit 1
    fi
    (
        cd "$TEMP_DIR"
        "$INPUT_PATH" --appimage-extract >/dev/null
    )
    APPDIR="$TEMP_DIR/squashfs-root"
fi

for required in \
    AppRun \
    usr/bin/swap-alert \
    usr/share/applications/com.swapalert.app.desktop \
    usr/share/icons/hicolor/scalable/apps/com.swapalert.app.svg \
    usr/share/metainfo/com.swapalert.app.metainfo.xml; do
    if [[ ! -e "$APPDIR/$required" ]]; then
        echo "Required AppImage payload is missing: $required" >&2
        exit 1
    fi
done

if ! find "$APPDIR/usr/lib" -name 'libQt6Core.so*' -print -quit | grep -q .; then
    echo "Bundled Qt Core library was not found." >&2
    exit 1
fi
if ! find "$APPDIR/usr/plugins" "$APPDIR/usr/lib" \
        -name 'libqxcb.so' -print -quit 2>/dev/null | grep -q .; then
    echo "Bundled Qt XCB platform plugin was not found." >&2
    exit 1
fi
if ldd "$APPDIR/usr/bin/swap-alert" | grep -q 'not found'; then
    echo "The packaged executable has unresolved shared-library dependencies." >&2
    ldd "$APPDIR/usr/bin/swap-alert" >&2
    exit 1
fi

appstreamcli validate --no-net "$APPDIR/usr/share/metainfo/com.swapalert.app.metainfo.xml"
desktop-file-validate "$APPDIR/usr/share/applications/com.swapalert.app.desktop"

if [[ -f "$INPUT_PATH" ]]; then
    APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen "$INPUT_PATH" --version
else
    QT_QPA_PLATFORM=offscreen "$APPDIR/AppRun" --version
fi

echo "AppImage verification passed: $INPUT_PATH"
