#!/bin/bash

set -euo pipefail

if [[ $# -ne 1 || ( ! -f "$1" && ! -d "$1" ) ]]; then
    echo "Usage: $0 /path/to/Swap-Alert.app-or-DMG" >&2
    exit 1
fi

MOUNT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/swap-alert-verify.XXXXXX")"
ATTACHED=0

cleanup() {
    if [[ "$ATTACHED" == "1" ]]; then
        hdiutil detach "$MOUNT_DIR" -quiet || true
    fi
    rmdir "$MOUNT_DIR" 2>/dev/null || true
}
trap cleanup EXIT

INPUT_PATH="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [[ -d "$INPUT_PATH" && "$INPUT_PATH" == *.app ]]; then
    APP_PATH="$INPUT_PATH"
else
    hdiutil attach "$INPUT_PATH" -mountpoint "$MOUNT_DIR" -nobrowse -readonly -quiet
    ATTACHED=1
    APP_PATH="$(find "$MOUNT_DIR" -maxdepth 1 -name '*.app' -print -quit)"
    if [[ -z "$APP_PATH" ]]; then
        echo "No application bundle was found in the DMG." >&2
        exit 1
    fi
fi

/usr/bin/codesign --verify --deep --strict --verbose=2 "$APP_PATH"
/usr/bin/plutil -lint "$APP_PATH/Contents/Info.plist"

BAD_LINKS=0
while IFS= read -r -d '' candidate; do
    if /usr/bin/file "$candidate" | grep -q 'Mach-O'; then
        DEPENDENCIES="$(/usr/bin/otool -L "$candidate")"
        while IFS= read -r dependencyLine; do
            dependency="$(sed -E 's/^[[:space:]]+//; s/ \(compatibility.*$//' <<< "$dependencyLine")"
            if [[ "$dependency" == /opt/homebrew/* || "$dependency" == /usr/local/opt/* ]]; then
                # A dylib's first entry is its own install name. Dependencies on
                # that install name are still rejected, but the self-ID is safe.
                if [[ "$(basename "$dependency")" != "$(basename "$candidate")" ]]; then
                    echo "Development-only dependency in $candidate: $dependency" >&2
                    BAD_LINKS=1
                fi
                continue
            fi
            case "$dependency" in
                @rpath/*)
                    resolved="$APP_PATH/Contents/Frameworks/${dependency#@rpath/}"
                    ;;
                @executable_path/*)
                    resolved="$APP_PATH/Contents/MacOS/${dependency#@executable_path/}"
                    ;;
                @loader_path/*)
                    resolved="$(dirname "$candidate")/${dependency#@loader_path/}"
                    ;;
                *)
                    continue
                    ;;
            esac
            if [[ ! -e "$resolved" ]]; then
                echo "Unresolved dependency in $candidate: $dependency" >&2
                BAD_LINKS=1
            fi
        done < <(tail -n +2 <<< "$DEPENDENCIES")
    fi
done < <(find "$APP_PATH" -type f -print0)

if [[ "$BAD_LINKS" == "1" ]]; then
    exit 1
fi

if [[ "${VERIFY_GATEKEEPER:-0}" == "1" ]]; then
    /usr/sbin/spctl --assess --type execute --verbose=2 "$APP_PATH"
fi

echo "Package verification passed: $INPUT_PATH"
