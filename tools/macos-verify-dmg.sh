#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 dmg-path

Verifies a WinUAE macOS drag-install DMG.

The check mounts the image, validates the app/layout resources, and runs the
bundled executable with -h from an isolated HOME to catch missing runtime
libraries without starting emulation.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

dmg_path="${1:-}"
mount_dir=""
launch_home=""
launch_log=""

cleanup() {
    if [[ -n "${launch_home}" && -d "${launch_home}" ]]; then
        rm -rf "${launch_home}"
    fi
    if [[ -n "${launch_log}" && -f "${launch_log}" ]]; then
        rm -f "${launch_log}"
    fi
    if [[ -n "${mount_dir}" && -d "${mount_dir}" ]]; then
        hdiutil detach "${mount_dir}" -quiet >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: macOS DMG verification requires Darwin/macOS" >&2
    exit 1
fi

if [[ -z "${dmg_path}" ]]; then
    usage >&2
    exit 1
fi

if [[ ! -f "${dmg_path}" ]]; then
    echo "error: DMG not found: ${dmg_path}" >&2
    exit 1
fi

hdiutil verify "${dmg_path}" >/dev/null
mount_dir="$(hdiutil attach "${dmg_path}" -readonly -noverify -noautoopen | awk -F '\t' '/\/Volumes\// { print $NF; exit }')"
if [[ -z "${mount_dir}" || ! -d "${mount_dir}" ]]; then
    echo "error: failed to mount ${dmg_path}" >&2
    exit 1
fi

app_dir="${mount_dir}/WinUAE.app"
info_plist="${app_dir}/Contents/Info.plist"

require_path() {
    local path="$1"
    local description="$2"
    if [[ ! -e "${path}" ]]; then
        echo "error: missing ${description}: ${path}" >&2
        exit 1
    fi
}

require_file() {
    local path="$1"
    local description="$2"
    if [[ ! -f "${path}" ]]; then
        echo "error: missing ${description}: ${path}" >&2
        exit 1
    fi
}

require_path "${app_dir}" "app bundle"
require_file "${info_plist}" "Info.plist"
require_file "${app_dir}/Contents/MacOS/WinUAE" "bundle executable"
if [[ ! -x "${app_dir}/Contents/MacOS/WinUAE" ]]; then
    echo "error: bundle executable is not executable: ${app_dir}/Contents/MacOS/WinUAE" >&2
    exit 1
fi
require_file "${app_dir}/Contents/Resources/WinUAE.icns" "application icon"
require_file "${app_dir}/Contents/Resources/README_unix.md" "bundled README"

if [[ ! -L "${mount_dir}/Applications" ]]; then
    echo "error: missing /Applications symlink" >&2
    exit 1
fi
if [[ "$(readlink "${mount_dir}/Applications")" != "/Applications" ]]; then
    echo "error: Applications symlink does not point to /Applications" >&2
    exit 1
fi

require_file "${mount_dir}/.DS_Store" "Finder layout metadata"
require_file "${mount_dir}/.background/background.tiff" "Finder background image"
require_file "${mount_dir}/.VolumeIcon.icns" "volume icon"

for record in Iloc bwsp icvp; do
    if ! LC_ALL=C grep -aq "${record}" "${mount_dir}/.DS_Store"; then
        echo "error: Finder layout metadata is missing ${record} record in ${mount_dir}/.DS_Store" >&2
        exit 1
    fi
done

if command -v GetFileInfo >/dev/null 2>&1; then
    volume_attrs="$(GetFileInfo -a "${mount_dir}" 2>/dev/null || true)"
    case "${volume_attrs}" in
        *C*) ;;
        *)
            echo "error: custom volume icon attribute is not set on ${mount_dir}" >&2
            exit 1
            ;;
    esac
fi

plist_get() {
    /usr/libexec/PlistBuddy -c "Print $1" "${info_plist}" 2>/dev/null || true
}

if [[ "$(plist_get ':CFBundleExecutable')" != "WinUAE" ]]; then
    echo "error: CFBundleExecutable is not WinUAE" >&2
    exit 1
fi
if [[ "$(plist_get ':CFBundleIconFile')" != "WinUAE.icns" ]]; then
    echo "error: CFBundleIconFile is not WinUAE.icns" >&2
    exit 1
fi
if [[ "$(plist_get ':CFBundleDocumentTypes:0:CFBundleTypeExtensions:0')" != "uae" ]]; then
    echo "error: .uae document type is not registered in Info.plist" >&2
    exit 1
fi

launch_home="$(mktemp -d -t winuae-dmg-home.XXXXXX)"
launch_log="$(mktemp -t winuae-dmg-launch.XXXXXX)"
if ! HOME="${launch_home}" QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy "${app_dir}/Contents/MacOS/WinUAE" -h >"${launch_log}" 2>&1; then
    echo "error: bundled executable did not start successfully from the mounted DMG" >&2
    sed -n '1,120p' "${launch_log}" >&2
    exit 1
fi

echo "verified ${dmg_path}"
