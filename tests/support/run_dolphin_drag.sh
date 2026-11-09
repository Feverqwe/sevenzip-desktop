#!/usr/bin/env bash
set -euo pipefail

if [[ ${1:-} != nested ]]; then
    exec dbus-run-session -- xvfb-run -a -s '-screen 0 1600x900x24' bash "$0" nested "$@"
fi
shift

# KIO workers create Unix sockets under this directory.
fixture="$(mktemp -d /tmp/sz-dolphin.XXXXXX)"
compositor=
cleanup() {
    if [[ -n $compositor ]]; then
        kill "$compositor" 2>/dev/null || true
        wait "$compositor" 2>/dev/null || true
    fi
    rm -rf "$fixture"
}
trap cleanup EXIT

export XDG_RUNTIME_DIR="$fixture" WAYLAND_DISPLAY=sevenzip-test-wayland
export XDG_CONFIG_HOME="$fixture/config" XDG_CACHE_HOME="$fixture/cache" XDG_DATA_HOME="$fixture/data"
weston --backend=x11-backend.so --use-pixman --width=1500 --height=800 --debug \
    --socket="$WAYLAND_DISPLAY" --log="$fixture/weston.log" &
compositor=$!
for ((attempt=0; attempt<100; ++attempt)); do
    [[ -S "$fixture/$WAYLAND_DISPLAY" ]] && break
    sleep 0.05
done
if [[ ! -S "$fixture/$WAYLAND_DISPLAY" ]]; then
    cat "$fixture/weston.log"
    exit 1
fi

export QT_QPA_PLATFORM=wayland SEVENZIP_TEST_DOLPHIN=1
export SEVENZIP_SETTINGS_PATH="$fixture/settings.ini"
timeout -k 2 25 "$@" archiveDragReachesNativeDropReceiver:dolphin-cancel \
    archiveDragReachesNativeDropReceiver:external \
    archiveDragReachesNativeDropReceiver:dolphin-clipboard
