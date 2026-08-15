#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo "Usage: $0 /path/to/swap-alert-package.deb-or.rpm" >&2
    exit 1
fi

PACKAGE_PATH="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
REQUIRED_PATHS=(
    usr/bin/swap-alert
    usr/share/applications/com.swapalert.app.desktop
    usr/share/icons/hicolor/scalable/apps/com.swapalert.app.svg
    usr/share/metainfo/com.swapalert.app.metainfo.xml
)

case "$PACKAGE_PATH" in
    *.deb)
        command -v dpkg-deb >/dev/null || {
            echo "dpkg-deb is required to verify a DEB package." >&2
            exit 1
        }
        [[ "$(dpkg-deb -f "$PACKAGE_PATH" Package)" == "swap-alert" ]] || {
            echo "Unexpected DEB package name." >&2
            exit 1
        }
        CONTENTS="$(dpkg-deb --contents "$PACKAGE_PATH")"
        DEPENDENCIES="$(dpkg-deb -f "$PACKAGE_PATH" Depends)"
        dpkg-deb --info "$PACKAGE_PATH" >/dev/null
        ;;
    *.rpm)
        command -v rpm >/dev/null || {
            echo "rpm is required to verify an RPM package." >&2
            exit 1
        }
        [[ "$(rpm -qp --queryformat '%{NAME}' "$PACKAGE_PATH")" == "swap-alert" ]] || {
            echo "Unexpected RPM package name." >&2
            exit 1
        }
        CONTENTS="$(rpm -qpl "$PACKAGE_PATH")"
        DEPENDENCIES="$(rpm -qp --requires "$PACKAGE_PATH")"
        rpm -qpi "$PACKAGE_PATH" >/dev/null
        ;;
    *)
        echo "Expected a .deb or .rpm package." >&2
        exit 2
        ;;
esac

for required in "${REQUIRED_PATHS[@]}"; do
    if ! grep -Fq "$required" <<<"$CONTENTS"; then
        echo "Package payload is missing: /$required" >&2
        exit 1
    fi
done

if ! grep -Eqi 'qt6|libQt6' <<<"$DEPENDENCIES"; then
    echo "Package metadata does not contain an automatic Qt 6 dependency." >&2
    exit 1
fi

echo "Native package verification passed: $PACKAGE_PATH"
