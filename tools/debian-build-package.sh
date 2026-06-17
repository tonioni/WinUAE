#!/usr/bin/env bash
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${WINUAE_DEB_BUILD_DIR:-/tmp/winuae_deb_build}"
build_type="${WINUAE_DEB_BUILD_TYPE:-RelWithDebInfo}"
jobs="${WINUAE_DEB_JOBS:-}"
cmake_args=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] [-- <cmake-args>]

Options:
  --build-dir DIR     CMake build directory (default: ${build_dir})
  --build-type TYPE   CMake build type (default: ${build_type})
  --jobs N            Parallel build jobs (default: detected CPU count)
  -h, --help          Show this help

Extra arguments after -- are passed to the CMake configure step.
EOF
}

while (($#)); do
    case "$1" in
        --build-dir)
            if (($# < 2)); then
                echo "error: --build-dir requires a directory" >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        --build-type)
            if (($# < 2)); then
                echo "error: --build-type requires a value" >&2
                exit 2
            fi
            build_type="$2"
            shift 2
            ;;
        --jobs|-j)
            if (($# < 2)); then
                echo "error: --jobs requires a value" >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --)
            shift
            cmake_args+=("$@")
            break
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ "$(uname -s)" != "Linux" ]; then
    echo "error: Debian packages can only be built on Linux" >&2
    exit 1
fi

for tool in cmake cpack dpkg-deb dpkg-shlibdeps; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "error: required tool not found: ${tool}" >&2
        exit 1
    fi
done

if [ -z "${jobs}" ]; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"
fi

cmake -S "${source_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    "${cmake_args[@]}"

cmake --build "${build_dir}" \
    --target winuae_unix_linux_package_metadata_check
cmake --build "${build_dir}" --target package --parallel "${jobs}"

mapfile -t debs < <(
    find "${build_dir}" -maxdepth 1 -type f -name '*.deb' -printf '%T@ %p\n' |
        sort -nr |
        cut -d' ' -f2-
)
if ((${#debs[@]} == 0)); then
    echo "error: package build completed but no .deb was found" >&2
    echo "error: checked ${build_dir}" >&2
    exit 1
fi

echo
echo "Built Debian package:"
ppc_qemu_enabled=false
if grep -Eq '^WINUAE_UNIX_WITH_PPC_QEMU:BOOL=(ON|TRUE|1)$' \
    "${build_dir}/CMakeCache.txt"; then
    ppc_qemu_enabled=true
fi
for deb in "${debs[@]}"; do
    echo "  ${deb}"
    dpkg-deb --field "${deb}" Package Version Architecture Depends

    contents="$(dpkg-deb --contents "${deb}")"
    if ! grep -Eq ' \./usr/bin/winuae$' <<<"${contents}"; then
        echo "error: package does not contain /usr/bin/winuae" >&2
        exit 1
    fi
    if grep -Eq ' \./usr/bin/winuae_unix$' <<<"${contents}"; then
        echo "error: package should not install /usr/bin/winuae_unix" >&2
        exit 1
    fi
    if [[ "${ppc_qemu_enabled}" == true ]]; then
        if ! grep -Eq ' \./usr/lib.*/winuae/plugins/qemu-uae\.so$' <<<"${contents}"; then
            echo "error: package does not contain qemu-uae.so" >&2
            exit 1
        fi
    fi
done
