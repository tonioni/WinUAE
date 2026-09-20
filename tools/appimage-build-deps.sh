#!/usr/bin/env bash
set -euo pipefail

# Build SDL3 and Qt6 from source into a private prefix for the portable Linux
# (AppImage) build. This is the Linux counterpart to tools/macos-build-deps.sh:
# it exists because the oldest Ubuntu that gives us a widely-compatible glibc
# (24.04 LTS, glibc 2.39) does not ship SDL3 at all and ships a Qt6 much older
# than the version the project targets. Everything else (glib, FFmpeg, libpng,
# FLAC, ALSA, ...) is taken from the distribution, exactly as the .deb build
# does, because those are available on 24.04 at the same glibc.
#
# Unlike the macOS Qt build (cocoa platform, no xcb/fontconfig/opengl), the
# Linux Qt build must produce the xcb platform plugin so the configuration UI
# can actually open a window; the build is verified for that below.
#
# Point CMAKE_PREFIX_PATH at the resulting prefix when configuring WinUAE.

usage() {
    cat <<EOF
Usage: $0 [prefix]

Build SDL3 and Qt6 from source into a private prefix (Linux/AppImage build).

Arguments:
  prefix  Install prefix for SDL3 and Qt6.
          Defaults to WINUAE_DEPS_PREFIX or <repo>/../winuae-linux-deps.

Environment:
  WINUAE_DEPS_BUILD_DIR   Build directory. Defaults to <prefix>/build.
  WINUAE_DEPS_JOBS        Parallel build jobs. Defaults to nproc.
  WINUAE_SDL3_SOURCE      SDL3 source tree. Required unless WINUAE_SKIP_SDL3=1.
  WINUAE_QT_SOURCE        qtbase source tree. Required unless WINUAE_SKIP_QT=1.
  WINUAE_SKIP_SDL3=1      Do not build SDL3.
  WINUAE_SKIP_QT=1        Do not build Qt6.
  WINUAE_SDL3_CMAKE_ARGS  Extra whitespace-separated args for the SDL3 CMake.
  WINUAE_QT_CONFIGURE_ARGS Extra whitespace-separated args for Qt configure.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "${script_dir}/.." && pwd)"
prefix="${1:-${WINUAE_DEPS_PREFIX:-${source_dir}/../winuae-linux-deps}}"
build_dir="${WINUAE_DEPS_BUILD_DIR:-${prefix}/build}"
sdl_source="${WINUAE_SDL3_SOURCE:-}"
qt_source="${WINUAE_QT_SOURCE:-}"

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "error: this dependency build is Linux-only" >&2
    exit 1
fi

jobs="${WINUAE_DEPS_JOBS:-}"
if [[ -z "${jobs}" ]]; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 4)"
fi

mkdir -p "${prefix}" "${build_dir}"

require_source() {
    local name="$1" path="$2"
    if [[ -z "${path}" ]]; then
        echo "error: ${name} source path is required (set WINUAE_${name}_SOURCE)" >&2
        usage >&2
        exit 1
    fi
    if [[ ! -d "${path}" ]]; then
        echo "error: ${name} source directory not found: ${path}" >&2
        exit 1
    fi
}

split_extra_args() {
    extra_args=()
    local raw="$1"
    if [[ -n "${raw}" ]]; then
        # shellcheck disable=SC2206
        extra_args=(${raw})
    fi
}

run_cmake_build() {
    local src="$1" bld="$2"
    shift 2
    cmake -S "${src}" -B "${bld}" -G Ninja "$@"
    cmake --build "${bld}" -j "${jobs}"
    cmake --install "${bld}"
}

if [[ "${WINUAE_SKIP_SDL3:-0}" != "1" ]]; then
    echo "==> Building SDL3"
    require_source "SDL3" "${sdl_source}"
    sdl3_cmake_args=(
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="${prefix}"
        -DSDL_SHARED=ON
        -DSDL_STATIC=OFF
        -DSDL_TESTS=OFF
        -DSDL_EXAMPLES=OFF
        -DSDL_INSTALL_TESTS=OFF
    )
    split_extra_args "${WINUAE_SDL3_CMAKE_ARGS:-}"
    sdl3_cmake_args+=(${extra_args[@]+"${extra_args[@]}"})
    run_cmake_build "${sdl_source}" "${build_dir}/sdl3" "${sdl3_cmake_args[@]}"

    # Wayland compositors such as GNOME need libdecor when they do not offer
    # server-side decorations. Without its development headers SDL silently
    # builds without that fallback and regular WinUAE windows have no frame.
    sdl_config="$(find "${build_dir}/sdl3" -type f \
        -path '*/build_config/SDL_build_config.h' -print -quit)"
    if [[ -z "${sdl_config}" ]] || ! grep -qx '#define HAVE_LIBDECOR_H 1' "${sdl_config}"; then
        echo "error: SDL3 was built without libdecor Wayland-decoration support" >&2
        echo "       install libdecor development headers (for example libdecor-0-dev) and rebuild" >&2
        exit 1
    fi
fi

if [[ "${WINUAE_SKIP_QT:-0}" != "1" ]]; then
    echo "==> Building Qt6 (qtbase)"
    require_source "QT" "${qt_source}"
    if [[ ! -x "${qt_source}/configure" ]]; then
        echo "error: expected a qtbase checkout with a configure script at ${qt_source}" >&2
        exit 1
    fi

    qt_build="${build_dir}/qt"
    mkdir -p "${qt_build}"

    # Release-oriented, self-contained where practical. fontconfig stays ON so
    # the UI can find system fonts; xcb is left to autodetection (the workflow
    # installs the xcb dev libraries) and verified after install.
    qt_configure_args=(
        -prefix "${prefix}"
        -release
        -opensource
        -confirm-license
        -nomake examples
        -nomake tests
        -no-dbus
        -no-icu
        -fontconfig
        -qt-doubleconversion
        -qt-pcre
        -qt-zlib
        -qt-libpng
        -qt-libjpeg
        # Qt's fontconfig feature requires the system FreeType integration.
        -system-freetype
        -qt-harfbuzz
    )
    split_extra_args "${WINUAE_QT_CONFIGURE_ARGS:-}"
    qt_configure_args+=(${extra_args[@]+"${extra_args[@]}"})

    (
        cd "${qt_build}"
        "${qt_source}/configure" \
            "${qt_configure_args[@]}" \
            -- \
            -DCMAKE_BUILD_TYPE=Release
    )
    cmake --build "${qt_build}" -j "${jobs}"
    cmake --install "${qt_build}"

    # A Qt without the xcb platform plugin cannot open a window on Linux, which
    # would make the packaged configuration UI silently unusable. Fail loudly
    # here instead.
    if ! find "${prefix}" -type f -name 'libqxcb.so' | grep -q .; then
        echo "error: Qt built without the xcb platform plugin (libqxcb.so missing)" >&2
        echo "       ensure the xcb/xkbcommon dev libraries are installed before building Qt" >&2
        exit 1
    fi
fi

env_file="${prefix}/winuae-linux-deps-env.sh"
cat > "${env_file}" <<EOF
export CMAKE_PREFIX_PATH="${prefix}\${CMAKE_PREFIX_PATH:+:\${CMAKE_PREFIX_PATH}}"
export PKG_CONFIG_PATH="${prefix}/lib/pkgconfig:${prefix}/share/pkgconfig\${PKG_CONFIG_PATH:+:\${PKG_CONFIG_PATH}}"
export PATH="${prefix}/bin:\${PATH}"
export LD_LIBRARY_PATH="${prefix}/lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
export QMAKE="${prefix}/bin/qmake"
EOF

cat <<EOF

Linux dependencies installed to: ${prefix}

Use:
  source "${env_file}"
  cmake -S "${source_dir}" -B /tmp/winuae_cmake_linux \\
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
    -DCMAKE_PREFIX_PATH="${prefix}"
EOF
