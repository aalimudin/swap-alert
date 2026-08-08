#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build-appimage}"
DIST_DIR="${DIST_DIR:-$PROJECT_DIR/dist}"
APPDIR="${APPDIR:-$DIST_DIR/SwapAlert.AppDir}"

resolve_tool() {
    local requested="$1"
    if [[ "$requested" == */* ]]; then
        [[ -x "$requested" ]] && printf '%s\n' "$requested" && return 0
    else
        command -v "$requested" 2>/dev/null && return 0
    fi
    return 1
}

LINUXDEPLOY="$(resolve_tool "${LINUXDEPLOY:-linuxdeploy}" || true)"
QT_PLUGIN="$(resolve_tool "${LINUXDEPLOY_PLUGIN_QT:-linuxdeploy-plugin-qt}" || true)"
if [[ -z "$LINUXDEPLOY" || -z "$QT_PLUGIN" ]]; then
    echo "linuxdeploy and linuxdeploy-plugin-qt are required." >&2
    echo "Set LINUXDEPLOY and LINUXDEPLOY_PLUGIN_QT to their executable paths." >&2
    exit 1
fi

for tool in cmake ctest file ninja; do
    if ! command -v "$tool" >/dev/null; then
        echo "$tool is required to build the AppImage package." >&2
        exit 1
    fi
done

case "$(uname -m)" in
    x86_64) PACKAGE_ARCH="x86_64" ;;
    aarch64|arm64) PACKAGE_ARCH="aarch64" ;;
    *) echo "Unsupported AppImage architecture: $(uname -m)" >&2; exit 1 ;;
esac

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --parallel
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
    ctest --test-dir "$BUILD_DIR" --output-on-failure

cmake -E remove_directory "$APPDIR"
cmake -E make_directory "$APPDIR" "$DIST_DIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

VERSION="$($APPDIR/usr/bin/swap-alert --version | awk '{print $NF}')"
OUTPUT_PATH="$DIST_DIR/Swap-Alert-$VERSION-$PACKAGE_ARCH.AppImage"
cmake -E rm -f "$OUTPUT_PATH"

export PATH="$(dirname "$QT_PLUGIN"):$PATH"
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"
# linuxdeploy's bundled strip can lag new ELF features such as RELR. Release
# builds already omit debug information, so skipping this second strip is safe.
export NO_STRIP=1
export LDAI_OUTPUT="$OUTPUT_PATH"
export LINUXDEPLOY_OUTPUT_APP_NAME="Swap-Alert"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

"$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt --output appimage

if [[ ! -x "$OUTPUT_PATH" ]]; then
    echo "linuxdeploy did not create the expected artifact: $OUTPUT_PATH" >&2
    exit 1
fi

echo "Created: $OUTPUT_PATH"
