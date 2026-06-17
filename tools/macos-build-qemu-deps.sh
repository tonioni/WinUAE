#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 [prefix]

Build QEMU-UAE macOS dependencies into a private prefix with a fixed
deployment target. The resulting prefix is intended for the QEMU-UAE plugin
build and keeps the plugin from linking against newer Homebrew libraries.

Arguments:
  prefix  Install prefix for GLib. Defaults to
          WINUAE_QEMU_DEPS_PREFIX, WINUAE_DEPS_PREFIX, or
          <repo>/../winuae-macos-deps.

Environment:
  WINUAE_MACOS_DEPLOYMENT_TARGET  Minimum macOS version. Defaults to 13.0.
  WINUAE_QEMU_DEPS_BUILD_DIR      Build directory. Defaults to
                                  <prefix>/build/qemu-deps.
  WINUAE_DEPS_JOBS                Parallel build jobs. Defaults to hw.ncpu.
  WINUAE_GLIB_SOURCE              GLib source tree. Required.
  WINUAE_MESON                    meson executable. Defaults to meson in PATH
                                  or ../qemu-uae-v11.0/build/pyvenv/bin/meson.
  WINUAE_NINJA                    ninja executable. Defaults to ninja in PATH.
  WINUAE_QEMU_BUILD_TOOLS_DIR     Optional tools prefix. If set,
                                  <prefix>/bin/ninja is used as fallback.
  WINUAE_GLIB_MESON_ARGS          Extra arguments passed to GLib Meson.

Extra argument variables are whitespace-separated argv fragments.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "${script_dir}/.." && pwd)"
target="${WINUAE_MACOS_DEPLOYMENT_TARGET:-13.0}"
prefix="${1:-${WINUAE_QEMU_DEPS_PREFIX:-${WINUAE_DEPS_PREFIX:-${source_dir}/../winuae-macos-deps}}}"
build_dir="${WINUAE_QEMU_DEPS_BUILD_DIR:-${prefix}/build/qemu-deps}"
glib_source="${WINUAE_GLIB_SOURCE:-}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: macOS QEMU dependency builds require Darwin/macOS" >&2
    exit 1
fi

jobs="${WINUAE_DEPS_JOBS:-}"
if [[ -z "${jobs}" ]]; then
    jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
fi

require_source() {
    local name="$1"
    local path="$2"
    if [[ -z "${path}" ]]; then
        echo "error: ${name} source path is required" >&2
        usage >&2
        exit 1
    fi
    if [[ ! -d "${path}" ]]; then
        echo "error: ${name} source directory not found: ${path}" >&2
        exit 1
    fi
}

find_tool() {
    local env_value="$1"
    local tool_name="$2"
    local fallback="$3"

    if [[ -n "${env_value}" ]]; then
        echo "${env_value}"
        return
    fi
    if command -v "${tool_name}" >/dev/null 2>&1; then
        command -v "${tool_name}"
        return
    fi
    if [[ -n "${fallback}" && -x "${fallback}" ]]; then
        echo "${fallback}"
        return
    fi

    echo "error: ${tool_name} not found" >&2
    exit 1
}

split_extra_args() {
    extra_args=()
    if [[ -n "${1:-}" ]]; then
        # Extra-arg variables are whitespace-separated argv fragments.
        # Split once here and pass the result as an array at call sites.
        # shellcheck disable=SC2206
        extra_args=($1)
    fi
}

run_meson_build() {
    local src="$1"
    local bld="$2"
    shift 2

    if [[ -f "${bld}/build.ninja" ]]; then
        "${meson_executable}" setup --reconfigure "${bld}" "$@"
    else
        "${meson_executable}" setup "${bld}" "${src}" "$@"
    fi
    "${ninja_executable}" -C "${bld}" -j "${jobs}"
    "${ninja_executable}" -C "${bld}" install
}

require_source "GLib" "${glib_source}"

meson_fallback="${source_dir}/../qemu-uae-v11.0/build/pyvenv/bin/meson"
ninja_fallback=""
if [[ -n "${WINUAE_QEMU_BUILD_TOOLS_DIR:-}" ]]; then
    ninja_fallback="${WINUAE_QEMU_BUILD_TOOLS_DIR}/bin/ninja"
fi
meson_executable="$(find_tool "${WINUAE_MESON:-}" meson "${meson_fallback}")"
ninja_executable="$(find_tool "${WINUAE_NINJA:-}" ninja "${ninja_fallback}")"

mkdir -p "${prefix}" "${build_dir}"

export MACOSX_DEPLOYMENT_TARGET="${target}"
export PATH="$(dirname "${ninja_executable}"):${prefix}/bin:${PATH}"
export PKG_CONFIG_LIBDIR="${prefix}/lib/pkgconfig:${prefix}/share/pkgconfig"

common_meson_args=(
    --prefix="${prefix}"
    --libdir=lib
    --buildtype=release
    -Ddefault_library=shared
)

glib_args=(
    "${common_meson_args[@]}"
    --force-fallback-for=libpcre2-8,libffi,intl
    -Dtests=false
    -Dinstalled_tests=false
    -Dglib_debug=disabled
    -Dglib_assert=false
    -Dglib_checks=false
    -Dman-pages=disabled
    -Ddocumentation=false
    -Dgtk_doc=false
    -Dnls=disabled
    -Dselinux=disabled
    -Dxattr=false
    -Dlibmount=disabled
    -Dsysprof=disabled
    -Dintrospection=disabled
    -Ddtrace=disabled
    -Dsystemtap=disabled
)
if [[ -n "${WINUAE_GLIB_MESON_ARGS:-}" ]]; then
    split_extra_args "${WINUAE_GLIB_MESON_ARGS}"
    glib_args+=(${extra_args[@]+"${extra_args[@]}"})
fi

run_meson_build "${glib_source}" "${build_dir}/glib" "${glib_args[@]}"

"${script_dir}/macos-check-deployment-target.sh" "${prefix}" "${target}"

env_file="${prefix}/winuae-macos-deps-env.sh"
cat > "${env_file}" <<EOF
export CMAKE_PREFIX_PATH="${prefix}\${CMAKE_PREFIX_PATH:+:\${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${prefix}/lib/pkgconfig:${prefix}/share/pkgconfig\${PKG_CONFIG_PATH:+:\${PKG_CONFIG_PATH}}"
export QEMU_UAE_DEPS_PREFIX="${prefix}"
export WINUAE_MACOS_DEPLOYMENT_TARGET="${target}"
export PATH="${prefix}/bin:\${PATH}"
EOF

cat <<EOF
macOS QEMU-UAE dependencies installed to: ${prefix}
Deployment target verified: ${target}

Use:
  source "${env_file}"
  cmake --build /tmp/winuae_cmake_macos \\
    --target winuae_unix_qemu_uae_plugin -j
EOF
