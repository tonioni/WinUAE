#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 app-path

Launches a packaged WinUAE.app from an isolated HOME and verifies that the
Qt configuration window appears. This is a release smoke test, not a normal
build step.

Environment:
  WINUAE_MACOS_SMOKE_TIMEOUT       Seconds to wait. Defaults to 20.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

app_path="${1:-}"
smoke_home=""
smoke_log=""
timeout_file=""
launched=0

cleanup() {
    if [[ "${launched}" == "1" && -n "${bundle_id:-}" ]]; then
        osascript - "${bundle_id}" <<'APPLESCRIPT' >/dev/null 2>&1 || true
on run argv
    tell application id (item 1 of argv) to quit
end run
APPLESCRIPT
    fi
    if [[ -n "${smoke_home}" && -d "${smoke_home}" ]]; then
        rm -rf "${smoke_home}"
    fi
    if [[ -n "${smoke_log}" && -f "${smoke_log}" ]]; then
        rm -f "${smoke_log}"
    fi
    if [[ -n "${timeout_file}" && -f "${timeout_file}" ]]; then
        rm -f "${timeout_file}"
    fi
}
trap cleanup EXIT

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: macOS app smoke testing requires Darwin/macOS" >&2
    exit 1
fi

if [[ -z "${app_path}" ]]; then
    usage >&2
    exit 1
fi

if [[ -d "${app_path}/WinUAE.app" ]]; then
    app_path="${app_path}/WinUAE.app"
fi

executable="${app_path}/Contents/MacOS/WinUAE"
if [[ ! -x "${executable}" ]]; then
    echo "error: app executable not found: ${executable}" >&2
    exit 1
fi

bundle_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "${app_path}/Contents/Info.plist" 2>/dev/null || true)"
if [[ -z "${bundle_id}" ]]; then
    echo "error: CFBundleIdentifier missing from ${app_path}/Contents/Info.plist" >&2
    exit 1
fi

timeout="${WINUAE_MACOS_SMOKE_TIMEOUT:-20}"
smoke_home="$(mktemp -d -t winuae-app-smoke-home.XXXXXX)"
smoke_log="$(mktemp -t winuae-app-smoke-log.XXXXXX)"
timeout_file="$(mktemp -t winuae-app-smoke-timeout.XXXXXX)"
rm -f "${timeout_file}"

open -n -F -W \
    --env "HOME=${smoke_home}" \
    --env "WINUAE_MACOS_APP_SMOKE=1" \
    -o "${smoke_log}" \
    --stderr "${smoke_log}" \
    "${app_path}" &
open_pid="$!"
launched=1

(
    sleep "${timeout}"
    if kill -0 "${open_pid}" >/dev/null 2>&1; then
        : > "${timeout_file}"
        osascript - "${bundle_id}" <<'APPLESCRIPT' >/dev/null 2>&1 || true
on run argv
    tell application id (item 1 of argv) to quit
end run
APPLESCRIPT
    fi
) &
watchdog_pid="$!"

open_status=0
wait "${open_pid}" || open_status="$?"
kill "${watchdog_pid}" >/dev/null 2>&1 || true
wait "${watchdog_pid}" >/dev/null 2>&1 || true

if [[ -f "${timeout_file}" ]]; then
    echo "error: timed out waiting for packaged Qt app smoke mode to finish" >&2
    sed -n '1,120p' "${smoke_log}" >&2
    exit 1
fi
if [[ "${open_status}" != "0" ]]; then
    echo "error: packaged app launch failed with status ${open_status}" >&2
    sed -n '1,120p' "${smoke_log}" >&2
    exit 1
fi
if ! grep -q '^WINUAE_QT_SMOKE_WINDOW_VISIBLE$' "${smoke_log}"; then
    echo "error: packaged app did not report a visible Qt configuration window" >&2
    sed -n '1,120p' "${smoke_log}" >&2
    exit 1
fi

echo "verified packaged Qt app launch: ${app_path}"
