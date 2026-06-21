#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 [build-dir] [output-dir]

Creates a drag-install WinUAE DMG from an existing macOS build tree.

Arguments:
  build-dir   CMake build directory containing the winuae binary.
              Defaults to WINUAE_BUILD_DIR or the current directory.
  output-dir  Directory that will receive WinUAE.app and the final DMG.
              Defaults to <build-dir>/package.

Environment:
  WINUAE_DMG_CODESIGN_IDENTITY codesign identity for the final DMG.
                               Defaults to WINUAE_CODESIGN_IDENTITY when set.
  WINUAE_DMG_CODESIGN_OPTIONS  Extra options passed to codesign for the DMG.
  WINUAE_NOTARY_PROFILE        notarytool keychain profile. When set, submit
                               the final DMG and staple the ticket.
  WINUAE_SKIP_NOTARIZATION=1   Do not submit/staple even if a profile is set.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "${script_dir}/.." && pwd)"
build_dir="${1:-${WINUAE_BUILD_DIR:-$(pwd)}}"
output_dir="${2:-${build_dir}/package}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: macOS DMG creation requires Darwin/macOS" >&2
    exit 1
fi

major="$(awk '/^#define UAEMAJOR / { print $3; exit }' "${source_dir}/include/options.h")"
minor="$(awk '/^#define UAEMINOR / { print $3; exit }' "${source_dir}/include/options.h")"
revision="$(awk '/^#define UAESUBREV / { print $3; exit }' "${source_dir}/include/options.h")"
version="${major:-0}.${minor:-0}.${revision:-0}"

app_dir="$("${script_dir}/macos-bundle.sh" "${build_dir}" "${output_dir}" | awk 'NF { line = $0 } END { print line }')"
staging_dir="${output_dir}/dmg-root"
volume_name="WinUAE"
rw_dmg="${output_dir}/WinUAE-${version}.rw.dmg"
final_dmg="${output_dir}/WinUAE-${version}.dmg"
mount_dir=""

cleanup() {
    if [[ -n "${mount_dir}" && -d "${mount_dir}" ]]; then
        hdiutil detach "${mount_dir}" -quiet >/dev/null 2>&1 || true
        rmdir "${mount_dir}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

split_extra_args() {
    if [[ -n "${1:-}" ]]; then
        # Intentionally split user-provided extra flags the same way a shell would.
        # shellcheck disable=SC2206
        extra_args=($1)
    else
        extra_args=()
    fi
}

rm -rf "${staging_dir}" "${rw_dmg}" "${final_dmg}"
mkdir -p "${staging_dir}/.background"
cp -R "${app_dir}" "${staging_dir}/WinUAE.app"
ln -s /Applications "${staging_dir}/Applications"

volume_icon="${app_dir}/Contents/Resources/WinUAE.icns"
apply_volume_icon() {
    local target_dir="$1"

    if [[ ! -f "${volume_icon}" ]]; then
        return
    fi

    cp "${volume_icon}" "${target_dir}/.VolumeIcon.icns"
    if command -v SetFile >/dev/null 2>&1; then
        SetFile -a C "${target_dir}" || true
        SetFile -a V "${target_dir}/.VolumeIcon.icns" || true
    fi
}

apply_volume_icon "${staging_dir}"

detach_existing_volume_mount() {
    local target_mount="/Volumes/${volume_name}"

    if [[ ! -e "${target_mount}" ]]; then
        return
    fi

    if ! hdiutil info | awk -F '\t' -v target="${target_mount}" '$NF == target { found = 1 } END { exit found ? 0 : 1 }'; then
        echo "error: ${target_mount} already exists and is not a mounted disk image" >&2
        echo "error: detach or rename it before creating the WinUAE DMG" >&2
        exit 1
    fi

    echo "detaching existing ${target_mount} before creating the WinUAE DMG"
    hdiutil detach "${target_mount}" -quiet
    for _ in {1..20}; do
        if [[ ! -e "${target_mount}" ]]; then
            return
        fi
        sleep 0.5
    done

    echo "error: ${target_mount} is still present after detach" >&2
    exit 1
}

background_tiff="${staging_dir}/.background/background.tiff"
background_source="${source_dir}/od-unix/graphics/dmg_background.tiff"
if [[ ! -f "${background_source}" ]]; then
    echo "error: missing DMG background: ${background_source}" >&2
    exit 1
fi
cp "${background_source}" "${background_tiff}"

detach_existing_volume_mount
hdiutil create -volname "${volume_name}" -srcfolder "${staging_dir}" -fs HFS+ -format UDRW -ov "${rw_dmg}" >/dev/null
mount_dir="$(hdiutil attach "${rw_dmg}" -readwrite -noverify -noautoopen | awk -F '\t' '/\/Volumes\// { print $NF; exit }')"
if [[ -z "${mount_dir}" || ! -d "${mount_dir}" ]]; then
    echo "error: failed to mount ${rw_dmg}" >&2
    exit 1
fi

apply_volume_icon "${mount_dir}"

require_finder_layout_records() {
    local ds_store="$1"
    local missing=0

    for record in Iloc bwsp icvp; do
        if ! LC_ALL=C grep -aq "${record}" "${ds_store}"; then
            echo "error: Finder layout metadata is missing ${record} record in ${ds_store}" >&2
            missing=1
        fi
    done

    return "${missing}"
}

if ! command -v osascript >/dev/null 2>&1; then
    echo "error: osascript is required to write the DMG Finder layout" >&2
    exit 1
fi

osascript <<EOF
tell application "Finder"
    set dmgFolder to POSIX file "${mount_dir}" as alias
    set backgroundPicture to POSIX file "${mount_dir}/.background/background.tiff" as alias
    open dmgFolder
    delay 5
    set dmgWindow to container window of dmgFolder
    set current view of dmgWindow to icon view
    set viewOptions to icon view options of dmgWindow
    set background picture of viewOptions to backgroundPicture
    set arrangement of viewOptions to not arranged
    set icon size of viewOptions to 96
    update dmgFolder
    delay 5
    try
        close dmgWindow
    end try

    open dmgFolder
    delay 1
    set dmgWindow to container window of dmgFolder
    set current view of dmgWindow to icon view
    try
        set sidebar width of dmgWindow to 0
    end try
    try
        set toolbar visible of dmgWindow to false
    end try
    try
        set statusbar visible of dmgWindow to false
    end try
    set bounds of dmgWindow to {100, 100, 740, 500}
    set viewOptions to icon view options of dmgWindow
    set arrangement of viewOptions to not arranged
    set icon size of viewOptions to 96
    set background picture of viewOptions to backgroundPicture
    set position of item "WinUAE.app" of dmgFolder to {178, 200}
    set position of item "Applications" of dmgFolder to {462, 200}
    update dmgFolder
    delay 5
    try
        close dmgWindow
    end try

    open dmgFolder
    delay 1
    try
        close container window of dmgFolder
    end try
end tell
EOF
for _ in {1..20}; do
    if [[ -f "${mount_dir}/.DS_Store" ]] && require_finder_layout_records "${mount_dir}/.DS_Store" >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done
if [[ ! -f "${mount_dir}/.DS_Store" ]]; then
    echo "error: Finder did not write ${mount_dir}/.DS_Store; DMG background layout was not saved" >&2
    exit 1
fi
require_finder_layout_records "${mount_dir}/.DS_Store"

apply_volume_icon "${mount_dir}"
if [[ -f "${volume_icon}" ]] && command -v SetFile >/dev/null 2>&1 && command -v GetFileInfo >/dev/null 2>&1; then
    volume_attrs="$(GetFileInfo -a "${mount_dir}" 2>/dev/null || true)"
    case "${volume_attrs}" in
        *C*) ;;
        *)
            echo "error: custom volume icon attribute was not set on ${mount_dir}" >&2
            exit 1
            ;;
    esac
fi

sync
hdiutil detach "${mount_dir}" -quiet
mount_dir=""
hdiutil convert "${rw_dmg}" -format UDZO -imagekey zlib-level=9 -o "${final_dmg}" -ov >/dev/null

dmg_codesign_identity="${WINUAE_DMG_CODESIGN_IDENTITY:-${WINUAE_CODESIGN_IDENTITY:-}}"
if [[ -n "${dmg_codesign_identity}" && "${dmg_codesign_identity}" != "-" ]] && command -v codesign >/dev/null 2>&1; then
    dmg_codesign_args=(--force --sign "${dmg_codesign_identity}")
    split_extra_args "${WINUAE_DMG_CODESIGN_OPTIONS:-}"
    dmg_codesign_args+=(${extra_args[@]+"${extra_args[@]}"})
    codesign "${dmg_codesign_args[@]}" "${final_dmg}"
fi

hdiutil verify "${final_dmg}" >/dev/null

if [[ "${WINUAE_SKIP_NOTARIZATION:-0}" != "1" && -n "${WINUAE_NOTARY_PROFILE:-}" ]]; then
    if ! command -v xcrun >/dev/null 2>&1; then
        echo "error: notarization requires xcrun/notarytool" >&2
        exit 1
    fi
    xcrun notarytool submit "${final_dmg}" --keychain-profile "${WINUAE_NOTARY_PROFILE}" --wait
    xcrun stapler staple "${final_dmg}"
fi

"${script_dir}/macos-verify-dmg.sh" "${final_dmg}" >/dev/null
rm -rf "${rw_dmg}" "${staging_dir}"

echo "${final_dmg}"
