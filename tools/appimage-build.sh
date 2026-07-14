#!/usr/bin/env bash
set -euo pipefail

# Build a relocatable WinUAE AppImage from the Unix/Linux CMake install tree.
#
# This mirrors tools/debian-build-package.sh: it configures and builds the
# project, installs the exact same files the .deb ships (/usr/bin/winuae, the
# .desktop file, the hicolor icon, and the qemu-uae.so PPC plugin) into a
# throwaway AppDir, and then wraps that AppDir with linuxdeploy + appimagetool.
#
# Because the integrated configuration UI is a Qt6 application, the Qt runtime
# is bundled via linuxdeploy-plugin-qt. The qemu-uae.so plugin is scanned so its
# own shared-library dependencies (glib, etc.) are pulled in as well.

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${WINUAE_APPIMAGE_BUILD_DIR:-/tmp/winuae_appimage_build}"
build_type="${WINUAE_APPIMAGE_BUILD_TYPE:-RelWithDebInfo}"
appdir="${WINUAE_APPIMAGE_APPDIR:-}"
output_dir="${WINUAE_APPIMAGE_OUTPUT_DIR:-}"
jobs="${WINUAE_APPIMAGE_JOBS:-}"
cmake_args=()

# AppImage tooling. Pin these via the environment for reproducible releases.
tool_cache="${WINUAE_APPIMAGE_TOOL_DIR:-${build_dir}/appimage-tools}"
linuxdeploy_url="${WINUAE_APPIMAGE_LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage}"
linuxdeploy_qt_url="${WINUAE_APPIMAGE_LINUXDEPLOY_QT_URL:-https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage}"
appimagetool_url="${WINUAE_APPIMAGE_APPIMAGETOOL_URL:-https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage}"

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] [-- <cmake-args>]

Options:
  --build-dir DIR     CMake build directory (default: ${build_dir})
  --build-type TYPE   CMake build type (default: ${build_type})
  --appdir DIR        AppDir staging directory (default: <build-dir>/AppDir)
  --output-dir DIR    Where the .AppImage is written (default: <build-dir>)
  --jobs N            Parallel build jobs (default: detected CPU count)
  -h, --help          Show this help

Extra arguments after -- are passed to the CMake configure step, matching
tools/debian-build-package.sh (e.g. -DWINUAE_QEMU_UAE_BUILDER_DIR=...).
EOF
}

while (($#)); do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { echo "error: --build-dir requires a directory" >&2; exit 2; }
            build_dir="$2"; shift 2 ;;
        --build-type)
            [[ $# -ge 2 ]] || { echo "error: --build-type requires a value" >&2; exit 2; }
            build_type="$2"; shift 2 ;;
        --appdir)
            [[ $# -ge 2 ]] || { echo "error: --appdir requires a directory" >&2; exit 2; }
            appdir="$2"; shift 2 ;;
        --output-dir)
            [[ $# -ge 2 ]] || { echo "error: --output-dir requires a directory" >&2; exit 2; }
            output_dir="$2"; shift 2 ;;
        --jobs|-j)
            [[ $# -ge 2 ]] || { echo "error: --jobs requires a value" >&2; exit 2; }
            jobs="$2"; shift 2 ;;
        --help|-h)
            usage; exit 0 ;;
        --)
            shift; cmake_args+=("$@"); break ;;
        *)
            echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != "Linux" ]; then
    echo "error: AppImages can only be built on Linux" >&2
    exit 1
fi

for tool in cmake curl file; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "error: required tool not found: ${tool}" >&2
        exit 1
    fi
done

if [ -z "${jobs}" ]; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"
fi
if [ -z "${appdir}" ]; then
    appdir="${build_dir}/AppDir"
fi
if [ -z "${output_dir}" ]; then
    output_dir="${build_dir}"
fi

# Derive the version the same way CMake does, so the AppImage filename matches
# the .deb/DMG (WinUAE-<major>.<minor>.<subrev>-x86_64.AppImage).
read_version_field() {
    local macro="$1"
    sed -n "s/^#define ${macro}[[:space:]]\\+\\([0-9]\\+\\).*/\\1/p" \
        "${source_dir}/include/options.h" | head -n1
}
version_major="$(read_version_field UAEMAJOR)"
version_minor="$(read_version_field UAEMINOR)"
version_subrev="$(read_version_field UAESUBREV)"
version="${version_major:-0}.${version_minor:-0}.${version_subrev:-0}"

echo "==> Configuring WinUAE (${build_type})"
cmake -S "${source_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    "${cmake_args[@]}"

echo "==> Building winuae_unix and the qemu-uae plugin"
cmake --build "${build_dir}" --parallel "${jobs}"

echo "==> Installing into AppDir: ${appdir}"
rm -rf "${appdir}"
DESTDIR="${appdir}" cmake --install "${build_dir}" --prefix /usr

exe="${appdir}/usr/bin/winuae"
desktop="${appdir}/usr/share/applications/net.winuae.WinUAE.desktop"
icon="${appdir}/usr/share/icons/hicolor/256x256/apps/winuae.png"
for required in "${exe}" "${desktop}" "${icon}"; do
    if [ ! -e "${required}" ]; then
        echo "error: expected file missing from install tree: ${required}" >&2
        exit 1
    fi
done

# The PPC plugin is installed under /usr/lib*/winuae/plugins. Feed its directory
# to linuxdeploy so its own shared-library dependencies get bundled too.
plugin_deploy_args=()
while IFS= read -r plugin_dir; do
    [ -n "${plugin_dir}" ] || continue
    echo "==> Bundling plugin dependencies from ${plugin_dir}"
    plugin_deploy_args+=(--deploy-deps-only "${plugin_dir}")
done < <(find "${appdir}/usr" -type d -path '*/winuae/plugins' 2>/dev/null)

# Fetch the AppImage tooling.
mkdir -p "${tool_cache}"
fetch_tool() {
    local url="$1" dest="$2"
    if [ ! -x "${dest}" ]; then
        echo "==> Downloading $(basename "${dest}")"
        curl -fL --retry 3 -o "${dest}" "${url}"
        chmod +x "${dest}"
    fi
}
linuxdeploy="${tool_cache}/linuxdeploy-x86_64.AppImage"
linuxdeploy_qt="${tool_cache}/linuxdeploy-plugin-qt-x86_64.AppImage"
appimagetool="${tool_cache}/appimagetool-x86_64.AppImage"
fetch_tool "${linuxdeploy_url}" "${linuxdeploy}"
fetch_tool "${linuxdeploy_qt_url}" "${linuxdeploy_qt}"
fetch_tool "${appimagetool_url}" "${appimagetool}"

# CI containers rarely have FUSE; make the AppImage tools self-extract instead.
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"
# linuxdeploy-plugin-qt is discovered by name on PATH.
export PATH="${tool_cache}:${PATH}"
# Name the artifact WinUAE-<version>-x86_64.AppImage.
export VERSION="${version}"
export OUTPUT="${output_dir}/WinUAE-${version}-x86_64.AppImage"

mkdir -p "${output_dir}"

echo "==> Running linuxdeploy (bundling Qt runtime)"
"${linuxdeploy}" \
    --appdir "${appdir}" \
    --executable "${exe}" \
    --desktop-file "${desktop}" \
    --icon-file "${icon}" \
    "${plugin_deploy_args[@]}" \
    --plugin qt \
    --output appimage

if [ ! -f "${OUTPUT}" ]; then
    # linuxdeploy occasionally ignores $OUTPUT and writes to the CWD; recover
    # the most recently produced AppImage from there.
    produced="$(find . -maxdepth 1 -type f -name '*.AppImage' \
        -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n1 | cut -d' ' -f2-)"
    if [ -n "${produced}" ]; then
        mv -f "${produced}" "${OUTPUT}"
    fi
fi

if [ ! -f "${OUTPUT}" ]; then
    echo "error: linuxdeploy completed but no AppImage was produced" >&2
    exit 1
fi

echo
echo "Built AppImage:"
echo "  ${OUTPUT}"
ls -lh "${OUTPUT}"
