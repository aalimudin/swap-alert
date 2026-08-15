#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${NATIVE_BUILD_DIR:-$PROJECT_DIR/build-native}"
DIST_DIR="${DIST_DIR:-$PROJECT_DIR/dist}"
REQUESTED_FORMAT="${1:-auto}"

usage() {
    echo "Usage: $0 [auto|deb|rpm|all]" >&2
}

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "Native Linux packages must be built on Linux." >&2
    exit 1
fi

for tool in cmake cpack ctest ninja; do
    if ! command -v "$tool" >/dev/null; then
        echo "$tool is required to build native Linux packages." >&2
        exit 1
    fi
done

detect_format() {
    local distro=""
    if [[ -r /etc/os-release ]]; then
        distro="$(sed -nE 's/^(ID|ID_LIKE)=//p' /etc/os-release | tr -d '\"' | tr '\n' ' ')"
    fi

    if [[ "$distro" == *debian* || "$distro" == *ubuntu* ]]; then
        echo deb
    elif [[ "$distro" == *fedora* || "$distro" == *rhel* || "$distro" == *suse* ]]; then
        echo rpm
    elif command -v dpkg-deb >/dev/null && command -v dpkg-shlibdeps >/dev/null; then
        echo deb
    elif command -v rpmbuild >/dev/null && command -v rpm >/dev/null; then
        echo rpm
    else
        echo "Unable to detect a native package toolchain." >&2
        echo "Install dpkg-dev for DEB packages or rpm-build for RPM packages." >&2
        exit 1
    fi
}

case "$REQUESTED_FORMAT" in
    auto) FORMATS=("$(detect_format)") ;;
    deb) FORMATS=(deb) ;;
    rpm) FORMATS=(rpm) ;;
    all) FORMATS=(deb rpm) ;;
    *) usage; exit 2 ;;
esac

for format in "${FORMATS[@]}"; do
    case "$format" in
        deb)
            for tool in dpkg-deb dpkg-shlibdeps file; do
                if ! command -v "$tool" >/dev/null; then
                    echo "$tool is required for DEB packages; install dpkg-dev and file." >&2
                    exit 1
                fi
            done
            ;;
        rpm)
            for tool in rpm rpmbuild; do
                if ! command -v "$tool" >/dev/null; then
                    echo "$tool is required for RPM packages; install rpm-build." >&2
                    exit 1
                fi
            done
            ;;
    esac
done

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DSWAP_ALERT_PACKAGE_LICENSE="${PACKAGE_LICENSE:-LicenseRef-Proprietary}"
cmake --build "$BUILD_DIR" --parallel
QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
    ctest --test-dir "$BUILD_DIR" --output-on-failure

mkdir -p "$DIST_DIR"
for format in "${FORMATS[@]}"; do
    case "$format" in
        deb) generator=DEB ;;
        rpm) generator=RPM ;;
    esac
    cpack --config "$BUILD_DIR/CPackConfig.cmake" \
        -G "$generator" -B "$DIST_DIR"
done

echo "Created native package artifacts:"
find "$DIST_DIR" -maxdepth 1 -type f \( -name 'swap-alert*.deb' -o -name 'swap-alert*.rpm' \) \
    -printf '  %p\n' | sort
