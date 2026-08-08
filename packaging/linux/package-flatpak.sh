#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MANIFEST="${MANIFEST:-$SCRIPT_DIR/com.swapalert.app.yml}"
BUILD_DIR="${FLATPAK_BUILD_DIR:-$PROJECT_DIR/build-flatpak}"
REPO_DIR="${FLATPAK_REPO_DIR:-$PROJECT_DIR/dist/flatpak-repo}"
DIST_DIR="${DIST_DIR:-$PROJECT_DIR/dist}"
APP_ID="com.swapalert.app"
BRANCH="${FLATPAK_BRANCH:-stable}"

for tool in flatpak flatpak-builder; do
    if ! command -v "$tool" >/dev/null; then
        echo "$tool is required to build the Flatpak package." >&2
        exit 1
    fi
done

mkdir -p "$DIST_DIR"
BUILDER_ARGS=(--force-clean --run-tests --repo="$REPO_DIR")
if [[ "${INSTALL_DEPS:-1}" == "1" ]]; then
    BUILDER_ARGS+=(--user --install-deps-from=flathub)
fi
flatpak-builder "${BUILDER_ARGS[@]}" "$BUILD_DIR" "$MANIFEST"

VERSION="$(sed -nE 's/.*project\(SwapAlert VERSION ([^ ]]+).*/\1/p' "$PROJECT_DIR/CMakeLists.txt")"
if [[ -z "$VERSION" ]]; then
    echo "Unable to determine the project version from CMakeLists.txt." >&2
    exit 1
fi
BUNDLE_PATH="$DIST_DIR/Swap-Alert-$VERSION.flatpak"
rm -f -- "$BUNDLE_PATH"
flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
    "$REPO_DIR" "$BUNDLE_PATH" "$APP_ID" "$BRANCH"

echo "Created: $BUNDLE_PATH"
