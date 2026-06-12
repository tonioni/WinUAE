#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 <output-plugin>

Builds qemu-uae.so through the external uae-ppc-plugin builder, which
downloads a SHA-verified QEMU source tarball (or uses a local one),
applies the UAE patch deck, and builds the plugin.

Environment:
  WINUAE_QEMU_UAE_BUILDER_DIR    uae-ppc-plugin checkout. When missing and
                                 fetching is allowed, it is cloned.
  WINUAE_QEMU_UAE_BUILDER_URL    Clone URL. Defaults to the reinauer repo.
  WINUAE_QEMU_UAE_BUILDER_FETCH  0 disables cloning a missing builder.
  WINUAE_QEMU_UAE_BUILDER_CLONE_DIR
                                 Clone destination. Defaults to
                                 <work-dir>/uae-ppc-plugin.
  WINUAE_QEMU_UAE_QEMU_TARBALL   Local QEMU source tarball for offline
                                 builds; passed to the builder as --tarball.
  WINUAE_QEMU_UAE_STRIP          0 keeps symbols in the built plugin.
  QEMU_UAE_WORK_DIR              Builder working directory.
  QEMU_UAE_DEPS_PREFIX           Private GLib/libslirp dependency prefix.
  WINUAE_MACOS_DEPLOYMENT_TARGET macOS deployment target for the plugin.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || $# -lt 1 ]]; then
    usage
    [[ $# -lt 1 ]] && exit 2
    exit 0
fi

output_plugin="$1"
builder_dir="${WINUAE_QEMU_UAE_BUILDER_DIR:-}"
builder_url="${WINUAE_QEMU_UAE_BUILDER_URL:-https://github.com/reinauer/uae-ppc-plugin.git}"
builder_fetch="${WINUAE_QEMU_UAE_BUILDER_FETCH:-1}"
work_dir="${QEMU_UAE_WORK_DIR:-$(pwd)/qemu-uae-work}"
clone_dir="${WINUAE_QEMU_UAE_BUILDER_CLONE_DIR:-${work_dir}/uae-ppc-plugin}"
tarball="${WINUAE_QEMU_UAE_QEMU_TARBALL:-}"

builder_script() {
    printf '%s/build-qemu-uae-plugin.sh' "$1"
}

if [[ -z "${builder_dir}" || ! -x "$(builder_script "${builder_dir}")" ]]; then
    if [[ -x "$(builder_script "${clone_dir}")" ]]; then
        builder_dir="${clone_dir}"
    elif [[ "${builder_fetch}" != "0" ]]; then
        echo "Cloning QEMU-UAE plugin builder from ${builder_url}"
        mkdir -p "$(dirname "${clone_dir}")"
        git clone --depth 1 "${builder_url}" "${clone_dir}"
        builder_dir="${clone_dir}"
    else
        echo "error: uae-ppc-plugin builder not found" >&2
        echo "hint: set WINUAE_QEMU_UAE_BUILDER_DIR to a checkout of" >&2
        echo "      ${builder_url}, or allow cloning" >&2
        exit 1
    fi
fi

args=( --output "${output_plugin}" )
if [[ -n "${tarball}" ]]; then
    if [[ ! -f "${tarball}" ]]; then
        echo "error: WINUAE_QEMU_UAE_QEMU_TARBALL does not exist: ${tarball}" >&2
        exit 1
    fi
    args+=( --tarball "${tarball}" )
fi

export QEMU_UAE_WORK_DIR="${work_dir}"
"$(builder_script "${builder_dir}")" "${args[@]}"

if [[ "${WINUAE_QEMU_UAE_STRIP:-1}" != "0" ]]; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
        strip -x "${output_plugin}"
    else
        strip --strip-unneeded "${output_plugin}"
    fi
fi
