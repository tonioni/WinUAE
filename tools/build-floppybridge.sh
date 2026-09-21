#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="${WINUAE_FLOPPYBRIDGE_SOURCE_DIR:-}"
output="${1:-${WINUAE_FLOPPYBRIDGE_OUTPUT:-}}"
work_dir="${WINUAE_FLOPPYBRIDGE_WORK_DIR:-$(pwd)/floppybridge-work}"
repo_url="${WINUAE_FLOPPYBRIDGE_URL:-https://github.com/RobSmithDev/FloppyDriveBridge.git}"
revision="${WINUAE_FLOPPYBRIDGE_REVISION:-710fa15cb200303f8c4bde1c931786175f301a68}"
fetch="${WINUAE_FLOPPYBRIDGE_FETCH:-1}"

if [[ -z "${output}" ]]; then
    echo "usage: $0 <output-FloppyBridge.so>" >&2
    exit 2
fi

if [[ -z "${source_dir}" ]]; then
    source_dir="${work_dir}/FloppyDriveBridge"
fi

if [[ ! -d "${source_dir}/.git" ]]; then
    if [[ "${fetch}" != "1" ]]; then
        echo "error: FloppyDriveBridge source is missing: ${source_dir}" >&2
        exit 1
    fi
    mkdir -p "$(dirname "${source_dir}")"
    git clone "${repo_url}" "${source_dir}"
fi

if ! git -C "${source_dir}" cat-file -e "${revision}^{commit}" 2>/dev/null; then
    if [[ "${fetch}" != "1" ]]; then
        echo "error: FloppyDriveBridge revision is unavailable: ${revision}" >&2
        exit 1
    fi
    git -C "${source_dir}" fetch --depth 1 origin "${revision}"
fi
if [[ "$(git -C "${source_dir}" rev-parse HEAD)" != "$(git -C "${source_dir}" rev-parse "${revision}^{commit}")" ]]; then
    git -C "${source_dir}" checkout --detach "${revision}"
fi

case "$(uname -s)" in
    Darwin)
        deployment_target="${WINUAE_MACOS_DEPLOYMENT_TARGET:-13.0}"
        shared_flags="-std=c++17 -dynamiclib -fPIC -mmacosx-version-min=${deployment_target} -Ifloppybridge -Iwindows"
        ;;
    Linux)
        # The pinned upstream fallback passes numeric 2000000 to cfsetspeed(),
        # which expects B2000000 and rejects DrawBridge ports on the Ubuntu
        # versions used for portable builds. Carry the focused fix until an
        # equivalent upstream commit is available.
        serial_patch="${script_dir}/patches/floppybridge-linux-b2000000.patch"
        if patch --batch --forward --silent --dry-run -d "${source_dir}" -p1 \
            <"${serial_patch}" >/dev/null 2>&1; then
            patch --batch --forward -d "${source_dir}" -p1 <"${serial_patch}"
        elif patch --batch --reverse --silent --dry-run -d "${source_dir}" -p1 \
            <"${serial_patch}" >/dev/null 2>&1; then
            echo "FloppyBridge 2 Mbaud patch already applied"
        else
            echo "error: could not apply FloppyBridge 2 Mbaud patch" >&2
            exit 1
        fi
        shared_flags="-std=c++17 -shared -fPIC -Ifloppybridge -Iwindows"
        ;;
    *)
        echo "error: unsupported FloppyBridge host: $(uname -s)" >&2
        exit 1
        ;;
esac

mkdir -p "$(dirname "${output}")"
make -B -C "${source_dir}" \
    CXX="${CXX:-c++}" \
    TARGET="${output}" \
    CPPFLAGS="${shared_flags}"

if [[ "$(uname -s)" == "Darwin" ]]; then
    install_name_tool -id "@rpath/FloppyBridge.so" "${output}"
fi

if [[ ! -f "${output}" ]]; then
    echo "error: FloppyBridge build did not produce ${output}" >&2
    exit 1
fi

if [[ "$(uname -s)" == "Linux" ]]; then
    mkdir -p "${work_dir}"
    baud_smoke="${work_dir}/floppybridge-linux-baud-smoke"
    "${CXX:-c++}" -std=c++17 \
        -I"${source_dir}/floppybridge" \
        "${script_dir}/floppybridge-linux-baud-smoke.cpp" \
        "${source_dir}/floppybridge/SerialIO.cpp" \
        "${source_dir}/floppybridge/ftdi.cpp" \
        -ldl \
        -o "${baud_smoke}"
    "${baud_smoke}"
fi
