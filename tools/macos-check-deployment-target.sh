#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 app-or-binary-path deployment-target

Checks Mach-O files under a macOS app bundle, or a single Mach-O file, and
fails if any file requires a macOS version newer than the requested deployment
target.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

root="${1:-}"
target="${2:-}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: deployment-target checking requires Darwin/macOS" >&2
    exit 1
fi
if [[ -z "${root}" || -z "${target}" ]]; then
    usage >&2
    exit 1
fi
if [[ ! -e "${root}" ]]; then
    echo "error: path not found: ${root}" >&2
    exit 1
fi

version_gt() {
    local a="$1"
    local b="$2"
    local ai bi
    IFS=. read -r -a av <<< "${a}"
    IFS=. read -r -a bv <<< "${b}"
    for i in 0 1 2; do
        ai="${av[$i]:-0}"
        bi="${bv[$i]:-0}"
        if ((10#${ai} > 10#${bi})); then
            return 0
        fi
        if ((10#${ai} < 10#${bi})); then
            return 1
        fi
    done
    return 1
}

mach_o_minos() {
    otool -l "$1" 2>/dev/null | awk '
        /LC_BUILD_VERSION/ { build = 1; old = 0; next }
        build && /minos/ { print $2; exit }
        /LC_VERSION_MIN_MACOSX/ { old = 1; build = 0; next }
        old && /version/ { print $2; exit }
    '
}

check_file() {
    local file="$1"
    local info minos

    info="$(file -b "${file}" 2>/dev/null || true)"
    case "${info}" in
        *Mach-O*) ;;
        *) return 0 ;;
    esac

    minos="$(mach_o_minos "${file}")"
    if [[ -n "${minos}" ]] && version_gt "${minos}" "${target}"; then
        echo "error: ${file} requires macOS ${minos}, newer than deployment target ${target}" >&2
        return 1
    fi
    return 0
}

failed=0
if [[ -f "${root}" ]]; then
    check_file "${root}" || failed=1
else
    while IFS= read -r -d '' file_path; do
        check_file "${file_path}" || failed=1
    done < <(find "${root}" -type f -print0)
fi

if ((failed)); then
    exit 1
fi
echo "verified macOS deployment target ${target}: ${root}"
