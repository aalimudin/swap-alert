#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo "Usage: $0 /path/to/Swap-Alert.flatpak" >&2
    exit 1
fi

INPUT_PATH="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/swap-alert-flatpak-verify.XXXXXX")"
cleanup() {
    rm -rf -- "$TEMP_DIR"
}
trap cleanup EXIT

flatpak build-import-bundle --no-update-summary "$TEMP_DIR/repo" "$INPUT_PATH"
REF_PATH="$TEMP_DIR/repo/refs/heads/app/com.swapalert.app/$(flatpak --default-arch)/stable"
if [[ ! -s "$REF_PATH" ]]; then
    echo "The expected Flatpak application ref was not imported." >&2
    exit 1
fi

echo "Flatpak verification passed: $INPUT_PATH"
