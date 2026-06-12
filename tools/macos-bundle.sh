#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 [build-dir] [output-dir]

Creates a local WinUAE.app bundle from an existing macOS build tree.

Arguments:
  build-dir   CMake build directory containing the winuae binary.
              Defaults to WINUAE_BUILD_DIR or the current directory.
  output-dir  Directory that will receive WinUAE.app.
              Defaults to <build-dir>/package.

Environment:
  WINUAE_SKIP_MACDEPLOYQT=1  Do not run macdeployqt even if it is available.
  WINUAE_SKIP_MACOS_DEPLOYMENT_CHECK=1
                              Do not check bundled Mach-O deployment targets.
  WINUAE_SKIP_CODESIGN=1     Do not ad-hoc codesign the bundle.
  WINUAE_CODESIGN_IDENTITY   codesign identity. Defaults to "-" for ad-hoc.
  WINUAE_CODESIGN_OPTIONS    Extra options passed to codesign, for example
                              "--options runtime --timestamp".
  WINUAE_CODESIGN_ENTITLEMENTS
                              Optional entitlements plist passed to codesign.
  WINUAE_QEMU_UAE_PLUGIN      Optional qemu-uae.so path to copy into
                              Contents/PlugIns.
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
executable="${build_dir}/winuae"
if [[ ! -x "${executable}" && -x "${build_dir}/winuae_unix" ]]; then
    # Build directories from before the binary rename.
    executable="${build_dir}/winuae_unix"
fi
app_dir="${output_dir}/WinUAE.app"
contents_dir="${app_dir}/Contents"
macos_dir="${contents_dir}/MacOS"
resources_dir="${contents_dir}/Resources"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: macOS app bundling requires Darwin/macOS" >&2
    exit 1
fi

if [[ ! -x "${executable}" ]]; then
    echo "error: executable not found: ${executable}" >&2
    echo "hint: build the winuae_unix target first, or pass the CMake build directory" >&2
    exit 1
fi

major="$(awk '/^#define UAEMAJOR / { print $3; exit }' "${source_dir}/include/options.h")"
minor="$(awk '/^#define UAEMINOR / { print $3; exit }' "${source_dir}/include/options.h")"
revision="$(awk '/^#define UAESUBREV / { print $3; exit }' "${source_dir}/include/options.h")"
version="${major:-0}.${minor:-0}.${revision:-0}"
deployment_target="${WINUAE_MACOS_DEPLOYMENT_TARGET:-}"
if [[ -z "${deployment_target}" && -f "${build_dir}/CMakeCache.txt" ]]; then
    deployment_target="$(awk -F= '/^CMAKE_OSX_DEPLOYMENT_TARGET:/ { print $2; exit }' "${build_dir}/CMakeCache.txt")"
fi
if [[ -z "${deployment_target}" ]]; then
    deployment_target="13.0"
fi

cmake_cache_value() {
    local key="$1"
    if [[ -f "${build_dir}/CMakeCache.txt" ]]; then
        awk -F= -v key="${key}" '$1 ~ "^" key ":" { print $2; exit }' "${build_dir}/CMakeCache.txt"
    fi
}

append_qt_plugin_candidate() {
    local candidate="$1"
    if [[ -n "${candidate}" && -f "${candidate}/platforms/libqcocoa.dylib" ]]; then
        qt_plugin_candidates+=("${candidate}")
    fi
}

split_extra_args() {
    if [[ -n "${1:-}" ]]; then
        # Intentionally split user-provided extra flags the same way a shell would.
        # shellcheck disable=SC2206
        extra_args=($1)
    else
        extra_args=()
    fi
}

path_in_list() {
    local needle="$1"
    shift
    local item
    for item in "$@"; do
        if [[ "${item}" == "${needle}" ]]; then
            return 0
        fi
    done
    return 1
}

copy_private_dylib_deps() {
    local root_binary="$1"
    local install_prefix="$2"
    local frameworks_dir="${contents_dir}/Frameworks"
    local queue=("${root_binary}")
    local visited=()

    mkdir -p "${frameworks_dir}"
    while [[ ${#queue[@]} -gt 0 ]]; do
        local binary="${queue[0]}"
        queue=("${queue[@]:1}")
        if path_in_list "${binary}" ${visited[@]+"${visited[@]}"}; then
            continue
        fi
        visited+=("${binary}")

        local dep
        while IFS= read -r dep; do
            case "${dep}" in
                ""|@*|/usr/lib/*|/System/Library/*)
                    continue
                    ;;
            esac
            if [[ ! -f "${dep}" ]]; then
                continue
            fi

            local name target
            name="$(basename "${dep}")"
            target="${frameworks_dir}/${name}"
            if [[ ! -f "${target}" ]]; then
                cp "${dep}" "${target}"
                chmod u+w "${target}" 2>/dev/null || true
                install_name_tool -id "@rpath/${name}" "${target}" \
                    2>/dev/null || true
            fi
            install_name_tool -change "${dep}" "${install_prefix}/${name}" \
                "${binary}" 2>/dev/null || true
            if ! path_in_list "${target}" ${visited[@]+"${visited[@]}"} \
                    && ! path_in_list "${target}" ${queue[@]+"${queue[@]}"}; then
                queue+=("${target}")
            fi
        done < <(otool -L "${binary}" | awk 'NR > 1 { print $1 }')
    done
}

rm -rf "${app_dir}"
mkdir -p "${macos_dir}" "${resources_dir}/od-win32/resources"

cp "${executable}" "${macos_dir}/WinUAE"
find "${source_dir}/od-win32/resources" -maxdepth 1 -type f \
    ! -name '*.rc' \
    ! -name '*.manifest' \
    ! -name 'resource.h' \
    -exec cp '{}' "${resources_dir}/od-win32/resources/" ';'
mkdir -p "${resources_dir}/od-unix/share/filter-presets"
cp "${source_dir}/od-unix/share/filter-presets/"*.filter \
    "${resources_dir}/od-unix/share/filter-presets/"
cp "${source_dir}/README_unix.md" "${resources_dir}/README_unix.md"

if [[ -f "${source_dir}/od-win32/resources/winuae.ico" ]]; then
    cp "${source_dir}/od-win32/resources/winuae.ico" "${resources_dir}/winuae.ico"
    if command -v sips >/dev/null 2>&1; then
        sips -s format icns "${source_dir}/od-win32/resources/winuae.ico" --out "${resources_dir}/WinUAE.icns" >/dev/null
    fi
fi

cat > "${contents_dir}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
    "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>WinUAE</string>
    <key>CFBundleExecutable</key>
    <string>WinUAE</string>
    <key>CFBundleIdentifier</key>
    <string>net.winuae.unix</string>
    <key>CFBundleIconFile</key>
    <string>WinUAE.icns</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>WinUAE</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${version}</string>
    <key>CFBundleVersion</key>
    <string>${version}</string>
    <key>LSMinimumSystemVersion</key>
    <string>${deployment_target}</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>uae</string>
            </array>
            <key>CFBundleTypeName</key>
            <string>WinUAE Configuration</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
        </dict>
    </array>
</dict>
</plist>
EOF

qemu_uae_plugin=""
for candidate in \
    "${WINUAE_QEMU_UAE_PLUGIN:-}" \
    "${build_dir}/qemu-uae.so" \
    "${build_dir}/plugins/qemu-uae.so" \
    "${source_dir}/../qemu-uae-v11.0/build/qemu-uae.so"
do
    if [[ -n "${candidate}" && -f "${candidate}" ]]; then
        qemu_uae_plugin="${candidate}"
        break
    fi
done

copy_qemu_uae_plugin() {
    if [[ -n "${qemu_uae_plugin}" ]]; then
        mkdir -p "${contents_dir}/PlugIns"
        cp "${qemu_uae_plugin}" "${contents_dir}/PlugIns/qemu-uae.so"
        install_name_tool -add_rpath "@loader_path/../Frameworks" \
            "${contents_dir}/PlugIns/qemu-uae.so" 2>/dev/null || true
        copy_private_dylib_deps \
            "${contents_dir}/PlugIns/qemu-uae.so" \
            "@loader_path/../Frameworks"
    fi
}

macdeployqt_executable="${WINUAE_MACDEPLOYQT:-}"
if [[ -z "${macdeployqt_executable}" ]]; then
    macdeployqt_executable="$(cmake_cache_value MACDEPLOYQT_EXECUTABLE)"
fi
if [[ -z "${macdeployqt_executable}" ]]; then
    macdeployqt_executable="$(command -v macdeployqt || true)"
fi

if [[ "${WINUAE_SKIP_MACDEPLOYQT:-0}" != "1" && -n "${macdeployqt_executable}" && -x "${macdeployqt_executable}" ]]; then
    macdeployqt_args=("${app_dir}" -always-overwrite -no-plugins -verbose=0)
    if "${macdeployqt_executable}" -help 2>&1 | grep -q -- "-no-codesign"; then
        macdeployqt_args+=(-no-codesign)
    fi
    "${macdeployqt_executable}" "${macdeployqt_args[@]}"

    qt_plugin_root=""
    qt_plugin_candidates=()
    append_qt_plugin_candidate "${WINUAE_QT_PLUGIN_ROOT:-}"

    qt6_dir="$(cmake_cache_value Qt6_DIR)"
    if [[ -n "${qt6_dir}" && -d "${qt6_dir}" ]]; then
        qt_prefix="$(cd "${qt6_dir}/../../.." && pwd)"
        append_qt_plugin_candidate "${qt_prefix}/plugins"
        append_qt_plugin_candidate "${qt_prefix}/share/qt/plugins"
        append_qt_plugin_candidate "${qt_prefix}/share/qt6/plugins"
        if [[ -x "${qt_prefix}/bin/qtpaths" ]]; then
            append_qt_plugin_candidate "$("${qt_prefix}/bin/qtpaths" --plugin-dir 2>/dev/null || true)"
        fi
    fi

    macdeployqt_dir="$(cd "$(dirname "${macdeployqt_executable}")" && pwd)"
    if [[ -x "${macdeployqt_dir}/qtpaths" ]]; then
        append_qt_plugin_candidate "$("${macdeployqt_dir}/qtpaths" --plugin-dir 2>/dev/null || true)"
    fi

    for candidate in "${qt_plugin_candidates[@]}" \
        /opt/homebrew/share/qt/plugins \
        /opt/homebrew/opt/qt/share/qt/plugins \
        /opt/homebrew/opt/qt6/share/qt/plugins \
        /opt/homebrew/opt/qt@6/share/qt/plugins \
        /usr/local/share/qt/plugins \
        /usr/local/opt/qt/share/qt/plugins \
        /usr/local/opt/qt6/share/qt/plugins \
        /usr/local/opt/qt@6/share/qt/plugins
    do
        if [[ -n "${candidate}" && -f "${candidate}/platforms/libqcocoa.dylib" ]]; then
            qt_plugin_root="${candidate}"
            break
        fi
    done

    if [[ -n "${qt_plugin_root}" ]]; then
        copy_qt_plugin() {
            local relative="$1"
            local source="${qt_plugin_root}/${relative}"
            local target="${contents_dir}/PlugIns/${relative}"
            if [[ -f "${source}" ]]; then
                mkdir -p "$(dirname "${target}")"
                cp "${source}" "${target}"
                install_name_tool -add_rpath "@loader_path/../../Frameworks" "${target}" 2>/dev/null || true
            fi
        }
        copy_qt_plugin "platforms/libqcocoa.dylib"
        copy_qt_plugin "imageformats/libqico.dylib"
        copy_qt_plugin "styles/libqmacstyle.dylib"
    fi
fi

copy_qemu_uae_plugin

if [[ "${WINUAE_SKIP_MACOS_DEPLOYMENT_CHECK:-0}" != "1" ]]; then
    "${script_dir}/macos-check-deployment-target.sh" "${app_dir}" "${deployment_target}" >&2
fi

if [[ "${WINUAE_SKIP_CODESIGN:-0}" != "1" ]] && command -v codesign >/dev/null 2>&1; then
    codesign_identity="${WINUAE_CODESIGN_IDENTITY:--}"
    codesign_args=(--force --deep --sign "${codesign_identity}")
    split_extra_args "${WINUAE_CODESIGN_OPTIONS:-}"
    codesign_args+=(${extra_args[@]+"${extra_args[@]}"})
    if [[ -n "${WINUAE_CODESIGN_ENTITLEMENTS:-}" ]]; then
        codesign_args+=(--entitlements "${WINUAE_CODESIGN_ENTITLEMENTS}")
    fi
    codesign "${codesign_args[@]}" "${app_dir}"
fi

echo "${app_dir}"
