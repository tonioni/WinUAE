#!/usr/bin/env bash
set -euo pipefail

# Build a relocatable WinUAE AppImage from the Unix/Linux CMake install tree.
#
# This mirrors tools/debian-build-package.sh: it configures and builds the
# project, installs the exact same files the .deb ships (/usr/bin/winuae, the
# .desktop file, the hicolor icon, qemu-uae.so, and FloppyBridge.so) into a
# throwaway AppDir, and then wraps that AppDir with linuxdeploy + appimagetool.
#
# Because the integrated configuration UI is a Qt6 application, the Qt runtime
# is bundled via linuxdeploy-plugin-qt. The qemu-uae.so plugin is scanned so its
# own shared-library dependencies (glib, etc.) are pulled in as well. SDL loads
# libdecor dynamically, so its runtime and decoration plugin are staged
# explicitly rather than relying on them being installed on the target host.

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

for tool in cmake curl file pkg-config; do
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

echo "==> Building winuae_unix and runtime plugins"
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

if grep -Eq '^WINUAE_UNIX_BUNDLE_DRIVE_SOUNDS:BOOL=(ON|TRUE|1)$' \
    "${build_dir}/CMakeCache.txt"; then
    drive_sounds_dir="$(find "${appdir}/usr" -type d \
        -path '*/winuae/plugins/floppysounds' -print -quit)"
    if [[ -z "${drive_sounds_dir}" ]]; then
        echo "error: AppDir does not contain plugins/floppysounds" >&2
        exit 1
    fi
    "${source_dir}/tools/require-drive-sounds.sh" "${drive_sounds_dir}"
fi

# SDL's Wayland backend dlopens libdecor, so linuxdeploy cannot discover it by
# walking DT_NEEDED entries. Find and stage both the core runtime and one actual
# decoration plugin. The workflow installs the small cairo plugin; gtk remains
# a supported fallback for local builds on distributions that only ship it.
libdecor_libdir="$(pkg-config --variable=libdir libdecor-0 2>/dev/null || true)"
libdecor_library="${WINUAE_APPIMAGE_LIBDECOR_LIBRARY:-}"
if [[ -z "${libdecor_library}" && -n "${libdecor_libdir}" ]]; then
    for candidate in \
        "${libdecor_libdir}/libdecor-0.so.0" \
        "${libdecor_libdir}/libdecor-0.so"; do
        if [[ -e "${candidate}" ]]; then
            libdecor_library="${candidate}"
            break
        fi
    done
fi
if [[ -z "${libdecor_library}" || ! -e "${libdecor_library}" ]]; then
    echo "error: libdecor runtime not found; install libdecor-0" >&2
    exit 1
fi

libdecor_plugin="${WINUAE_APPIMAGE_LIBDECOR_PLUGIN:-}"
if [[ -z "${libdecor_plugin}" && -n "${libdecor_libdir}" ]]; then
    for plugin_name in libdecor-cairo.so libdecor-gtk.so; do
        candidate="${libdecor_libdir}/libdecor/plugins-1/${plugin_name}"
        if [[ -f "${candidate}" ]]; then
            libdecor_plugin="${candidate}"
            break
        fi
    done
fi
if [[ -z "${libdecor_plugin}" || ! -f "${libdecor_plugin}" ]]; then
    echo "error: libdecor decoration plugin not found" >&2
    echo "       install libdecor-0-plugin-1-cairo (or a gtk plugin)" >&2
    exit 1
fi

libdecor_appdir="${appdir}/usr/lib/libdecor/plugins-1"
mkdir -p "${libdecor_appdir}"
cp -L "${libdecor_plugin}" "${libdecor_appdir}/$(basename "${libdecor_plugin}")"

# Keep portable artifacts aligned with the Debian package: when the real-drive
# backend is enabled, its FloppyBridge module must make it into the AppDir.
if grep -Eq '^WINUAE_UNIX_WITH_FLOPPYBRIDGE:BOOL=(ON|TRUE|1)$' \
    "${build_dir}/CMakeCache.txt"; then
    if ! find "${appdir}/usr" -type f -path '*/winuae/plugins/FloppyBridge.so' \
        -print -quit | grep -q .; then
        echo "error: AppDir does not contain FloppyBridge.so" >&2
        exit 1
    fi
fi

# The PPC plugin is installed under /usr/lib*/winuae/plugins. Feed its directory
# to linuxdeploy so its own shared-library dependencies get bundled too.
plugin_deploy_args=()
while IFS= read -r plugin_dir; do
    [ -n "${plugin_dir}" ] || continue
    echo "==> Bundling plugin dependencies from ${plugin_dir}"
    plugin_deploy_args+=(--deploy-deps-only "${plugin_dir}")
done < <(find "${appdir}/usr" -type d -path '*/winuae/plugins' 2>/dev/null)
echo "==> Bundling libdecor plugin dependencies from ${libdecor_appdir}"
plugin_deploy_args+=(--deploy-deps-only "${libdecor_appdir}")

# Point libdecor at the bundled plugin. The normal linuxdeploy AppRun for this
# application is a symlink to winuae, so this wrapper preserves that behavior
# while adding the one runtime variable libdecor needs for relocation.
custom_apprun="${build_dir}/AppRun.winuae"
cat >"${custom_apprun}" <<'EOF'
#!/bin/sh
set -eu

appdir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export LIBDECOR_PLUGIN_DIR="${appdir}/usr/lib/libdecor/plugins-1"
exec "${appdir}/usr/bin/winuae" "$@"
EOF
chmod +x "${custom_apprun}"

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
    --library "${libdecor_library}" \
    --desktop-file "${desktop}" \
    --icon-file "${icon}" \
    --custom-apprun "${custom_apprun}" \
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
